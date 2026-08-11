<template>
    <select class="form-select" :value="modelValue" @change="$emit('update:modelValue', $event.target.value)">
        <option :value="null" disabled>Select a search backend</option>
        <option v-for="backend in backends" :key="backend.name" :value="backend.name">
            {{ backend.name }} ({{ backend.backend_type }})
        </option>
    </select>
</template>

<script setup>
import { onMounted, ref } from "vue";

import { api } from "../api.js";

defineProps({
    modelValue: { type: String, default: null }
});
defineEmits(["update:modelValue"]);

const backends = ref([]);

onMounted(async () => {
    backends.value = await api.get("/api/search_backend");
});
</script>
