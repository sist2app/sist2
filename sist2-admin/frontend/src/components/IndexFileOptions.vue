<template>
    <div class="card mb-3">
        <div class="card-header">Index file options</div>
        <div class="card-body">
            <div class="form-check form-switch">
                <input class="form-check-input" type="checkbox" id="lowMemoryMode"
                       v-model="options.low_memory_mode">
                <label class="form-check-label" for="lowMemoryMode">Enable low-memory mode</label>
            </div>
        </div>
    </div>
</template>

<script setup>
defineProps({
    options: { type: Object, required: true }
});
</script>
