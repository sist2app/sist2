import {test} from "node:test";
import assert from "node:assert/strict";
import {spawn} from "node:child_process";
import {waitForChildExit} from "./child.js";

/** A process that exits straight away */
function quickChild() {
    return spawn(process.execPath, ["-e", "console.log('done')"], {stdio: ["ignore", "pipe", "pipe"]});
}

/** A process that exits while something it started keeps its output open, the way a stuck worker does */
function childWithLingeringWorker() {
    return spawn(process.execPath, [
        "-e",
        "require('node:child_process').spawn('sleep', ['30'], {stdio: 'inherit'}); process.exit(0);"
    ], {stdio: ["ignore", "pipe", "pipe"]});
}

test("a process that finishes normally is waited for", async () => {
    const {code, orphaned} = await waitForChildExit(quickChild());

    assert.equal(code, 0);
    assert.equal(orphaned, false);
});

test("a process whose output is held by something it started still finishes the task", async () => {
    const child = childWithLingeringWorker();

    const started = Date.now();
    const {code, orphaned} = await waitForChildExit(child, 300);

    assert.equal(code, 0);
    assert.equal(orphaned, true);
    // Without this the task would wait for the lingering process, which is 30 seconds away
    assert.ok(Date.now() - started < 5000, `waited ${Date.now() - started}ms`);
});

test("the exit code is reported once, however the process ended", async () => {
    const child = spawn(process.execPath, ["-e", "process.exit(3)"], {stdio: ["ignore", "pipe", "pipe"]});

    const {code, signal} = await waitForChildExit(child);

    assert.equal(code, 3);
    assert.equal(signal, null);
});
