# Usage

```
Usage: sist2 scan [OPTION]... PATH
   or: sist2 index [OPTION]... INDEX
   or: sist2 sqlite-index [OPTION]... INDEX
   or: sist2 web [OPTION]... INDEX...

Lightning-fast file system indexer and search tool.

    -h, --help                        show this help message and exit
    -v, --version                     Print version and exit.
    --verbose                         Turn on logging.
    --very-verbose                    Turn on debug messages.
    --json-logs                       Output logs in JSON format.

Scan options
    -t, --threads=<int>               Number of threads. DEFAULT: 1
    -q, --thumbnail_count-quality=<int>     Thumbnail quality, on a scale of 0 to 100, 100 being the best. DEFAULT: 50
    --thumbnail_count-size=<int>            Thumbnail size, in pixels. DEFAULT: 552
    --thumbnail_count-count=<int>           Number of thumbnails to generate. Set a value > 1 to create video previews, set to 0 to disable thumbnails. DEFAULT: 1
    --content-size=<int>              Number of bytes to be extracted from text documents. Set to 0 to disable. DEFAULT: 32768
    -o, --output=<str>                Output index file path. DEFAULT: index.sist2
    --incremental                     If the output file path exists, only scan new or modified files.
    --optimize-index                  Defragment index file after scan to reduce its file size.
    --rewrite-url=<str>               Serve files from this url instead of from disk.
    --name=<str>                      Index display name. DEFAULT: index
    --depth=<int>                     Scan up to DEPTH subdirectories deep. Use 0 to only scan files in PATH. DEFAULT: -1
    --archive=<str>                   Archive file mode (skip|list|shallow|recurse). skip: don't scan, list: only save file names as text, shallow: don't scan archives inside archives. DEFAULT: recurse
    --archive-passphrase=<str>        Passphrase for encrypted archive files
    --ocr-lang=<str>                  Tesseract language (use 'tesseract --list-langs' to see which are installed on your machine)
    --ocr-images                      Enable OCR'ing of image files.
    --ocr-ebooks                      Enable OCR'ing of ebook files.
    -e, --exclude=<str>               Files that match this regex will not be scanned.
    --fast                            Only index file names & mime type.
    --treemap-threshold=<str>         Relative size threshold for treemap (see USAGE.md). DEFAULT: 0.0005
    --mem-buffer=<int>                Maximum memory buffer size per thread in MiB for files inside archives (see USAGE.md). DEFAULT: 2000
    --read-subtitles                  Read subtitles from media files.
    --fast-epub                       Faster but less accurate EPUB parsing (no thumbnails, metadata).
    --checksums                       Calculate file checksums when scanning.
    --list-file=<str>                 Specify a list of newline-delimited paths to be scanned instead of normal directory traversal. Use '-' to read from stdin.

Index options
    -t, --threads=<int>               Number of threads. DEFAULT: 1
    --es-url=<str>                    Elasticsearch url with port. DEFAULT: http://localhost:9200
    --es-insecure-ssl                 Do not verify SSL connections to Elasticsearch.
    --es-index=<str>                  Elasticsearch index name. DEFAULT: sist2
    -p, --print                       Print JSON documents to stdout instead of indexing to elasticsearch.
    --incremental-index               Conduct incremental indexing. Assumes that the old index is already ingested in Elasticsearch.
    --script-file=<str>               Path to user script.
    --mappings-file=<str>             Path to Elasticsearch mappings.
    --settings-file=<str>             Path to Elasticsearch settings.
    --async-script                    Execute user script asynchronously.
    --batch-size=<int>                Index batch size. DEFAULT: 70
    -f, --force-reset                 Reset Elasticsearch mappings and settings.

sqlite-index options
    --search-index=<str>              Path to search index. Will be created if it does not exist yet.

Web options
    --es-url=<str>                    Elasticsearch url. DEFAULT: http://localhost:9200
    --es-insecure-ssl                 Do not verify SSL connections to Elasticsearch.
    --search-index=<str>              Path to SQLite search index.
    --es-index=<str>                  Elasticsearch index name. DEFAULT: sist2
    --bind=<str>                      Listen for connections on this address. DEFAULT: localhost:4090
    --auth=<str>                      Basic auth in user:password format
    --auth0-audience=<str>            API audience/identifier
    --auth0-domain=<str>              Application domain
    --auth0-client-id=<str>           Application client ID
    --auth0-public-key-file=<str>     Path to Auth0 public key file extracted from <domain>/pem
    --tag-auth=<str>                  Basic auth in user:password format for tagging
    --tagline=<str>                   Tagline in navbar
    --dev                             Serve html & js files from disk (for development)
    --lang=<str>                      Default UI language. Can be changed by the user

Made by simon987 <me@simon987.net>. Released under GPL-3.0
```

#### Thumbnail database size estimation

See chart below for rough estimate of thumbnail_count size vs. thumbnail_count size & quality arguments:

For example, `--thumbnail_count-size=500`, `--thumbnail_count-quality=50` for a directory with 8 million images will create a thumbnail_count database 
that is about `8000000 * 11.8kB = 94.4GB`.

