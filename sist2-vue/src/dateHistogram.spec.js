import {test} from "node:test";
import assert from "node:assert/strict";
import {dateHistogramBins} from "./util.js";

const MONTH = 2629800;

test("dateHistogramBins keeps every bucket the index reports", () => {
    // Level counts used to leave nothing to draw: every bar was at the 90th percentile
    const bins = dateHistogramBins([
        {bucket: 3 * MONTH, count: 4},
        {bucket: MONTH, count: 4},
        {bucket: 2 * MONTH, count: 4}
    ]);

    assert.equal(bins.length, 3);
    assert.deepEqual(bins.map(b => b.length), [4, 4, 4]);
});

test("dateHistogramBins puts the buckets in time order and spans a month each", () => {
    const bins = dateHistogramBins([
        {bucket: 2 * MONTH, count: 1},
        {bucket: MONTH, count: 9}
    ]);

    assert.deepEqual(bins.map(b => b.x0), [MONTH, 2 * MONTH]);
    assert.deepEqual(bins.map(b => b.x1), [2 * MONTH, 3 * MONTH]);
    assert.deepEqual(bins.map(b => b.length), [9, 1]);
});

test("dateHistogramBins reads the numbers the API sends as strings", () => {
    const bins = dateHistogramBins([{bucket: "1000000", count: "7"}]);

    assert.deepEqual(bins, [{length: 7, x0: 1000000, x1: 1000000 + MONTH}]);
});
