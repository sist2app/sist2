import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import readline from "node:readline";

import { SCRIPT_FOLDER } from "./config.js";

export const SCRIPT_TEMPLATES = {
    "CLIP - Generate embeddings to predict the most relevant image based on the text prompt": {
        type: "git",
        git_repository: "https://github.com/sist2app/sist2-script-clip",
        script: null,
        extra_args: "--num-tags=1 --tags-file=general.txt --color=#dcd7ff"
    },
    "Whisper - Speech to text with OpenAI Whisper": {
        type: "git",
        git_repository: "https://github.com/simon987/sist2-script-whisper",
        script: null,
        extra_args: "--model=base --num-threads=4 --color=#51da4c --tag"
    },
    "Hamburger - Simple script example": {
        type: "simple",
        git_repository: null,
        script: "from sist2 import Sist2Index\n" +
            "import sys\n" +
            "\n" +
            "index = Sist2Index(sys.argv[1])\n" +
            "for doc in index.document_iter():\n" +
            "    doc.json_data[\"tag\"] = [\"hamburger.#00FF00\"]\n" +
            "    index.update_document(doc)\n" +
            "\n" +
            "index.sync_tag_table()\n" +
            "index.commit()\n" +
            "\n" +
            "print(\"Done!\")\n",
        extra_args: ""
    },
    "(Blank)": {
        type: "simple",
        git_repository: null,
        script: "",
        extra_args: ""
    }
};

export function createScriptFromTemplate(name, templateName) {
    const template = SCRIPT_TEMPLATES[templateName];
    if (template === undefined) {
        throw new Error(`Unknown script template: ${templateName}`);
    }
    return {
        name: name,
        type: template.type,
        git_repository: template.git_repository,
        force_clone: false,
        script: template.script,
        extra_args: template.extra_args
    };
}

export function scriptDir(script) {
    return path.join(SCRIPT_FOLDER, script.name);
}

export function scriptExecutable(script) {
    return path.join(scriptDir(script), "run.sh");
}

export function renameScriptDir(script, newName) {
    const dir = scriptDir(script);
    if (fs.existsSync(dir)) {
        fs.renameSync(dir, scriptDir({ name: newName }));
    }
}

export function deleteScriptDir(script) {
    fs.rmSync(scriptDir(script), {
        recursive: true,
        force: true
    });
}

function runCommand(command, args, cwd, onLog, onSpawn) {
    return new Promise((resolve, reject) => {
        const child = spawn(command, args, {
            cwd: cwd,
            stdio: ["ignore", "pipe", "pipe"]
        });

        if (onSpawn) {
            onSpawn(child);
        }

        for (const stream of [child.stdout, child.stderr]) {
            const lines = readline.createInterface({ input: stream });
            lines.on("line", (line) => {
                if (line.trim() !== "") {
                    onLog({ stdout: line });
                }
            });
        }

        child.on("error", reject);
        child.on("close", (code) => {
            if (code === 0) {
                resolve();
            } else {
                reject(new Error(`${command} exited with code ${code}`));
            }
        });
    });
}

async function setupGitScript(script, onLog, onSpawn) {
    const dir = scriptDir(script);

    onLog({ "sist2-admin": `Cloning ${script.git_repository}` });

    if (script.force_clone || !fs.existsSync(path.join(dir, ".git"))) {
        if (script.force_clone) {
            fs.rmSync(dir, {
                recursive: true,
                force: true
            });
        }
        await runCommand(
            "git",
            ["clone", script.git_repository, dir],
            undefined,
            onLog,
            onSpawn
        );
    } else {
        await runCommand(
            "git",
            ["-C", dir, "pull"],
            undefined,
            onLog,
            onSpawn
        );
    }

    const setupScript = path.join(dir, "setup.sh");
    if (fs.existsSync(setupScript)) {
        onLog({ "sist2-admin": `Executing setup script ${setupScript}` });
        fs.chmodSync(setupScript, 0o755);
        await runCommand(setupScript, [], dir, onLog, onSpawn);
        onLog({ stdout: `Executed setup script ${setupScript}` });
    }

    onLog({ "sist2-admin": `Initialized git repository in ${dir}` });
}

function setupSimpleScript(script) {
    fs.writeFileSync(scriptExecutable(script), "#!/bin/bash\npython run.py \"$@\"");
    fs.writeFileSync(path.join(scriptDir(script), "run.py"), script.script);
}

export async function setupScript(script, onLog, onSpawn) {
    fs.mkdirSync(scriptDir(script), { recursive: true });

    if (script.type === "git") {
        await setupGitScript(script, onLog, onSpawn);
    } else {
        setupSimpleScript(script);
    }

    fs.chmodSync(scriptExecutable(script), 0o755);
}

// Same semantics as Python's shlex.split(): quotes may appear anywhere in a
// token (--file="a b" is one argument) and are stripped from the result
export function splitArgs(argString) {
    if (!argString) {
        return [];
    }

    const args = [];
    let current = "";
    let inToken = false;
    let quote = null;

    for (const char of argString) {
        if (quote !== null) {
            if (char === quote) {
                quote = null;
            } else {
                current += char;
            }
        } else if (char === "\"" || char === "'") {
            quote = char;
            inToken = true;
        } else if (/\s/.test(char)) {
            if (inToken) {
                args.push(current);
                current = "";
                inToken = false;
            }
        } else {
            current += char;
            inToken = true;
        }
    }

    if (quote !== null) {
        throw new Error(`No closing quotation in arguments: ${argString}`);
    }
    if (inToken) {
        args.push(current);
    }
    return args;
}
