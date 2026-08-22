import fs from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import readline from "node:readline";

import { waitForChildExit } from "./child.js";
import { DATA_FOLDER, LOG_FOLDER, SIST2_BINARY } from "./config.js";
import { jobRepository, searchBackendRepository, taskHistoryRepository } from "./db.js";
import { logger } from "./log.js";
import { notify } from "./notifications.js";
import { indexArgs, runSist2, scanArgs, writeTempFile } from "./sist2.js";
import { scriptDir, scriptExecutable, setupScript, splitArgs } from "./scripts.js";
import { restartRunningFrontends } from "./frontends.js";

const QUEUE_TICK_INTERVAL = 1000;
const SKIPPED_RETURN_CODE = -1;

function createProgress() {
    return {
        done: 0,
        count: 0,
        index_size: 0,
        store_size: 0,
        waiting: false
    };
}

export function taskLogFile(taskId) {
    return path.join(LOG_FOLDER, `sist2-${taskId}.log`);
}

class Task {
    constructor(jobName, displayName, dependsOn) {
        this.id = crypto.randomUUID();
        this.jobName = jobName;
        this.displayName = displayName;
        this.dependsOn = dependsOn;
        this.progress = createProgress();
        this.started = null;
        this.ended = null;
        this.child = null;
        this._logStream = null;
        this._logClosed = false;
    }

    json() {
        return {
            id: this.id,
            job_name: this.jobName,
            display_name: this.displayName,
            progress: this.progress,
            started: this.started,
            ended: this.ended,
            depends_on: this.dependsOn
        };
    }

    log(payload) {
        const line = `${JSON.stringify(payload)}\n`;

        // Callbacks (e.g. deferred frontend restarts) may log after the task
        // is done; append directly instead of leaking a new write stream
        if (this._logClosed) {
            fs.appendFileSync(taskLogFile(this.id), line);
            return;
        }

        if (this._logStream === null) {
            this._logStream = fs.createWriteStream(taskLogFile(this.id), { flags: "a" });
        }
        this._logStream.write(line);
    }

    onLog(payload) {
        if (payload.progress !== undefined) {
            this.progress = {
                done: payload.progress.done,
                count: payload.progress.count,
                index_size: payload.progress.index_size,
                store_size: payload.progress.tn_size,
                waiting: false
            };
            return;
        }
        this.log(payload);
    }

    onSpawn(child) {
        this.child = child;
    }

    closeLog() {
        this._logClosed = true;
        if (this._logStream !== null) {
            this._logStream.end();
            this._logStream = null;
        }
    }

    async run() {
        throw new Error("NotImplementedError");
    }
}

// Debug builds of sist2 exit with code 1 on ASan leak reports; treat as success.
function isOkReturnCode(returnCode) {
    if (SIST2_BINARY.includes("debug")) {
        return returnCode === 0 || returnCode === 1;
    }
    return returnCode === 0;
}

function scanOutputName(jobName) {
    const timestamp = new Date().toISOString().slice(0, 19).replaceAll(":", "-");
    return `scan-${jobName.replaceAll("/", "_")}-${timestamp}.sist2`;
}

export class ScanTask extends Task {
    async run() {
        const job = jobRepository.get(this.jobName);
        if (job === null) {
            this.log({ "sist2-admin": `Job not found: ${this.jobName}` });
            return SKIPPED_RETURN_CODE;
        }

        job.scan_options.name = job.name;

        let output;
        if (job.index_path !== null && !job.do_full_scan) {
            output = job.index_path;
        } else {
            output = scanOutputName(job.name);
        }

        const outputPath = path.join(DATA_FOLDER, output);
        const args = scanArgs(job.scan_options, outputPath);

        const returnCode = await runSist2(
            args,
            {
                onLog: (payload) => this.onLog(payload),
                onSpawn: (child) => this.onSpawn(child)
            }
        );
        this.ended = new Date().toISOString();

        if (!isOkReturnCode(returnCode)) {
            this.log({ "sist2-admin": `Process returned non-zero exit code (${returnCode})` });
            logger.info(`Task ${this.displayName} failed (${returnCode})`);
            return returnCode;
        }

        // Re-fetch the job: it may have been edited through the API while the scan ran
        const fresh = jobRepository.get(this.jobName) ?? job;

        if (fresh.previous_index_path !== null && fresh.previous_index_path !== output) {
            this.log({ "sist2-admin": `Remove ${fresh.previous_index_path}` });
            fs.rmSync(path.join(DATA_FOLDER, fresh.previous_index_path), { force: true });
        }

        fresh.index_path = output;
        fresh.previous_index_path = output;
        fresh.last_index_date = new Date().toISOString();
        fresh.do_full_scan = false;
        jobRepository.update(fresh);
        this.log({ "sist2-admin": `Save last_index_date=${fresh.last_index_date}` });

        logger.info(`Completed ${this.displayName} (return_code=${returnCode})`);
        return 0;
    }
}

