<template>
    <div @mouseenter="showAddButton = true" @mouseleave="showAddButton = false">

        <b-modal v-model="showModal" :title="$t('saveTagModalTitle')" hide-footer no-fade centered size="lg" static
                 lazy>
            <b-row>
                <b-col style="flex-grow: 2" sm>
                    <VueSimpleSuggest
                            ref="suggest"
                            :value="tagText"
                            @select="setTagText($event)"
                            @input="setTagText($event)"
                            class="form-control-fix-flex"
                            style="margin-top: 17px"
                            :list="suggestTag"
                            :max-suggestions="0"
                            :placeholder="$t('saveTagPlaceholder')"
                    >
                        <!-- Suggestion item template-->
                        <div slot="suggestion-item" slot-scope="{ suggestion, query}"
                        >
                            <div class="suggestion-line">
                <span
                        class="badge badge-suggestion"
                        :style="{background: getBg(suggestion), color: getFg(suggestion)}"
                >
                <strong>{{ query }}</strong>{{ getSuggestionWithoutQueryPrefix(suggestion, query) }}
                </span>
                            </div>
                        </div>
                    </VueSimpleSuggest>
                </b-col>
                <b-col class="mt-4">
                    <TwitterColorPicker v-model="color" triangle="hide" :width="252"
                                        class="mr-auto ml-auto"></TwitterColorPicker>
                </b-col>
            </b-row>

            <b-button variant="primary" style="float: right" class="mt-2" @click="saveTag()">{{ $t("confirm") }}
            </b-button>
        </b-modal>


        <template v-for="tag in hit._tags">
            <!-- User tag-->
            <div v-if="tag.userTag" :key="tag.rawText" style="display: inline-block">
        <span
                :id="hit._id+tag.rawText"
                :title="tag.text"
                tabindex="-1"
                class="badge pointer"
                :style="badgeStyle(tag)" :class="badgeClass(tag)"
                @click.right="onTagRightClick(tag, $event)"
        >{{ tag.text.split(".").pop() }}</span>

                <b-popover :target="hit._id+tag.rawText" triggers="focus blur" placement="top">
                    <b-button variant="danger" @click="onTagDeleteClick(tag, $event)">{{ $t("deleteTag") }}</b-button>
                </b-popover>
            </div>

            <span
                    v-else :key="tag.text"
                    class="badge"
                    :style="badgeStyle(tag)" :class="badgeClass(tag)"
            >{{ tag.text.split(".").pop() }}</span>
        </template>

        <!-- Add button -->
        <small v-if="showAddButton" class="badge add-tag-button" @click="tagAdd()">{{ $t("addTag") }}</small>

        <!-- Size tag-->
        <small v-else class="text-muted badge-size" style="padding-left: 2px">{{
            humanFileSize(hit._source.size)
            }}</small>
    </div>
</template>

<script>
import {humanFileSize, lum} from "@/util";
import Vue from "vue";
import {Twitter} from 'vue-color'
import Sist2Api from "@/Sist2Api";
import VueSimpleSuggest from 'vue-simple-suggest'

