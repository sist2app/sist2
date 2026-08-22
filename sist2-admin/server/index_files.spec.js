import {test, before, after, beforeEach} from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

/*
 * Every scan writes an index file into the data folder. A job that is deleted or a scan that is
 * killed leaves one behind, and there was no way to find out which files those were.
 */

let dataFolder;
let listIndexFiles;
let deleteIndexFile;
let jobRepository;
let searchBackendRepository;
let createDefaultJob;
let createDefaultSearchBackend;

before(async () => {
    dataFolder = fs.mkdtempSync(path.join(os.tmpdir(), "sist2-admin-indexes-"));
    process.env.DATA_FOLDER = dataFolder;

    ({listIndexFiles, deleteIndexFile} = await import("./index_files.js"));
    ({jobRepository, searchBackendRepository} = await import("./db.js"));
    ({createDefaultJob, createDefaultSearchBackend} = await import("./models.js"));
});

after(() => {
    fs.rmSync(dataFolder, {recursive: true, force: true});
});

beforeEach(() => {
    for (const name of fs.readdirSync(dataFolder)) {
        if (name.endsWith(".sist2")) {
            fs.rmSync(path.join(dataFolder, name), {force: true});
        }
    }
});

function writeIndex(name, size = 0) {
    fs.writeFileSync(path.join(dataFolder, name), Buffer.alloc(size));
}

test("an index no job or backend refers to is reported as unused", () => {
    writeIndex("scan-gone-2024-01-01.sist2", 128);

    const files = listIndexFiles();
    const orphan = files.find((file) => file.name === "scan-gone-2024-01-01.sist2");

    assert.equal(orphan.used_by, null);
    assert.equal(orphan.size, 128);
});

test("the index of a job says which job that is", () => {
    writeIndex("scan-kept-2024-02-02.sist2");

    const job = createDefaultJob("keeper");
    job.index_path = "scan-kept-2024-02-02.sist2";
    jobRepository.insert(job);

    const file = listIndexFiles().find((candidate) => candidate.name === "scan-kept-2024-02-02.sist2");

    assert.equal(file.used_by, "job keeper");
});

test("the search index of a backend says which backend that is", () => {
    searchBackendRepository.insert(createDefaultSearchBackend("spec-sqlite", "sqlite"));
    writeIndex("search-index-spec-sqlite.sist2");

    const file = listIndexFiles().find((candidate) => candidate.name === "search-index-spec-sqlite.sist2");

    assert.equal(file.used_by, "search backend spec-sqlite");
});

test("an unused index can be deleted", () => {
    writeIndex("scan-unused-2024-03-03.sist2");

    assert.equal(deleteIndexFile("scan-unused-2024-03-03.sist2"), null);
    assert.equal(fs.existsSync(path.join(dataFolder, "scan-unused-2024-03-03.sist2")), false);
});

test("an index in use is kept", () => {
    writeIndex("scan-kept-2024-02-02.sist2");

    assert.match(deleteIndexFile("scan-kept-2024-02-02.sist2"), /In use by job keeper/);
    assert.equal(fs.existsSync(path.join(dataFolder, "scan-kept-2024-02-02.sist2")), true);
});

test("nothing outside the data folder can be deleted", () => {
    assert.match(deleteIndexFile("../../etc/passwd"), /Not an index file/);
    assert.match(deleteIndexFile("state.db"), /Not an index file/);
    assert.match(deleteIndexFile("no-such-file.sist2"), /No such index file/);
});
