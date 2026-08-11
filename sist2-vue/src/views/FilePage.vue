<template>
    <div style="margin-left: auto; margin-right: auto;" class="container">
        <Preloader v-if="loading"></Preloader>
        <b-card v-else-if="!loading && found">
            <b-card-title :title="doc._source.name + ext(doc)">
                {{ doc._source.name + ext(doc) }}
            </b-card-title>

            <!-- Thumbnail-->
            <div style="position: relative; margin-left: auto; margin-right: auto; text-align: center">
                <FullThumbnail :doc="doc" :small-badge="false" @onThumbnailClick="onThumbnailClick()"></FullThumbnail>
            </div>

            <!-- Audio player-->
            <audio v-if="doc._props.isAudio" ref="audio" preload="none" class="audio-fit fit" controls
                   :type="doc._source.mime"
                   :src="`f/${sid(doc)}`"></audio>

            <InfoTable :doc="doc" v-if="doc"></InfoTable>

            <div v-if="doc._source.content" class="content-div">{{ doc._source.content }}</div>
        </b-card>
        <div v-else>
            <b-card>
                <b-card-title>{{ $t("filePage.notFound") }}</b-card-title>
            </b-card>
        </div>
    </div>
</template>

<script>
import Preloader from "@/components/Preloader.vue";
import InfoTable from "@/components/InfoTable.vue";
import Sist2Api from "@/Sist2Api";
import {ext, sid} from "@/util";
import Vue from "vue";
import FullThumbnail from "@/components/FullThumbnail.vue";

export default Vue.extend({
    name: "FilePage",
    components: {
        FullThumbnail,
        Preloader,
        InfoTable
    },
    data() {
        return {
            loading: true,
            found: false,
            doc: null
        }
    },
    methods: {
        ext: ext,
        sid: sid,
        onThumbnailClick() {
            window.open(`/f/${sid(this.doc)}`, "_blank");
        },
        findByCustomField(field, id) {
            return {
                query: {
                    bool: {
                        must: [
                            {
                                match: {
                                    [field]: id
                                }
                            }
                        ]
                    }
                },
                size: 1
            }
        },
        findById(id) {
            return {
                query: {
                    bool: {
                        must: [
                            {
                                match: {
                                    "_id": id
                                }
                            }
                        ]
                    }
                },
                size: 1
            }
        },
        findByName(name) {
            return {
                query: {
                    bool: {
                        must: [
                            {
                                match: {
                                    "name": name
                                }
                            }
                        ]
                    }
                },
                size: 1
            }
        }

    },
    mounted() {
        let query = null;
        if (this.$route.query.byId) {
            query = this.findById(this.$route.query.byId);
        } else if (this.$route.query.byName) {
            query = this.findByName(this.$route.query.byName);
        } else if (this.$route.query.by && this.$route.query.q) {
            query = this.findByCustomField(this.$route.query.by, this.$route.query.q)
        }

        if (query) {
            Sist2Api.esQuery(query).then(result => {
                if (result.hits.hits.length === 0) {
                    this.found = false;
                } else {
                    this.doc = result.hits.hits[0];
                    this.found = true;
                }

                this.loading = false;
            });
        } else {
            this.loading = false;
            this.found = false;
        }
    }
});
</script>

<style scoped>
.img-wrapper {
    display: inline-block;
}
</style>