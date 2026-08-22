<template>
    <div v-if="isMobile">
        <b-form-select
                :value="selectedIndicesIds"
                @change="onSelect($event)"
                :options="indices" multiple :select-size="6" text-field="name"
                value-field="id"></b-form-select>
    </div>
    <div v-else>

        <div class="d-flex justify-content-between align-content-center">
      <span>
        {{ selectedIndices.length }}
        {{ selectedIndices.length === 1 ? $t("indexPicker.selectedIndex") : $t("indexPicker.selectedIndices") }}
      </span>

            <div>
                <b-button variant="link" @click="selectAll()"> {{ $t("indexPicker.selectAll") }}</b-button>
                <b-button variant="link" @click="selectNone()"> {{ $t("indexPicker.selectNone") }}</b-button>
            </div>
        </div>

        <b-list-group id="index-picker-desktop" class="unselectable">
            <b-list-group-item
                    v-for="idx in indices"
                    @click="toggleIndex(idx, $event)"
                    @click.shift="shiftClick(idx, $event)"
                    class="d-flex justify-content-between align-items-center list-group-item-action pointer"
                    :class="{active: lastClickIndex === idx}"
                    :key="idx.id"
            >
                <div class="d-flex">
                    <b-checkbox style="pointer-events: none" :checked="isSelected(idx)"></b-checkbox>
                    {{ idx.name }}
                    <div v-if="hasEmbeddings(idx)" style="vertical-align: center; margin-left: 5px">
                        <MLIcon small style="top: -1px; position: relative"></MLIcon>
                    </div>
                    <span class="text-muted timestamp-text ml-2"
                          style="top: 1px; position: relative">{{ formatIdxDate(idx.timestamp) }}</span>
                </div>
                <b-badge class="version-badge">v{{ idx.version }}</b-badge>
            </b-list-group-item>
        </b-list-group>
    </div>
</template>

<script>
import SmallBadge from "./SmallBadge.vue"
import {mapActions, mapGetters} from "vuex";
import Vue from "vue";
import {format} from "date-fns";
import MLIcon from "@/components/icons/MlIcon.vue";

export default Vue.extend({
    components: {
        MLIcon,
        SmallBadge
    },
    data() {
        return {
            loading: true,
            lastClickIndex: null
        }
    },
    computed: {
        ...mapGetters([
            "indices", "selectedIndices"
        ]),
        selectedIndicesIds() {
            return this.selectedIndices.map(idx => idx.id)
        },
        isMobile() {
            return window.innerWidth <= 650;
        }
    },
    methods: {
        ...mapActions({
            setSelectedIndices: "setSelectedIndices"
        }),
        shiftClick(index, e) {
            if (this.lastClickIndex === null) {
                return;
            }

            const select = this.isSelected(this.lastClickIndex);

            let leftBoundary = this.indices.indexOf(this.lastClickIndex);
            let rightBoundary = this.indices.indexOf(index);

            if (rightBoundary < leftBoundary) {
                let tmp = leftBoundary;
                leftBoundary = rightBoundary;
                rightBoundary = tmp;
            }

            for (let i = leftBoundary; i <= rightBoundary; i++) {
                if (select) {
                    if (!this.isSelected(this.indices[i])) {
                        this.setSelectedIndices([this.indices[i], ...this.selectedIndices]);
                    }
                } else {
                    this.setSelectedIndices(this.selectedIndices.filter(idx => idx !== this.indices[i]));
                }
            }
        },
        selectAll() {
            this.setSelectedIndices(this.indices);
        },
        selectNone() {
            this.setSelectedIndices([]);
        },
        onSelect(value) {
            this.setSelectedIndices(this.indices.filter(idx => value.includes(idx.id)));
        },
        formatIdxDate(timestamp) {
            return format(new Date(timestamp * 1000), "yyyy-MM-dd");
        },
        toggleIndex(index, e) {
            if (e.shiftKey) {
                return;
            }

            this.lastClickIndex = index;
            if (this.isSelected(index)) {
                this.setSelectedIndices(this.selectedIndices.filter(idx => idx.id !== index.id));
            } else {
                this.setSelectedIndices([index, ...this.selectedIndices]);
            }
        },
        isSelected(index) {
            return this.selectedIndices.find(idx => idx.id === index.id) != null;
        },
        hasEmbeddings(index) {
            return index.models.length > 0;
        },
    },
})
</script>

<style scoped>
.timestamp-text {
    line-height: 24px;
    font-size: 80%;
}

.version-badge {
    color: #222 !important;
    background: none;
}

.theme-black .version-badge {
    color: #eee !important;
}

.list-group-item {
    padding: 0.2em 0.4em;
}

#index-picker-desktop {
    overflow-y: auto;
    max-height: 132px;
}

.btn-link:focus {
    box-shadow: none;
}

.unselectable {
    user-select: none;
    -ms-user-select: none;
    -moz-user-select: none;
    -webkit-user-select: none;
}

.list-group-item.active {
    z-index: 2;
    background-color: inherit;
    color: inherit;
}

/* Only the border color: the shorthand would put back the top border that
   .list-group-item + .list-group-item drops, drawing 2px seams between rows. */
.theme-black .list-group-item {
    background: #212121;
    color: #e0e0e0;
    border-color: rgba(255, 255, 255, 0.1);
}

.theme-black .list-group-item.active {
    z-index: 2;
    background-color: inherit;
    color: inherit;
    border-color: rgba(255, 255, 255, 0.3);
}

.theme-black .list-group {
    border-radius: 0;
}
</style>