import axios from "axios";
import {strUnescape, lum, sid} from "./util";
import Sist2Query from "@/Sist2ElasticsearchQuery";
import store from "@/store";


class Sist2Api {

    baseUrl;
    sist2Info;
    queryfunc;

    constructor(baseUrl) {
        this.baseUrl = baseUrl;
    }

    init(queryFunc) {
        this.queryfunc = queryFunc;
    }

    backend() {
        return this.sist2Info.searchBackend;
    }

    models() {
        const allModels = this.sist2Info.indices
            .map(idx => idx.models)
            .flat();

        return allModels
            .filter((v, i, a) => a.findIndex(v2 => (v2.id === v.id)) === i)
    }

    getSist2Info() {
        return axios.get(`${this.baseUrl}i`).then(resp => {
            this.sist2Info = resp.data;

            return resp.data;
        })
    }

    setHitProps(hit) {
        hit["_props"] = {};

        const mimeCategory = hit._source.mime == null ? null : hit._source.mime.split("/")[0];

        if ("parent" in hit._source) {
            hit._props.isSubDocument = true;
        }

        if ("thumbnail" in hit._source && hit._source.thumbnail > 0) {
            hit._props.hasThumbnail = true;

            if (Number.isNaN(Number(hit._source.thumbnail))) {
                // Backwards compatibility
                hit._props.tnNum = 1;
                hit._props.hasVidPreview = false;
            } else {
                hit._props.tnNum = Number(hit._source.thumbnail);
                hit._props.hasVidPreview = hit._props.tnNum > 1;
            }
        }

        switch (mimeCategory) {
            case "image":
                if (hit._source.videoc === "gif") {
                    hit._props.isGif = true;
                } else {
                    hit._props.isImage = true;
                }
                if ("width" in hit._source && !hit._props.isSubDocument && hit._source.videoc !== "tiff"
                    && hit._source.videoc !== "raw" && hit._source.videoc !== "ppm"
                    && hit._source.mime !== "image/jp2") {
                    hit._props.isPlayableImage = true;
                }
                if ("width" in hit._source && "height" in hit._source) {
                    hit._props.imageAspectRatio = hit._source.width / hit._source.height;
                }
                break;
            case "video":
                if ("videoc" in hit._source) {
                    hit._props.isVideo = true;
                }
                if (hit._props.isVideo) {
                    const videoc = hit._source.videoc;
                    const mime = hit._source.mime;

                    hit._props.isPlayableVideo = mime != null &&
                        mime.startsWith("video/") &&
                        !hit._props.isSubDocument &&
                        hit._source.extension !== "mkv" &&
                        hit._source.extension !== "avi" &&
                        hit._source.extension !== "mov" &&
                        videoc !== "hevc" &&
                        videoc !== "mpeg1video" &&
                        videoc !== "mpeg2video" &&
                        videoc !== "wmv3";
                }
                break;
            case "audio":
                if ("audioc" in hit._source && !hit._props.isSubDocument) {
                    hit._props.isAudio = true;
                }
                break;
        }
    }

    setHitTags(hit) {
        const tags = [];

        // User tags
        if ("tag" in hit._source) {
            hit._source.tag.forEach(tag => {
                tags.push(this.createUserTag(tag));
            })
        }

        hit._tags = tags;
    }

    createUserTag(tag) {
        const tokens = tag.split(".");

        const colorToken = tokens.pop();

        const bg = colorToken;
        const fg = lum(colorToken) > 50 ? "#000" : "#fff";

        return {
            style: "user",
            fg: fg,
            bg: bg,
            text: tokens.join("."),
            rawText: tag,
            userTag: true,
        };
    }

    search() {
        if (this.backend() === "sqlite") {
            return this.ftsQuery(this.queryfunc())
        } else {
            return this.esQuery(this.queryfunc());
        }
    }

    _getIndexRoot(indexId) {
        return this.sist2Info.indices.find(idx => idx.id === indexId).root;
    }

