<template>
    <div class="mb-2">
        <a class="btn btn-link" :href="`#${to}`">Back</a>
    </div>
</template>

<script setup>
defineProps({
    to: { type: String, required: true }
});
</script>
