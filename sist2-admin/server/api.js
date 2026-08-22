import { execFile } from "node:child_process";
import fs from "node:fs";
import http from "node:http";
import https from "node:https";
import path from "node:path";

import { DATA_FOLDER, LOG_FOLDER } from "./config.js";
import {
    frontendRepository,
    jobRepository,
    searchBackendRepository,
    taskHistoryRepository,
    userScriptRepository
} from "./db.js";
import { esUrlPort, parseEsUrl } from "./es_url.js";
import { HttpError, RESPONSE_HANDLED, Router, openSse } from "./http.js";
import { logger } from "./log.js";
import {
    FULL_SCAN_FIELDS,
    createDefaultFrontend,
    createDefaultJob,
    normalizeFrontend,
    normalizeJob,
    normalizeSearchBackend,
    normalizeUserScript,
    createDefaultSearchBackend
} from "./models.js";
import { subscribe } from "./notifications.js";
import {
    isFrontendRunning,
    nextAvailableBind,
    startFrontend,
    stopFrontend
} from "./frontends.js";
import { SCRIPT_TEMPLATES, createScriptFromTemplate, deleteScriptDir, renameScriptDir } from "./scripts.js";
import {
    UserScriptTask,
    deleteTaskLogs,
    jobProblem,
    submitJob,
    taskLogFile,
    taskQueue
} from "./tasks.js";
import { followFile, readLastLines } from "./tail.js";

const ES_PING_TIMEOUT = 5000;
const WHOLE_FILE = 0;

let tesseractLangs = [];

export function detectTesseractLangs() {
    execFile("tesseract", ["--list-langs"], (error, stdout) => {
        if (error) {
            logger.warn(`Could not detect tesseract languages: ${error.message}`);
            return;
        }
        tesseractLangs = stdout
            .split("\n")
            .slice(1)
            .map((line) => line.trim())
            .filter((line) => line !== "");
    });
}

const NAME_REGEX = /^[a-zA-Z0-9-_,.; ]+$/;

// A user script name is used as a folder name under SCRIPT_FOLDER
function validScriptName(name) {
    return typeof name === "string" && NAME_REGEX.test(name) && name !== "." && name !== "..";
}

function getJobOr404(name) {
    const job = jobRepository.get(name);
    if (job === null) {
        throw new HttpError(404, "job not found");
    }
    return job;
}

function getFrontendOr404(name) {
    const frontend = frontendRepository.get(name);
    if (frontend === null) {
        throw new HttpError(404, "frontend not found");
    }
    return frontend;
}

export function defaultSearchBackendName() {
    const backends = searchBackendRepository.list();
    if (backends.length === 0) {
        return null;
    }
    for (const preferred of ["default-elasticsearch", "elasticsearch"]) {
        if (backends.some((backend) => backend.name === preferred)) {
            return preferred;
        }
    }
    return backends[0].name;
}

function withRunning(frontend) {
    return {
        ...frontend,
        running: isFrontendRunning(frontend.name)
    };
}

function pingEs(esUrl, insecure) {
    return new Promise((resolve) => {
        const url = parseEsUrl(esUrl);

        if (url === null) {
            resolve({
                ok: false,
                message: "Invalid URL"
            });
            return;
        }

        const transport = url.protocol === "https:" ? https : http;
        const options = {
            timeout: ES_PING_TIMEOUT,
            rejectUnauthorized: !insecure
        };
        if (url.username !== "") {
            options.auth = `${decodeURIComponent(url.username)}:${decodeURIComponent(url.password)}`;
        }

        const target = `${url.protocol}//${url.host}${url.pathname}`;
        const request = transport.get(target, options, (response) => {
            if (response.statusCode === 401) {
                response.resume();
                resolve({
                    ok: false,
                    message: "Authentication failure"
                });
                return;
            }

            const chunks = [];
            response.on("data", (chunk) => chunks.push(chunk));
            response.on("end", () => {
                try {
                    const parsed = JSON.parse(Buffer.concat(chunks).toString("utf8"));
                    resolve({
                        ok: true,
                        message: `Elasticsearch version ${parsed.version.number}`
                    });
                } catch (e) {
                    resolve({
                        ok: false,
                        message: "Could not read version"
                    });
                }
            });
        });

        request.on("timeout", () => {
            request.destroy();
            resolve({
                ok: false,
                message: "Connection timeout"
            });
        });

        request.on("error", (e) => {
            if (e.code !== undefined && e.code.startsWith("ERR_TLS")) {
                resolve({
                    ok: false,
                    message: "Invalid SSL certificate"
                });
                return;
            }
            if (e.code === "UNABLE_TO_VERIFY_LEAF_SIGNATURE" || e.code === "DEPTH_ZERO_SELF_SIGNED_CERT"
                || e.code === "SELF_SIGNED_CERT_IN_CHAIN" || e.code === "CERT_HAS_EXPIRED") {
                resolve({
                    ok: false,
                    message: "Invalid SSL certificate"
                });
                return;
            }
            resolve({
                ok: false,
                message: `Could not connect to ${url.hostname}:${esUrlPort(url)}`
            });
        });
    });
}