    esQuery(query) {
        return axios.post(`${this.baseUrl}es`, query).then(resp => {
            const res = resp.data;

            if (res.hits?.hits) {
                res.hits.hits.forEach((hit) => {
                    hit["_source"]["name"] = strUnescape(hit["_source"]["name"]);
                    hit["_source"]["path"] = strUnescape(hit["_source"]["path"]);
                    hit["_source"]["indexRoot"] = this._getIndexRoot(hit["_source"]["index"]);

                    this.setHitProps(hit);
                    this.setHitTags(hit);
                });
            }

            return res;
        });
    }

    ftsQuery(query) {
        return axios.post(`${this.baseUrl}fts/search`, query).then(resp => {
            const res = resp.data;

            if (res.hits.hits) {
                res.hits.hits.forEach(hit => {
                    hit["_source"]["name"] = strUnescape(hit["_source"]["name"]);
                    hit["_source"]["path"] = strUnescape(hit["_source"]["path"]);

                    this.setHitProps(hit);
                    this.setHitTags(hit);

                    if ("highlight" in hit) {
                        hit["highlight"]["name"] = [hit["highlight"]["name"]];
                        hit["highlight"]["content"] = [hit["highlight"]["content"]];
                    }
                });
            }

            return res;
        });
    }

    getMimeTypesEs(query) {
        const AGGS = {
            mimeTypes: {
                terms: {
                    field: "mime",
                    size: 10000
                }
            }
        };

        if (!query) {
            query = {
                aggs: AGGS,
                size: 0,
            };
        } else {
            query.size = 0;
            query.aggs = AGGS;
        }

        return this.esQuery(query).then(resp => {
            return resp["aggregations"]["mimeTypes"]["buckets"].map(bucket => ({
                mime: bucket.key,
                count: bucket.doc_count
            }));

        });
    }

    getMimeTypesSqlite() {
        return axios.get(`${this.baseUrl}fts/mimetypes`)
            .then(resp => {
                return resp.data;
            });
    }

    async getMimeTypes(query = undefined) {
        let buckets;

        if (this.backend() === "sqlite") {
            buckets = await this.getMimeTypesSqlite();
        } else {
            buckets = await this.getMimeTypesEs(query);
        }

        const mimeMap = [];

        buckets.sort((a, b) => a.mime > b.mime).forEach((bucket) => {
            const tmp = bucket.mime.split("/");
            const category = tmp[0];
            const mime = tmp[1];

            let category_exists = false;

            const child = {
                "id": bucket.mime,
                "text": `${mime} (${bucket.count})`
            };

            mimeMap.forEach(node => {
                if (node.text === category) {
                    node.children.push(child);
                    category_exists = true;
                }
            });

            if (!category_exists) {
                mimeMap.push({text: category, children: [child], id: category});
            }
        })

        mimeMap.forEach(node => {
            if (node.children) {
                node.children.sort((a, b) => a.id.localeCompare(b.id));
            }
        })
        mimeMap.sort((a, b) => a.id.localeCompare(b.id))

        return {buckets, mimeMap};
    }

    _createEsTag(tag, count) {
        const tokens = tag.split(".");

        if (/.*\.#[0-9a-fA-F]{6}/.test(tag)) {
            return {
                id: tokens.slice(0, -1).join("."),
                color: tokens.pop(),
                isLeaf: true,
                count: count
            };
        }

        return {
            id: tag,
            count: count,
            isLeaf: false,
            color: undefined
        };
    }

    getTagsEs() {
        return this.esQuery({
            aggs: {
                tags: {
                    terms: {
                        field: "tag",
                        size: 65535
                    }
                }
            },
            size: 0,
        }).then(resp => {
            return resp["aggregations"]["tags"]["buckets"]
                .sort((a, b) => a["key"].localeCompare(b["key"]))
                .map((bucket) => this._createEsTag(bucket["key"], bucket["doc_count"]));
        });
    }

