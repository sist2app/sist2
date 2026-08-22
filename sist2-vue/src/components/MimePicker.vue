<template>
  <div>
    <div id="mimeTree"></div>
    <div class="d-flex justify-content-end">
      <b-button variant="link" @click="selectAll()">{{ $t("mimePicker.selectAll") }}</b-button>
      <b-button variant="link" @click="selectNone()">{{ $t("mimePicker.selectNone") }}</b-button>
      <b-button variant="link" @click="invertSelection()">{{ $t("mimePicker.invert") }}</b-button>
    </div>
  </div>
</template>

<script>
import InspireTree from "inspire-tree";
import InspireTreeDOM from "inspire-tree-dom";

import "inspire-tree-dom/dist/inspire-tree-light.min.css";
import {getSelectedTreeNodes, getTreeNodeAttributes} from "@/util";
import Sist2Api from "@/Sist2Api";
import Sist2Query from "@/Sist2ElasticsearchQuery";

export default {
  name: "MimePicker",
  data() {
    return {
      mimeTree: null,
      stashedMimeTreeAttributes: null,
      updateBusy: false
    }
  },
  mounted() {
    this.$store.subscribe((mutation) => {
      if (mutation.type === "setUiMimeMap" && this.mimeTree === null) {
        this.initializeTree();
      } else if (mutation.type === "busSearch") {
        this.updateTree();
      }
    });
  },
  methods: {
    handleTreeClick(node, e) {
      if (e === "indeterminate" || e === "collapsed" || e === 'rendered' || e === "focused") {
        return;
      }

      if (this.updateBusy) {
        return;
      }

      this.$store.commit("setSelectedMimeTypes", getSelectedTreeNodes(this.mimeTree));
    },
    selectAll() {
      this.bulkSelect((node) => node.select());
    },
    selectNone() {
      this.bulkSelect((node) => node.deselect());
    },
    invertSelection() {
      this.bulkSelect((node) => node.selected() ? node.deselect() : node.select());
    },
    /** Applies fn to every media type, then searches once rather than once per checkbox */
    bulkSelect(fn) {
      if (this.mimeTree === null || this.updateBusy) {
        return;
      }
      this.updateBusy = true;

      this.mimeTree.recurseDown((node) => {
        if (!node.hasChildren()) {
          fn(node);
        }
      });

      this.updateBusy = false;
      this.$store.commit("setSelectedMimeTypes", getSelectedTreeNodes(this.mimeTree));
    },
    updateTree() {

      if (this.$store.getters.optUpdateMimeMap === false) {
        return;
      }

      if (this.updateBusy) {
        return
      }
      this.updateBusy = true;

      if (this.stashedMimeTreeAttributes === null) {
        this.stashedMimeTreeAttributes = getTreeNodeAttributes(this.mimeTree);
      }

      const query = Sist2Query.searchQuery();

      Sist2Api.getMimeTypes(query).then(({buckets, mimeMap}) => {
        this.$store.commit("setUiMimeMap", mimeMap);
        this.$store.commit("setUiDetailsMimeAgg", buckets);

        this.mimeTree.removeAll();
        this.mimeTree.addNodes(mimeMap);

        // Restore selected mimes
        if (this.stashedMimeTreeAttributes === null) {
          // NOTE: This happens when successive fast searches are triggered
          this.stashedMimeTreeAttributes = {};
          // Always add the selected mime types
          this.$store.state.selectedMimeTypes.forEach(mime => {
            this.stashedMimeTreeAttributes[mime] = {
              checked: true
            }
          });
        }

        Object.entries(this.stashedMimeTreeAttributes).forEach(([mime, attributes]) => {
          if (this.mimeTree.node(mime)) {
            if (attributes.checked) {
              this.mimeTree.node(mime).select();
            }
            if (attributes.collapsed === false) {
              this.mimeTree.node(mime).expand();
            }
          }
        });
        this.stashedMimeTreeAttributes = null;
        this.updateBusy = false;
      });
    },

    initializeTree() {
      const mimeMap = this.$store.state.uiMimeMap;

      this.mimeTree = new InspireTree({
        selection: {
          mode: "checkbox"
        },
        data: mimeMap
      });

      new InspireTreeDOM(this.mimeTree, {
        target: "#mimeTree"
      });
      this.mimeTree.on("node.state.changed", this.handleTreeClick);
      this.mimeTree.deselect();

      if (this.$store.state._onLoadSelectedMimeTypes.length > 0) {
        this.$store.state._onLoadSelectedMimeTypes.forEach(mime => {
          this.mimeTree.node(mime).select();
        });
      }
    }
  }
}
</script>

<style scoped>
#mimeTree {
  max-height: 350px;
  overflow: auto;
}
</style>