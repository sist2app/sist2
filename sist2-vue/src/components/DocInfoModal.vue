<template>
    <b-modal :visible="show" size="lg" :hide-footer="true" static lazy @close="$emit('close')" @hide="$emit('close')"
    >
        <template #modal-title>
            <h5 class="modal-title" :title="doc._source.name + ext(doc)">
                {{ doc._source.name + ext(doc) }}
                <router-link :to="`/file?byId=${doc._id}`">#</router-link>
            </h5>
        </template>

        <img v-if="doc._props.hasThumbnail" :src="`t/${sid(doc)}`" alt="" class="fit card-img-top">

        <InfoTable :doc="doc"></InfoTable>

        <LazyContentDiv :doc="doc"></LazyContentDiv>
    </b-modal>
</template>

<script>
import {ext, sid} from "@/util";
import InfoTable from "@/components/InfoTable.vue";
import LazyContentDiv from "@/components/LazyContentDiv.vue";

export default {
    name: "DocInfoModal",
    components: {LazyContentDiv, InfoTable},
    props: ["doc", "show"],
    methods: {
        ext: ext,
        sid: sid
    }
}
</script>

<style scoped>

</style>