export class IndexTask extends Task {
    async run() {
        const job = jobRepository.get(this.jobName);
        if (job === null) {
            this.log({ "sist2-admin": `Job not found: ${this.jobName}` });
            return SKIPPED_RETURN_CODE;
        }

        const backendName = job.index_options.search_backend;
        const backend = searchBackendRepository.get(backendName);
        if (backend === null) {
            this.log({ "sist2-admin": `Search backend not found: ${backendName}` });
            logger.error(`Search backend not found: ${backendName}`);
            return SKIPPED_RETURN_CODE;
        }

        const tempFiles = [];
        let mappingsFile = null;
        let settingsFile = null;
        if (backend.es_mappings) {
            mappingsFile = writeTempFile("es-mappings", backend.es_mappings);
            tempFiles.push(mappingsFile);
        }
        if (backend.es_settings) {
            settingsFile = writeTempFile("es-settings", backend.es_settings);
            tempFiles.push(settingsFile);
        }

        const args = indexArgs(
            job.index_path,
            job.index_options,
            backend,
            mappingsFile,
            settingsFile
        );

        const returnCode = await runSist2(
            args,
            {
                onLog: (payload) => this.onLog(payload),
                onSpawn: (child) => this.onSpawn(child),
                tempFiles: tempFiles
            }
        );
        this.ended = new Date().toISOString();

        const ok = returnCode === 0 || returnCode === 1;

        if (ok) {
            restartRunningFrontends((payload) => this.log(payload));
        }

        // Re-fetch the job: it may have been edited through the API while the index ran
        const fresh = jobRepository.get(this.jobName) ?? job;
        fresh.status = ok ? "indexed" : "failed";
        fresh.previous_index_path = fresh.index_path;
        jobRepository.update(fresh);

        this.log({ "sist2-admin": `Index task finished return_code=${returnCode}, ok=${ok}` });
        logger.info(`Completed ${this.displayName} (return_code=${returnCode})`);
        return returnCode;
    }
}

export class UserScriptTask extends Task {
    constructor(userScript, jobName, displayName, dependsOn) {
        super(jobName, displayName, dependsOn);
        this.userScript = userScript;
    }

    async run() {
        const job = jobRepository.get(this.jobName);
        if (job === null || job.index_path === null) {
            this.log({ "sist2-admin": `Job has no index: ${this.jobName}` });
            return SKIPPED_RETURN_CODE;
        }

        try {
            await setupScript(
                this.userScript,
                (payload) => this.onLog(payload),
                (child) => this.onSpawn(child)
            );
        } catch (e) {
            logger.error(`Setup for ${this.userScript.name} failed: ${e.message}`);
            this.log({ "sist2-admin": `Setup for ${this.userScript.name} failed: ${e.message}` });
            return SKIPPED_RETURN_CODE;
        }

        const executable = scriptExecutable(this.userScript);
        const indexPath = path.join(DATA_FOLDER, job.index_path);
        const args = [indexPath, ...splitArgs(this.userScript.extra_args)];

        this.log({ "sist2-admin": `Starting user script ${executable} with args ${JSON.stringify(args)}` });

        const returnCode = await new Promise((resolve) => {
            const child = spawn(executable, args, {
                cwd: scriptDir(this.userScript),
                stdio: ["ignore", "pipe", "pipe"]
            });
            this.onSpawn(child);

            for (const [stream, key] of [[child.stdout, "stdout"], [child.stderr, "stderr"]]) {
                const lines = readline.createInterface({ input: stream });
                lines.on("line", (line) => {
                    if (line.trim() === "") {
                        return;
                    }
                    if (line.startsWith("$PROGRESS ")) {
                        try {
                            this.onLog({ progress: JSON.parse(line.slice("$PROGRESS ".length)) });
                        } catch (e) {
                            this.log({ "sist2-admin": `Could not decode progress line: ${line}` });
                        }
                        return;
                    }
                    this.log({ [key]: line });
                });
            }

            child.on("error", (e) => {
                this.log({ "sist2-admin": `Failed to start user script: ${e.message}` });
                resolve(SKIPPED_RETURN_CODE);
            });
            waitForChildExit(child).then(({ code, orphaned }) => {
                if (orphaned) {
                    this.log({
                        "sist2-admin": "The user script has exited, but something it started is still "
                            + "running and holding on to its output."
                    });
                }
                if (code === null) {
                    resolve(SKIPPED_RETURN_CODE);
                    return;
                }
                resolve(code);
            });
        });

        this.ended = new Date().toISOString();
        this.log({ "sist2-admin": `User script exited with code ${returnCode}` });

        // Match Python behavior: a script failure does not fail the job chain.
        return 0;
    }
}

class TaskQueue {
    constructor() {

        this._queue = [];
        this._running = new Map();
        this._interval = null;
    }

