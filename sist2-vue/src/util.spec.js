import {test} from "node:test";
import assert from "node:assert/strict";
import {escapeHtml, highlightHtml} from "./util.js";

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
