const TYPE_TEXT = "text";
const TYPE_INTEGER = "integer";
const TYPE_REAL = "real";
const TYPE_BOOLEAN = "boolean";

export const SCAN_OPTION_TYPES = {
    path: TYPE_TEXT,
    threads: TYPE_INTEGER,
    thumbnail_quality: TYPE_INTEGER,
    thumbnail_size: TYPE_INTEGER,
    thumbnail_count: TYPE_INTEGER,
    content_size: TYPE_INTEGER,
    depth: TYPE_INTEGER,
    archive: TYPE_TEXT,
    archive_passphrase: TYPE_TEXT,
    ocr_lang: TYPE_TEXT,
    ocr_images: TYPE_BOOLEAN,
    ocr_ebooks: TYPE_BOOLEAN,
    exclude: TYPE_TEXT,
    fast: TYPE_BOOLEAN,
    treemap_threshold: TYPE_REAL,
    mem_buffer: TYPE_INTEGER,
    read_subtitles: TYPE_BOOLEAN,
    fast_epub: TYPE_BOOLEAN,
    checksums: TYPE_BOOLEAN,
    incremental: TYPE_BOOLEAN,
    optimize_index: TYPE_BOOLEAN,
    rewrite_url: TYPE_TEXT
};

export const WEB_OPTION_TYPES = {
    search_backend: TYPE_TEXT,
    bind: TYPE_TEXT,
    tagline: TYPE_TEXT,
    lang: TYPE_TEXT,
    auth: TYPE_TEXT,
    tag_auth: TYPE_TEXT,
    auth0_audience: TYPE_TEXT,
    auth0_domain: TYPE_TEXT,
    auth0_client_id: TYPE_TEXT,
    auth0_public_key: TYPE_TEXT,
    dev: TYPE_BOOLEAN,
    verbose: TYPE_BOOLEAN
};

export const SEARCH_BACKEND_TYPES = {
    backend_type: TYPE_TEXT,
    search_index: TYPE_TEXT,
    es_url: TYPE_TEXT,
    es_insecure_ssl: TYPE_BOOLEAN,
    es_index: TYPE_TEXT,
    es_mappings: TYPE_TEXT,
    es_settings: TYPE_TEXT,
    threads: TYPE_INTEGER,
    batch_size: TYPE_INTEGER
};

export const USER_SCRIPT_TYPES = {
    type: TYPE_TEXT,
    git_repository: TYPE_TEXT,
    force_clone: TYPE_BOOLEAN,
    script: TYPE_TEXT,
    extra_args: TYPE_TEXT
};

export const JOB_FIELD_TYPES = {
    cron_expression: TYPE_TEXT,
    schedule_enabled: TYPE_BOOLEAN,
    keep_last_n_logs: TYPE_INTEGER,
    index_path: TYPE_TEXT,
    previous_index_path: TYPE_TEXT,
    last_index_date: TYPE_TEXT,
    status: TYPE_TEXT,
    do_full_scan: TYPE_BOOLEAN
};

export const INDEX_OPTION_TYPES = {
    search_backend: TYPE_TEXT,
    incremental_index: TYPE_BOOLEAN
};

export const FULL_SCAN_FIELDS = [
    "path",
    "thumbnail_count",
    "thumbnail_quality",
    "thumbnail_size",
    "content_size",
    "depth",
    "archive",
    "archive_passphrase",
    "ocr_lang",
    "ocr_images",
    "ocr_ebooks",
    "fast",
    "checksums",
    "read_subtitles"
];

// Job fields the client may set through PUT /api/job; the rest
// (index_path, previous_index_path, last_index_date, status, do_full_scan)
// are managed by the server task lifecycle
const CLIENT_JOB_FIELDS = [
    "cron_expression",
    "schedule_enabled",
    "keep_last_n_logs"
];

