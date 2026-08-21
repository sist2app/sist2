# Compresses INPUT to OUTPUT as a gzip stream. FORMAT raw writes the file content with no
# archive metadata, so the result is exactly gzip(INPUT).
file(ARCHIVE_CREATE
        OUTPUT ${OUTPUT}
        PATHS ${INPUT}
        FORMAT raw
        COMPRESSION GZip
        COMPRESSION_LEVEL 9
)