export default Vue.extend({
    components: {
        "TwitterColorPicker": Twitter,
        VueSimpleSuggest
    },
    props: ["hit"],
    data() {
        return {
            showAddButton: false,
            showModal: false,
            tagText: null,
            color: {
                hex: "#e0e0e0",
            },
        }
    },
    methods: {
        humanFileSize: humanFileSize,
        getSuggestionWithoutQueryPrefix(suggestion, query) {
            return suggestion.id.slice(query.length, -8)
        },
        getBg(suggestion) {
            return suggestion.id.slice(-7);
        },
        getFg(suggestion) {
            return lum(suggestion.id.slice(-7)) > 50 ? "#000" : "#fff";
        },
        setTagText(value) {
            this.$refs.suggest.clearSuggestions();

            if (typeof value === "string") {
                this.tagText = {
                    id: value,
                    title: value
                };
                return;
            }

            this.color = {
                hex: "#" + value.id.split("#")[1]
            }

            this.tagText = value;
        },
        badgeClass(tag) {
            return `badge-${tag.style}`;
        },
        badgeStyle(tag) {
            return {
                background: tag.bg,
                color: tag.fg,
            };
        },
        onTagDeleteClick(tag, e) {
            this.hit._tags = this.hit._tags.filter(t => t !== tag);

            Sist2Api.deleteTag(tag.rawText, this.hit).then(() => {
                //toast
                this.$store.commit("busUpdateWallItems");
                this.$store.commit("busUpdateTags");
            });
        },
        tagAdd() {
            this.showModal = true;
        },
        saveTag() {
            if (this.tagText.id.includes("#")) {
                this.$bvToast.toast(
                    this.$t("toast.invalidTag"),
                    {
                        title: this.$t("toast.invalidTagTitle"),
                        noAutoHide: true,
                        toaster: "b-toaster-bottom-right",
                        headerClass: "toast-header-error",
                        bodyClass: "toast-body-error",
                    });
                return;
            }

            let tag = this.tagText.id + this.color.hex.replace("#", ".#");
            const userTags = this.hit._tags.filter(t => t.userTag);

            if (userTags.find(t => t.rawText === tag) != null) {
                this.$bvToast.toast(
                    this.$t("toast.dupeTag"),
                    {
                        title: this.$t("toast.dupeTagTitle"),
                        noAutoHide: true,
                        toaster: "b-toaster-bottom-right",
                        headerClass: "toast-header-error",
                        bodyClass: "toast-body-error",
                    });
                return;
            }

            this.hit._tags.push(Sist2Api.createUserTag(tag));

            Sist2Api.saveTag(tag, this.hit).then(() => {
                this.tagText = null;
                this.showModal = false;
                this.$store.commit("busUpdateWallItems");
                this.$store.commit("busUpdateTags");
                // TODO: toast
            });
        },
        async suggestTag(term) {
            term = term.toLowerCase();

            const choices = await this.getTagChoices(term);

            let matches = [];
            for (let i = 0; i < choices.length; i++) {
                if (~choices[i].toLowerCase().indexOf(term)) {
                    matches.push(choices[i]);
                }
            }

            return matches.sort().map(match => {
                return {
                    title: match.split(".").slice(0, -1).join("."),
                    id: match
                }
            });
        },
        getTagChoices(prefix) {
            return new Promise(getPaths => {
                Sist2Api.getTagSuggestions(prefix)
                    .then(paths => getPaths(paths))
            });
        }
    },
});
</script>

<style scoped>


.badge-video {
    color: #FFFFFF;
    background-color: #F27761;
}

.badge-image {
    color: #FFFFFF;
    background-color: #AA99C9;
}

.badge-audio {
    color: #FFFFFF;
    background-color: #00ADEF;
}

.badge-user {
    color: #212529;
    background-color: #e0e0e0;
}

.badge-user:hover, .add-tag-button:hover {
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.12), 0 1px 2px rgba(0, 0, 0, 0.24);
}

.badge-text {
    color: #FFFFFF;
    background-color: #FAAB3C;
}

.badge {
    margin-right: 3px;
    user-select: none;
}

.badge-delete {
    margin-right: -2px;
    margin-left: 2px;
    margin-top: -1px;
    font-family: monospace;
    font-size: 90%;
    background: rgba(0, 0, 0, 0.2);
    padding: 0.1em 0.4em;
    color: white;
    cursor: pointer;
}

.badge-size {
    width: 50px;
    display: inline-block;
}

.add-tag-button {
    cursor: pointer;
    color: #212529;
    background-color: #e0e0e0;
    width: 50px;
}

.badge-suggestion {
    font-size: 90%;
    font-weight: normal;
}
</style>

<style>
.vc-twitter-body {
    padding: 0 !important;
}

.vc-twitter {
    box-shadow: none !important;
    background: none !important;
}

.tooltip {
    user-select: none;
}

.toast {
    border: none;
}
</style>