// Coerce client-supplied JSON values to the declared field type so they
// compare cleanly (===) against values read back from the database
function coerce(type, value) {
    if (value === null || value === undefined) {
        return null;
    }
    if (type === TYPE_BOOLEAN) {
        return Boolean(value);
    }
    if (type === TYPE_INTEGER || type === TYPE_REAL) {
        return Number(value);
    }
    return String(value);
}

function fromRow(type, value) {
    if (value === null || value === undefined) {
        return null;
    }
    if (type === TYPE_BOOLEAN) {
        return value !== 0;
    }
    return value;
}

function toRow(type, value) {
    if (value === null || value === undefined) {
        return null;
    }
    if (type === TYPE_BOOLEAN) {
        if (value) {
            return 1;
        }
        return 0;
    }
    if (type === TYPE_INTEGER || type === TYPE_REAL) {
        return Number(value);
    }
    return String(value);
}

function mapFromRow(types, row, prefix) {
    const result = {};
    for (const [field, type] of Object.entries(types)) {
        result[field] = fromRow(type, row[prefix + field]);
    }
    return result;
}

function mapToRow(types, obj, prefix) {
    const result = {};
    for (const [field, type] of Object.entries(types)) {
        result[prefix + field] = toRow(type, obj[field]);
    }
    return result;
}

export function rowToJob(row, userScripts) {
    const job = mapFromRow(JOB_FIELD_TYPES, row, "");
    job.name = row.name;
    job.scan_options = mapFromRow(SCAN_OPTION_TYPES, row, "scan_");
    job.index_options = {
        search_backend: fromRow(TYPE_TEXT, row.search_backend),
        incremental_index: fromRow(TYPE_BOOLEAN, row.incremental_index)
    };
    job.user_scripts = userScripts;
    return job;
}

export function jobToParams(job) {
    const params = mapToRow(JOB_FIELD_TYPES, job, "");
    params.name = job.name;
    Object.assign(params, mapToRow(SCAN_OPTION_TYPES, job.scan_options, "scan_"));
    params.search_backend = toRow(TYPE_TEXT, job.index_options.search_backend);
    params.incremental_index = toRow(TYPE_BOOLEAN, job.index_options.incremental_index);
    return params;
}

export function rowToFrontend(row, jobs) {
    return {
        name: row.name,
        jobs: jobs,
        auto_start: fromRow(TYPE_BOOLEAN, row.auto_start),
        extra_query_args: fromRow(TYPE_TEXT, row.extra_query_args),
        custom_url: fromRow(TYPE_TEXT, row.custom_url),
        web_options: mapFromRow(WEB_OPTION_TYPES, row, "web_")
    };
}

export function frontendToParams(frontend) {
    const params = mapToRow(WEB_OPTION_TYPES, frontend.web_options, "web_");
    params.name = frontend.name;
    params.auto_start = toRow(TYPE_BOOLEAN, frontend.auto_start);
    params.extra_query_args = toRow(TYPE_TEXT, frontend.extra_query_args);
    params.custom_url = toRow(TYPE_TEXT, frontend.custom_url);
    return params;
}

export function rowToSearchBackend(row) {
    const backend = mapFromRow(SEARCH_BACKEND_TYPES, row, "");
    backend.name = row.name;
    return backend;
}

export function searchBackendToParams(backend) {
    const params = mapToRow(SEARCH_BACKEND_TYPES, backend, "");
    params.name = backend.name;
    return params;
}

export function rowToUserScript(row) {
    const script = mapFromRow(USER_SCRIPT_TYPES, row, "");
    script.name = row.name;
    return script;
}

export function userScriptToParams(script) {
    const params = mapToRow(USER_SCRIPT_TYPES, script, "");
    params.name = script.name;
    return params;
}

