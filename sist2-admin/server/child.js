/**
 * How long to keep reading the output of a process that has already exited. Workers sist2 starts
 * inherit its output, so one that outlives it holds the pipes open; "close" would never arrive
 * and the task would look like it is still running long after it finished.
 */
export const OUTPUT_DRAIN_TIMEOUT = 5000;

/**
 * Waits for a child process to finish, whether or not what it started lets go of its output.
 *
 * @returns {Promise<{code: number|null, signal: string|null, orphaned: boolean}>} orphaned is true
 *          when the process exited but its output was still held by something else.
 */
export function waitForChildExit(child, drainTimeout = OUTPUT_DRAIN_TIMEOUT) {
    return new Promise((resolve) => {
        let settled = false;
        let drainTimer = null;

        const finish = (code, signal, orphaned) => {
            if (settled) {
                return;
            }
            settled = true;
            clearTimeout(drainTimer);

            if (orphaned) {
                // Nothing else will be read from them, and the task is over
                child.stdout?.destroy();
                child.stderr?.destroy();
            }

            resolve({ code, signal, orphaned });
        };

        child.on("close", (code, signal) => finish(code, signal, false));

        child.on("exit", (code, signal) => {
            drainTimer = setTimeout(() => finish(code, signal, true), drainTimeout);
        });
    });
}
