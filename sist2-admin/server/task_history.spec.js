import {test, before, after} from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

/*
 * A task used to be written to the history only once it was over, so a job that hung, or an admin
 * that was restarted while a scan was running, left no trace of the task at all.
 */

let dataFolder;
let taskQueue;
let taskHistoryRepository;

before(async () => {
    dataFolder = fs.mkdtempSync(path.join(os.tmpdir(), "sist2-admin-spec-"));
    process.env.DATA_FOLDER = dataFolder;

    ({taskQueue} = await import("./tasks.js"));
    ({taskHistoryRepository} = await import("./db.js"));
});

after(() => {
    fs.rmSync(dataFolder, {recursive: true, force: true});
});

/** A task the test finishes by hand, standing in for a sist2 process */
function pendingTask(id) {
    let finish;
    const done = new Promise((resolve) => finish = resolve);

    return {
        id: id,
        jobName: "job",
        displayName: `Task ${id}`,
        dependsOn: null,
        started: null,
        ended: null,
        child: null,
        run: () => done,
        log: () => undefined,
        closeLog: () => undefined,
        finish: finish
    };
}

function historyRow(id) {
    return taskHistoryRepository.get(id);
}

async function waitFor(predicate) {
    for (let i = 0; i < 100; i++) {
        if (predicate()) {
            return;
        }
        await new Promise((resolve) => setTimeout(resolve, 10));
    }
    throw new Error("Timed out");
}

const scan = pendingTask("scan-task");
const hung = pendingTask("hung-task");

test("a task that is still running has a history row", () => {
    taskQueue.submit(scan);
    taskQueue._tick();

    const row = historyRow(scan.id);
    assert.notEqual(row, null);
    assert.equal(row.name, "Task scan-task");
    assert.equal(row.started, scan.started);
    assert.equal(row.ended, null);
    assert.equal(row.return_code, null);
});

test("a task that is still running does not satisfy a dependency", () => {
    assert.equal(taskHistoryRepository.doneIds().has(scan.id), false);
});

test("the history row is completed when the task ends", async () => {
    scan.finish(0);
    await waitFor(() => historyRow(scan.id).return_code !== null);

    const row = historyRow(scan.id);
    assert.equal(row.return_code, 0);
    assert.notEqual(row.ended, null);
    assert.equal(taskHistoryRepository.doneIds().has(scan.id), true);
});

test("a task left running by a previous process is written off", () => {
    taskQueue.submit(hung);
    taskQueue._tick();
    assert.equal(historyRow(hung.id).return_code, null);

    taskHistoryRepository.writeOffUnfinished(-2);

    assert.equal(historyRow(hung.id).return_code, -2);
    assert.equal(historyRow(scan.id).return_code, 0);
});
