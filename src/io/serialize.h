#ifndef SIST2_SERIALIZE_H
#define SIST2_SERIALIZE_H

#include "src/sist.h"

void write_document(document_t *doc);

/** Name of the field a meta key is written to, in the index and in Elasticsearch */
char *get_meta_key_text(enum metakey meta_key);

#endif