    start() {
        this._interval = setInterval(() => this._tick(), QUEUE_TICK_INTERVAL);
    }

    submit(task) {
        logger.info(`Submitted task to queue ${task.displayName}`);
        this._queue.push(task);
    }

    tasks() {
        return [...this._running.values(), ...this._queue];
    }

    kill(taskId) {
        const task = this._running.get(taskId);
        if (task === undefined || task.child === null) {
            return false;
        }
        logger.info(`Killing task ${taskId} (pid=${task.child.pid})`);
        task.child.kill("SIGTERM");
        return true;
    }

    killAll() {
        for (const task of this._running.values()) {
            if (task.child !== null) {
                task.child.kill("SIGTERM");
            }
        }
    }

    _tick() {
        if (this._running.size >= 1 || this._queue.length === 0) {
            return;
        }

        const task = this._queue[0];

        if (task.dependsOn !== null) {
            const done = taskHistoryRepository.doneIds();
            if (!done.has(task.dependsOn)) {
                return;
            }
            if (taskHistoryRepository.failedIds().has(task.dependsOn)) {
                this._queue.shift();
                logger.warn(`Skipping task "${task.displayName}": dependency failed`);
                taskHistoryRepository.insert({
                    id: task.id,
                    name: task.displayName,
                    job_name: task.jobName,
                    started: new Date().toISOString(),
                    ended: new Date().toISOString(),
                    return_code: SKIPPED_RETURN_CODE,
                    has_logs: 0
                });
                return;
            }
        }

        this._queue.shift();
        this._running.set(task.id, task);
        task.started = new Date().toISOString();
        logger.info(`Started task ${task.displayName}`);

        task.run()
            .catch((e) => {
                logger.error(`Task ${task.displayName} crashed: ${e.stack}`);
                task.log({ "sist2-admin": `Task crashed: ${e.message}` });
                return SKIPPED_RETURN_CODE;
            })
            .then((returnCode) => this._onTaskDone(task, returnCode));
    }

    _onTaskDone(task, returnCode) {
        this._running.delete(task.id);

        if (task.ended === null) {
            task.ended = new Date().toISOString();
        }
        task.closeLog();

        taskHistoryRepository.insert({
            id: task.id,
            name: task.displayName,
            job_name: task.jobName,
            started: task.started,
            ended: task.ended,
            return_code: returnCode,
            has_logs: 1
        });

        const job = jobRepository.get(task.jobName);
        if (job !== null) {
            for (const row of taskHistoryRepository.logsToDelete(job.name, job.keep_last_n_logs)) {
                deleteTaskLogs(row.id);
            }
        }

        if (task instanceof IndexTask) {
            if (returnCode === 0 || returnCode === 1) {
                notify({
                    message: "notifications.indexCompleted",
                    job: task.jobName
                });
            } else {
                notify({
                    message: "notifications.indexFailed",
                    job: task.jobName
                });
            }
        }
    }
}

export function deleteTaskLogs(taskId) {
    taskHistoryRepository.setHasLogs(taskId, 0);
    fs.rmSync(taskLogFile(taskId), { force: true });
}

export const taskQueue = new TaskQueue();

/**
 * Why this job cannot run, or null when it can. Both of these are found when the tasks are already
 * under way otherwise: an empty path scans everything, and a backend that is not there fails the
 * index task once the scan has finished.
 *
 * @returns {string|null}
 */
export function jobProblem(job) {
    const scanPath = job.scan_options.path;

    if (scanPath === null || scanPath === undefined || scanPath.trim() === "") {
        return "This job has no path to scan. Set one in the job's options.";
    }

    const name = job.index_options.search_backend;

    if (name === null || name === undefined) {
        return "This job has no search backend. Pick one in the job's options.";
    }

    if (searchBackendRepository.get(name) === null) {
        return `This job's search backend no longer exists: ${name}`;
    }

    return null;
}

export function submitJob(job, userScriptsByName) {
    if (job.status === "created") {
        job.status = "started";
        jobRepository.update(job);
    }

    const scanTask = new ScanTask(job.name, `Scan [${job.name}]`, null);

    let indexDependsOn = scanTask;
    const scriptTasks = [];
    for (const scriptName of job.user_scripts) {
        const script = userScriptsByName(scriptName);
        if (script === null) {
            logger.error(`User script not found: ${scriptName}`);
            continue;
        }
        const task = new UserScriptTask(
            script,
            job.name,
            `Script <${scriptName}> [${job.name}]`,
            scanTask.id
        );
        scriptTasks.push(task);
        indexDependsOn = task;
    }

    const indexTask = new IndexTask(job.name, `Index [${job.name}]`, indexDependsOn.id);

    taskQueue.submit(scanTask);
    for (const task of scriptTasks) {
        taskQueue.submit(task);
    }
    taskQueue.submit(indexTask);
}
