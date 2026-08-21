import fs from "node:fs";
import path from "node:path";
import { DatabaseSync } from "node:sqlite";

import { DB_FILE, MIGRATIONS_FOLDER } from "./config.js";
import { logger } from "./log.js";
import {
    JOB_FIELD_TYPES,
    SCAN_OPTION_TYPES,
    SEARCH_BACKEND_TYPES,
    USER_SCRIPT_TYPES,
    WEB_OPTION_TYPES,
    frontendToParams,
    jobToParams,
    rowToFrontend,
    rowToJob,
    rowToSearchBackend,
    rowToUserScript,
    searchBackendToParams,
    userScriptToParams
} from "./models.js";

const LEGACY_BACKUP_SUFFIX = ".python.bak";

function renameLegacyDatabase() {
    if (!fs.existsSync(DB_FILE)) {
        return;
    }

    const probe = new DatabaseSync(DB_FILE, { readOnly: true });
    const legacyTable = probe
        .prepare("SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'sist2_admin'")
        .get();
    probe.close();

    if (legacyTable === undefined) {
        return;
    }

    const backupFile = DB_FILE + LEGACY_BACKUP_SUFFIX;
    fs.renameSync(DB_FILE, backupFile);
    logger.warn(`Found Python sist2-admin database; state was NOT migrated. Old file moved to ${backupFile}`);
}

renameLegacyDatabase();

const connection = new DatabaseSync(DB_FILE);
connection.exec("PRAGMA journal_mode = WAL");
connection.exec("PRAGMA foreign_keys = ON");

export function transaction(fn) {
    connection.exec("BEGIN");
    try {
        const result = fn();
        connection.exec("COMMIT");
        return result;
    } catch (e) {
        connection.exec("ROLLBACK");
        throw e;
    }
}

function applyMigrations() {
    connection.exec(
        "CREATE TABLE IF NOT EXISTS migration (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, applied_at TEXT NOT NULL)"
    );

    const applied = new Set();
    for (const row of connection.prepare("SELECT name FROM migration").all()) {
        applied.add(row.name);
    }

    const files = fs.readdirSync(MIGRATIONS_FOLDER)
        .filter((file) => file.endsWith(".sql"))
        .sort();

    for (const file of files) {
        if (applied.has(file)) {
            continue;
        }

        const sql = fs.readFileSync(path.join(MIGRATIONS_FOLDER, file), "utf8");
        transaction(() => {
            connection.exec(sql);
            connection
                .prepare("INSERT INTO migration (name, applied_at) VALUES (:name, :applied_at)")
                .run({ name: file, applied_at: new Date().toISOString() });
        });
        logger.info(`Applied database migration ${file}`);
    }
}

applyMigrations();

const JOB_COLUMNS = [
    "name",
    ...Object.keys(JOB_FIELD_TYPES),
    "search_backend",
    "incremental_index",
    ...Object.keys(SCAN_OPTION_TYPES).map((field) => `scan_${field}`)
];

const FRONTEND_COLUMNS = [
    "name",
    "auto_start",
    "extra_query_args",
    "custom_url",
    ...Object.keys(WEB_OPTION_TYPES).map((field) => `web_${field}`)
];

const SEARCH_BACKEND_COLUMNS = [
    "name",
    ...Object.keys(SEARCH_BACKEND_TYPES)
];

const USER_SCRIPT_COLUMNS = [
    "name",
    ...Object.keys(USER_SCRIPT_TYPES)
];

function insertSql(table, columns) {
    const bindings = columns.map((column) => `:${column}`).join(", ");
    return `INSERT INTO ${table} (${columns.join(", ")}) VALUES (${bindings})`;
}

function updateSql(table, columns) {
    const assignments = columns
        .filter((column) => column !== "name")
        .map((column) => `${column} = :${column}`)
        .join(", ");
    return `UPDATE ${table} SET ${assignments} WHERE name = :name`;
}

function selectSql(table, columns) {
    return `SELECT ${columns.join(", ")} FROM ${table}`;
}

