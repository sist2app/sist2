<template>
    <div class="container">
        <Lightbox></Lightbox>
        <HelpDialog :show="showHelp" @close="showHelp = false"></HelpDialog>

        <b-card v-if="uiLoading">
            <Preloader></Preloader>
        </b-card>

        <b-alert v-show="!uiLoading && showEsConnectionError" show variant="danger" class="mt-2">
            {{ $t("toast.esConnErr") }}
        </b-alert>

        <b-card v-show="!uiLoading && !showEsConnectionError" id="search-panel">
            <SearchBar @show-help="showHelp=true"></SearchBar>
            <EmbeddingsSearchBar v-if="hasEmbeddings" class="mt-3"></EmbeddingsSearchBar>
            <b-row>
                <b-col style="height: 70px;" sm="6">
                    <SizeSlider></SizeSlider>
                </b-col>
                <b-col>
                    <PathTree @search="search(true)"></PathTree>
                </b-col>
            </b-row>
            <b-row>
                <b-col sm="6">
                    <DateSlider></DateSlider>
                    <b-row>
                        <b-col>
                            <IndexPicker></IndexPicker>
                        </b-col>
                    </b-row>
                </b-col>
                <b-col>
                    <b-tabs justified>
                        <b-tab :title="$t('mimeTypes')">
                            <MimePicker></MimePicker>
                        </b-tab>
                        <b-tab :title="$t('tags')">
                            <TagPicker :show-search-bar="$store.state.optShowTagPickerFilter"></TagPicker>
                        </b-tab>
                    </b-tabs>
                </b-col>
            </b-row>
        </b-card>

        <div v-show="docs.length === 0 && !uiLoading">
            <Preloader v-if="searchBusy" class="mt-3"></Preloader>

            <ResultsCard></ResultsCard>
        </div>

        <div v-if="docs.length > 0">
            <ResultsCard></ResultsCard>

            <DocCardWall v-if="optDisplay==='grid'" :docs="docs" :append="appendFunc"></DocCardWall>
            <DocList v-else :docs="docs" :append="appendFunc"></DocList>
        </div>
    </div>
</template>

<script>
import {debounce as _debounce, sid} from "@/util";
import Preloader from "@/components/Preloader.vue";
import {mapActions, mapGetters, mapMutations} from "vuex";
import SearchBar from "@/components/SearchBar.vue";
import IndexPicker from "@/components/IndexPicker.vue";
import Vue from "vue";
import Sist2Query from "@/Sist2ElasticsearchQuery";
import DocCardWall from "@/components/DocCardWall.vue";
import Lightbox from "@/components/Lightbox.vue";
import LightboxCaption from "@/components/LightboxCaption.vue";
import MimePicker from "../components/MimePicker.vue";
import ResultsCard from "@/components/ResultsCard.vue";
import PathTree from "@/components/PathTree.vue";
import SizeSlider from "@/components/SizeSlider.vue";
import DateSlider from "@/components/DateSlider.vue";
import TagPicker from "@/components/TagPicker.vue";
import DocList from "@/components/DocList.vue";
import HelpDialog from "@/components/HelpDialog.vue";
import EmbeddingsSearchBar from "@/components/EmbeddingsSearchBar.vue";
import Sist2Api from "@/Sist2Api";


