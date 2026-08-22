import { spawn } from "node:child_process";

import { waitForChildExit } from "./child.js";
import fs from "node:fs";
import path from "node:path";
import readline from "node:readline";

import { DATA_FOLDER, SIST2_BINARY, TMP_FOLDER } from "./config.js";
import { logger } from "./log.js";

export function scanArgs(scanOptions, outputPath) {
    const options = scanOptions;
    const args = [
        "scan",
        options.path,
        `--threads=${options.threads}`,
        `--thumbnail-quality=${options.thumbnail_quality}`,
        `--thumbnail-count=${options.thumbnail_count}`,
        `--thumbnail-size=${options.thumbnail_size}`,
        `--content-size=${options.content_size}`,
        `--output=${outputPath}`,
        `--depth=${options.depth}`,
        `--archive=${options.archive}`,
        `--mem-buffer=${options.mem_buffer}`
    ];

    if (options.incremental) {
        args.push("--incremental");
    }
    if (options.optimize_index) {
        args.push("--optimize-index");
    }
    if (options.rewrite_url) {
        args.push(`--rewrite-url=${options.rewrite_url}`);
    }
    if (options.name) {
        args.push(`--name=${options.name}`);
    }
    if (options.archive_passphrase) {
        args.push(`--archive-passphrase=${options.archive_passphrase}`);
    }
    if (options.ocr_lang) {
        args.push(`--ocr-lang=${options.ocr_lang}`);
    }
    if (options.ocr_ebooks) {
        args.push("--ocr-ebooks");
    }
    if (options.ocr_images) {
        args.push("--ocr-images");
    }
    if (options.exclude) {
        args.push(`--exclude=${options.exclude}`);
    }
    if (options.fast) {
        args.push("--fast");
    }
    if (options.treemap_threshold) {
        args.push(`--treemap-threshold=${options.treemap_threshold}`);
    }
    if (options.no_stats) {
        args.push("--no-stats");
    }
    if (options.read_subtitles) {
        args.push("--read-subtitles");
    }
    if (options.fast_epub) {
        args.push("--fast-epub");
    }
    if (options.checksums) {
        args.push("--checksums");
    }

    return args;
}

export function indexArgs(indexPath, indexOptions, backend, mappingsFile, settingsFile) {
    const absolutePath = path.join(DATA_FOLDER, indexPath);

    if (backend.backend_type === "sqlite") {
        const searchIndexAbsolute = path.join(DATA_FOLDER, backend.search_index);
        return ["sqlite-index", absolutePath, "--search-index", searchIndexAbsolute];
    }

    const args = [
        "index",
        absolutePath,
        `--threads=${backend.threads}`,
        `--es-url=${backend.es_url}`,
        `--es-index=${backend.es_index}`,
        `--batch-size=${backend.batch_size}`
    ];

    if (backend.es_insecure_ssl) {
        args.push("--es-insecure-ssl");
    }
    if (mappingsFile) {
        args.push(`--mappings-file=${mappingsFile}`);
    }
    if (settingsFile) {
        args.push(`--settings-file=${settingsFile}`);
    }
    if (indexOptions.incremental_index) {
        args.push("--incremental-index");
    }

    return args;
}