function jobUserScripts(jobName) {
    const rows = connection
        .prepare("SELECT script_name FROM job_user_script WHERE job_name = :job_name ORDER BY position")
        .all({ job_name: jobName });
    return rows.map((row) => row.script_name);
}

function writeJobUserScripts(jobName, scriptNames) {
    connection
        .prepare("DELETE FROM job_user_script WHERE job_name = :job_name")
        .run({ job_name: jobName });

    const insert = connection.prepare(
        "INSERT INTO job_user_script (job_name, script_name, position) VALUES (:job_name, :script_name, :position)"
    );
    for (const [position, scriptName] of scriptNames.entries()) {
        insert.run({
            job_name: jobName,
            script_name: scriptName,
            position: position
        });
    }
}

export const jobRepository = {
    list() {
        const rows = connection.prepare(`${selectSql("job", JOB_COLUMNS)} ORDER BY name`).all();
        return rows.map((row) => rowToJob(row, jobUserScripts(row.name)));
    },
    get(name) {
        const row = connection
            .prepare(`${selectSql("job", JOB_COLUMNS)} WHERE name = :name`)
            .get({ name: name });
        if (row === undefined) {
            return null;
        }
        return rowToJob(row, jobUserScripts(row.name));
    },
    insert(job) {
        transaction(() => {
            connection.prepare(insertSql("job", JOB_COLUMNS)).run(jobToParams(job));
            writeJobUserScripts(job.name, job.user_scripts);
        });
    },
    update(job) {
        transaction(() => {
            connection.prepare(updateSql("job", JOB_COLUMNS)).run(jobToParams(job));
            writeJobUserScripts(job.name, job.user_scripts);
        });
    },
    delete(name) {
        connection.prepare("DELETE FROM job WHERE name = :name").run({ name: name });
    }
};

function frontendJobs(frontendName) {
    const rows = connection
        .prepare("SELECT job_name FROM frontend_job WHERE frontend_name = :frontend_name ORDER BY job_name")
        .all({ frontend_name: frontendName });
    return rows.map((row) => row.job_name);
}

function writeFrontendJobs(frontendName, jobNames) {
    connection
        .prepare("DELETE FROM frontend_job WHERE frontend_name = :frontend_name")
        .run({ frontend_name: frontendName });

    const insert = connection.prepare(
        "INSERT INTO frontend_job (frontend_name, job_name) VALUES (:frontend_name, :job_name)"
    );
    for (const jobName of jobNames) {
        insert.run({
            frontend_name: frontendName,
            job_name: jobName
        });
    }
}

export const frontendRepository = {
    list() {
        const rows = connection.prepare(`${selectSql("frontend", FRONTEND_COLUMNS)} ORDER BY name`).all();
        return rows.map((row) => rowToFrontend(row, frontendJobs(row.name)));
    },
    get(name) {
        const row = connection
            .prepare(`${selectSql("frontend", FRONTEND_COLUMNS)} WHERE name = :name`)
            .get({ name: name });
        if (row === undefined) {
            return null;
        }
        return rowToFrontend(row, frontendJobs(row.name));
    },
    insert(frontend) {
        transaction(() => {
            connection.prepare(insertSql("frontend", FRONTEND_COLUMNS)).run(frontendToParams(frontend));
            writeFrontendJobs(frontend.name, frontend.jobs);
        });
    },
    update(frontend) {
        transaction(() => {
            connection.prepare(updateSql("frontend", FRONTEND_COLUMNS)).run(frontendToParams(frontend));
            writeFrontendJobs(frontend.name, frontend.jobs);
        });
    },
    delete(name) {
        connection.prepare("DELETE FROM frontend WHERE name = :name").run({ name: name });
    },
    jobsReferencing(jobName) {
        const rows = connection
            .prepare("SELECT frontend_name FROM frontend_job WHERE job_name = :job_name")
            .all({ job_name: jobName });
        return rows.map((row) => row.frontend_name);
    }
};

