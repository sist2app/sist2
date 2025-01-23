import os
import shutil
import stat
import subprocess
from enum import Enum

from git import Repo
from pydantic import BaseModel

from config import SCRIPT_FOLDER


class ScriptType(Enum):
    LOCAL = "local"
    SIMPLE = "simple"
    GIT = "git"


def set_executable(file):
    os.chmod(file, os.stat(file).st_mode | stat.S_IEXEC)


def _initialize_git_repository(url, path, log_cb, force_clone, set_pid_cb):
    log_cb({"sist2-admin": f"Cloning {url}"})

    if force_clone or not os.path.exists(os.path.join(path, ".git")):
        if force_clone:
            shutil.rmtree(path, ignore_errors=True)
        Repo.clone_from(url, path)
    else:
        repo = Repo(path)
        repo.remote("origin").pull()

    setup_script = os.path.join(path, "setup.sh")
    if setup_script:
        log_cb({"sist2-admin": f"Executing setup script {setup_script}"})

        set_executable(setup_script)
        proc = subprocess.Popen([setup_script], cwd=path, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        set_pid_cb(proc.pid)
        proc.wait()
        stdout = proc.stdout.read()

        for line in stdout.split(b"\n"):
            if line:
                log_cb({"stdout": line.decode()})

        log_cb({"stdout": f"Executed setup script {setup_script}, return code = {proc.returncode}"})

        if proc.returncode != 0:
            raise Exception("Error when running setup script!")

    log_cb({"sist2-admin": f"Initialized git repository in {path}"})


class UserScript(BaseModel):
    name: str
    type: ScriptType
    git_repository: str = None
    force_clone: bool = False
    script: str = None
    extra_args: str = ""

    def script_dir(self):
        return os.path.join(SCRIPT_FOLDER, self.name)

    def setup(self, log_cb, set_pid_cb):
        os.makedirs(self.script_dir(), exist_ok=True)

        if self.type == ScriptType.GIT:
            _initialize_git_repository(self.git_repository, self.script_dir(), log_cb, self.force_clone, set_pid_cb)
            self.force_clone = False
        elif self.type == ScriptType.SIMPLE:
            self._setup_simple()

        set_executable(self.get_executable())

    def _setup_simple(self):
        with open(self.get_executable(), "w") as f:
            f.write(
                "#!/bin/bash\n"
                "python run.py \"$@\""
            )

        with open(os.path.join(self.script_dir(), "run.py"), "w") as f:
            f.write(self.script)

    def get_executable(self):
        return os.path.join(self.script_dir(), "run.sh")

    def delete_dir(self):
        shutil.rmtree(self.script_dir(), ignore_errors=True)


SCRIPT_TEMPLATES = {
    "CLIP - Generate embeddings to predict the most relevant image based on the text prompt": lambda name: UserScript(
        name=name,
        type=ScriptType.GIT,
        git_repository="https://github.com/sist2app/sist2-script-clip",
        extra_args="--num-tags=1 --tags-file=general.txt --color=#dcd7ff"
    ),
    "Whisper - Speech to text with OpenAI Whisper": lambda name: UserScript(
        name=name,
        type=ScriptType.GIT,
        git_repository="https://github.com/simon987/sist2-script-whisper",
        extra_args="--model=base --num-threads=4 --color=#51da4c --tag"
    ),
    "Hamburger - Simple script example": lambda name: UserScript(
        name=name,
        type=ScriptType.SIMPLE,
        script=
        'from sist2 import Sist2Index\n'
        'import sys\n'
        '\n'
        'index = Sist2Index(sys.argv[1])\n'
        'for doc in index.document_iter():\n'
        '    doc.json_data["tag"] = ["hamburger.#00FF00"]\n'
        '    index.update_document(doc)\n'
        '\n'
        'index.sync_tag_table()\n'
        'index.commit()\n'
        '\n'
        'print("Done!")\n'
    ),
    "(Blank)": lambda name: UserScript(
        name=name,
        type=ScriptType.SIMPLE,
        script=""
    )
}
