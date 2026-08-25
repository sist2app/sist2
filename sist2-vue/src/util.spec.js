import {test} from "node:test";
import assert from "node:assert/strict";
import {escapeHtml, excerptText, highlightHtml, queryTerms} from "./util.js";

test("escapeHtml neutralizes markup", () => {
    assert.equal(
        escapeHtml("Sun'><img src=x onerror=alert(1)>set.jpg"),
        "Sun&#39;&gt;&lt;img src=x onerror=alert(1)&gt;set.jpg"
    );
});

test("escapeHtml leaves plain text alone", () => {
    assert.equal(escapeHtml("Report 2024.pdf"), "Report 2024.pdf");
});

test("escapeHtml accepts non-string values", () => {
    assert.equal(escapeHtml(42), "42");
});

test("highlightHtml keeps mark tags and escapes the rest", () => {
    assert.equal(
        highlightHtml("<mark>Sun</mark><img src=x onerror=alert(1)>"),
        "<mark>Sun</mark>&lt;img src=x onerror=alert(1)&gt;"
    );
});

test("highlightHtml does not restore mark tags with attributes", () => {
    assert.equal(
        highlightHtml("<mark onmouseover=alert(1)>x</mark>"),
        "&lt;mark onmouseover=alert(1)&gt;x</mark>"
    );
});

test("queryTerms drops operators and field names", () => {
    assert.deepEqual(queryTerms("name:report AND budget OR draft*"), ["report", "budget", "draft*"]);
});

test("queryTerms is empty for an empty query", () => {
    assert.deepEqual(queryTerms(""), []);
    assert.deepEqual(queryTerms(undefined), []);
});

test("excerptText marks the terms it finds", () => {
    assert.equal(
        excerptText("the quick brown fox", ["brown"], 30),
        "the quick <mark>brown</mark> fox"
    );
});

test("excerptText matches a prefix term", () => {
    assert.equal(
        excerptText("running water", ["run*"], 30),
        "<mark>running</mark> water"
    );
});

test("excerptText windows around the first match", () => {
    const text = Array.from({length: 40}, (_, i) => `w${i}`).join(" ") + " needle tail";
    const excerpt = excerptText(text, ["needle"], 6);

    // A third of the window leads the match
    assert.equal(excerpt, "w38 w39 <mark>needle</mark> tail");
});

test("excerptText starts at the beginning when nothing matches", () => {
    assert.equal(excerptText("one two three four", [], 2), "one two");
});

test("excerptText returns null for empty text", () => {
    assert.equal(excerptText("", ["x"], 30), null);
});