export const searchBackendRepository = {
    list() {
        const rows = connection
            .prepare(`${selectSql("search_backend", SEARCH_BACKEND_COLUMNS)} ORDER BY name`)
            .all();
        return rows.map(rowToSearchBackend);
    },
    get(name) {
        const row = connection
            .prepare(`${selectSql("search_backend", SEARCH_BACKEND_COLUMNS)} WHERE name = :name`)
            .get({ name: name });
        if (row === undefined) {
            return null;
        }
        return rowToSearchBackend(row);
    },
    insert(backend) {
        connection.prepare(insertSql("search_backend", SEARCH_BACKEND_COLUMNS)).run(searchBackendToParams(backend));
    },
    update(backend) {
        connection.prepare(updateSql("search_backend", SEARCH_BACKEND_COLUMNS)).run(searchBackendToParams(backend));
    },
    delete(name) {
        connection.prepare("DELETE FROM search_backend WHERE name = :name").run({ name: name });
    }
};

export const userScriptRepository = {
    list() {
        const rows = connection
            .prepare(`${selectSql("user_script", USER_SCRIPT_COLUMNS)} ORDER BY name`)
            .all();
        return rows.map(rowToUserScript);
    },
    get(name) {
        const row = connection
            .prepare(`${selectSql("user_script", USER_SCRIPT_COLUMNS)} WHERE name = :name`)
            .get({ name: name });
        if (row === undefined) {
            return null;
        }
        return rowToUserScript(row);
    },
    insert(script) {
        connection.prepare(insertSql("user_script", USER_SCRIPT_COLUMNS)).run(userScriptToParams(script));
    },
    update(script) {
        connection.prepare(updateSql("user_script", USER_SCRIPT_COLUMNS)).run(userScriptToParams(script));
    },
    delete(name) {
        connection.prepare("DELETE FROM user_script WHERE name = :name").run({ name: name });
    },
    rename(name, newName) {
        const params = { name: name, new_name: newName };
        transaction(() => {
            // Checked at commit, so the job rows can follow the script row
            connection.exec("PRAGMA defer_foreign_keys = ON");
            connection.prepare("UPDATE user_script SET name = :new_name WHERE name = :name").run(params);
            connection
                .prepare("UPDATE job_user_script SET script_name = :new_name WHERE script_name = :name")
                .run(params);
        });
    },
    jobsReferencing(scriptName) {
        const rows = connection
            .prepare("SELECT job_name FROM job_user_script WHERE script_name = :script_name")
            .all({ script_name: scriptName });
        return rows.map((row) => row.job_name);
    }
};

const TASK_DONE_COLUMNS = [
    "id",
    "name",
    "job_name",
    "started",
    "ended",
    "return_code",
    "has_logs"
];

export const taskHistoryRepository = {
    insert(row) {
        connection.prepare(insertSql("task_done", TASK_DONE_COLUMNS)).run(row);
    },
    get(taskId) {
        const row = connection
            .prepare(`${selectSql("task_done", TASK_DONE_COLUMNS)} WHERE id = :id`)
            .get({ id: taskId });
        if (row === undefined) {
            return null;
        }
        return row;
    },
    listDescending() {
        return connection
            .prepare(`${selectSql("task_done", TASK_DONE_COLUMNS)} ORDER BY started DESC`)
            .all();
    },
    doneIds() {
        const done = new Set();
        for (const row of connection.prepare("SELECT id FROM task_done").all()) {
            done.add(row.id);
        }
        return done;
    },
    failedIds() {
        const failed = new Set();
        for (const row of connection.prepare("SELECT id FROM task_done WHERE return_code != 0").all()) {
            failed.add(row.id);
        }
        return failed;
    },
    setHasLogs(taskId, hasLogs) {
        connection
            .prepare("UPDATE task_done SET has_logs = :has_logs WHERE id = :id")
            .run({
                id: taskId,
                has_logs: hasLogs
            });
    },
    logsToDelete(jobName, keepLastN) {
        if (keepLastN < 0) {
            return [];
        }
        return connection
            .prepare(
                `SELECT id, name, job_name, started, ended, return_code, has_logs
                 FROM task_done
                 WHERE has_logs = 1 AND job_name = :job_name
                 ORDER BY started DESC
                 LIMIT -1 OFFSET :keep_last_n`
            )
            .all({
                job_name: jobName,
                keep_last_n: keepLastN
            });
    }
};
