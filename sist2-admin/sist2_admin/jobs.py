import json
import logging
import os.path
import shlex
import signal
import uuid
from datetime import datetime
from enum import Enum
from io import TextIOWrapper
from logging import FileHandler
from subprocess import Popen
import subprocess
from threading import Lock, Thread
from time import sleep
from typing import List
from uuid import uuid4, UUID

from hexlib.db import PersistentState
from pydantic import BaseModel

from config import logger, LOG_FOLDER, DATA_FOLDER
from notifications import Notifications
from sist2 import ScanOptions, IndexOptions, Sist2
from state import RUNNING_FRONTENDS, get_log_files_to_remove, delete_log_file
from web import Sist2Frontend
from script import UserScript


class JobStatus(Enum):
    CREATED = "created"
    STARTED = "started"
    INDEXED = "indexed"
    FAILED = "failed"


class Sist2Job(BaseModel):
    name: str
    scan_options: ScanOptions
    index_options: IndexOptions

    user_scripts: List[str] = []

    cron_expression: str
    schedule_enabled: bool = False

    keep_last_n_logs: int = -1

    previous_index: str = None
    index_path: str = None
    previous_index_path: str = None
    last_index_date: datetime = None
    status: JobStatus = JobStatus("created")
    last_modified: datetime
    etag: str = None
    do_full_scan: bool = False

    def __init__(self, **kwargs):
        super().__init__(**kwargs)

    @staticmethod
    def create_default(name: str):
        return Sist2Job(
            name=name,
            scan_options=ScanOptions(path="/"),
            index_options=IndexOptions(),
            last_modified=datetime.utcnow(),
            cron_expression="0 0 * * *"
        )


class Sist2TaskProgress:

    def __init__(self, done: int = 0, count: int = 0, index_size: int = 0, tn_size: int = 0, waiting: bool = False):
        self.done = done
        self.count = count
        self.index_size = index_size
        self.store_size = tn_size
        self.waiting = waiting

    def percent(self):
        return (self.done / self.count) if self.count else 0


class Sist2Task:

    def __init__(self, job: Sist2Job, display_name: str, depends_on: uuid.UUID = None):
        self.job = job
        self.display_name = display_name

        self.progress = Sist2TaskProgress()
        self.id = uuid4()
        self.pid = None
        self.started = None
        self.ended = None
        self.depends_on = depends_on

        self._logger = logging.Logger(name=f"{self.id}")
        self._logger.addHandler(FileHandler(os.path.join(LOG_FOLDER, f"sist2-{self.id}.log")))

    def json(self):
        return {
            "id": self.id,
            "job": self.job,
            "display_name": self.display_name,
            "progress": self.progress,
            "started": self.started,
            "ended": self.ended,
            "depends_on": self.depends_on,
        }

    def log_callback(self, log_json):

        if "progress" in log_json:
            self.progress = Sist2TaskProgress(**log_json["progress"])
        elif self._logger:
            self._logger.info(json.dumps(log_json))

    def run(self, sist2: Sist2, db: PersistentState):
        self.started = datetime.utcnow()

        logger.info(f"Started task {self.display_name}")

    def set_pid(self, pid):
        self.pid = pid