    getTagsSqlite() {
        return axios.get(`${this.baseUrl}fts/tags`)
            .then(resp => {
                return resp.data.map(tag => this._createEsTag(tag.tag, tag.count))
            });
    }

    async getTags() {
        let tags;
        if (this.backend() === "sqlite") {
            tags = await this.getTagsSqlite();
        } else {
            tags = await this.getTagsEs();
        }

        // Remove duplicates (same tag with different color)
        const seen = new Set();

        return tags.filter((t) => {
            if (seen.has(t.id)) {
                return false;
            }
            seen.add(t.id);
            return true;
        });
    }

    saveTag(tag, hit) {
        return axios.post(`${this.baseUrl}tag/${sid(hit)}`, {
            delete: false,
            name: tag,
        });
    }

    deleteTag(tag, hit) {
        return axios.post(`${this.baseUrl}tag/${sid(hit)}`, {
            delete: true,
            name: tag,
        });
    }

    searchPaths(indexId, minDepth, maxDepth, prefix = null) {
        if (this.backend() === "sqlite") {
            return this.searchPathsSqlite(indexId, minDepth, minDepth, prefix);
        } else {
            return this.searchPathsEs(indexId, minDepth, maxDepth, prefix);
        }
    }

    searchPathsSqlite(indexId, minDepth, maxDepth, prefix) {
        return axios.post(`${this.baseUrl}fts/paths`, {
            indexId, minDepth, maxDepth, prefix
        }).then(resp => {
            return resp.data;
        });
    }

    searchPathsEs(indexId, minDepth, maxDepth, prefix) {

        const query = {
            query: {
                bool: {
                    filter: [
                        {term: {index: indexId}},
                        {range: {_depth: {gte: minDepth, lte: maxDepth}}},
                    ]
                }
            },
            aggs: {
                paths: {
                    terms: {
                        field: "path",
                        size: 10000
                    }
                }
            },
            size: 0
        };

        if (prefix != null) {
            query["query"]["bool"]["must"] = {
                prefix: {
                    path: prefix,
                }
            };
        }

        return this.esQuery(query).then(resp => {
            const buckets = resp["aggregations"]["paths"]["buckets"];

            if (!buckets) {
                return [];
            }

            return buckets
                .map(bucket => ({
                    path: bucket.key,
                    count: bucket.doc_count
                }));
        });
    }

    getDateRangeSqlite() {
        return axios.get(`${this.baseUrl}fts/dateRange`)
            .then(resp => ({
                min: resp.data.dateMin,
                max: (resp.data.dateMax === resp.data.dateMin)
                    ? resp.data.dateMax + 1
                    : resp.data.dateMax,
            }));
    }

    getDateRange() {
        if (this.backend() === "sqlite") {
            return this.getDateRangeSqlite();
        } else {
            return this.getDateRangeEs();
        }
    }

    getDateRangeEs() {
        return this.esQuery({
            // TODO: filter current selected indices
            aggs: {
                dateMin: {min: {field: "mtime"}},
                dateMax: {max: {field: "mtime"}},
            },
            size: 0
        }).then(res => {
            const range = {
                min: res.aggregations.dateMin.value / 1000,
                max: res.aggregations.dateMax.value / 1000,
            }

            if (range.min == null) {
                range.min = 0;
                range.max = 1;
            } else if (range.min === range.max) {
                range.max += 1;
            }

            return range;
        });
    }

    getPathSuggestionsSqlite(text) {
        return axios.post(`${this.baseUrl}fts/paths`, {
            prefix: text,
            minDepth: 1,
            maxDepth: 10000
        }).then(resp => {
            return resp.data.map(bucket => bucket.path);
        })
    }