![thumbnail_size](thumbnail_size.png)

### Scan examples

Simple scan
```bash
sist2 scan ~/Documents

sist2 scan \
    --threads 4 --content-size 16000000 --thumbnail_count-quality 2 --archive shallow \
    --name "My Documents" --rewrite-url "http://nas.domain.local/My Documents/" \
    ~/Documents -o ./documents.sist2
```

Incremental scan

If the index file does not exist, `--incremental` has no effect.
```bash
sist scan ~/Documents -o ./documents.sist2
sist scan ~/Documents -o ./documents.sist2 --incremental
# or
sist scan ~/Documents -o ./documents.sist2 --incremental
sist scan ~/Documents -o ./documents.sist2 --incremental
```

### Excluding files

You can use the `--exclude` option to specify exclude patterns. For more complex setups, you can create a 
`.sist2ignore` file at the root of the scan path (For example, `~/Documents/.sist2ignore` for the example above). 

The syntax for sist2ignore is the same as .gitignore for Git (reference [here](https://git-scm.com/docs/gitignore)).

Example:

**.sist2ignore**
```gitignore
# Ignore all PDF files
*.pdf

# But don't ignore them for the /important_files/ directory
!/important_files/*.pdf

# Ignore all files in _staging/ directories
_staging/
```

### Index documents to Elasticsearch search backend

```bash
sist2 index --force-reset --batch-size 1000 --es-url http://localhost:9200 ./my_index.sist2
sist2 index ./my_index.sist2
```

#### Index documents to SQLite search backend
```bash
# The search index will be created if it does not exist already
sist2 sqlite-index ./index1.sist2 --search-index search.sist2
sist2 sqlite-index ./index2.sist2 --search-index search.sist2
```

**Save index in JSON format**
```bash
sist2 index --print ./my_index.sist2 > my_index.ndjson
```

**Inspect contents of an index**
```bash
sist2 index --print ./my_index.sist2 | jq | less
```

## Web

### Web examples

**Single index (Elasticsearch backend)**
```bash
sist2 web --auth admin:hunter2 --bind 0.0.0.0:8888 my_index.sist2
```

**Multiple indices (Elasticsearch backend)**
```bash
# Indices will be displayed in this order in the web interface
sist2 web index1.sist2 index2.sist2 index3.sist2 index4.sist2
```

**SQLite search backend**
```bash
sist2 web --search-index search.sist2 index1.sist2
```

#### Auth0 authentication

See [auth0.md](auth0.md)

### rewrite_url

When the `rewrite_url` field is not empty, the web module ignores the `root`
field and will return a HTTP redirect to `<rewrite_url><path>/<name><extension>`
instead of serving the file from disk. 
Both the `root` and `rewrite_url` fields are safe to manually modify from the 
`descriptor.json` file.

# Elasticsearch

Elasticsearch versions >=6.8.0, 7.X.X and 8.X.X are supported by sist2. 

Using a version >=7.14.0 is recommended to enable the following features:

- Bug fix for large documents (See #198)

Using a version >=8.0.0 is recommended to enable the following features:

- Approximate KNN search for Embeddings search (faster queries).

When using a legacy version of ES, a notice will be displayed next to the sist2 version in the web UI.
If you don't care about the features above, you can ignore it or disable it in the configuration page.

# Embeddings search

Since v3.2.0, User scripts can be used to generate _embeddings_ (vector of float32 numbers) which are stored in the .sist2 index file 
(see [scripting](scripting.md)). Embeddings can be used for:

* Nearest-neighbor queries (e.g. "return the documents most similar to this one")
* Semantic searches (e.g. "return the documents that are most closely related to the given topic")

In theory, embeddings can be created for any type of documents (image, text, audio etc.).

For example, the [clip](https://github.com/sist2app/sist2-script-clip) User Script, generates 512-d embeddings of images
(videos are also supported using the thumbnails generated by sist2). When the user enters a query in the "Embeddings Search"
textbox, the query's embedding is generated in their browser, leveraging the ONNX web runtime.

<details>
  <summary>Screenshots</summary>
 
 ![embeddings-1](embeddings-1.png)
 ![embeddings-2](embeddings-2.png)

1. Embeddings search bar. You can select the model using the dropdown on the left.
2. This icon appears for indices with embeddings search enabled.
3. Documents with this icon have embeddings. Click on the icon to perform KNN search.
</details>


# Tagging

### Manual tagging

You can modify tags of individual documents directly from the 
 `web` interface. Note that you can setup authentication for this feature
 with the `--tag-auth` option (See [web options](#web-options))

![manual_tag](manual_tag.png)

Tags that are manually added are saved both in the 
 index folder (in `/tags/`) and in Elasticsearch*. When re-`index`ing, 
 they are read from the index and automatically applied.
 
You can safely copy the `/tags/` database to another index.

See [Automatic tagging](#automatic-tagging) for information about tag 
 hierarchies and tag colors.

\* *It can take a few seconds to take effect in new search queries.*


### Automatic tagging

See [scripting](scripting.md) documentation.
