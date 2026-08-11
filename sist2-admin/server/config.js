import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

function envOr(name, fallback) {
    const value = process.env[name];
    if (value === undefined || value === "") {
        return fallback;
    }
    return value;
}

const SERVER_DIR = path.dirname(fileURLToPath(import.meta.url));
const ADMIN_ROOT = path.join(SERVER_DIR, "..");
const REPO_ROOT = path.join(ADMIN_ROOT, "..");

function defaultBinary() {
    const candidates = [
        path.join(REPO_ROOT, "build", "sist2"),
        path.join(REPO_ROOT, "build", "sist2_debug")
    ];
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }
    return "sist2";
}

export const SIST2_BINARY = envOr("SIST2_BINARY", defaultBinary());
export const DATA_FOLDER = envOr("DATA_FOLDER", path.join(ADMIN_ROOT, "data"));
export const LOG_FOLDER = path.join(DATA_FOLDER, "logs");
export const SCRIPT_FOLDER = path.join(DATA_FOLDER, "scripts");
export const TMP_FOLDER = path.join(DATA_FOLDER, "tmp");
export const WEBSERVER_PORT = Number(envOr("ADMIN_PORT", "8080"));
export const DB_FILE = path.join(DATA_FOLDER, "state.db");
export const FRONTEND_DIST = path.join(SERVER_DIR, "..", "frontend", "dist");
export const MIGRATIONS_FOLDER = path.join(SERVER_DIR, "migrations");

for (const folder of [DATA_FOLDER, LOG_FOLDER, SCRIPT_FOLDER, TMP_FOLDER]) {
    fs.mkdirSync(folder, { recursive: true });
}
