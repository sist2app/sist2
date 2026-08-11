<template>
    <div v-if="doc._props.hasThumbnail" class="img-wrapper" @mouseenter="onTnEnter()" @mouseleave="onTnLeave()"
         @touchstart="onTouchStart()">
        <div v-if="doc._props.isAudio" class="card-img-overlay" :class="{'small-badge': smallBadge}">
            <span class="badge badge-resolution">{{ humanTime(doc._source.duration) }}</span>
        </div>

        <div
                v-if="doc._props.isImage && doc._props.imageAspectRatio < 5"
                class="card-img-overlay"
                :class="{'small-badge': smallBadge}">
            <span class="badge badge-resolution">{{ `${doc._source.width}x${doc._source.height}` }}</span>
        </div>

        <div v-if="(doc._props.isVideo || doc._props.isGif) && doc._source.duration > 0"
             class="card-img-overlay"
             :class="{'small-badge': smallBadge}">
            <span class="badge badge-resolution">{{ humanTime(doc._source.duration) }}</span>
        </div>

        <div v-if="doc._props.isPlayableVideo" class="play">
            <svg viewBox="0 0 494.942 494.942" xmlns="http://www.w3.org/2000/svg">
                <path d="m35.353 0 424.236 247.471-424.236 247.471z"/>
            </svg>
        </div>

        <img ref="tn"
             v-if="doc._props.isPlayableImage || doc._props.isPlayableVideo"
             :src="tnSrc"
             alt=""
             :style="{height: (doc._props.isGif && hover) ? `${tnHeight()}px` : undefined}"
             class="pointer fit card-img-top" @click="onThumbnailClick()">
        <img v-else :src="tnSrc" alt=""
             class="fit card-img-top">

        <ThumbnailProgressBar v-if="hover && doc._props.hasVidPreview"
                              :progress="(currentThumbnailNum + 1) / (doc._props.tnNum)"
        ></ThumbnailProgressBar>
    </div>
</template>

<script>
import {humanTime, sid} from "@/util";
import ThumbnailProgressBar from "@/components/ThumbnailProgressBar.vue";

export default {
    name: "FullThumbnail",
    props: ["doc", "smallBadge"],
    components: {ThumbnailProgressBar},
    data() {
        return {
            hover: false,
            currentThumbnailNum: 0,
            timeoutId: null
        }
    },
    created() {
        this.$store.subscribe((mutation) => {
            if (mutation.type === "busTnTouchStart" && mutation.payload !== this.doc._id) {
                this.onTnLeave();
            }
        });
    },
    computed: {
        tnSrc() {
            return this.getThumbnailSrc(this.currentThumbnailNum);
        },
    },
    methods: {
        sid: sid,
        getThumbnailSrc(thumbnailNum) {
            const doc = this.doc;
            const props = doc._props;
            if (props.isGif && this.hover) {
                return `f/${sid(doc)}`;
            }
            return (this.currentThumbnailNum === 0)
                ? `t/${sid(doc)}`
                : `t/${sid(doc)}/${String(thumbnailNum).padStart(4, "0")}`;
        },
        humanTime: humanTime,
        onThumbnailClick() {
            this.$emit("onThumbnailClick");
        },
        tnHeight() {
            return this.$refs.tn.height;
        },
        tnWidth() {
            return this.$refs.tn.width;
        },
        onTnEnter() {
            this.hover = true;
            const start = Date.now()
            if (this.doc._props.hasVidPreview) {
                let img = new Image();
                img.src = this.getThumbnailSrc(this.currentThumbnailNum + 1);
                img.onload = () => {
                    this.currentThumbnailNum += 1;
                    this.scheduleNextTnNum(Date.now() - start);
                }
            }
        },
        onTnLeave() {
            this.currentThumbnailNum = 0;
            this.hover = false;
            if (this.timeoutId !== null) {
                window.clearTimeout(this.timeoutId);
                this.timeoutId = null;
            }
        },
        scheduleNextTnNum(offset = 0) {
            const INTERVAL = (this.$store.state.optVidPreviewInterval ?? 700) - offset;
            this.timeoutId = window.setTimeout(() => {
                const start = Date.now();
                if (!this.hover) {
                    return;
                }
                if (this.currentThumbnailNum === this.doc._props.tnNum - 1) {
                    this.currentThumbnailNum = 0;
                    this.scheduleNextTnNum();
                } else {
                    let img = new Image();
                    img.src = this.getThumbnailSrc(this.currentThumbnailNum + 1);
                    img.onload = () => {
                        this.currentThumbnailNum += 1;
                        this.scheduleNextTnNum(Date.now() - start);
                    }
                }
            }, INTERVAL);
        },
        onTouchStart() {
            this.$store.commit("busTnTouchStart", this.doc._id);
            if (!this.hover) {
                this.onTnEnter()
            }
        },
    }
}
</script>

<style scoped>
.img-wrapper {
    position: relative;
}

.img-wrapper:hover svg {
    fill: rgba(0, 0, 0, 1);
}

.card-img-top {
    border-top-left-radius: 0;
    border-top-right-radius: 0;
}

.play {
    position: absolute;
    width: 25px;
    height: 25px;
    left: 50%;
    top: 50%;
    transform: translate(-50%, -50%);
    pointer-events: none;
}

.play svg {
    fill: rgba(0, 0, 0, 0.7);
}

.badge-resolution {
    color: #c6c6c6;
    background-color: #272727CC;
    padding: 2px 3px;
}

.card-img-overlay {
    pointer-events: none;
    padding: 2px 6px;
    bottom: 4px;
    top: unset;
    left: unset;
    right: 0;
}

.small-badge {
    padding: 1px 3px;
    font-size: 70%;
}

</style>