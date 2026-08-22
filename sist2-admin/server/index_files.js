import fs from "node:fs";
import path from "node:path";

import { DATA_FOLDER } from "./config.js";
import { jobRepository, searchBackendRepository } from "./db.js";

/**
 * The index files in the data folder, and what still needs each one.
 *
 * A scan that was killed, or a job deleted by an older version of sist2-admin, leaves its index
 * behind: the files pile up with nothing pointing at them and no way to tell which is which.
 *
 * @returns {{name: string, size: number, modified: string, used_by: string|null}[]}
 */
export function listIndexFiles() {
    const usedBy = new Map();

    for (const job of jobRepository.list()) {
        for (const indexPath of [job.index_path, job.previous_index_path]) {
            if (indexPath !== null && indexPath !== undefined) {
                usedBy.set(path.basename(indexPath), `job ${job.name}`);
            }
        }
    }

    for (const backend of searchBackendRepository.list()) {
        if (backend.search_index !== null && backend.search_index !== undefined) {
            usedBy.set(path.basename(backend.search_index), `search backend ${backend.name}`);
        }
    }

    return fs.readdirSync(DATA_FOLDER)
        .filter((name) => name.endsWith(".sist2"))
        .map((name) => {
            const stat = fs.statSync(path.join(DATA_FOLDER, name));

            return {
                name: name,
                size: stat.size,
                modified: stat.mtime.toISOString(),
                used_by: usedBy.get(name) ?? null
            };
        })
        .sort((a, b) => a.name.localeCompare(b.name));
}

/**
 * Deletes an index file that nothing is using.
 *
 * @returns {string|null} why it was not deleted, or null when it was
 */
export function deleteIndexFile(name) {
    if (name !== path.basename(name) || !name.endsWith(".sist2")) {
        return "Not an index file";
    }

    const file = listIndexFiles().find((candidate) => candidate.name === name);

    if (file === undefined) {
        return "No such index file";
    }

    if (file.used_by !== null) {
        return `In use by ${file.used_by}`;
    }

    fs.rmSync(path.join(DATA_FOLDER, name), { force: true });

    return null;
}
