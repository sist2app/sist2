<template>
    <div class="content-div" v-if="fragments.length">
        <template v-for="(fragment, i) in fragments">
            <span v-if="i > 0" :key="`separator-${i}`" class="fragment-separator">
                <br><i>[…]</i><br>
            </span>
            <a v-if="pageLink(i)" :key="`page-${i}`" class="badge badge-primary hit-page"
               :href="pageLink(i)" target="_blank">{{ $t("page") }} {{ page(i) }}</a>
            <span v-else-if="page(i)" :key="`page-${i}`" class="badge badge-secondary hit-page">
                {{ $t("page") }} {{ page(i) }}
            </span>
            <span :key="`fragment-${i}`" v-html="fragment"></span>
        </template>
    </div>
</template>

<script>
import {highlightHtml, sid} from "@/util";

export default {
    name: "ContentDiv",
    props: ["doc"],
    computed: {
        fragments() {
            if (!this.doc.highlight) {
                return [];
            }

            const fragments = this.doc.highlight["content.nGram"] ?? this.doc.highlight.content;

            return fragments === undefined ? [] : fragments.map(highlightHtml);
        },
        // The file endpoint reads the file off the disk, so a page of a PDF inside an archive has
        // nothing to open
        canOpenAtPage() {
            return this.doc._source.mime === "application/pdf" && !this.doc._props?.isSubDocument;
        }
    },
    methods: {
        /** The page the fragment was taken from, 0 for a document that is not paginated */
        page(index) {
            return this.doc.hit_pages?.[index] ?? 0;
        },
        pageLink(index) {
            return this.canOpenAtPage && this.page(index)
                ? `f/${sid(this.doc)}#page=${this.page(index)}`
                : null;
        }
    }
}
</script>

<style scoped>
.fragment-separator i {
    line-height: 2.4;
}

.hit-page {
    text-decoration: none;
    margin-right: 0.4em;
}
</style>
