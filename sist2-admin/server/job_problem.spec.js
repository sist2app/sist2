import {test, before, after} from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

/*
 * A job is checked before its tasks are queued. Both of these problems used to be found only once
 * the work was under way: a job with no path scanned everything, and a job naming no search
 * backend ran its whole scan and then failed with "Search backend not found: null".
 */

let dataFolder;
let jobProblem;
let createDefaultJob;

before(async () => {
    dataFolder = fs.mkdtempSync(path.join(os.tmpdir(), "sist2-admin-spec-"));
    process.env.DATA_FOLDER = dataFolder;

    ({jobProblem} = await import("./tasks.js"));
    ({createDefaultJob} = await import("./models.js"));
});

after(() => {
    fs.rmSync(dataFolder, {recursive: true, force: true});
});

/** A path to scan that exists, so that only the backend is in question */
function jobWithPath() {
    const job = createDefaultJob("spec");
    job.scan_options.path = "/tmp";
    return job;
}

test("a job with no path to scan cannot be run", () => {
    const job = createDefaultJob("spec");

    assert.equal(job.scan_options.path, "");
    assert.match(jobProblem(job), /no path to scan/);
});

test("a path of nothing but whitespace is no path at all", () => {
    const job = createDefaultJob("spec");
    job.scan_options.path = "   ";

    assert.match(jobProblem(job), /no path to scan/);
});

test("a job with no search backend cannot be run", () => {
    const job = jobWithPath();

    assert.match(jobProblem(job), /no search backend/);
});

test("a job naming a backend that no longer exists cannot be run", () => {
    const job = jobWithPath();
    job.index_options.search_backend = "deleted-backend";

    assert.match(jobProblem(job), /no longer exists: deleted-backend/);
});

test("a job naming a backend that exists can be run", async () => {
    const {searchBackendRepository} = await import("./db.js");
    const {createDefaultSearchBackend} = await import("./models.js");

    searchBackendRepository.insert(createDefaultSearchBackend("spec-backend", "sqlite"));

    const job = jobWithPath();
    job.index_options.search_backend = "spec-backend";

    assert.equal(jobProblem(job), null);
});
