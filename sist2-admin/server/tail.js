import fs from "node:fs";

const TAIL_BLOCK_SIZE = 16384;
const FOLLOW_POLL_INTERVAL = 250;

export function readLastLines(file, n) {
    let fd;
    try {
        fd = fs.openSync(file, "r");
    } catch (e) {
        return [];
    }

    try {
        const size = fs.fstatSync(fd).size;
        let position = size;
        let data = "";

        while (position > 0) {
            const readSize = Math.min(TAIL_BLOCK_SIZE, position);
            position -= readSize;

            const buffer = Buffer.alloc(readSize);
            fs.readSync(fd, buffer, 0, readSize, position);
            data = buffer.toString("utf8") + data;

            if (data.split("\n").length > n + 1) {
                break;
            }
        }

        const lines = data.split("\n").filter((line) => line !== "");
        return lines.slice(-n);
    } finally {
        fs.closeSync(fd);
    }
}

export function followFile(file, startPosition, onLine) {
    let position = startPosition;
    let carry = "";
    let stopped = false;

    const readNew = () => {
        let stats;
        try {
            stats = fs.statSync(file);
        } catch (e) {
            return;
        }

        if (stats.size < position) {
            position = 0;
            carry = "";
        }
        if (stats.size === position) {
            return;
        }

        const stream = fs.createReadStream(file, {
            start: position,
            end: stats.size - 1
        });
        position = stats.size;

        stream.on("data", (chunk) => {
            const data = carry + chunk.toString("utf8");
            const lines = data.split("\n");
            carry = lines.pop();
            for (const line of lines) {
                if (line !== "" && !stopped) {
                    onLine(line);
                }
            }
        });
    };

    const interval = setInterval(readNew, FOLLOW_POLL_INTERVAL);

    return () => {
        stopped = true;
        clearInterval(interval);
    };
}
