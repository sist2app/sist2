import path from "node:path";

import { DATA_FOLDER, LOG_FOLDER } from "./config.js";
import { frontendRepository, jobRepository, searchBackendRepository } from "./db.js";
import { logger } from "./log.js";
import { spawnWeb, webArgs, writeTempFile } from "./sist2.js";
import { readLastLines } from "./tail.js";

const STARTUP_CHECK_DELAY = 300;

const runningFrontends = new Map();

export function frontendLogFile(name) {
    return path.join(LOG_FOLDER, `frontend-${name}.log`);
}

export function isFrontendRunning(name) {
    return runningFrontends.has(name);
}

function sleep(ms) {
    return new Promise((resolve) => {
        setTimeout(resolve, ms);
    });
}

export async function startFrontend(frontend, onLog) {
    const indices = [];
    for (const jobName of frontend.jobs) {
        const job = jobRepository.get(jobName);
        if (job === null || job.index_path === null) {
            logger.warn(`Frontend ${frontend.name}: job ${jobName} has no index yet, skipping`);
            continue;
        }
        indices.push(path.join(DATA_FOLDER, job.index_path));
    }

    if (indices.length === 0) {
        logger.error(`Frontend ${frontend.name}: no indices to serve`);
        return false;
    }

    const backendName = frontend.web_options.search_backend;
    const backend = searchBackendRepository.get(backendName);
    if (backend === null) {
        logger.error(`Search backend not found: ${backendName}`);
        return false;
    }

    const tempFiles = [];
    let auth0PublicKeyFile = null;
    if (frontend.web_options.auth0_public_key) {
        auth0PublicKeyFile = writeTempFile("auth0-public-key", frontend.web_options.auth0_public_key);
        tempFiles.push(auth0PublicKeyFile);
    }

    const args = webArgs(
        frontend.web_options,
        backend,
        indices,
        auth0PublicKeyFile
    );

    const child = spawnWeb(args, frontendLogFile(frontend.name), tempFiles);

    child.on("close", () => {
        if (runningFrontends.get(frontend.name) === child) {
            runningFrontends.delete(frontend.name);
        }
    });

    await sleep(STARTUP_CHECK_DELAY);

    if (child.exitCode !== null) {
        logger.error(`Frontend ${frontend.name} exited too quickly, check ${frontendLogFile(frontend.name)}:`);
        for (const line of readLastLines(frontendLogFile(frontend.name), 3)) {
            logger.error(line);
        }
        return false;
    }

    runningFrontends.set(frontend.name, child);

    if (onLog) {
        onLog({ "sist2-admin": `Started frontend ${frontend.name} pid=${child.pid}` });
    }

    return true;
}

export function stopFrontend(name) {
    const child = runningFrontends.get(name);
    if (child === undefined) {
        return;
    }
    runningFrontends.delete(name);
    child.kill("SIGTERM");
}

export function stopAllFrontends() {
    for (const name of [...runningFrontends.keys()]) {
        stopFrontend(name);
    }
}

export function restartRunningFrontends(onLog) {
    for (const name of [...runningFrontends.keys()]) {
        const frontend = frontendRepository.get(name);
        if (frontend === null) {
            continue;
        }

        const child = runningFrontends.get(name);
        runningFrontends.delete(name);
        child.kill("SIGTERM");

        child.once("close", () => {
            startFrontend(frontend, onLog).catch((e) => {
                logger.error(`Failed to restart frontend ${name}: ${e.message}`);
            });
        });

        if (onLog) {
            onLog({ "sist2-admin": `Restarting frontend ${name}` });
        }
    }
}

export async function startAutoStartFrontends() {
    for (const frontend of frontendRepository.list()) {
        if (frontend.auto_start && frontend.jobs.length > 0) {
            await startFrontend(frontend, null);
        }
    }
}

export function nextAvailableBind() {
    const usedPorts = new Set();
    for (const frontend of frontendRepository.list()) {
        const port = Number(frontend.web_options.bind.split(":").pop());
        usedPorts.add(port);
    }

    let port = 4090;
    while (usedPorts.has(port)) {
        port += 1;
    }
    return `0.0.0.0:${port}`;
}
