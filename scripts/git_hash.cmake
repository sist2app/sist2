# Regenerates git_hash.h, touching the file only when the hash actually
# changed so that dependent objects are not recompiled on every build.
# Usage: cmake -DSOURCE_DIR=... -DOUTPUT_FILE=... -P git_hash.cmake

execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY ${SOURCE_DIR}
        RESULT_VARIABLE GIT_RESULT
        OUTPUT_VARIABLE COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
)

if (NOT GIT_RESULT EQUAL 0)
    set(COMMIT_HASH unknown)
endif ()

file(WRITE ${OUTPUT_FILE}.tmp "static const char *const Sist2CommitHash = \"${COMMIT_HASH}\";\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUTPUT_FILE}.tmp ${OUTPUT_FILE})
file(REMOVE ${OUTPUT_FILE}.tmp)
