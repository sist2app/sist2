<template>
    <div class="featured-line" v-html="featuredLineHtml"></div>
</template>

<script>
import {escapeHtml, humanDate, humanFileSize} from "@/util";

function scopedEval(context, expr) {
    const evaluator = Function.apply(null, [...Object.keys(context), "expr", "return eval(expr)"]);
    return evaluator.apply(null, [...Object.values(context), expr]);
}


export default {
    name: "FeaturedFieldsLine",
    props: ["doc"],
    computed: {
        featuredLineHtml() {
            if (this.$store.getters.optFeaturedFields === undefined) {
                return "";
            }

            const scope = {doc: this.doc._source, humanDate: humanDate, humanFileSize: humanFileSize};

            return this.$store.getters.optFeaturedFields
                .replaceAll(/\$\{([^}]*)}/g, (match, g1) => {
                    return escapeHtml(scopedEval(scope, g1));
                });
        }
    }
}
</script>

<style scoped>

.featured-line {
    font-size: 90%;
    font-family: 'Source Sans Pro', 'Helvetica Neue', Arial, sans-serif;
    color: #424242;
    padding-left: 2px;
}

.theme-black .featured-line {
    color: #bebebe;
}
</style>