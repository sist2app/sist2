import {test} from "node:test";
import assert from "node:assert";
import fs from "node:fs";
import path from "node:path";
import {fileURLToPath} from "node:url";

import {WordPieceTokenizer} from "./WordPieceTokenizer.js";

// The real bert-base-uncased vocabulary, as one token per line: a trimmed one would let a wrong
// longest match pass. The expected ids come from the tokenizer the embeddings were generated with
// (see create_tokenizer_data.py in sist2-models).
const dir = path.dirname(fileURLToPath(import.meta.url));

const vocab = {};
fs.readFileSync(path.join(dir, "testdata/vocab.txt"), "utf-8")
    .split("\n")
    .forEach((token, id) => {
        if (token !== "") {
            vocab[token] = id;
        }
    });

const cases = JSON.parse(fs.readFileSync(path.join(dir, "testdata/wordpiece-cases.json"), "utf-8"));

const tokenizer = new WordPieceTokenizer({
    vocab: vocab,
    unk_token: "[UNK]",
    cls_token: "[CLS]",
    sep_token: "[SEP]",
    do_lower_case: true,
    max_length: 256
});

test("encodes the same ids as the python tokenizer", () => {
    for (const [text, expected] of Object.entries(cases)) {
        assert.deepStrictEqual(tokenizer.encode(text), expected, `text: ${JSON.stringify(text)}`);
    }
});

test("a word with no piece in the vocabulary becomes one unknown token", () => {
    assert.deepStrictEqual(tokenizer.tokenize("🙂"), ["[UNK]"]);
});

test("truncates to what the model takes, keeping the special tokens", () => {
    const short = new WordPieceTokenizer({
        vocab: vocab, unk_token: "[UNK]", cls_token: "[CLS]", sep_token: "[SEP]",
        do_lower_case: true, max_length: 5
    });

    const ids = short.encode("turning the boat through the wind");

    assert.strictEqual(ids.length, 5);
    assert.strictEqual(ids[0], vocab["[CLS]"]);
    assert.strictEqual(ids[ids.length - 1], vocab["[SEP]"]);
});
