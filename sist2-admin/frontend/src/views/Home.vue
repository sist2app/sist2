<template>
    <ul class="nav nav-tabs mb-3">
        <li class="nav-item" v-for="(title, index) in TABS" :key="index">
            <a class="nav-link" :class="{active: tab === index}" href="#" @click.prevent="setTab(index)">
                {{ title }}
            </a>
        </li>
    </ul>

    <div class="alert alert-danger" v-if="error">{{ error }}</div>

    <div class="alert alert-info" v-if="loaded && jobs.length === 0">
        <h5>Welcome to sist2-admin!</h5>
        <p class="mb-1">To index and search your files:</p>
        <ol class="mb-0">
            <li>
                Pick a search backend: <strong>default-sqlite</strong> works out of the box,
                <strong>default-elasticsearch</strong> requires a running Elasticsearch instance.
            </li>
            <li>Create a job below, then set its scan path and search backend.</li>
            <li>Click <strong>Index now</strong> on the job page and monitor progress in Tasks.</li>
            <li>
                In the <strong>Frontends</strong> tab, select the job in a frontend, click
                <strong>Start</strong>, then open it with <strong>Go</strong>.
            </li>
        </ol>
    </div>

    <div v-show="tab === 0">
        <div class="card mb-3">
            <div class="card-header">Search backends</div>
            <div class="card-body">
                <form class="input-group mb-3" @submit.prevent="createBackend()">
                    <input class="form-control" v-model="newBackendName" placeholder="New search backend name">
                    <button class="btn btn-primary" type="submit" :disabled="!validName(newBackendName)">
                        Create
                    </button>
                </form>
                <div class="list-group">
                    <a v-for="backend in backends" :key="backend.name"
                       class="list-group-item list-group-item-action d-flex align-items-center"
                       :href="`#/search_backend/${backend.name}`">
                        {{ backend.name }}
                        <span class="badge text-bg-secondary ms-2">{{ backend.backend_type }}</span>
                    </a>
                </div>
            </div>
        </div>

        <div class="card mb-3">
            <div class="card-header">Jobs</div>
            <div class="card-body">
                <form class="input-group mb-3" @submit.prevent="createJob()">
                    <input class="form-control" v-model="newJobName" placeholder="New job name">
                    <button class="btn btn-primary" type="submit" :disabled="!validName(newJobName)">
                        Create
                    </button>
                </form>
                <p class="text-muted" v-if="jobs.length === 0">Create a new job to get started!</p>
                <div class="list-group">
                    <a v-for="job in jobs" :key="job.name"
                       class="list-group-item list-group-item-action d-flex align-items-center"
                       :href="`#/job/${job.name}`">
                        {{ job.name }}
                        <span class="badge text-bg-secondary ms-2">{{ job.status }}</span>
                        <span class="badge text-bg-light ms-2" v-if="job.schedule_enabled">{{ job.cron_expression
                            }}</span>
                        <span class="ms-auto text-muted">last scan: {{ fromNow(job.last_index_date) }}</span>
                    </a>
                </div>
            </div>
        </div>
    </div>

    <div v-show="tab === 1">
        <div class="card mb-3">
            <div class="card-header">User Scripts</div>
            <div class="card-body">
                <label class="form-label">Select template</label>
                <div class="form-check" v-for="template in store.info.user_script_templates" :key="template">
                    <input class="form-check-input" type="radio" :id="template" :value="template"
                           v-model="selectedTemplate">
                    <label class="form-check-label" :for="template">{{ template }}</label>
                </div>
                <form class="input-group my-3" @submit.prevent="createScript()">
                    <input class="form-control" v-model="newScriptName" placeholder="New script name" maxlength="16">
                    <button class="btn btn-primary" type="submit"
                            :disabled="!validName(newScriptName) || !selectedTemplate">
                        Create
                    </button>
                </form>
                <hr>
                <div class="list-group">
                    <a v-for="script in scripts" :key="script.name"
                       class="list-group-item list-group-item-action d-flex align-items-center"
                       :href="`#/user_script/${script.name}`">
                        {{ script.name }}
                        <span class="badge text-bg-secondary ms-2">{{ script.type }}</span>
                    </a>
                </div>
            </div>
        </div>
    </div>

    <div v-show="tab === 2">
        <div class="card mb-3">
            <div class="card-header">Frontends</div>
            <div class="card-body">
                <form class="input-group mb-3" @submit.prevent="createFrontend()">
                    <input class="form-control" v-model="newFrontendName" placeholder="New frontend name">
                    <button class="btn btn-primary" type="submit" :disabled="!validName(newFrontendName)">
                        Create
                    </button>
                </form>
                <div class="list-group">
                    <a v-for="frontend in frontends" :key="frontend.name"
                       class="list-group-item list-group-item-action d-flex align-items-center"
                       :href="`#/frontend/${frontend.name}`">
                        {{ frontend.name }}
                        <span class="badge ms-2"
                              :class="frontend.running ? 'text-bg-success' : 'text-bg-secondary'">
                            {{ frontend.running ? "online" : "offline" }}
                        </span>
                        <span class="ms-auto text-muted">{{ frontend.web_options.bind }}</span>
                    </a>
                </div>
            </div>
        </div>
    </div>
</template>

<script setup>
import { onMounted, ref } from "vue";

import { api } from "../api.js";
import { navigate, route } from "../router.js";
import { store } from "../store.js";
import { fromNow } from "../util.js";

const TABS = ["Backend", "User Scripts", "Frontends"];
const NAME_REGEX = /^[a-zA-Z0-9-_,.; ]+$/;

const tab = ref(route.params.tab === undefined ? 0 : Number(route.params.tab));
const error = ref(null);
const loaded = ref(false);

const backends = ref([]);
const jobs = ref([]);
const scripts = ref([]);
const frontends = ref([]);

const newBackendName = ref("");
const newJobName = ref("");
const newScriptName = ref("");
const newFrontendName = ref("");
const selectedTemplate = ref(null);

function validName(name) {
    return NAME_REGEX.test(name);
}

function setTab(index) {
    tab.value = index;
    window.history.replaceState(null, "", `#/${index}`);
}

async function reload() {
    backends.value = await api.get("/api/search_backend");
    jobs.value = await api.get("/api/job");
    scripts.value = await api.get("/api/user_script");
    frontends.value = await api.get("/api/frontend");
    loaded.value = true;
}

async function createBackend() {
    try {
        await api.post(`/api/search_backend/${encodeURIComponent(newBackendName.value)}`);
        navigate(`/search_backend/${newBackendName.value}`);
    } catch (e) {
        error.value = e.message;
    }
}

async function createJob() {
    try {
        await api.post(`/api/job/${encodeURIComponent(newJobName.value)}`);
        navigate(`/job/${newJobName.value}`);
    } catch (e) {
        error.value = e.message;
    }
}

async function createScript() {
    try {
        const template = encodeURIComponent(selectedTemplate.value);
        await api.post(`/api/user_script/${encodeURIComponent(newScriptName.value)}?template=${template}`);
        navigate(`/user_script/${newScriptName.value}`);
    } catch (e) {
        error.value = e.message;
    }
}

async function createFrontend() {
    try {
        await api.post(`/api/frontend/${encodeURIComponent(newFrontendName.value)}`);
        navigate(`/frontend/${newFrontendName.value}`);
    } catch (e) {
        error.value = e.message;
    }
}

onMounted(reload);
</script>