    getPathSuggestionsEs(text) {
        return this.esQuery({
            suggest: {
                path: {
                    prefix: text,
                    completion: {
                        field: "suggest-path",
                        skip_duplicates: true,
                        size: 10000
                    }
                }
            }
        }).then(resp => {
            return resp["suggest"]["path"][0]["options"]
                .map(opt => opt["_source"]["path"]);
        });
    }

    getPathSuggestions(text) {
        if (this.backend() === "sqlite") {
            return this.getPathSuggestionsSqlite(text);
        } else {
            return this.getPathSuggestionsEs(text)
        }
    }

    getTreemapStat(indexId) {
        return `${this.baseUrl}s/${indexId}/TMAP`;
    }

    getMimeStat(indexId) {
        return `${this.baseUrl}s/${indexId}/MAGG`;
    }

    getSizeStat(indexId) {
        return `${this.baseUrl}s/${indexId}/SAGG`;
    }

    getDateStat(indexId) {
        return `${this.baseUrl}s/${indexId}/DAGG`;
    }

    getDocumentEs(sid, highlight, fuzzy) {
        const query = Sist2Query.searchQuery();

        if (highlight) {
            const fields = fuzzy
                ? {"content.nGram": {}}
                : {content: {}};

            query.highlight = {
                pre_tags: ["<mark>"],
                post_tags: ["</mark>"],
                number_of_fragments: 0,
                fields,
            };

            if (!store.state.sist2Info.esVersionLegacy) {
                query.highlight.max_analyzed_offset = 999_999;
            }
        }

        if ("knn" in query) {
            query.query = {
                bool: {
                    must: []
                }
            };
            delete query.knn;
        }

        if ("function_score" in query.query) {
            query.query = query.query.function_score.query;
        }

        if (!("must" in query.query.bool)) {
            query.query.bool.must = [];
        } else if (!Array.isArray(query.query.bool.must)) {
            query.query.bool.must = [query.query.bool.must];
        }

        query.query.bool.must.push({match: {_id: sid}});

        delete query["sort"];
        delete query["aggs"];
        delete query["search_after"];
        delete query.query["function_score"];

        query._source = {
            includes: ["content", "name", "path", "extension"]
        }

        query.size = 1;

        return this.esQuery(query).then(resp => {
            if (resp.hits.hits.length === 1) {
                return resp.hits.hits[0];
            }
            return null;
        });
    }

    getDocumentSqlite(sid) {
        return axios.get(`${this.baseUrl}fts/d/${sid}`)
            .then(resp => ({
                _source: resp.data
            }));
    }

    getDocument(sid, highlight, fuzzy) {
        if (this.backend() === "sqlite") {
            return this.getDocumentSqlite(sid);
        } else {
            return this.getDocumentEs(sid, highlight, fuzzy);
        }
    }

    getTagSuggestions(prefix) {
        if (this.backend() === "sqlite") {
            return this.getTagSuggestionsSqlite(prefix);
        } else {
            return this.getTagSuggestionsEs(prefix);
        }
    }

    getTagSuggestionsSqlite(prefix) {
        return axios.post(`${this.baseUrl}fts/suggestTags`, prefix)
            .then(resp => (resp.data));
    }

    getTagSuggestionsEs(prefix) {
        return this.esQuery({
            suggest: {
                tag: {
                    prefix: prefix,
                    completion: {
                        field: "suggest-tag",
                        skip_duplicates: true,
                        size: 10000
                    }
                }
            }
        }).then(resp => {
            const result = [];
            resp["suggest"]["tag"][0]["options"].map(opt => opt["_source"]["tag"]).forEach(tags => {
                tags.forEach(tag => {
                    const t = tag.slice(0, -8);
                    if (!result.find(x => x.slice(0, -8) === t)) {
                        result.push(tag);
                    }
                });
            });
            return result;
        });
    }

    getEmbeddings(sid, modelId) {
        return axios.post(`${this.baseUrl}e/${sid}/${modelId.toString().padStart(3, '0')}`)
            .then(resp => (resp.data));
    }
}

export default new Sist2Api("");