![GitHub](https://img.shields.io/github/license/sist2app/sist2.svg)
[![CodeFactor](https://www.codefactor.io/repository/github/sist2app/sist2/badge?s=05daa325188aac4eae32c786f3d9cf4e0593f822)](https://www.codefactor.io/repository/github/sist2app/sist2)
[![ci](https://github.com/sist2app/sist2/actions/workflows/ci.yml/badge.svg)](https://github.com/sist2app/sist2/actions/workflows/ci.yml)

**Demo**: [sist2.simon987.net](https://sist2.simon987.net/)

**Community URL:** [Discord](https://discord.gg/2PEjDy3Rfs)

# sist2

sist2 (Simple incremental search tool)

*Warning: sist2 is in early development*

![search panel](docs/sist2.gif)

## Features

* Fast, low memory usage, multi-threaded
* Manage & schedule scan jobs with simple web interface (Docker only)
* Mobile-friendly Web interface
* Extracts text and metadata from common file types \*
* Generates thumbnails \*
* Incremental scanning
* Manual tagging from the UI and automatic tagging based on file attributes via [user scripts](docs/scripting.md)
* Recursive scan inside archive files \*\*
* OCR support with tesseract \*\*\*
* Stats page & disk utilisation visualization

\* See [format support](#format-support)    
\*\* See [Archive files](#archive-files)    
\*\*\* See [OCR](#ocr)

## Getting Started

### Using Docker Compose *(Windows/Linux/Mac)*

```yaml
services:
  elasticsearch:
    image: elasticsearch:7.17.9
    restart: unless-stopped
    volumes:
      # This directory must have 1000:1000 permissions (or update PUID & PGID below)
      - /data/sist2-es-data/:/usr/share/elasticsearch/data
    environment:
      - "discovery.type=single-node"
      - "ES_JAVA_OPTS=-Xms2g -Xmx2g"
      - "PUID=1000"
      - "PGID=1000"
  sist2-admin:
    image: sist2app/sist2:latest
    restart: unless-stopped
    volumes:
      - /data/sist2-admin-data/:/sist2-admin/
      - /<path to index>/:/host
    ports:
      - 4090:4090
      # NOTE: Don't expose this port publicly!
      - 8080:8080
    entrypoint: node
    command:
      - /root/sist2-admin/server/main.js
```

Navigate to http://localhost:8080/ to configure sist2-admin.

### Using the executable file *(Linux/WSL only)*

1. Choose search backend (See [comparison](#search-backends)):
    * **Elasticsearch**: have an Elasticsearch (version >= 6.8.X, ideally >=7.14.0) instance running
        1. Download [from official website](https://www.elastic.co/downloads/elasticsearch)
        2. *(or)* Run using docker:
            ```bash
            docker run -d -p 9200:9200 -e "discovery.type=single-node" elasticsearch:7.17.9
            ```
    * **SQLite**: No installation required

2. Download the [latest sist2 release](https://github.com/sist2app/sist2/releases).
   Select the file corresponding to your CPU architecture and mark the binary as executable with `chmod +x`.
3. See [usage guide](docs/USAGE.md) for command line usage.

Example usage:

1. Scan a directory: `sist2 scan ~/Documents --output ./documents.sist2`
2. Prepare search index:
    * **Elasticsearch**: `sist2 index --es-url http://localhost:9200 ./documents.sist2`
    * **SQLite**: `sist2 sqlite-index --search-index ./search.sist2 ./documents.sist2`
3. Start web interface: 
   * **Elasticsearch**: `sist2 web ./documents.sist2`
   * **SQLite**: `sist2 web --search-index ./search.sist2 ./documents.sist2`

## Format support

| File type                                                                 | Library                                                                      | Content  | Thumbnail   | Metadata                                                                                                                               |
|:--------------------------------------------------------------------------|:-----------------------------------------------------------------------------|:---------|:------------|:---------------------------------------------------------------------------------------------------------------------------------------|
| pdf,xps,fb2,epub                                                          | MuPDF                                                                        | text+ocr | yes         | author, title                                                                                                                          |
| cbz,cbr                                                                   | [libscan](https://github.com/sist2app/sist2/tree/master/libscan) | -        | yes         | -                                                                                                                                      |
| `audio/*`                                                                 | ffmpeg                                                                       | -        | yes         | ID3 tags                                                                                                                               |
| `video/*`                                                                 | ffmpeg                                                                       | -        | yes         | title, comment, artist                                                                                                                 |
| `image/*`                                                                 | ffmpeg                                                                       | ocr      | yes         | [Common EXIF tags](https://github.com/sist2app/sist2/blob/efdde2734eca9b14a54f84568863b7ffd59bdba3/src/parsing/media.c#L190), GPS tags |
| raw, rw2, dng, cr2, crw, dcr, k25, kdc, mrw, pef, xf3, arw, sr2, srf, erf | LibRaw                                                                       | no       | yes         | Common EXIF tags, GPS tags                                                                                                             |
| ttf,ttc,cff,woff,fnt,otf                                                  | Freetype2                                                                    | -        | yes, `bmp`  | Name & style                                                                                                                           |
| `text/plain`                                                              | [libscan](https://github.com/sist2app/sist2/tree/master/libscan) | yes      | no          | -                                                                                                                                      |
| html, xml                                                                 | [libscan](https://github.com/sist2app/sist2/tree/master/libscan) | yes      | no          | -                                                                                                                                      |
| tar, zip, rar, 7z, ar ...                                                 | Libarchive                                                                   | yes\*    | -           | no                                                                                                                                     |
| docx, xlsx, pptx                                                          | [libscan](https://github.com/sist2app/sist2/tree/master/libscan) | yes      | if embedded | creator, modified_by, title                                                                                                            |
| doc (MS Word 1-2003, incl. DOS and Macintosh)                             | [libantiword2](https://github.com/shyyio/libantiword2)                       | yes      | no          | author, title, modified_by                                                                                                             |
| mobi, azw, azw3                                                           | libmobi                                                                      | yes      | yes         | author, title                                                                                                                          |
| wpd (WordPerfect)                                                         | libwpd                                                                       | yes      | no          | *planned*                                                                                                                              |
| json, jsonl, ndjson                                                       | [libscan](https://github.com/sist2app/sist2/tree/master/libscan) | yes      | -           | -                                                                                                                                      |

\* *See [Archive files](#archive-files)*

### Archive files

**sist2** will scan files stored into archive files (zip, tar, 7z...) as if they were directly in the file system.
Recursive (archives inside archives)
scan is also supported.

**Limitations**:

* Support for parsing media files with formats that require *seek* (e.g. `.gif`, `.mp4` w/ fragmented metadata etc.)
  is limitted (see `--mem-buffer` option)
* Archive files are scanned sequentially, by a single thread. On systems where
  **sist2** is not I/O bound, scans might be faster when larger archives are split into smaller parts.

### OCR

You can enable OCR support for ebook (pdf,xps,fb2,epub) or image file types with the
`--ocr-lang <lang>` option in combination with `--ocr-images` and/or `--ocr-ebooks`.
Download the language data files with your package manager (`apt install tesseract-ocr-eng`) or
directly [from Github](https://github.com/tesseract-ocr/tesseract/wiki/Data-Files).

The `sist2app/sist2` image comes with common languages
(hin, jpn, eng, fra, rus, spa, chi_sim, deu, pol) pre-installed.

You can use the `+` separator to specify multiple languages. The language
name must be identical to the `*.traineddata` file installed on your system
(use `chi_sim` rather than `chi-sim`).

Examples:

```bash
sist2 scan --ocr-ebooks --ocr-lang jpn ~/Books/Manga/
sist2 scan --ocr-images --ocr-lang eng ~/Images/Screenshots/
sist2 scan --ocr-ebooks --ocr-images --ocr-lang eng+chi_sim ~/Chinese-Bilingual/
```

### Search backends

sist2 v3.0.7+ supports SQLite search backend. The SQLite search backend has
fewer features and generally comparable query performance for medium-size
indices, but it uses much less memory and is easier to set up.

|                                              |                       SQLite                        |                                                             Elasticsearch                                                             |
|----------------------------------------------|:---------------------------------------------------:|:-------------------------------------------------------------------------------------------------------------------------------------:|
| Requires separate search engine installation |                                                     |                                                                   ✓                                                                   |
| Memory footprint                             |                        ~20MB                        |                                                                >500MB                                                                 |
| Query syntax                                 |      [fts5](https://www.sqlite.org/fts5.html)       | [query_string](https://www.elastic.co/guide/en/elasticsearch/reference/current/query-dsl-query-string-query.html#query-string-syntax) |
| Fuzzy search                                 |                                                     |                                                                   ✓                                                                   |
| Media Types tree real-time updating          |                                                     |                                                                   ✓                                                                   |
| Manual tagging                               |                          ✓                          |                                                                   ✓                                                                   |
| User scripts                                 |                          ✓                          |                                                                   ✓                                                                   |
| Media Type breakdown for search results      |                                                     |                                                                   ✓                                                                   |
| Embeddings search                            |                      ✓ *O(n)*                       |                                                              ✓ *O(logn)*                                                              |

## Build from source

You can compile **sist2** by yourself if you don't want to use the pre-compiled binaries

### Using docker

```bash
git clone https://github.com/sist2app/sist2/
cd sist2
# Static binary for the current architecture
scripts/make_static.sh
# ...or the full runtime image
docker buildx build . -t my-sist2-image
```

### Using a linux computer

1. Install compile-time dependencies

   ```bash
   apt install gcc g++ python3 yasm ragel automake autotools-dev wget libtool libssl-dev curl zip unzip tar xorg-dev libglu1-mesa-dev libxcursor-dev libxml2-dev libxinerama-dev gettext nasm git nodejs
   ```

2. Install [vcpkg](https://github.com/microsoft/vcpkg)
3. Build (the frontends must be built **before** the C binary: their `dist/` output is
   embedded into the executable)

    ```bash
    git clone https://github.com/sist2app/sist2/
    cd sist2
    (cd sist2-vue; npm install; npm run build)
    (cd sist2-admin; npm install; npm run build)
    cmake -B build -DSIST_DEBUG=off -DCMAKE_TOOLCHAIN_FILE=<VCPKG_ROOT>/scripts/buildsystems/vcpkg.cmake
    cmake --build build -j $(nproc)
    ```
