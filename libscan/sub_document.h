#ifndef SCAN_SUB_DOCUMENT_H
#define SCAN_SUB_DOCUMENT_H

#include "scan.h"

/* Mail inside mail inside mail: past this the message was built to make the scan never end */
#define SUB_DOCUMENT_MAX_DEPTH 16

/** How many messages or archives had to be opened to reach this file */
int sub_document_depth(const char *filepath);

/** Name of a sub-document, with anything that would forge a path in it replaced */
void sub_document_sanitize_name(const char *name, char *buf, size_t buf_size);

/**
 * A job for documents held in memory by their container. The buffer is handed over one document
 * at a time by sub_document_submit(); the job itself is reused for all of them.
 */
parse_job_t *sub_document_job_create(vfile_t *f, const char *parent, log_callback_t log,
                                     logf_callback_t logf);

/**
 * Hands one buffer to the parser as a document of its own. `name` is used as it stands — a caller
 * building it from something the file chose passes it through sub_document_sanitize_name() first,
 * a caller spelling out a path of its own sanitizes each component. Returns FALSE when the
 * document could not be named, and nothing was parsed.
 */
int sub_document_submit(parse_callback_t parse, vfile_t *f, parse_job_t *sub_job, const char *name,
                        const char *data, size_t size);

#endif
