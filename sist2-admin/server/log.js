import fs from "node:fs";
import path from "node:path";

import { LOG_FOLDER } from "./config.js";

const LOG_FILE = path.join(LOG_FOLDER, "sist2-admin.log");
const LOG_MAX_SIZE = 1024 * 1024;

function rotateIfNeeded() {
    let stats;
    try {
        stats = fs.statSync(LOG_FILE);
    } catch (e) {
        return;
    }

    if (stats.size < LOG_MAX_SIZE) {
        return;
    }

    fs.renameSync(LOG_FILE, `${LOG_FILE}.1`);
}

function write(level, message) {
    const line = `${new Date().toISOString()} [${level}] ${message}`;

    console.log(line);

    rotateIfNeeded();
    fs.appendFileSync(LOG_FILE, `${line}\n`);
}

export const logger = {
    info(message) {
        write("INFO", message);
    },
    warn(message) {
        write("WARNING", message);
    },
    error(message) {
        write("ERROR", message);
    }
};