export function webArgs(webOptions, backend, indices, auth0PublicKeyFile) {
    const options = webOptions;
    const args = [
        "web",
        `--bind=${options.bind}`,
        `--tagline=${options.tagline}`,
        `--lang=${options.lang}`,
        `--theme=${options.theme}`
    ];

    if (backend.backend_type === "sqlite") {
        const searchIndexAbsolute = path.join(DATA_FOLDER, backend.search_index);
        args.push(`--search-index=${searchIndexAbsolute}`);
    } else {
        args.push(`--es-url=${backend.es_url}`);
        args.push(`--es-index=${backend.es_index}`);
        if (backend.es_insecure_ssl) {
            args.push("--es-insecure-ssl");
        }
    }

    if (options.auth0_audience) {
        args.push(`--auth0-audience=${options.auth0_audience}`);
    }
    if (options.auth0_domain) {
        args.push(`--auth0-domain=${options.auth0_domain}`);
    }
    if (options.auth0_client_id) {
        args.push(`--auth0-client-id=${options.auth0_client_id}`);
    }
    if (auth0PublicKeyFile) {
        args.push(`--auth0-public-key-file=${auth0PublicKeyFile}`);
    }
    if (options.auth) {
        args.push(`--auth=${options.auth}`);
    }
    if (options.tag_auth) {
        args.push(`--tag-auth=${options.tag_auth}`);
    }
    if (options.dev) {
        args.push("--dev");
    }
    if (options.verbose) {
        args.push("--very-verbose");
    }

    args.push(...indices);

    return args;
}

export function writeTempFile(prefix, content) {
    const file = path.join(TMP_FOLDER, `${prefix}-${crypto.randomUUID()}.txt`);
    fs.writeFileSync(file, content);
    return file;
}

export function sweepTempFolder() {
    for (const file of fs.readdirSync(TMP_FOLDER)) {
        fs.rmSync(path.join(TMP_FOLDER, file), { force: true });
    }
}

function pumpJsonLines(stream, onLog) {
    const lines = readline.createInterface({ input: stream });
    lines.on("line", (line) => {
        if (line.trim() === "") {
            return;
        }
        try {
            onLog(JSON.parse(line));
        } catch (e) {
            onLog({ "sist2-admin": `Could not decode log line: ${line}` });
        }
    });
}

function pumpRawLines(stream, key, onLog) {
    const lines = readline.createInterface({ input: stream });
    lines.on("line", (line) => {
        if (line.trim() === "") {
            return;
        }
        onLog({ [key]: line });
    });
}

export function runSist2(args, { onLog, onSpawn, tempFiles = [] }) {
    const fullArgs = [...args, "--json-logs", "--very-verbose"];

    onLog({ "sist2-admin": `Starting sist2 command with args ${JSON.stringify(fullArgs)}` });

    return new Promise((resolve) => {
        const child = spawn(SIST2_BINARY, fullArgs, { stdio: ["ignore", "pipe", "pipe"] });

        onSpawn(child);

        pumpJsonLines(child.stdout, onLog);
        pumpRawLines(child.stderr, "stderr", onLog);

        child.on("error", (e) => {
            onLog({ "sist2-admin": `Failed to start sist2: ${e.message}` });
            resolve(-1);
        });

        waitForChildExit(child).then(({ code, signal, orphaned }) => {
            for (const file of tempFiles) {
                fs.rmSync(file, { force: true });
            }

            if (orphaned) {
                onLog({
                    "sist2-admin": "sist2 has exited, but something it started is still running and "
                        + "holding on to its output. The task is finished; that process is not."
                });
            }

            if (code === null) {
                onLog({ "sist2-admin": `sist2 terminated by signal ${signal}` });
                resolve(-1);
                return;
            }
            resolve(code);
        });
    });
}

export function spawnWeb(args, logFile, tempFiles = []) {
    logger.info(`Starting frontend ${SIST2_BINARY} ${args.join(" ")}`);

    const logStream = fs.createWriteStream(logFile, { flags: "a" });
    const onLog = (payload) => {
        logStream.write(`${JSON.stringify(payload)}\n`);
    };

    const child = spawn(SIST2_BINARY, args, { stdio: ["ignore", "pipe", "pipe"] });

    pumpJsonLines(child.stdout, onLog);
    pumpRawLines(child.stderr, "stderr", onLog);

    child.on("error", (e) => {
        logger.error(`Failed to start frontend: ${e.message}`);
    });

    child.on("close", (code) => {
        logger.info(`Web frontend exited with return code ${code}`);
        for (const file of tempFiles) {
            fs.rmSync(file, { force: true });
        }
        logStream.end();
    });

    return child;
}
