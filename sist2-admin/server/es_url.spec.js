import {test} from "node:test";
import assert from "node:assert/strict";
import {esUrlPort, parseEsUrl} from "./es_url.js";

test("a full URL is taken as it is", () => {
    const url = parseEsUrl("http://localhost:9200");

    assert.equal(url.hostname, "localhost");
    assert.equal(esUrlPort(url), 9200);
});

test("an address typed without a scheme still works", () => {
    const url = parseEsUrl("localhost:9200");

    assert.equal(url.protocol, "http:");
    assert.equal(url.hostname, "localhost");
    assert.equal(esUrlPort(url), 9200);
});

test("an address with no port keeps the port of its scheme", () => {
    assert.equal(esUrlPort(parseEsUrl("http://elasticsearch")), 80);
    assert.equal(esUrlPort(parseEsUrl("https://elasticsearch")), 443);
    assert.equal(esUrlPort(parseEsUrl("elasticsearch")), 80);
});

test("surrounding whitespace is ignored", () => {
    assert.equal(parseEsUrl("  http://localhost:9200  ").hostname, "localhost");
});

test("credentials and a path survive", () => {
    const url = parseEsUrl("https://user:pass@es.example.com:9243/prefix");

    assert.equal(url.username, "user");
    assert.equal(url.pathname, "/prefix");
    assert.equal(esUrlPort(url), 9243);
});

test("what is not an address of an Elasticsearch node is refused", () => {
    assert.equal(parseEsUrl(""), null);
    assert.equal(parseEsUrl("   "), null);
    assert.equal(parseEsUrl(null), null);
    assert.equal(parseEsUrl("not a url"), null);
    assert.equal(parseEsUrl("ftp://localhost:9200"), null);
    assert.equal(parseEsUrl("http://"), null);
});
