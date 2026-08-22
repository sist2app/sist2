import store from "@/store";

const SORT_MODES = {
    score: {
        "sort": "score",
    },
    random: {
        "sort": "random"
    },
    dateAsc: {
        "sort": "mtime"
    },
    dateDesc: {
        "sort": "mtime",
        "sortAsc": false
    },
    sizeAsc: {
        "sort": "size",
    },
    sizeDesc: {
        "sort": "size",
        "sortAsc": false
    },
    nameAsc: {
        "sort": "name",
    },
    nameDesc: {
        "sort": "name",
        "sortAsc": false
    }
};


class Sist2ElasticsearchQuery {

    searchQuery() {

        const getters = store.getters;

        const searchText = getters.searchText;
        const selectedPaths = getters.selectedPaths;
        const sizeMin = getters.sizeMin;
        const sizeMax = getters.sizeMax;
        const dateMin = getters.dateMin;
        const dateMax = getters.dateMax;
        const size = getters.size;
        const after = getters.lastDoc;
        const selectedIndexIds = getters.selectedIndices.map((idx) => idx.id)
        const selectedMimeTypes = getters.selectedMimeTypes;
        const selectedTags = getters.selectedTags;

        const q = {
            "pageSize": size
        }

        Object.assign(q, SORT_MODES[getters.sortMode]);

        if (!after) {
            q["fetchAggregations"] = true;
        }
        if (searchText) {
            q["query"] = searchText;
        }
        if (selectedPaths.length > 0) {
            q["paths"] = selectedPaths.map(path => path.endsWith("/") ? path.slice(0, -1) : path);
        }
        if (sizeMin) {
            q["sizeMin"] = sizeMin;
        }
        if (sizeMax) {
            q["sizeMax"] = sizeMax;
        }
        if (dateMin) {
            q["dateMin"] = dateMin;
        }
        if (dateMax) {
            q["dateMax"] = dateMax;
        }
        if (after) {
            q["after"] = after.sort;
        }
        if (selectedIndexIds.length > 0) {
            q["indexIds"] = selectedIndexIds;
        }
        if (selectedMimeTypes.length > 0) {
            q["mimeTypes"] = selectedMimeTypes;
        }
        if (selectedTags.length > 0) {
            q["tags"] = selectedTags
        }
        if (getters.sortMode === "random") {
            q["seed"] = getters.seed;
        }
        if (getters.optHighlight) {
            q["highlight"] = true;
            q["highlightContextSize"] = Number(getters.optFragmentSize);
        }

        if (getters.embedding) {
            q["model"] = getters.embeddingsModel;
            q["embedding"] = getters.embedding;
            q["sort"] = "embedding";
            q["sortAsc"] = false;
        } else if (getters.sortMode === "embedding") {
            q["sort"] = "sort"
            q["sortAsc"] = true;
        }

        q["searchInPath"] = getters.optSearchInPath;

        return q;
    }
}


export default new Sist2ElasticsearchQuery();