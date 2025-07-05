#include "walk.h"
#include "src/ctx.h"
#include "src/parsing/fs_util.h"

#include <ftw.h>
#include <pthread.h>

#define STR_STARTS_WITH(x, y) (strncmp(y, x, strlen(y) - 1) == 0)


int sub_strings[30];
#define EXCLUDED(str) (pcre_exec(ScanCtx.exclude, ScanCtx.exclude_extra, str, strlen(str), 0, 0, sub_strings, sizeof(sub_strings)) >= 0)

int handle_entry(const char *filepath, const struct stat *info, int typeflag, struct FTW *ftw) {

    if (ftw->level > ScanCtx.depth) {
        if (typeflag == FTW_D) {
            return FTW_SKIP_SUBTREE;
        }
        return FTW_CONTINUE;
    }

    if (ScanCtx.exclude != NULL && EXCLUDED(filepath)) {
        LOG_DEBUGF("walk.c", "Excluded: %s", filepath);

        if (typeflag == FTW_D) {
            return FTW_SKIP_SUBTREE;
        }

        return FTW_CONTINUE;
    }

    if (ignorelist_is_ignored(ScanCtx.ignorelist, filepath)) {
        LOG_DEBUGF("walk.c", "Ignored: %s", filepath);

        if (typeflag == FTW_D) {
            return FTW_SKIP_SUBTREE;
        }

        return FTW_CONTINUE;
    }

    if (typeflag == FTW_F && S_ISREG(info->st_mode)) {
        parse_job_t *job = create_parse_job(filepath, (int) info->st_mtim.tv_sec, info->st_size);

        tpool_add_work(ScanCtx.pool, &(job_t) {
                .type = JOB_PARSE_JOB,
                .parse_job = job
        });
        free(job);
    }

    return FTW_CONTINUE;
}

#define MAX_FILE_DESCRIPTORS 64

int walk_directory_tree(const char *dirpath) {
    return nftw(dirpath, handle_entry, MAX_FILE_DESCRIPTORS, FTW_PHYS | FTW_ACTIONRETVAL);
}

int iterate_file_list(void *input_file) {

    char buf[PATH_MAX];
    struct stat info;

    while (fgets(buf, sizeof(buf), input_file) != NULL) {

        // Remove trailing newline
        *(buf + strlen(buf) - 1) = '\0';

        int stat_ret = stat(buf, &info);

        if (stat_ret != 0) {
            LOG_ERRORF("walk.c", "Could not stat file %s (%s)", buf, strerror(errno));
            continue;
        }

        if (!S_ISREG(info.st_mode)) {
            LOG_ERRORF("walk.c", "Is not a regular file: %s", buf);
            continue;
        }

        char *absolute_path = canonicalize_file_name(buf);

        if (absolute_path == NULL) {
            LOG_FATALF("walk.c", "FIXME: Could not get absolute path of %s", buf);
        }

        if (ScanCtx.exclude != NULL && EXCLUDED(absolute_path)) {
            LOG_DEBUGF("walk.c", "Excluded: %s", absolute_path);
            continue;
        }

        if (!STR_STARTS_WITH(absolute_path, ScanCtx.index.desc.root)) {
            LOG_FATALF("walk.c", "File is not a children of root folder (%s): %s", ScanCtx.index.desc.root, buf);
        }

        parse_job_t *job = create_parse_job(absolute_path, (int) info.st_mtim.tv_sec, info.st_size);
        free(absolute_path);

        tpool_add_work(ScanCtx.pool, &(job_t) {
                .type = JOB_PARSE_JOB,
                .parse_job = job
        });
        free(job);
    }

    return 0;
}