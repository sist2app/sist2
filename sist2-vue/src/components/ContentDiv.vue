<template>
  <div class="content-div" v-html="content()" v-if="content()"></div>
</template>

<script>
import {highlightHtml} from "@/util";

const FRAGMENT_SEPARATOR = "<br /><i style='line-height: 2.4'>[…]</i><br/>";

export default {
  name: "ContentDiv",
  props: ["doc"],
  methods: {
    content() {
      if (!this.doc.highlight) {
        return null;
      }

      if (this.doc.highlight["content.nGram"]) {
        return this.fragmentsHtml(this.doc.highlight["content.nGram"]);
      }
      if (this.doc.highlight.content) {
        return this.fragmentsHtml(this.doc.highlight.content);
      }
    },
    fragmentsHtml(fragments) {
      return fragments.map(highlightHtml).join(FRAGMENT_SEPARATOR);
    }
  }
}
</script>

<style scoped>

</style>