class Sist2ScanTask(Sist2Task):

    def run(self, sist2: Sist2, db: PersistentState):
        super().run(sist2, db)

        self.job.scan_options.name = self.job.name

        if self.job.index_path is not None and not self.job.do_full_scan:
            self.job.scan_options.output = self.job.index_path
        else:
            self.job.scan_options.output = None

        return_code = sist2.scan(self.job.scan_options, logs_cb=self.log_callback, set_pid_cb=self.set_pid)
        self.ended = datetime.utcnow()

        is_ok = (return_code in (0, 1)) if "debug" in sist2.bin_path else (return_code == 0)

        if not is_ok:
            self._logger.error(json.dumps({"sist2-admin": f"Process returned non-zero exit code ({return_code})"}))
            logger.info(f"Task {self.display_name} failed ({return_code})")
        else:
            self.job.index_path = self.job.scan_options.output
            self.job.last_index_date = datetime.utcnow()
            self.job.do_full_scan = False
            db["jobs"][self.job.name] = self.job
            self._logger.info(json.dumps({"sist2-admin": f"Save last_index_date={self.job.last_index_date}"}))

        logger.info(f"Completed {self.display_name} ({return_code=})")

        # Remove old index
        if is_ok:
            if self.job.previous_index_path is not None and self.job.previous_index_path != self.job.index_path:
                self._logger.info(json.dumps({"sist2-admin": f"Remove {self.job.previous_index_path=}"}))
                try:
                    os.remove(self.job.previous_index_path)
                except FileNotFoundError:
                    pass

            self.job.previous_index_path = self.job.index_path
            db["jobs"][self.job.name] = self.job

        if is_ok:
            return 0

        return return_code


class Sist2IndexTask(Sist2Task):

    def __init__(self, job: Sist2Job, display_name: str, depends_on: Sist2Task):
        super().__init__(job, display_name, depends_on=depends_on.id)

    def run(self, sist2: Sist2, db: PersistentState):
        super().run(sist2, db)

        self.job.index_options.path = self.job.scan_options.output

        search_backend = db["search_backends"][self.job.index_options.search_backend]
        if search_backend is None:
            logger.error(f"Error while running task: search backend not found: {self.job.index_options.search_backend}")
            return -1

        logger.debug(f"Fetched search backend options for {self.job.index_options.search_backend}")

        return_code = sist2.index(self.job.index_options, search_backend, logs_cb=self.log_callback, set_pid_cb=self.set_pid)
        self.ended = datetime.utcnow()

        duration = self.ended - self.started

        ok = return_code in (0, 1)

        if ok:
            self.restart_running_frontends(db, sist2)

        # Update status
        self.job.status = JobStatus("indexed") if ok else JobStatus("failed")
        self.job.previous_index_path = self.job.index_path
        db["jobs"][self.job.name] = self.job

        self._logger.info(json.dumps({"sist2-admin": f"Sist2Scan task finished {return_code=}, {duration=}, {ok=}"}))

        logger.info(f"Completed {self.display_name} ({return_code=})")

        return return_code

    def restart_running_frontends(self, db: PersistentState, sist2: Sist2):
        for frontend_name, pid in RUNNING_FRONTENDS.items():
            frontend = db["frontends"][frontend_name]
            frontend: Sist2Frontend

            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(pid, 0)
            except ChildProcessError:
                pass

            backend_name = frontend.web_options.search_backend
            search_backend = db["search_backends"][backend_name]
            if search_backend is None:
                logger.error(f"Error while running task: search backend not found: {backend_name}")
                return -1

            logger.debug(f"Fetched search backend options for {backend_name}")

            frontend.web_options.indices = [
                os.path.join(DATA_FOLDER, db["jobs"][j].index_path)
                for j in frontend.jobs
            ]

            pid = sist2.web(frontend.web_options, search_backend, frontend.name)
            RUNNING_FRONTENDS[frontend_name] = pid

            self._logger.info(json.dumps({"sist2-admin": f"Restart frontend {pid=} {frontend_name=}"}))


