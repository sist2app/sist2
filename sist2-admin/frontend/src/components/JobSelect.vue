<template>
    <select class="form-select" :value="modelValue" @change="$emit('update:modelValue', $event.target.value)">
        <option :value="null" disabled>Select a job</option>
        <option v-for="job in jobs" :key="job.name" :value="job.name">{{ job.name }}</option>
    </select>
</template>

<script setup>
import { onMounted, ref } from "vue";

import { api } from "../api.js";

defineProps({
    modelValue: { type: String, default: null }
});
defineEmits(["update:modelValue"]);

const jobs = ref([]);

onMounted(async () => {
    const allJobs = await api.get("/api/job");
    jobs.value = allJobs.filter((job) => job.index_path !== null);
});
</script>
