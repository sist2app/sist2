<template>
    <Preloader v-if="loading"></Preloader>
    <div v-else-if="content">
        <div class="content-div" v-html="content"></div>
    </div>
</template>

<script>
import Sist2Api from "@/Sist2Api";
import Preloader from "@/components/Preloader.vue";

export default {
    name: "LazyContentDiv",
    components: {Preloader},
    props: ["sid"],
    data() {
        return {
            content: "",
            loading: true,
        }
    },
    mounted() {
        Sist2Api
            .getDocument(this.sid, this.$store.state.optHighlight, this.$store.state.fuzzy)
            .then(doc => {
                this.loading = false;

                if (doc) {
                    this.content = this.getContent(doc)
                }
            });
    },
    methods: {
        getContent(doc) {
            if (!doc.highlight) {
                return doc._source.content;
            }

            if (doc.highlight["content.nGram"]) {
                return doc.highlight["content.nGram"][0];
            }
            if (doc.highlight.content) {
                return doc.highlight.content[0];
            }
        },
    }
}
</script>
