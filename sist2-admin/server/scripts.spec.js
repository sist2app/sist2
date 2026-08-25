import {test} from "node:test";
import assert from "node:assert/strict";

import {SCRIPT_TEMPLATES, createScriptFromTemplate, splitArgs} from "./scripts.js";

test("every template makes a script the runner can set up", () => {
    for (const name of Object.keys(SCRIPT_TEMPLATES)) {
        const script = createScriptFromTemplate("my script", name);

        assert.equal(script.name, "my script", name);
        assert.ok(["git", "simple"].includes(script.type), `${name}: type ${script.type}`);

        if (script.type === "git") {
            // setupGitScript() clones this and runs the setup.sh it finds
            assert.match(script.git_repository, /^https:\/\/github\.com\/[\w.-]+\/[\w.-]+$/, name);
            assert.equal(script.script, null, name);
        } else {
            assert.equal(script.git_repository, null, name);
            assert.equal(typeof script.script, "string", name);
        }

        // The arguments are handed to the script as they are, so they have to parse
        assert.doesNotThrow(() => splitArgs(script.extra_args), name);
    }
});

test("an unknown template is refused", () => {
    assert.throws(() => createScriptFromTemplate("my script", "Nonexistent"),
        /Unknown script template/);
});

test("arguments are split the way python's shlex would", () => {
    assert.deepEqual(splitArgs("--num-tags=1 --color=#dcd7ff"), ["--num-tags=1", "--color=#dcd7ff"]);
    assert.deepEqual(splitArgs('--tags-file="my tags.txt"'), ["--tags-file=my tags.txt"]);
    assert.deepEqual(splitArgs(""), []);
    assert.throws(() => splitArgs('--file="unclosed'), /No closing quotation/);
});
