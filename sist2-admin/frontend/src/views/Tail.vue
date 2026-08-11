<template>
    <div>
        <BackButton to="/tasks"/>
        <div class="d-flex align-items-center mb-2 flex-wrap">
            <h5 class="mb-0 me-3">Reading log file {{ route.params.taskId }}</h5>
            <div class="form-check form-switch me-3">
                <input class="form-check-input" type="checkbox" id="followMode" v-model="follow">
                <label class="form-check-label" for="followMode">Follow</label>
            </div>
            <span class="me-2">Log level:</span>
            <div class="form-check form-check-inline" v-for="level in LEVELS" :key="level">
                <input class="form-check-input" type="checkbox" :id="`level-${level}`" :value="level"
                       v-model="enabledLevels">
                <label class="form-check-label" :for="`level-${level}`">{{ level }}</label>
            </div>
        </div>

        <div ref="container" class="log-container font-monospace p-2"></div>
    </div>
</template>

<script setup>
import { onMounted, onUnmounted, ref, watch } from "vue";

import BackButton from "../components/BackButton.vue";
import { api } from "../api.js";
import { route } from "../router.js";

const LEVELS = ["DEBUG", "INFO", "WARNING", "ERROR", "FATAL", "ADMIN"];
const LEVEL_COLORS = {
    DEBUG: "#909090",
    INFO: "#ced4da",
    WARNING: "#ffc107",
    ERROR: "#ff5722",
    FATAL: "#ff5722",
    ADMIN: "#00bcd4"
};
const FOLLOW_LINES = 32;
const MAX_RENDERED_LINES = 2000;

const follow = ref(true);
const enabledLevels = ref(["INFO", "WARNING", "ERROR", "FATAL", "ADMIN"]);
const container = ref(null);

let events = null;

function classify(payload) {
    if (payload.stderr !== undefined) {
        return { level: "ERROR", text: payload.stderr };
    }
    if (payload.stdout !== undefined) {
        return { level: "INFO", text: payload.stdout };
    }
    if (payload["sist2-admin"] !== undefined) {
        return { level: "ADMIN", text: payload["sist2-admin"] };
    }
    if (payload.level !== undefined) {
        let text = payload.message;
        if (payload.filepath !== undefined) {
            text = `[${payload.datetime} ${payload.filepath}] ${payload.message}`;
        }
        return { level: payload.level, text: text };
    }
    return { level: "INFO", text: JSON.stringify(payload) };
}

function appendLine(rawLine) {
    let entry;
    try {
        entry = classify(JSON.parse(rawLine));
    } catch (e) {
        entry = { level: "INFO", text: rawLine };
    }

    if (!enabledLevels.value.includes(entry.level)) {
        return;
    }

    const element = document.createElement("div");
    element.textContent = entry.text;
    element.dataset.level = entry.level;
    const color = LEVEL_COLORS[entry.level];
    if (color !== undefined) {
        element.style.color = color;
    }
    container.value.appendChild(element);

    while (container.value.childElementCount > MAX_RENDERED_LINES) {
        container.value.removeChild(container.value.firstChild);
    }

    if (follow.value) {
        container.value.scrollTop = container.value.scrollHeight;
    }
}

function connect() {
    if (events !== null) {
        events.close();
    }
    container.value.replaceChildren();

    const n = follow.value ? FOLLOW_LINES : 0;
    events = api.events(`/api/task/${route.params.taskId}/log?n=${n}`);
    events.onmessage = (event) => {
        appendLine(JSON.parse(event.data).line);
    };
}

watch(follow, connect);
watch(enabledLevels, connect, { deep: true });

onMounted(connect);

onUnmounted(() => {
    if (events !== null) {
        events.close();
    }
});
</script>

<style scoped>
.log-container {
    background-color: #212529;
    height: 75vh;
    overflow-y: scroll;
    font-size: 12px;
    white-space: pre-wrap;
}
</style>
