import {test} from "node:test";
import assert from "node:assert/strict";
import {cronMatches} from "./cron.js";

/** A local date, the way the scheduler compares them */
function at(hour, minute, day = 15, month = 5, year = 2026) {
    return new Date(year, month - 1, day, hour, minute);
}

test("a step value runs every nth hour", () => {
    assert.equal(cronMatches("0 */2 * * *", at(0, 0)), true);
    assert.equal(cronMatches("0 */2 * * *", at(2, 0)), true);
    assert.equal(cronMatches("0 */2 * * *", at(1, 0)), false);
});

test("a step value runs every nth minute", () => {
    assert.equal(cronMatches("*/15 * * * *", at(3, 0)), true);
    assert.equal(cronMatches("*/15 * * * *", at(3, 15)), true);
    assert.equal(cronMatches("*/15 * * * *", at(3, 16)), false);
});

test("a step value counts from where its range starts", () => {
    assert.equal(cronMatches("5/10 * * * *", at(9, 5)), true);
    assert.equal(cronMatches("5/10 * * * *", at(9, 25)), true);
    assert.equal(cronMatches("5/10 * * * *", at(9, 26)), false);

    assert.equal(cronMatches("0-30/10 * * * *", at(9, 20)), true);
    assert.equal(cronMatches("0-30/10 * * * *", at(9, 25)), false);
    // The step stops at the end of its range
    assert.equal(cronMatches("0-30/10 * * * *", at(9, 40)), false);
});

test("a step value works on the day of the month", () => {
    assert.equal(cronMatches("0 0 */3 * *", at(0, 0, 4)), true);
    assert.equal(cronMatches("0 0 */3 * *", at(0, 0, 5)), false);
});

test("a list of values matches any of them", () => {
    assert.equal(cronMatches("0,30 * * * *", at(9, 30)), true);
    assert.equal(cronMatches("0,30 * * * *", at(9, 31)), false);
});

test("7 means Sunday, the same as 0", () => {
    const sunday = new Date(2026, 4, 17, 0, 0);
    assert.equal(sunday.getDay(), 0);

    assert.equal(cronMatches("0 0 * * 7", sunday), true);
    assert.equal(cronMatches("0 0 * * 0", sunday), true);
});

test("every field has to match", () => {
    assert.equal(cronMatches("0 12 * * *", at(12, 0)), true);
    assert.equal(cronMatches("0 12 * * *", at(13, 0)), false);
    assert.equal(cronMatches("30 12 15 5 *", at(12, 30, 15, 5)), true);
    assert.equal(cronMatches("30 12 16 5 *", at(12, 30, 15, 5)), false);
});

test("an expression that cannot be read is rejected rather than ignored", () => {
    assert.throws(() => cronMatches("*/0 * * * *", at(0, 0)), /Invalid cron step/);
    assert.throws(() => cronMatches("0 0 * *", at(0, 0)), /Invalid cron expression/);
    assert.throws(() => cronMatches("99 * * * *", at(0, 0)), /Invalid cron field/);
});
