#ifndef SIST2_WORKER_H
#define SIST2_WORKER_H

/**
 * Dedicated descriptors rather than stdin/stdout: a parser library writing to stdout would otherwise
 * corrupt the frame stream. The worker inherits stdout/stderr from the master, so stray output and
 * log lines still reach the terminal.
 */
#define WORKER_IN_FD (3)
#define WORKER_OUT_FD (4)

/**
 * Entry point of a `sist2 scan --worker` process. Reads jobs until the master closes the pipe or
 * sends BYE.
 */
void worker_run();

#endif