export default Vue.extend({
    components: {
        EmbeddingsSearchBar,
        HelpDialog,
        DocList,
        TagPicker,
        DateSlider,
        SizeSlider, PathTree, ResultsCard, MimePicker, Lightbox, DocCardWall, IndexPicker, SearchBar, Preloader
    },
    data: () => ({
        uiLoading: true,
        search: undefined,
        docs: [],
        docIds: new Set(),
        docChecksums: new Set(),
        searchBusy: false,
        Sist2Query: Sist2Query,
        showHelp: false,
        showEsConnectionError: false
    }),
    computed: {
        ...mapGetters(["indices", "optDisplay"]),
        hasEmbeddings() {
            return Sist2Api.models().length > 0;
        },
    },
    mounted() {
        // Handle touch events
        window.ontouchend = () => this.$store.commit("busTouchEnd");
        window.ontouchcancel = () => this.$store.commit("busTouchEnd");

        this.search = _debounce(async (clear) => {
            if (clear) {
                await this.clearResults();
            }

            await this.searchNow();

        }, 350);

        this.$store.dispatch("loadFromArgs", this.$route).then(() => {
            this.$store.subscribe(() => this.$store.dispatch("updateArgs", this.$router));
            this.$store.subscribe((mutation) => {
                if ([
                    "setSizeMin", "setSizeMax", "setDateMin", "setDateMax", "setSearchText", "setPathText",
                    "setSortMode", "setOptHighlight", "setOptFragmentSize", "setFuzzy", "setSize", "setSelectedIndices",
                    "setSelectedMimeTypes", "setSelectedTags", "setOptQueryMode", "setOptSearchInPath",
                    "setEmbedding"
                ].includes(mutation.type)) {
                    if (this.searchBusy) {
                        return;
                    }

                    this.search(true);
                }
            });
        });

        this.setIndices(this.$store.getters["sist2Info"].indices)

        Sist2Api.getDateRange().then((range) => {
            this.setDateBoundsMin(range.min);
            this.setDateBoundsMax(range.max);

            const doBlankSearch = !this.$store.state.optUpdateMimeMap;

            Sist2Api.getMimeTypes(Sist2Query.searchQuery(doBlankSearch)).then(({mimeMap}) => {
                this.$store.commit("setUiMimeMap", mimeMap);
                this.uiLoading = false;
                this.search(true);
            });
        }).catch(error => {
            console.log(error);

            if (error.response.status === 503 || error.response.status === 500) {
                this.showEsConnectionError = true;
                this.uiLoading = false;
            } else {
                this.showErrorToast();
            }
        });
    },
    methods: {
        ...mapActions({
            setSist2Info: "setSist2Info",
        }),
        ...mapMutations({
            setIndices: "setIndices",
            setDateBoundsMin: "setDateBoundsMin",
            setDateBoundsMax: "setDateBoundsMax",
            setTags: "setTags",
        }),
        showErrorToast() {
            this.$bvToast.toast(
                this.$t("toast.esConnErr"),
                {
                    title: this.$t("toast.esConnErrTitle"),
                    noAutoHide: true,
                    toaster: "b-toaster-bottom-right",
                    headerClass: "toast-header-error",
                    bodyClass: "toast-body-error",
                });
        },
        showSyntaxErrorToast: function () {
            this.$bvToast.toast(
                this.$t("toast.esQueryErr"),
                {
                    title: this.$t("toast.esQueryErrTitle"),
                    noAutoHide: true,
                    toaster: "b-toaster-bottom-right",
                    headerClass: "toast-header-warning",
                    bodyClass: "toast-body-warning",
                });
        },
        async searchNow() {
            this.searchBusy = true;
            await this.$store.dispatch("incrementQuerySequence");
            this.$store.commit("busSearch");

            Sist2Api.search().then(async (resp) => {
                await this.handleSearch(resp);
                this.searchBusy = false;
            }).catch(err => {
                console.log(err)
                if (err.response.status === 500 && this.$store.state.optQueryMode === "advanced") {
                    this.showSyntaxErrorToast();
                } else {
                    this.showErrorToast();
                }
            });
        },
        async clearResults() {
            this.docs = [];
            this.docIds.clear();
            this.docChecksums.clear();
            await this.$store.dispatch("clearResults");
            this.$store.commit("setUiReachedScrollEnd", false);
        },
        async handleSearch(resp) {
            if (resp.hits.hits.length === 0 || resp.hits.hits.length < this.$store.state.optSize) {
                this.$store.commit("setUiReachedScrollEnd", true);
            }

            resp.hits.hits = resp.hits.hits.filter(hit => !this.docIds.has(hit._id));

            if (this.$store.state.optHideDuplicates) {
                resp.hits.hits = resp.hits.hits.filter(hit => {

                    if (!("checksum" in hit._source)) {
                        return true;
                    }

                    // A checksum only covers the bytes the parser read, so pair it with the size
                    const key = `${hit._source.checksum}:${hit._source.size}`;
                    const isDupe = !this.docChecksums.has(key);
                    this.docChecksums.add(key);
                    return isDupe;
                });
            }

            for (const hit of resp.hits.hits) {
                if (hit._props.isPlayableImage || hit._props.isPlayableVideo) {
                    hit._seq = await this.$store.dispatch("getKeySequence");
                    this.$store.commit("addLightboxSource", {
                        source: `f/${sid(hit)}`,
                        thumbnail_count: hit._props.hasThumbnail
                            ? `t/${sid(hit)}`
                            : null,
                        caption: {
                            component: LightboxCaption,
                            props: {hit: hit}
                        },
                        type: hit._props.isVideo ? "video" : "image"
                    });
                }
            }

            await this.$store.dispatch("remountLightbox");
            this.$store.commit("setLastQueryResult", resp);
            if (this.$store.state.firstQueryResults == null) {
                this.$store.commit("setFirstQueryResult", resp);
            }

            this.docs.push(...resp.hits.hits);

            resp.hits.hits.forEach(hit => this.docIds.add(hit._id));
        },
        appendFunc() {
            if (!this.$store.state.uiReachedScrollEnd && this.search && !this.searchBusy) {
                this.searchNow();
            }
        }
    },
    beforeRouteUpdate(to, from, next) {
        if (this.$store.state.uiLightboxIsOpen) {
            this.$store.commit("_setUiShowLightbox", false);
            next(false);
        } else {
            next();
        }
    },
})
</script>

<style>

#search-panel {
    box-shadow: 0 .125rem .25rem rgba(0, 0, 0, .08) !important;
    border-radius: 0;
    border: none;
}

.toast-header-info, .toast-body-info {
    background: #2196f3;
    color: #fff !important;
}

.toast-header-error, .toast-body-error {
    background: #a94442;
    color: #f2dede !important;
}

.toast-header-error {
    color: #fff !important;
    border-bottom: none;
    margin-bottom: -1em;
}

.toast-header-error .close {
    text-shadow: none;
}

.toast-header-warning, .toast-body-warning {
    background: #FF8F00;
    color: #FFF3E0 !important;
}
</style>