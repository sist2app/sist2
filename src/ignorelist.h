#ifndef SIST2_IGNORELIST_H
#define SIST2_IGNORELIST_H

#include "src/sist.h"

typedef struct ignorelist ignorelist_t;

ignorelist_t *ignorelist_create();

void ignorelist_destroy(ignorelist_t* ignorelist);

void ignorelist_load_ignore_file(ignorelist_t* ignorelist, const char* filepath);

int ignorelist_is_ignored(ignorelist_t* ignorelist, const char* filepath);

#endif //SIST2_IGNORELIST_H
