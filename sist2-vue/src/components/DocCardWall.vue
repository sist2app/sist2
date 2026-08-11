<template>
    <GridLayout
            ref="grid-layout"
            :options="gridOptions"
            @append="append"
            @layout-complete="$emit('layout-complete')"
    >
        <DocCard v-for="doc in docs" :key="sid(doc)" :doc="doc" :width="width"></DocCard>
    </GridLayout>
</template>

<script>
import {sid} from "@/util";
import Vue from "vue";
import DocCard from "@/components/DocCard.vue";

import VueInfiniteGrid from "@egjs/vue-infinitegrid";

Vue.use(VueInfiniteGrid);

export default Vue.extend({
    components: {
        DocCard,
    },
    props: ["docs", "append"],
    data() {
        return {
            width: 0,
            gridOptions: {
                align: "center",
                margin: 0,
                transitionDuration: 0,
                isOverflowScroll: false,
                isConstantSize: false,
                useFit: false,
                // Indicates whether keep the number of DOMs is maintained. If the useRecycle value is 'true', keep the number
                //  of DOMs is maintained. If the useRecycle value is 'false', the number of DOMs will increase as card elements
                //  are added.
                useRecycle: false
            }
        }
    },
    methods: {
        sid,
    },
    computed: {
        colCount() {
            const columns = this.$store.getters["optColumns"];

            if (columns === "auto") {
                return Math.round(this.$refs["grid-layout"].$el.scrollWidth / 300)
            }
            return columns;
        },
    },
    mounted() {
        this.width = this.$refs["grid-layout"].$el.scrollWidth / this.colCount;

        if (this.colCount === 1) {
            this.$refs["grid-layout"].$el.classList.add("grid-single-column");
        }

        this.$store.subscribe((mutation) => {
            if (mutation.type === "busUpdateWallItems" && this.$refs["grid-layout"]) {
                this.$refs["grid-layout"].updateItems();
            }
        });
    },
});
</script>

<style>
</style>
