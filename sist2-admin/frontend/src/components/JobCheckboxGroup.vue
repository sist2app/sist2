<template>
    <div>
        <p class="text-muted" v-if="jobs.length === 0">No jobs available.</p>
        <div class="form-check" v-for="job in jobs" :key="job.name">
            <input class="form-check-input" type="checkbox" :id="`job-${job.name}`" :value="job.name"
                   :checked="selected.includes(job.name)" :disabled="disabledReason(job) !== null"
                   @change="toggle(job.name)">
            <label class="form-check-label" :class="{'text-muted': disabledReason(job) !== null}"
                   :for="`job-${job.name}`">
                {{ job.name }}
                <span class="text-muted" v-if="disabledReason(job) !== null">({{ disabledReason(job) }})</span>
            </label>
        </div>
    </div>
</template>

<script setup>
import { onMounted, ref } from "vue";

import { api } from "../api.js";

const props = defineProps({
    selected: { type: Array, required: true },
    searchBackend: { type: String, default: null }
});

const jobs = ref([]);

function disabledReason(job) {
    if (job.status !== "indexed") {
        return "Has not been indexed yet";
    }
    if (job.index_options.search_backend !== props.searchBackend) {
        return `Uses a different search backend: ${job.index_options.search_backend}`;
    }
    return null;
}

function toggle(jobName) {
    const index = props.selected.indexOf(jobName);
    if (index === -1) {
        props.selected.push(jobName);
    } else {
        props.selected.splice(index, 1);
    }
}

onMounted(async () => {
    jobs.value = await api.get("/api/job");
});
</script>
