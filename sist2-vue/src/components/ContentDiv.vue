<template>
  <div class="content-div" v-html="content()" v-if="content()"></div>
</template>

<script>
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
        return this.doc.highlight["content.nGram"].join(FRAGMENT_SEPARATOR);
      }
      if (this.doc.highlight.content) {
        return this.doc.highlight.content.join(FRAGMENT_SEPARATOR);
      }
    }
  }
}
</script>

<style scoped>

</style>