class Sist2UserScriptTask(Sist2Task):

    def __init__(self, user_script: UserScript, job: Sist2Job, display_name: str, depends_on: Sist2Task = None):
        super().__init__(job, display_name, depends_on=depends_on.id if depends_on else None)
        self.user_script = user_script

    def run(self, sist2: Sist2, db: PersistentState):
        super().run(sist2, db)

        try:
            self.user_script.setup(self.log_callback, self.set_pid)
        except Exception as e:
            logger.error(f"Setup for {self.user_script.name} failed: ")
            logger.exception(e)
            self.log_callback({"sist2-admin": f"Setup for {self.user_script.name} failed: {e}"})
            return -1

        executable = self.user_script.get_executable()
        index_path = os.path.join(DATA_FOLDER, self.job.index_path)
        extra_args = self.user_script.extra_args

        args = [
            executable,
            index_path,
            *shlex.split(extra_args)
        ]

        self.log_callback({"sist2-admin": f"Starting user script with {executable=}, {index_path=}, {extra_args=}"})

        proc = Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=self.user_script.script_dir())
        self.set_pid(proc.pid)

        t_stderr = Thread(target=self._consume_logs, args=(self.log_callback, proc, "stderr", False))
        t_stderr.start()

        self._consume_logs(self.log_callback, proc, "stdout", True)

        self.ended = datetime.utcnow()

        return 0

    @staticmethod
    def _consume_logs(logs_cb, proc, stream, wait):
        pipe_wrapper = TextIOWrapper(getattr(proc, stream), encoding="utf8", errors="ignore")
        try:
            for line in pipe_wrapper:
                if line.strip() == "":
                    continue
                if line.startswith("$PROGRESS"):
                    progress = json.loads(line[len("$PROGRESS "):])
                    logs_cb({"progress": progress})
                    continue
                logs_cb({stream: line})
        finally:
            if wait:
                proc.wait()
            pipe_wrapper.close()


class TaskQueue:
    def __init__(self, sist2: Sist2, db: PersistentState, notifications: Notifications):
        self._lock = Lock()

        self._sist2 = sist2
        self._db = db
        self._notifications = notifications

        self._tasks = {}
        self._queue = []
        self._sem = 0

        self._thread = Thread(target=self._check_new_task, daemon=True)
        self._thread.start()

    def _tasks_failed(self):
        done = set()

        for row in self._db["task_done"].sql("WHERE return_code != 0"):
            done.add(uuid.UUID(row["id"]))

        return done

    def _tasks_done(self):

        done = set()

        for row in self._db["task_done"]:
            done.add(uuid.UUID(row["id"]))

        return done

    def _check_new_task(self):
        while True:
            with self._lock:
                for task in list(self._queue):
                    task: Sist2Task

                    if self._sem >= 1:
                        break

                    if not task.depends_on or task.depends_on in self._tasks_done():
                        self._queue.remove(task)

                        if task.depends_on in self._tasks_failed():
                            # The task which we depend on failed, continue
                            continue

                        self._sem += 1

                        t = Thread(target=self._run_task, args=(task,))

                        self._tasks[task.id] = {
                            "task": task,
                            "thread": t,
                        }

                        t.start()
                    break
            sleep(1)

    def tasks(self):
        return list(map(lambda t: t["task"], self._tasks.values()))

    def kill_task(self, task_id):

        task = self._tasks.get(UUID(task_id))

        if task:
            pid = task["task"].pid
            logger.info(f"Killing task {task_id} (pid={pid})")
            os.kill(pid, signal.SIGTERM)
            return True

        return False

    def _run_task(self, task: Sist2Task):
        task_result = task.run(self._sist2, self._db)

        with self._lock:
            del self._tasks[task.id]
            self._sem -= 1

            self._db["task_done"][task.id] = {
                "ended": task.ended,
                "started": task.started,
                "name": task.display_name,
                "return_code": task_result,
                "has_logs": 1
            }

            logs_to_delete = get_log_files_to_remove(self._db, task.job.name, task.job.keep_last_n_logs)
            for row in logs_to_delete:
                delete_log_file(self._db, row["id"])

        if isinstance(task, Sist2IndexTask):
            self._notifications.notify({
                "message": "notifications.indexCompleted",
                "job": task.job.name
            })

    def submit(self, task: Sist2Task):

        logger.info(f"Submitted task to queue {task.display_name}")

        with self._lock:
            self._queue.append(task)