export function createDefaultJob(name) {
    return {
        name: name,
        cron_expression: "0 0 * * *",
        schedule_enabled: false,
        keep_last_n_logs: -1,
        index_path: null,
        previous_index_path: null,
        last_index_date: null,
        status: "created",
        do_full_scan: false,
        scan_options: {
            path: "/",
            threads: 1,
            thumbnail_quality: 50,
            thumbnail_size: 552,
            thumbnail_count: 1,
            content_size: 32768,
            depth: -1,
            archive: "recurse",
            archive_passphrase: null,
            ocr_lang: null,
            ocr_images: false,
            ocr_ebooks: false,
            exclude: null,
            fast: false,
            treemap_threshold: 0.0005,
            mem_buffer: 2000,
            read_subtitles: false,
            fast_epub: false,
            checksums: false,
            incremental: true,
            optimize_index: false,
            rewrite_url: null
        },
        index_options: {
            search_backend: null,
            incremental_index: true
        },
        user_scripts: []
    };
}

export function createDefaultFrontend(name, bind, searchBackend) {
    return {
        name: name,
        jobs: [],
        auto_start: false,
        extra_query_args: "",
        custom_url: null,
        web_options: {
            search_backend: searchBackend,
            bind: bind,
            tagline: "Lightning-fast file system indexer and search tool",
            lang: "en",
            auth: null,
            tag_auth: null,
            auth0_audience: null,
            auth0_domain: null,
            auth0_client_id: null,
            auth0_public_key: null,
            dev: false,
            verbose: false
        }
    };
}

export function createDefaultSearchBackend(name, backendType) {
    return {
        name: name,
        backend_type: backendType,
        search_index: `search-index-${name.replaceAll("/", "_")}.sist2`,
        es_url: "http://elasticsearch:9200",
        es_insecure_ssl: false,
        es_index: "sist2",
        es_mappings: null,
        es_settings: null,
        threads: 1,
        batch_size: 70
    };
}

export function normalizeJob(body, existing) {
    const job = structuredClone(existing);
    for (const field of CLIENT_JOB_FIELDS) {
        if (body[field] !== undefined) {
            job[field] = coerce(JOB_FIELD_TYPES[field], body[field]);
        }
    }
    for (const field of Object.keys(SCAN_OPTION_TYPES)) {
        if (body.scan_options !== undefined && body.scan_options[field] !== undefined) {
            job.scan_options[field] = coerce(SCAN_OPTION_TYPES[field], body.scan_options[field]);
        }
    }
    for (const field of Object.keys(INDEX_OPTION_TYPES)) {
        if (body.index_options !== undefined && body.index_options[field] !== undefined) {
            job.index_options[field] = coerce(INDEX_OPTION_TYPES[field], body.index_options[field]);
        }
    }
    if (Array.isArray(body.user_scripts)) {
        job.user_scripts = body.user_scripts.map(String);
    }
    return job;
}

export function normalizeFrontend(body, name) {
    const frontend = createDefaultFrontend(name, "0.0.0.0:4090", null);
    for (const field of ["auto_start", "extra_query_args", "custom_url"]) {
        if (body[field] !== undefined) {
            frontend[field] = body[field];
        }
    }
    for (const field of Object.keys(WEB_OPTION_TYPES)) {
        if (body.web_options !== undefined && body.web_options[field] !== undefined) {
            frontend.web_options[field] = body.web_options[field];
        }
    }
    if (Array.isArray(body.jobs)) {
        frontend.jobs = body.jobs.map(String);
    }
    return frontend;
}

export function normalizeSearchBackend(body, name) {
    const backend = createDefaultSearchBackend(name, "elasticsearch");
    for (const field of Object.keys(SEARCH_BACKEND_TYPES)) {
        if (body[field] !== undefined) {
            backend[field] = body[field];
        }
    }
    backend.name = name;
    return backend;
}

export function normalizeUserScript(body, name) {
    const script = {
        name: name,
        type: "simple",
        git_repository: null,
        force_clone: false,
        script: null,
        extra_args: ""
    };
    for (const field of Object.keys(USER_SCRIPT_TYPES)) {
        if (body[field] !== undefined) {
            script[field] = body[field];
        }
    }
    return script;
}