export function createRouter() {
    const router = new Router();

    router.get("/api", () => {
        return {
            tesseract_langs: tesseractLangs,
            logs_folder: LOG_FOLDER,
            user_script_templates: Object.keys(SCRIPT_TEMPLATES)
        };
    });

    // Jobs

    router.get("/api/job", () => jobRepository.list());

    router.get("/api/job/:name", ({ params }) => getJobOr404(params.name));

    router.post("/api/job/:name", ({ params }) => {
        if (jobRepository.get(params.name) !== null) {
            throw new HttpError(409, "job already exists");
        }
        const job = createDefaultJob(params.name);
        // The same backend a new frontend gets: a job with none cannot be indexed
        job.index_options.search_backend = defaultSearchBackendName();
        jobRepository.insert(job);
        return job;
    });

    router.put("/api/job/:name", ({ params, body }) => {
        const existing = getJobOr404(params.name);
        const job = normalizeJob(body, existing);

        for (const field of FULL_SCAN_FIELDS) {
            if (job.scan_options[field] !== existing.scan_options[field]) {
                job.do_full_scan = true;
            }
        }

        try {
            jobRepository.update(job);
        } catch (e) {
            throw new HttpError(400, e.message);
        }
        return job;
    });

    router.delete("/api/job/:name", ({ params }) => {
        const job = getJobOr404(params.name);

        const referencedBy = frontendRepository.jobsReferencing(params.name);
        if (referencedBy.length > 0) {
            throw new HttpError(400, `in use (frontend: ${referencedBy.join(", ")})`);
        }

        if (job.index_path !== null) {
            fs.rmSync(path.join(DATA_FOLDER, job.index_path), { force: true });
        }

        jobRepository.delete(params.name);
    });

    router.post("/api/job/:name/run", ({ params, query }) => {
        const job = getJobOr404(params.name);

        const problem = jobProblem(job);
        if (problem !== null) {
            throw new HttpError(400, problem);
        }

        if (query.get("full") === "true") {
            job.do_full_scan = true;
            jobRepository.update(job);
        }

        submitJob(job, (scriptName) => userScriptRepository.get(scriptName));
    });

    router.get("/api/job/:name/logs_to_delete", ({ params, query }) => {
        const job = getJobOr404(params.name);
        const n = Number(query.get("n"));
        return taskHistoryRepository.logsToDelete(job.name, n);
    });

    // Frontends

    router.get("/api/frontend", () => frontendRepository.list().map(withRunning));

    router.get("/api/frontend/:name", ({ params }) => withRunning(getFrontendOr404(params.name)));

    router.post("/api/frontend/:name", ({ params }) => {
        if (frontendRepository.get(params.name) !== null) {
            throw new HttpError(409, "frontend already exists");
        }
        const frontend = createDefaultFrontend(
            params.name,
            nextAvailableBind(),
            defaultSearchBackendName()
        );
        frontendRepository.insert(frontend);
        return frontend;
    });

    router.put("/api/frontend/:name", ({ params, body }) => {
        getFrontendOr404(params.name);
        const frontend = normalizeFrontend(body, params.name);
        try {
            frontendRepository.update(frontend);
        } catch (e) {
            throw new HttpError(400, e.message);
        }
        return frontend;
    });

    router.delete("/api/frontend/:name", ({ params }) => {
        getFrontendOr404(params.name);
        stopFrontend(params.name);
        frontendRepository.delete(params.name);
    });

    router.post("/api/frontend/:name/start", async ({ params }) => {
        const frontend = getFrontendOr404(params.name);
        const ok = await startFrontend(frontend, null);
        if (!ok) {
            throw new HttpError(500, "frontend failed to start, check logs");
        }
    });

    router.post("/api/frontend/:name/stop", ({ params }) => {
        stopFrontend(params.name);
    });

    // Search backends

    router.get("/api/search_backend", () => searchBackendRepository.list());

    router.get("/api/search_backend/:name", ({ params }) => {
        const backend = searchBackendRepository.get(params.name);
        if (backend === null) {
            throw new HttpError(404, "search backend not found");
        }
        return backend;
    });

    router.post("/api/search_backend/:name", ({ params }) => {
        if (searchBackendRepository.get(params.name) !== null) {
            throw new HttpError(409, "search backend already exists");
        }
        const backend = createDefaultSearchBackend(params.name, "elasticsearch");
        searchBackendRepository.insert(backend);
        return backend;
    });

    router.put("/api/search_backend/:name", ({ params, body }) => {
        if (searchBackendRepository.get(params.name) === null) {
            throw new HttpError(404, "search backend not found");
        }
        const backend = normalizeSearchBackend(body, params.name);
        searchBackendRepository.update(backend);
        return backend;
    });

    router.delete("/api/search_backend/:name", ({ params }) => {
        const backend = searchBackendRepository.get(params.name);
        if (backend === null) {
            throw new HttpError(404, "search backend not found");
        }

        for (const frontend of frontendRepository.list()) {
            if (frontend.web_options.search_backend === params.name) {
                throw new HttpError(400, `in use (frontend: ${frontend.name})`);
            }
        }
        for (const job of jobRepository.list()) {
            if (job.index_options.search_backend === params.name) {
                throw new HttpError(400, `in use (job: ${job.name})`);
            }
        }

        searchBackendRepository.delete(params.name);

        if (backend.search_index !== null) {
            fs.rmSync(path.join(DATA_FOLDER, backend.search_index), { force: true });
        }
    });

    // User scripts

    router.get("/api/user_script", () => userScriptRepository.list());

    router.get("/api/user_script/:name", ({ params }) => {
        const script = userScriptRepository.get(params.name);
        if (script === null) {
            throw new HttpError(404, "user script not found");
        }
        return script;
    });

    router.post("/api/user_script/:name", ({ params, query }) => {
        if (!validScriptName(params.name)) {
            throw new HttpError(400, "invalid name");
        }
        if (userScriptRepository.get(params.name) !== null) {
            throw new HttpError(409, "user script already exists");
        }
        const template = query.get("template");
        let script;
        try {
            script = createScriptFromTemplate(params.name, template);
        } catch (e) {
            throw new HttpError(400, e.message);
        }
        userScriptRepository.insert(script);
        return script;
    });

    router.put("/api/user_script/:name", ({ params, body }) => {
        const previous = userScriptRepository.get(params.name);
        if (previous === null) {
            throw new HttpError(404, "user script not found");
        }

        const script = normalizeUserScript(body, params.name);
        if (previous.git_repository !== script.git_repository) {
            script.force_clone = true;
        }

        userScriptRepository.update(script);
        return script;
    });

    router.post("/api/user_script/:name/rename", ({ params, body }) => {
        const script = userScriptRepository.get(params.name);
        if (script === null) {
            throw new HttpError(404, "user script not found");
        }

        const newName = body === null ? undefined : body.name;
        if (!validScriptName(newName)) {
            throw new HttpError(400, "invalid name");
        }
        if (newName === params.name) {
            return script;
        }
        if (userScriptRepository.get(newName) !== null) {
            throw new HttpError(409, "user script already exists");
        }

        // A queued or running task holds the script directory
        const busy = taskQueue.tasks()
            .some((task) => task instanceof UserScriptTask && task.userScript.name === params.name);
        if (busy) {
            throw new HttpError(400, "in use (task)");
        }

        userScriptRepository.rename(params.name, newName);
        renameScriptDir(script, newName);
        return userScriptRepository.get(newName);
    });

    router.delete("/api/user_script/:name", ({ params }) => {
        const script = userScriptRepository.get(params.name);
        if (script === null) {
            throw new HttpError(404, "user script not found");
        }

        const referencedBy = userScriptRepository.jobsReferencing(params.name);
        if (referencedBy.length > 0) {
            throw new HttpError(400, `in use (job: ${referencedBy.join(", ")})`);
        }

        deleteScriptDir(script);
        userScriptRepository.delete(params.name);
    });

    router.post("/api/user_script/:name/run", ({ params, query }) => {
        const script = userScriptRepository.get(params.name);
        if (script === null) {
            throw new HttpError(404, "user script not found");
        }
        const job = getJobOr404(query.get("job"));

        const task = new UserScriptTask(
            script,
            job.name,
            `Script <${script.name}> [${job.name}]`,
            null
        );
        taskQueue.submit(task);
    });

    // Tasks

    router.get("/api/task", () => taskQueue.tasks().map((task) => task.json()));

    router.get("/api/task/history", () => taskHistoryRepository.listDescending());

    router.post("/api/task/:id/kill", ({ params }) => {
        return taskQueue.kill(params.id);
    });

    router.post("/api/task/:id/delete_logs", ({ params }) => {
        if (taskHistoryRepository.get(params.id) === null) {
            throw new HttpError(404, "task not found");
        }
        deleteTaskLogs(params.id);
    });

    router.get("/api/task/:id/log", ({ res, params, query }) => {
        const logFile = taskLogFile(params.id);
        const n = Number(query.get("n"));
        const sse = openSse(res);

        let startPosition = WHOLE_FILE;
        if (n > 0) {
            for (const line of readLastLines(logFile, n)) {
                sse.send({ line: line });
            }
            try {
                startPosition = fs.statSync(logFile).size;
            } catch (e) {
                startPosition = WHOLE_FILE;
            }
        }

        const stopFollowing = followFile(
            logFile,
            startPosition,
            (line) => sse.send({ line: line })
        );
        sse.onClose(stopFollowing);

        return RESPONSE_HANDLED;
    });

    // Push notifications

    router.get("/api/notifications", ({ res }) => {
        const sse = openSse(res);
        const unsubscribe = subscribe((notification) => sse.send(notification));
        sse.onClose(unsubscribe);
        return RESPONSE_HANDLED;
    });

    // Elasticsearch ping

    router.get("/api/ping_es", ({ query }) => {
        const insecure = query.get("insecure") === "true";
        return pingEs(query.get("url"), insecure);
    });

    return router;
}
