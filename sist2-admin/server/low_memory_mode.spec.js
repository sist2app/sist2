import assert from "node:assert/strict";
import test from "node:test";

import { createDefaultJob, normalizeJob } from "./models.js";
import { indexArgs, scanArgs } from "./sist2.js";

test("low-memory mode is off by default and can be saved in index-file options", () => {
    const job = createDefaultJob("documents");
    assert.equal(job.index_file_options.low_memory_mode, false);

    const updated = normalizeJob({ index_file_options: { low_memory_mode: true } }, job);
    assert.equal(updated.index_file_options.low_memory_mode, true);
});

test("scan arguments include low-memory mode only when enabled", () => {
    const job = createDefaultJob("documents");

    assert.ok(!scanArgs(job.scan_options, job.index_file_options, "/data/documents.sist2")
        .includes("--low-memory-mode"));
    assert.ok(scanArgs(job.scan_options, { low_memory_mode: true }, "/data/documents.sist2")
        .includes("--low-memory-mode"));
});

test("index arguments include low-memory mode for Elasticsearch and SQLite backends", () => {
    const job = createDefaultJob("documents");
    const elasticsearch = {
        backend_type: "elasticsearch",
        threads: 1,
        es_url: "http://localhost:9200",
        es_index: "sist2",
        batch_size: 70,
        es_insecure_ssl: false
    };
    const sqlite = { backend_type: "sqlite", search_index: "search.sist2" };

    const indexFileOptions = { low_memory_mode: true };

    assert.ok(indexArgs("documents.sist2", job.index_options, indexFileOptions, elasticsearch, null, null)
        .includes("--low-memory-mode"));
    assert.ok(indexArgs("documents.sist2", job.index_options, indexFileOptions, sqlite, null, null)
        .includes("--low-memory-mode"));
});
