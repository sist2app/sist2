<template>
    <b-container>

        <template>
            <b-card>
                <b-card-body>
                    <b-select v-model="selectedIndex" :options="indexOptions">
                        <template #first>
                            <b-form-select-option :value="null" disabled>{{
                                $t("indexPickerPlaceholder")
                                }}
                            </b-form-select-option>
                        </template>
                    </b-select>
                </b-card-body>
            </b-card>

            <b-card v-if="selectedIndex !== null" class="mt-3">
                <b-card-body>
                    <D3Treemap :index-id="selectedIndex"></D3Treemap>


                </b-card-body>
            </b-card>

            <b-card v-if="selectedIndex !== null" class="stats-card mt-3">
                <D3MimeBarCount :index-id="selectedIndex"></D3MimeBarCount>
                <D3MimeBarSize :index-id="selectedIndex"></D3MimeBarSize>
                <D3DateHistogram :index-id="selectedIndex"></D3DateHistogram>
                <D3SizeHistogram :index-id="selectedIndex"></D3SizeHistogram>
            </b-card>
        </template>
    </b-container>
</template>
<script>
import D3Treemap from "@/components/D3Treemap.vue";
import Sist2Api from "@/Sist2Api";
import Preloader from "@/components/Preloader.vue";
import D3MimeBarCount from "@/components/D3MimeBarCount.vue";
import D3MimeBarSize from "@/components/D3MimeBarSize.vue";
import D3DateHistogram from "@/components/D3DateHistogram.vue";
import D3SizeHistogram from "@/components/D3SizeHistogram.vue";

export default {
    components: {D3SizeHistogram, D3DateHistogram, D3MimeBarSize, D3MimeBarCount, D3Treemap, Preloader},
    data() {
        return {
            selectedIndex: null,
        }
    },
    computed: {
        indexOptions() {
            return this.indices.map(idx => {
                return {
                    text: idx.name,
                    value: idx.id
                };
            })
        },
        indices() {
            return this.$store.state.indices;
        }
    }
}
</script>

<style>

.stats-card {
    text-align: center;
    padding: 1em;
}
</style>