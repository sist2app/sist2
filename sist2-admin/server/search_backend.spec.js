import {test, before, after} from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

/*
 * A job names its search backend, and the index task looks it up when it runs. A job that names
 * none used to run its scan and only then fail with "Search backend not found: null".
 */

let dataFolder;
let searchBackendProblem;
let createDefaultJob;

before(async () => {
    dataFolder = fs.mkdtempSync(path.join(os.tmpdir(), "sist2-admin-spec-"));
    process.env.DATA_FOLDER = dataFolder;

    ({searchBackendProblem} = await import("./tasks.js"));
    ({createDefaultJob} = await import("./models.js"));
});

after(() => {
    fs.rmSync(dataFolder, {recursive: true, force: true});
});

test("a job with no search backend cannot be run", () => {
    const job = createDefaultJob("spec");

    assert.match(searchBackendProblem(job), /no search backend/);
});

test("a job naming a backend that no longer exists cannot be run", () => {
    const job = createDefaultJob("spec");
    job.index_options.search_backend = "deleted-backend";

    assert.match(searchBackendProblem(job), /no longer exists: deleted-backend/);
});

test("a job naming a backend that exists can be run", async () => {
    const {searchBackendRepository} = await import("./db.js");
    const {createDefaultSearchBackend} = await import("./models.js");

    searchBackendRepository.insert(createDefaultSearchBackend("spec-backend", "sqlite"));

    const job = createDefaultJob("spec");
    job.index_options.search_backend = "spec-backend";

    assert.equal(searchBackendProblem(job), null);
});
