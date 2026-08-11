CREATE TABLE search_backend (
    name TEXT PRIMARY KEY,
    backend_type TEXT NOT NULL DEFAULT 'elasticsearch',
    search_index TEXT,
    es_url TEXT,
    es_insecure_ssl INTEGER NOT NULL DEFAULT 0,
    es_index TEXT,
    es_mappings TEXT,
    es_settings TEXT,
    threads INTEGER NOT NULL DEFAULT 1,
    batch_size INTEGER NOT NULL DEFAULT 70
);

CREATE TABLE job (
    name TEXT PRIMARY KEY,
    cron_expression TEXT NOT NULL DEFAULT '0 0 * * *',
    schedule_enabled INTEGER NOT NULL DEFAULT 0,
    keep_last_n_logs INTEGER NOT NULL DEFAULT -1,
    index_path TEXT,
    previous_index_path TEXT,
    last_index_date TEXT,
    status TEXT NOT NULL DEFAULT 'created',
    do_full_scan INTEGER NOT NULL DEFAULT 0,
    search_backend TEXT REFERENCES search_backend (name),
    incremental_index INTEGER NOT NULL DEFAULT 1,
    scan_path TEXT NOT NULL DEFAULT '/',
    scan_threads INTEGER NOT NULL DEFAULT 1,
    scan_thumbnail_quality INTEGER NOT NULL DEFAULT 50,
    scan_thumbnail_size INTEGER NOT NULL DEFAULT 552,
    scan_thumbnail_count INTEGER NOT NULL DEFAULT 1,
    scan_content_size INTEGER NOT NULL DEFAULT 32768,
    scan_depth INTEGER NOT NULL DEFAULT -1,
    scan_archive TEXT NOT NULL DEFAULT 'recurse',
    scan_archive_passphrase TEXT,
    scan_ocr_lang TEXT,
    scan_ocr_images INTEGER NOT NULL DEFAULT 0,
    scan_ocr_ebooks INTEGER NOT NULL DEFAULT 0,
    scan_exclude TEXT,
    scan_fast INTEGER NOT NULL DEFAULT 0,
    scan_treemap_threshold REAL NOT NULL DEFAULT 0.0005,
    scan_mem_buffer INTEGER NOT NULL DEFAULT 2000,
    scan_read_subtitles INTEGER NOT NULL DEFAULT 0,
    scan_fast_epub INTEGER NOT NULL DEFAULT 0,
    scan_checksums INTEGER NOT NULL DEFAULT 0,
    scan_incremental INTEGER NOT NULL DEFAULT 1,
    scan_optimize_index INTEGER NOT NULL DEFAULT 0,
    scan_rewrite_url TEXT
);

CREATE TABLE user_script (
    name TEXT PRIMARY KEY,
    type TEXT NOT NULL,
    git_repository TEXT,
    force_clone INTEGER NOT NULL DEFAULT 0,
    script TEXT,
    extra_args TEXT NOT NULL DEFAULT ''
);

CREATE TABLE job_user_script (
    job_name TEXT NOT NULL REFERENCES job (name) ON DELETE CASCADE,
    script_name TEXT NOT NULL REFERENCES user_script (name),
    position INTEGER NOT NULL,
    PRIMARY KEY (job_name, script_name)
);

CREATE TABLE frontend (
    name TEXT PRIMARY KEY,
    auto_start INTEGER NOT NULL DEFAULT 0,
    extra_query_args TEXT NOT NULL DEFAULT '',
    custom_url TEXT,
    web_search_backend TEXT REFERENCES search_backend (name),
    web_bind TEXT NOT NULL DEFAULT '0.0.0.0:4090',
    web_tagline TEXT NOT NULL DEFAULT 'Lightning-fast file system indexer and search tool',
    web_lang TEXT NOT NULL DEFAULT 'en',
    web_auth TEXT,
    web_tag_auth TEXT,
    web_auth0_audience TEXT,
    web_auth0_domain TEXT,
    web_auth0_client_id TEXT,
    web_auth0_public_key TEXT,
    web_dev INTEGER NOT NULL DEFAULT 0,
    web_verbose INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE frontend_job (
    frontend_name TEXT NOT NULL REFERENCES frontend (name) ON DELETE CASCADE,
    job_name TEXT NOT NULL REFERENCES job (name),
    PRIMARY KEY (frontend_name, job_name)
);

CREATE TABLE task_done (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    job_name TEXT,
    started TEXT,
    ended TEXT,
    return_code INTEGER,
    has_logs INTEGER NOT NULL DEFAULT 1
);

CREATE INDEX idx_task_done_job ON task_done (job_name, started DESC);
