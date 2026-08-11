<template>
    <div v-if="frontend !== null">
        <BackButton to="/2"/>
        <div class="d-flex justify-content-between align-items-center mb-3">
            <h4 class="mb-0">
                [{{ frontend.name }}] frontend
                <span class="badge" :class="frontend.running ? 'text-bg-success' : 'text-bg-secondary'">
                    {{ frontend.running ? "online" : "offline" }}
                </span>
            </h4>
            <div>
                <button class="btn btn-success" @click="start()"
                        :disabled="frontend.running || frontend.jobs.length === 0">Start</button>
                <button class="btn btn-warning ms-2" @click="stop()" :disabled="!frontend.running">Stop</button>
                <a class="btn btn-primary ms-2" :class="{disabled: !frontend.running}" :href="frontendUrl"
                   target="_blank">Go</a>
                <button class="btn btn-danger ms-2" @click="deleteFrontend()">Delete</button>
            </div>
        </div>

        <div class="alert alert-danger" v-if="error">{{ error }}</div>
        <div class="alert alert-warning" v-if="frontend.jobs.length === 0">
            You must select at least one job to start this frontend
        </div>

        <div class="card mb-3">
            <div class="card-header">Search backend options</div>
            <div class="card-body">
                <label class="form-label">Search backend</label>
                <SearchBackendSelect v-model="frontend.web_options.search_backend"
                                     @update:modelValue="frontend.jobs.length = 0"/>
                <label class="form-label mt-3">Available jobs</label>
                <JobCheckboxGroup :selected="frontend.jobs"
                                  :search-backend="frontend.web_options.search_backend"/>
            </div>
        </div>

        <WebOptions :options="frontend.web_options"/>

        <div class="card mb-3">
            <div class="card-header">Advanced options</div>
            <div class="card-body">
                <div class="form-check form-switch mb-2">
                    <input class="form-check-input" type="checkbox" id="autoStart" v-model="frontend.auto_start">
                    <label class="form-check-label" for="autoStart">Start automatically</label>
                </div>
                <div class="mb-2">
                    <label class="form-label">Extra query arguments when launching from sist2-admin</label>
                    <input class="form-control" v-model="frontend.extra_query_args">
                </div>
                <div class="mb-2">
                    <label class="form-label">Custom URL when launching from sist2-admin</label>
                    <input class="form-control" v-model="frontend.custom_url">
                </div>
            </div>
        </div>
    </div>
</template>

<script setup>
import { computed, onMounted, ref, watch } from "vue";

import BackButton from "../components/BackButton.vue";
import JobCheckboxGroup from "../components/JobCheckboxGroup.vue";
import SearchBackendSelect from "../components/SearchBackendSelect.vue";
import WebOptions from "../components/WebOptions.vue";
import { api } from "../api.js";
import { navigate, route } from "../router.js";

const SAVE_DEBOUNCE = 500;

const frontend = ref(null);
const error = ref(null);

let saveTimeout = null;

const frontendUrl = computed(() => {
    if (frontend.value.custom_url) {
        return withQueryArgs(frontend.value.custom_url);
    }

    const bind = frontend.value.web_options.bind;
    const port = bind.split(":").pop();
    let host = bind.split(":")[0];
    if (host === "0.0.0.0") {
        host = window.location.hostname;
    }
    return withQueryArgs(`http://${host}:${port}/`);
});

function withQueryArgs(url) {
    if (frontend.value.extra_query_args) {
        return `${url}#?${frontend.value.extra_query_args}`;
    }
    return url;
}

async function save() {
    try {
        await api.put(`/api/frontend/${encodeURIComponent(frontend.value.name)}`, frontend.value);
        error.value = null;
    } catch (e) {
        error.value = e.message;
    }
}

async function start() {
    try {
        await api.post(`/api/frontend/${encodeURIComponent(frontend.value.name)}/start`);
        frontend.value.running = true;
        error.value = null;
    } catch (e) {
        error.value = `Could not start frontend: ${e.message}`;
    }
}

async function stop() {
    await api.post(`/api/frontend/${encodeURIComponent(frontend.value.name)}/stop`);
    frontend.value.running = false;
}

async function deleteFrontend() {
    if (!window.confirm(`Delete frontend ${frontend.value.name}?`)) {
        return;
    }
    await api.delete(`/api/frontend/${encodeURIComponent(frontend.value.name)}`);
    navigate("/2");
}

onMounted(async () => {
    frontend.value = await api.get(`/api/frontend/${encodeURIComponent(route.params.name)}`);

    watch(
        frontend,
        () => {
            window.clearTimeout(saveTimeout);
            saveTimeout = window.setTimeout(save, SAVE_DEBOUNCE);
        },
        { deep: true }
    );
});
</script>
