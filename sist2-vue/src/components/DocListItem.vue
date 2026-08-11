<template>
    <b-list-group-item class="flex-column align-items-start mb-2" :class="{'sub-document': doc._props.isSubDocument}"
                       @mouseenter="onTnEnter()" @mouseleave="onTnLeave()">

        <!-- Info modal-->
        <DocInfoModal :show="showInfo" :doc="doc" @close="showInfo = false"></DocInfoModal>

        <div class="media ml-2">

            <!-- Thumbnail-->
            <div v-if="doc._props.hasThumbnail" class="align-self-start mr-2 wrapper-sm">
                <div class="img-wrapper">
                    <div v-if="doc._props.isPlayableVideo" class="play">
                        <svg viewBox="0 0 494.942 494.942" xmlns="http://www.w3.org/2000/svg">
                            <path d="m35.353 0 424.236 247.471-424.236 247.471z"/>
                        </svg>
                    </div>

                    <img v-if="doc._props.isPlayableImage || doc._props.isPlayableVideo"
                         :src="(doc._props.isGif && hover) ? `f/${sid(doc)}` : `t/${sid(doc)}`"
                         alt=""
                         class="pointer fit-sm" @click="onThumbnailClick()">
                    <img v-else :src="`t/${sid(doc)}`" alt=""
                         class="fit-sm">
                </div>
            </div>
            <div v-else class="file-icon-wrapper" style="">
                <FileIcon></FileIcon>
            </div>

            <!-- Doc line-->
            <div class="doc-line ml-3">
                <div style="display: flex">
                    <span class="info-icon" @click="showInfo = true"></span>
                    <MLIcon v-if="doc._source.embedding" clickable @click="onEmbeddingClick()"></MLIcon>
                    <DocFileTitle :doc="doc"></DocFileTitle>
                </div>

                <!-- Content highlight -->
                <ContentDiv :doc="doc"></ContentDiv>

                <div class="path-row">
                    <div class="path-line" v-html="path()"></div>
                    <TagContainer :hit="doc"></TagContainer>
                </div>

                <div v-if="doc._source.pages || doc._source.author" class="path-row text-muted">
          <span v-if="doc._source.pages">{{ doc._source.pages }} {{
              doc._source.pages > 1 ? $t("pages") : $t("page")
              }}</span>
                    <span v-if="doc._source.author && doc._source.pages" class="mx-1">-</span>
                    <span v-if="doc._source.author">{{ doc._source.author }}</span>
                </div>

                <!-- Featured line -->
                <div style="display: flex">
                    <FeaturedFieldsLine :doc="doc"></FeaturedFieldsLine>
                </div>
            </div>
        </div>
    </b-list-group-item>
</template>

<script>
import TagContainer from "@/components/TagContainer.vue";
import DocFileTitle from "@/components/DocFileTitle.vue";
import DocInfoModal from "@/components/DocInfoModal.vue";
import ContentDiv from "@/components/ContentDiv.vue";
import FileIcon from "@/components/icons/FileIcon.vue";
import FeaturedFieldsLine from "@/components/FeaturedFieldsLine.vue";
import MLIcon from "@/components/icons/MlIcon.vue";
import Sist2Api from "@/Sist2Api";
import {sid} from "@/util";

export default {
    name: "DocListItem",
    components: {MLIcon, FileIcon, ContentDiv, DocInfoModal, DocFileTitle, TagContainer, FeaturedFieldsLine},
    props: ["doc"],
    data() {
        return {
            hover: false,
            showInfo: false
        }
    },
    methods: {
        sid: sid,
        async onThumbnailClick() {
            this.$store.commit("setUiLightboxSlide", this.doc._seq);
            await this.$store.dispatch("showLightbox");
        },
        onEmbeddingClick() {
            Sist2Api.getEmbeddings(sid(this.doc), this.$store.state.embeddingsModel).then(embeddings => {
                this.$store.commit("setEmbeddingText", "");
                this.$store.commit("setEmbedding", embeddings);
                this.$store.commit("setEmbeddingDoc", this.doc);
            })
        },
        path() {
            if (!this.doc.highlight) {
                return this.doc._source.path + "/"
            }
            if (this.doc.highlight["path.text"]) {
                return this.doc.highlight["path.text"] + "/"
            }

            if (this.doc.highlight["path.nGram"]) {
                return this.doc.highlight["path.nGram"] + "/"
            }
            return this.doc._source.path + "/"
        },
        onTnEnter() {
            this.hover = true;
        },
        onTnLeave() {
            this.hover = false;
        },
    }
}
</script>

<style scoped>
.sub-document {
    background: #AB47BC1F !important;
}

.theme-black .sub-document {
    background: #37474F !important;
}

.list-group {
    margin-top: 1em;
}

.list-group-item {
    padding: .25rem 0.5rem;

    box-shadow: 0 0.125rem 0.25rem rgb(0 0 0 / 8%) !important;
    border-radius: 0;
    border: none;
}

.path-row {
    display: -ms-flexbox;
    display: flex;
    -ms-flex-align: start;
    align-items: flex-start;
}

.path-line {
    color: #808080;
    text-overflow: ellipsis;
    overflow: hidden;
    white-space: nowrap;
    margin-right: 0.3em;
}

.theme-black .path-line {
    color: #bbb;
}

.play {
    position: absolute;
    width: 18px;
    height: 18px;
    left: 50%;
    top: 50%;
    transform: translate(-50%, -50%);
    pointer-events: none;
}

.play svg {
    fill: rgba(0, 0, 0, 0.7);
}

.list-group-item .img-wrapper {
    width: 88px;
    height: 88px;
    position: relative;
}

.fit-sm {
    max-height: 100%;
    max-width: 100%;
    width: auto;
    height: auto;
    position: absolute;
    top: 0;
    bottom: 0;
    left: 0;
    right: 0;
    margin: auto;

    /*box-shadow: 2px 2px 4px rgba(0, 0, 0, 0.12);*/
}

.doc-line {
    max-width: calc(100% - 88px - 1.5rem);
    flex: 1;
    vertical-align: middle;
    margin-top: auto;
    margin-bottom: auto;
}

.file-icon-wrapper {
    width: calc(88px + .5rem);
    height: 88px;
    position: relative;
}
</style>