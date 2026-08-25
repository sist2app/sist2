<template>
    <Preloader v-if="loading"></Preloader>
    <div v-else-if="content">
        <div v-if="hits.length > 1 || currentPage" class="hit-nav">
            <b-button-group size="sm">
                <b-button variant="outline-secondary" :disabled="hits.length < 2" @click="go(-1)">‹</b-button>
                <b-button variant="outline-secondary" :disabled="hits.length < 2" @click="go(1)">›</b-button>
            </b-button-group>
            <span v-if="hits.length" class="hit-count">{{ current + 1 }} / {{ hits.length }}</span>
            <a v-if="currentPage && canOpenAtPage" class="badge badge-primary hit-page"
               :href="`f/${sid(doc)}#page=${currentPage}`" target="_blank">
                {{ $t("page") }} {{ currentPage }}
            </a>
            <span v-else-if="currentPage" class="badge badge-secondary hit-page">
                {{ $t("page") }} {{ currentPage }}
            </span>
        </div>
        <div class="content-div" ref="content" v-html="content"></div>
    </div>
</template>

<script>
import Sist2Api from "@/Sist2Api";
import Preloader from "@/components/Preloader.vue";
import {contentHits, HIT_ID_PREFIX, markAll, parsePageBreaks, queryTerms, sid} from "@/util";

export default {
    name: "LazyContentDiv",
    components: {Preloader},
    props: ["doc"],
    data() {
        return {
            content: "",
            hits: [],
            current: 0,
            loading: true,
        }
    },
    computed: {
        currentPage() {
            return this.hits[this.current]?.page ?? null;
        },
        // The file endpoint reads the file off the disk, so a page of a PDF inside an archive has
        // nothing to open
        canOpenAtPage() {
            return this.doc._source.mime === "application/pdf" && !this.doc._props?.isSubDocument;
        }
    },
    mounted() {
        Sist2Api
            .getDocument(sid(this.doc), this.$store.state.optHighlight, this.$store.state.fuzzy)
            .then(doc => {
                this.loading = false;

                if (doc) {
                    const {html, hits} = contentHits(
                        this.getContent(doc),
                        parsePageBreaks(doc._source.page_breaks)
                    );

                    this.content = html;
                    this.hits = hits;

                    this.$nextTick(() => this.markCurrent(true));
                }
            });
    },
    methods: {
        sid: sid,
        getContent(doc) {
            if (doc.highlight) {
                if (doc.highlight["content.nGram"]) {
                    return doc.highlight["content.nGram"][0];
                }
                if (doc.highlight.content) {
                    return doc.highlight.content[0];
                }
            }

            if (!doc._source.content) {
                return "";
            }

            // The SQLite backend hands back the text of a document unmarked
            const terms = this.$store.state.optHighlight ? queryTerms(this.$store.getters.searchText) : [];

            return markAll(doc._source.content, terms);
        },
        go(delta) {
            if (this.hits.length === 0) {
                return;
            }

            this.markCurrent(false);
            this.current = (this.current + delta + this.hits.length) % this.hits.length;
            this.markCurrent(true);

            this.$refs.content
                ?.querySelector(`#${HIT_ID_PREFIX}${this.current}`)
                ?.scrollIntoView({block: "center"});
        },
        markCurrent(on) {
            const el = this.$refs.content?.querySelector(`#${HIT_ID_PREFIX}${this.current}`);

            if (el) {
                el.classList.toggle("current-hit", on);
            }
        }
    }
}
</script>

<style scoped>
.hit-nav {
    display: flex;
    align-items: center;
    gap: 0.5em;
    padding: 6px 3px;
    margin: 0 3px;
    /* The content of a long document scrolls past the modal, so the navigation follows it */
    position: sticky;
    top: 0;
    z-index: 1;
    background: #fff;
}

.theme-black .hit-nav {
    background: #212121;
}

.hit-count {
    font-size: 13px;
}

.hit-page {
    text-decoration: none;
}
</style>
