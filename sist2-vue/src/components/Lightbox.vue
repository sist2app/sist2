<template>
  <div :class="{'disable-animations': $store.state.optSimpleLightbox}">
    <FsLightbox
        ref="lightbox"
        :key="lightboxKey"
        :toggler="showLightbox"
        :sources="lightboxSources"
        :thumbs="lightboxThumbs"
        :captions="lightboxCaptions"
        :types="lightboxTypes"
        :source-index="lightboxSlide"
        :custom-toolbar-buttons="customButtons"
        :slideshow-time="$store.getters.optLightboxSlideDuration * 1000"
        :zoom-increment="0.25"
        :load-only-current-source="$store.getters.optLightboxLoadOnlyCurrent"
        :on-close="onClose"
        :on-open="onShow"
        :on-slide-change="onSlideChange"
    ></FsLightbox>

    <a id="lightbox-download" style="display: none"></a>
  </div>
</template>

<script>
import FsLightbox from "fslightbox-vue";

export default {
  name: "Lightbox",
  components: {FsLightbox},
  data() {
    return {
      disableAnimations: true,
      customButtons: [
        {
          viewBox: "0 0 384.928 384.928",
          d: "M321.339,245.334c-4.74-4.692-12.439-4.704-17.179,0l-99.551,98.564V12.03 c0-6.641-5.438-12.03-12.151-12.03s-12.151,5.39-12.151,12.03v331.868l-99.551-98.552c-4.74-4.704-12.439-4.704-17.179,0 s-4.74,12.319,0,17.011l120.291,119.088c4.692,4.644,12.499,4.644,17.191,0l120.291-119.088 C326.091,257.653,326.091,250.038,321.339,245.334C316.599,240.642,326.091,250.038,321.339,245.334z",
          width: "17px",
          height: "17px",
          title: "Download",
          onClick: this.onDownloadClick
        }
      ]
    }
  },
  computed: {
    showLightbox() {
      return this.$store.getters["uiShowLightbox"];
    },
    lightboxSources() {
      return this.$store.getters["uiLightboxSources"];
    },
    lightboxThumbs() {
      return this.$store.getters["uiLightboxThumbs"];
    },
    lightboxKey() {
      return this.$store.getters["uiLightboxKey"];
    },
    lightboxSlide() {
      return this.$store.getters["uiLightboxSlide"];
    },
    lightboxCaptions() {
      return this.$store.getters["uiLightboxCaptions"];
    },
    lightboxTypes() {
      return this.$store.getters["uiLightboxTypes"];
    }
  },
  mounted() {
    const listener = document.onkeydown;

    document.onkeydown = (e) => {

      const ret = this.keyDownListener(e)

      if (listener && ret) {
        return listener(e);
      }
    };

  },
  methods: {
    keyDownListener(e) {

      const isLightboxOpen = this.$refs.lightbox === undefined || this.$refs.lightbox.$el.tagName === undefined;

      if (isLightboxOpen) {
        return true;
      }

      const lightboxStore = this.$refs.lightbox.fsLightboxStore.slice(-1)[0];

      switch (e.key) {
        case " ": {
          e.preventDefault();
          e.stopPropagation();
          e.stopImmediatePropagation();

          // Find video at current slide, toggle play/pause
          [...document.getElementsByClassName("fslightbox-absoluted")].forEach(elem => {
            if (elem.style.transform === "translate(0px)" || elem.style.transform === "translate(0px, 0px)") {
              const vid = elem.getElementsByTagName("video")[0];

              if (vid) {
                if (vid.paused) {
                  vid.play();
                } else {
                  vid.pause()
                }
              }
            }

            return false;
          });

          return false;
        }
        case "ArrowUp":
        case "k": {
          if (!lightboxStore.data.isThumbing && lightboxStore.core.thumbsToggler) {
            lightboxStore.core.thumbsToggler.toggleThumbs();
          }
          return false;
        }
        case "ArrowDown":
        case "j": {
          if (lightboxStore.data.isThumbing && lightboxStore.core.thumbsToggler) {
            lightboxStore.core.thumbsToggler.toggleThumbs();
          }
          return false;
        }
        case "h": {
          if (lightboxStore.core.stageManager.getPreviousSlideIndex) {
            lightboxStore.core.slideIndexChanger.jumpTo(lightboxStore.core.stageManager.getPreviousSlideIndex());
          }
          return false;
        }
        case "l": {
          if (lightboxStore.core.stageManager.getNextSlideIndex) {
            lightboxStore.core.slideIndexChanger.jumpTo(lightboxStore.core.stageManager.getNextSlideIndex());
          }
          return false;
        }
      }

      return true;
    },
    onDownloadClick() {
      // The source of a transcoded picture is a re-encoded copy, so download the original instead
      const url = this.lightboxSources[this.lightboxSlide].replace(/^p\//, "f/");

      const a = document.getElementById("lightbox-download");
      a.setAttribute("href", url);
      a.setAttribute("download", "");
      a.click();
    },
    onShow() {
      this.$store.commit("setUiLightboxIsOpen", true);
    },
    onClose() {
      this.$store.commit("setUiLightboxIsOpen", false);
    },
    onSlideChange() {
      // Pause all videos when changing slide
      const videos = document.getElementsByTagName("video");
      if (videos.length === 0) {
        return
      }
      for (let el of videos) {
        el.pause();
      }
    },
  }

}
</script>

<style>
.fslightbox-toolbar-button:nth-child(2) {
  order: 1;
}

.fslightbox-toolbar-button:nth-child(1) {
  order: 2;
}

.fslightbox-toolbar-button:nth-child(3) {
  order: 3;
}

.fslightbox-toolbar-button:nth-child(4) {
  order: 4;
}

.fslightbox-toolbar-button:nth-child(5) {
  order: 5;
}

@media (max-width: 650px) {
  /* Disable fullscreen on mobile because it's buggy */
  .fslightbox-toolbar-button:nth-child(6) {
    display: none;
  }
}

.fslightbox-toolbar-button:nth-child(6) {
  order: 6;
}

.fslightbox-toolbar-button:nth-child(7) {
  order: 7;
}

.disable-animations .fslightbox-container {
  background: rgba(30,30,30,.9);
}

.disable-animations .fslightbox-transform-transition {
  transition: none;
}

.disable-animations .fslightbox-fade-in-strong {
  animation: none;
}

.fslightbox-container video, .fslightbox-container img {
  cursor: unset !important;
}
</style>