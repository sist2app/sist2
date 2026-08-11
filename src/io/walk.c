#include "walk.h"
#include "src/ctx.h"
#include "src/parsing/fs_util.h"

#include <dirent.h>
#include <pthread.h>

#define STR_STARTS_WITH(x, y) (strncmp(y, x, strlen(y) - 1) == 0)


int sub_strings[30];
#define EXCLUDED(str) (pcre_exec(ScanCtx.exclude, ScanCtx.exclude_extra, str, strlen(str), 0, 0, sub_strings, sizeof(sub_strings)) >= 0)

static void queue_parse_job(const char *filepath, const struct stat *info) {
    parse_job_t *job = create_parse_job(filepath, (int) info->st_mtim.tv_sec, info->st_size);

    tpool_add_work(ScanCtx.pool, &(job_t) {
            .type = JOB_PARSE_JOB,
            .parse_job = job
    });
    free(job);
}

// Skip an entry (and its subtree, for directories) when it is beyond the
// depth limit, matches the exclude regex, or is on the ignore list
static int is_pruned(const char *path, int level) {
    if (level > ScanCtx.depth) {
        return TRUE;
    }

    if (ScanCtx.exclude != NULL && EXCLUDED(path)) {
        LOG_DEBUGF("walk.c", "Excluded: %s", path);
        return TRUE;
    }

    if (ignorelist_is_ignored(ScanCtx.ignorelist, path)) {
        LOG_DEBUGF("walk.c", "Ignored: %s", path);
        return TRUE;
    }

    return FALSE;
}

static int walk_recurse(const char *dirpath, int level) {
    DIR *dir = opendir(dirpath);
    if (dir == NULL) {
        LOG_ERRORF("walk.c", "Could not open directory %s (%s)", dirpath, strerror(errno));
        // Match nftw(): failing to open the root is fatal, unreadable subdirectories are skipped
        return level == 1 ? -1 : 0;
    }

    char *path = malloc(PATH_MAX);
    struct stat info;
    struct dirent *entry;

    // dirpath only ends with '/' when scanning the filesystem root
    const char *sep = dirpath[strlen(dirpath) - 1] == '/' ? "" : "/";

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(path, PATH_MAX, "%s%s%s", dirpath, sep, entry->d_name);

        // Do not follow symlinks (equivalent to nftw() FTW_PHYS)
        if (lstat(path, &info) != 0) {
            LOG_ERRORF("walk.c", "Could not stat %s (%s)", path, strerror(errno));
            continue;
        }

        if (is_pruned(path, level)) {
            continue;
        }

        if (S_ISREG(info.st_mode)) {
            queue_parse_job(path, &info);
        } else if (S_ISDIR(info.st_mode)) {
            walk_recurse(path, level + 1);
        }
    }

    free(path);
    closedir(dir);
    return 0;
}

int walk_directory_tree(const char *dirpath) {
    char root[PATH_MAX];
    strncpy(root, dirpath, sizeof(root) - 1);
    root[sizeof(root) - 1] = '\0';

    // Strip trailing slashes so constructed paths match nftw() output
    size_t len = strlen(root);
    while (len > 1 && root[len - 1] == '/') {
        root[--len] = '\0';
    }

    if (is_pruned(root, 0)) {
        return 0;
    }

    return walk_recurse(root, 1);
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

        char *absolute_path = realpath(buf, NULL);

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