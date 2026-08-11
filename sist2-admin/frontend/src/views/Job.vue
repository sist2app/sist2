<template>
    <div v-if="job !== null">
        <BackButton to="/0"/>
        <div class="d-flex justify-content-between align-items-center mb-3">
            <h4 class="mb-0">
                [{{ job.name }}] job configuration
                <span class="badge text-bg-secondary">{{ job.status }}</span>
            </h4>
            <div>
                <div class="btn-group">
                    <button class="btn btn-success" @click="run(false)">Index now</button>
                    <button class="btn btn-success dropdown-toggle dropdown-toggle-split"
                            @click.stop="runMenuOpen = !runMenuOpen"></button>
                    <ul class="dropdown-menu" :class="{show: runMenuOpen}"
                        style="top: 100%; right: 0; left: auto">
                        <li>
                            <a class="dropdown-item" href="#" @click.prevent="run(true)">Full re-index</a>
                        </li>
                    </ul>
                </div>
                <button class="btn btn-danger ms-2" @click="deleteJob()">Delete</button>
            </div>
        </div>

        <div class="alert alert-danger" v-if="error">{{ error }}</div>
        <div class="alert alert-success" v-if="taskQueued">Task queued. Check the Tasks page to monitor the
            status.</div>
        <div class="alert alert-warning" v-if="!job.index_options.search_backend">
            You must select a search backend to run this job
        </div>

        <JobOptions :job="job"/>

        <div class="card mb-3">
            <div class="card-header">Search backend options</div>
            <div class="card-body">
                <label class="form-label">Search backend</label>
                <SearchBackendSelect v-model="job.index_options.search_backend"/>
            </div>
        </div>

        <UserScriptPicker :selected="job.user_scripts"/>

        <ScanOptions :options="job.scan_options"/>
    </div>
</template>

<script setup>
import { onMounted, onUnmounted, ref, watch } from "vue";

import BackButton from "../components/BackButton.vue";
import JobOptions from "../components/JobOptions.vue";
import ScanOptions from "../components/ScanOptions.vue";
import SearchBackendSelect from "../components/SearchBackendSelect.vue";
import UserScriptPicker from "../components/UserScriptPicker.vue";
import { api } from "../api.js";
import { navigate, route } from "../router.js";

const SAVE_DEBOUNCE = 500;

const job = ref(null);
const error = ref(null);
const taskQueued = ref(false);
const runMenuOpen = ref(false);

let saveTimeout = null;

async function save() {
    try {
        await api.put(`/api/job/${encodeURIComponent(job.value.name)}`, job.value);
        error.value = null;
    } catch (e) {
        error.value = e.message;
    }
}

async function run(full) {
    runMenuOpen.value = false;
    try {
        await api.post(`/api/job/${encodeURIComponent(job.value.name)}/run?full=${full}`);
        taskQueued.value = true;
    } catch (e) {
        error.value = e.message;
    }
}

async function deleteJob() {
    if (!window.confirm(`Delete job ${job.value.name}?`)) {
        return;
    }
    try {
        await api.delete(`/api/job/${encodeURIComponent(job.value.name)}`);
        navigate("/");
    } catch (e) {
        error.value = e.message;
    }
}

function closeRunMenu() {
    runMenuOpen.value = false;
}

onUnmounted(() => {
    document.removeEventListener("click", closeRunMenu);
});

onMounted(async () => {
    document.addEventListener("click", closeRunMenu);

    job.value = await api.get(`/api/job/${encodeURIComponent(route.params.name)}`);

    watch(
        job,
        () => {
            window.clearTimeout(saveTimeout);
            saveTimeout = window.setTimeout(save, SAVE_DEBOUNCE);
        },
        { deep: true }
    );
});
</script>
