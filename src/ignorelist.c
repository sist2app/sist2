#include "ignorelist.h"
#include "ctx.h"
#include <git2.h>

typedef struct ignorelist {
    git_repository *repo;
    char repo_path[PATH_MAX];
    int has_rules;
} ignorelist_t;

char *get_tempdir() {
    char *tempdir_env = getenv("TMPDIR");

    if (tempdir_env != NULL) {
        return tempdir_env;
    }

    return "/tmp/";
}

void ignorelist_destroy(ignorelist_t* ignorelist) {
    git_libgit2_shutdown();

    if (ignorelist->repo != NULL) {
        git_repository_free(ignorelist->repo);
    }

    free(ignorelist);
}

ignorelist_t *ignorelist_create() {
    git_libgit2_init();

    ignorelist_t *ignorelist = malloc(sizeof(ignorelist_t));

    ignorelist->repo = NULL;
    ignorelist->has_rules = FALSE;

    char *tempdir = get_tempdir();

    if (tempdir[strlen(tempdir) - 1] == '/') {
        sprintf(ignorelist->repo_path, "%ssist2-ignorelist-%d", tempdir, getpid());
    } else {
        sprintf(ignorelist->repo_path, "%s/sist2-ignorelist-%d", tempdir, getpid());
    }

    return ignorelist;
}

void ignorelist_load_ignore_file(ignorelist_t *ignorelist, const char *filepath) {

    FILE *file;
    char line[PATH_MAX * 2];

    file = fopen(filepath, "r");

    if(file == NULL) {
        // No ignore list
        return;
    }

    LOG_DEBUGF("ignorelist.c", "Opening temporary git repository %s", ignorelist->repo_path);
    int init_result = git_repository_init(&ignorelist->repo, ignorelist->repo_path, TRUE);

    if (init_result != 0) {
        LOG_FATALF("ignorelist.c", "Got error code from git_repository_init(): %d", init_result);
    }

    git_ignore_clear_internal_rules(ignorelist->repo);

    while(fgets(line, PATH_MAX * 2, file)){

        line[strlen(line) - 1] = '\0'; // Strip trailing newline
        char *rules = {line,};

        int result = git_ignore_add_rule(ignorelist->repo, rules);

        if (result == 0) {
            LOG_DEBUGF("ignorelist.c", "Load ignore rule: %s", line);
            ignorelist->has_rules = TRUE;
        } else {
            LOG_FATALF("ignorelist.c", "Invalid ignore rule: %s", line);
        }
    }

    fclose(file);
}

int ignorelist_is_ignored(ignorelist_t *ignorelist, const char *filepath) {

    if (!ignorelist->has_rules) {
        return FALSE;
    }

    const char *rel_path = filepath + ScanCtx.index.desc.root_len;

    int ignored = -1;

    int result = git_ignore_path_is_ignored(&ignored, ignorelist->repo, rel_path);

    if (result != 0) {
        LOG_FATALF("ignorelist.c", "git_ignore_path_is_ignored returned error code: %d", result);
    }

    return ignored;
}
