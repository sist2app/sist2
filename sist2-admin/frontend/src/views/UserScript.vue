<template>
    <div v-if="script !== null">
        <BackButton to="/1"/>
        <div class="d-flex justify-content-between align-items-center mb-3">
            <h4 class="mb-0">[{{ script.name }}] User Script</h4>
            <button class="btn btn-danger" @click="deleteScript()">Delete</button>
        </div>

        <div class="alert alert-danger" v-if="error">{{ error }}</div>
        <div class="alert alert-success" v-if="taskQueued">Task queued. Check the Tasks page to monitor the
            status.</div>

        <div class="card mb-3">
            <div class="card-header">User Script</div>
            <div class="card-body">
                <div class="mb-2">
                    <label class="form-label">Script type</label>
                    <select class="form-select" v-model="script.type">
                        <option value="simple">simple</option>
                        <option value="git">git</option>
                    </select>
                </div>

                <template v-if="script.type === 'git'">
                    <div class="mb-2">
                        <label class="form-label">Git repository URL</label>
                        <input class="form-control" v-model="script.git_repository">
                    </div>
                </template>
                <template v-else>
                    <div class="mb-2">
                        <label class="form-label">Script code (Python)</label>
                        <textarea class="form-control font-monospace" rows="12" v-model="script.script"></textarea>
                    </div>
                </template>

                <div class="mb-2">
                    <label class="form-label">Extra command line arguments</label>
                    <input class="form-control" v-model="script.extra_args">
                </div>
            </div>
        </div>

        <div class="card mb-3">
            <div class="card-header">Test/debug User Script</div>
            <div class="card-body">
                <label class="form-label">Select a job</label>
                <div class="input-group">
                    <JobSelect v-model="testJob"/>
                    <button class="btn btn-primary" :disabled="testJob === null" @click="testRun()">Test</button>
                </div>
            </div>
        </div>
    </div>
</template>

<script setup>
import { onMounted, ref, watch } from "vue";

import BackButton from "../components/BackButton.vue";
import JobSelect from "../components/JobSelect.vue";
import { api } from "../api.js";
import { navigate, route } from "../router.js";

const SAVE_DEBOUNCE = 500;

const script = ref(null);
const error = ref(null);
const taskQueued = ref(false);
const testJob = ref(null);

let saveTimeout = null;

async function save() {
    try {
        await api.put(`/api/user_script/${encodeURIComponent(script.value.name)}`, script.value);
        error.value = null;
    } catch (e) {
        error.value = e.message;
    }
}

async function testRun() {
    try {
        const job = encodeURIComponent(testJob.value);
        await api.post(`/api/user_script/${encodeURIComponent(script.value.name)}/run?job=${job}`);
        taskQueued.value = true;
    } catch (e) {
        error.value = e.message;
    }
}

async function deleteScript() {
    if (!window.confirm(`Delete user script ${script.value.name}?`)) {
        return;
    }
    try {
        await api.delete(`/api/user_script/${encodeURIComponent(script.value.name)}`);
        navigate("/1");
    } catch (e) {
        error.value = e.message;
    }
}

onMounted(async () => {
    script.value = await api.get(`/api/user_script/${encodeURIComponent(route.params.name)}`);

    watch(
        script,
        () => {
            window.clearTimeout(saveTimeout);
            saveTimeout = window.setTimeout(save, SAVE_DEBOUNCE);
        },
        { deep: true }
    );
});
</script>
