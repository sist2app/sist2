<template>
    <div>
        <div class="input-group" style="margin-bottom: 0.5em; margin-top: 1em">
            <div class="input-group-prepend">

                <b-button variant="outline-secondary" @click="$refs['path-modal'].show()">
                    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 576 512" width="20px">
                        <path
                                fill="currentColor"
                                d="M288 224h224a32 32 0 0 0 32-32V64a32 32 0 0 0-32-32H400L368 0h-80a32 32 0 0 0-32 32v64H64V8a8 8 0 0 0-8-8H40a8 8 0 0 0-8 8v392a16 16 0 0 0 16 16h208v64a32 32 0 0 0 32 32h224a32 32 0 0 0 32-32V352a32 32 0 0 0-32-32H400l-32-32h-80a32 32 0 0 0-32 32v64H64V128h192v64a32 32 0 0 0 32 32zm0 96h66.74l32 32H512v128H288zm0-288h66.74l32 32H512v128H288z"
                        />
                    </svg>
                </b-button>
            </div>

            <VueSimpleSuggest
                    class="form-control-fix-flex"
                    @input="setPathText"
                    @select="addPath($event)"
                    @keydown.enter.native="addPath(getPathText)"
                    :value="getPathText"
                    :list="suggestPath"
                    :max-suggestions="0"
                    :placeholder="$t('pathBar.placeholder')"
                    :debounce="200"
            >
                <!-- Suggestion item template-->
                <div slot="suggestion-item" slot-scope="{ suggestion, query }">
                    <div class="suggestion-line" :title="suggestion">
                        <strong>{{ query }}</strong>{{ getSuggestionWithoutQueryPrefix(suggestion, query) }}
                    </div>
                </div>
            </VueSimpleSuggest>

        </div>

        <div v-if="selectedPaths.length > 0" class="path-chips">
            <b-badge v-for="path in selectedPaths" :key="path" class="path-chip" variant="secondary"
                     @click="removePath(path)" :title="$t('pathBar.remove')">
                {{ path }}&nbsp;&times;
            </b-badge>
        </div>

        <b-modal ref="path-modal" :title="$t('pathBar.modalTitle')" size="lg" :hide-footer="true" static>
            <div id="pathTree"></div>
        </b-modal>
    </div>
</template>

<script>
import InspireTree from "inspire-tree";
import InspireTreeDOM from "inspire-tree-dom";

import "inspire-tree-dom/dist/inspire-tree-light.min.css";
import Sist2Api from "@/Sist2Api";
import {mapGetters, mapMutations} from "vuex";
import VueSimpleSuggest from 'vue-simple-suggest'
import 'vue-simple-suggest/dist/styles.css' // Optional CSS

export default {
    name: "PathTree",
    components: {
        VueSimpleSuggest
    },
    data() {
        return {
            mimeTree: null,
            pathItems: [],
            tmpPath: ""

        }
    },
    computed: {
        ...mapGetters(["getPathText", "selectedPaths"])
    },
    mounted() {
        this.$store.subscribe((mutation) => {
            // Wait until indices are loaded to get the root paths
            if (mutation.type === "setIndices") {
                let pathTree = new InspireTree({
                    data: (node, resolve, reject) => {
                        return this.getNextDepth(node);
                    },
                    sort: "text"
                });

                this.$store.state.indices.forEach(idx => {
                    pathTree.addNode({
                        id: "/" + idx.id,
                        values: ["/" + idx.id],
                        text: `/[${idx.name}]`,
                        index: idx.id,
                        depth: 0,
                        children: true
                    })
                });

                new InspireTreeDOM(pathTree, {
                    target: "#pathTree"
                });

                pathTree.on("node.click", this.handleTreeClick);
                pathTree.expand();
            }
        });
    },
    methods: {
        ...mapMutations(["setPathText", "setSelectedPaths"]),
        addPath(path) {
            if (typeof path !== "string") {
                return;
            }

            const normalized = path.trim().replace(/\/$/, "");

            if (normalized === "" || this.selectedPaths.includes(normalized)) {
                return;
            }

            this.setSelectedPaths([...this.selectedPaths, normalized]);
            this.setPathText("");
        },
        removePath(path) {
            this.setSelectedPaths(this.selectedPaths.filter(p => p !== path));
        },
        getSuggestionWithoutQueryPrefix(suggestion, query) {
            return suggestion.slice(query.length)
        },
        async getPathChoices() {
            return new Promise(getPaths => {
                Sist2Api.getPathSuggestions(this.getPathText).then(getPaths);
            });
        },
        async suggestPath(term) {
            if (!this.$store.state.optSuggestPath) {
                return []
            }

            term = term.toLowerCase();

            const choices = await this.getPathChoices();

            let matches = [];
            for (let i = 0; i < choices.length; i++) {
                if (~choices[i].toLowerCase().indexOf(term)) {
                    matches.push(choices[i]);
                }
            }
            return matches.sort((a, b) => a.length - b.length);
        },
        getNextDepth(node) {
            return Sist2Api
                .searchPaths(node.index, node.depth + 1, node.depth + 3, node.depth > 0 ? node.id : null)
                .then(buckets => {
                    const paths = [];

                    return buckets
                        .filter(bucket => bucket.path.length > node.id.length || node.id.startsWith("/"))
                        .sort((a, b) => a.path > b.path ? 1 : -1)
                        .map(bucket => {
                            if (paths.some(n => bucket.path.startsWith(n))) {
                                return null;
                            }

                            const name = node.id.startsWith("/") ? bucket.path : bucket.path.slice(node.id.length + 1);

                            paths.push(bucket.path);

                            return {
                                id: bucket.path,
                                text: `${name}/ (${bucket.count})`,
                                depth: node.depth + 1,
                                index: node.index,
                                values: [bucket.path],
                                children: true,
                            }
                        })
                        .filter(bucket => bucket !== null);
                });
        },
        handleTreeClick(e, node, handler) {
            if (node.depth !== 0) {
                // The modal stays open: picking several folders in a row is the point
                if (this.selectedPaths.includes(node.id)) {
                    this.removePath(node.id);
                } else {
                    this.addPath(node.id);
                }

                this.$emit("search");
            }

            handler();
        },
    },
}
</script>

<style scoped>
.path-chips {
    margin-bottom: 0.5em;
}

.path-chip {
    cursor: pointer;
    margin-right: 0.4em;
    font-size: 90%;
}

#mimeTree {
    max-height: 350px;
    overflow: auto;
}

.form-control-fix-flex {
    flex: 1 1 auto;
    width: 1%;
    min-width: 0;
    margin-bottom: 0;
}

.suggestion-line {
    max-width: 100%;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    line-height: 1.1;
}

</style>

<style>
.suggestions {
    max-height: 250px;
    overflow-y: auto;
}

.theme-black .suggestions {
    color: black
}
</style>