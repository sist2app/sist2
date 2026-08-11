import fs from "node:fs";
import http from "node:http";

import { SIST2_BINARY, WEBSERVER_PORT } from "./config.js";
import {
    frontendRepository,
    jobRepository,
    searchBackendRepository,
    userScriptRepository
} from "./db.js";
import { logger } from "./log.js";
import { createDefaultFrontend, createDefaultSearchBackend } from "./models.js";
import { createRouter, defaultSearchBackendName, detectTesseractLangs } from "./api.js";
import { handleRequest } from "./http.js";
import { sweepTempFolder } from "./sist2.js";
import { cronMatches, startCron } from "./cron.js";
import { submitJob, taskQueue } from "./tasks.js";
import { startAutoStartFrontends, stopAllFrontends } from "./frontends.js";

function initializeDefaults() {
    if (searchBackendRepository.list().length === 0) {
        searchBackendRepository.insert(createDefaultSearchBackend("default-elasticsearch", "elasticsearch"));
        searchBackendRepository.insert(createDefaultSearchBackend("default-sqlite", "sqlite"));
        logger.info("Created default search backends");
    }

    if (frontendRepository.list().length === 0) {
        frontendRepository.insert(
            createDefaultFrontend(
                "default",
                "0.0.0.0:4090",
                defaultSearchBackendName()
            )
        );
        logger.info("Created default frontend");
    }
}

function onCronTick(now) {
    for (const job of jobRepository.list()) {
        if (!job.schedule_enabled) {
            continue;
        }

        try {
            if (cronMatches(job.cron_expression, now)) {
                logger.info(`Cron triggered job ${job.name}`);
                submitJob(job, (scriptName) => userScriptRepository.get(scriptName));
            }
        } catch (e) {
            logger.error(`Invalid cron expression for job ${job.name}: ${e.message}`);
        }
    }
}

function shutdown() {
    logger.info("Shutting down");
    taskQueue.killAll();
    stopAllFrontends();
    process.exit(0);
}

if (SIST2_BINARY.includes("/") && !fs.existsSync(SIST2_BINARY)) {
    logger.warn(`sist2 binary not found at ${SIST2_BINARY}; set the SIST2_BINARY environment variable`);
}

sweepTempFolder();
initializeDefaults();
detectTesseractLangs();

taskQueue.start();
startCron(onCronTick);

await startAutoStartFrontends();

const router = createRouter();
const server = http.createServer((req, res) => {
    handleRequest(router, req, res);
});

server.listen(WEBSERVER_PORT, "0.0.0.0", () => {
    logger.info(`Started sist2-admin on port ${WEBSERVER_PORT}. Hello!`);
});

process.on("SIGTERM", shutdown);
process.on("SIGINT", shutdown);
