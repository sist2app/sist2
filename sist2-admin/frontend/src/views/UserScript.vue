<template>
    <div v-if="script !== null">
        <BackButton to="/1"/>
        <div class="d-flex justify-content-between align-items-center mb-3">
            <h4 class="mb-0">[{{ script.name }}] User Script</h4>
            <div>
                <button class="btn btn-secondary me-2" @click="startRename()">Rename</button>
                <button class="btn btn-danger" @click="deleteScript()">Delete</button>
            </div>
        </div>

        <form class="input-group mb-3" v-if="newName !== null" @submit.prevent="rename()">
            <input class="form-control" v-model="newName" ref="newNameInput" placeholder="New script name"
                   maxlength="16">
            <button class="btn btn-primary" type="submit" :disabled="!validName(newName)">Save</button>
            <button class="btn btn-secondary" type="button" @click="newName = null">Cancel</button>
        </form>

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
import { nextTick, onMounted, ref, watch } from "vue";

import BackButton from "../components/BackButton.vue";
import JobSelect from "../components/JobSelect.vue";
import { api } from "../api.js";
import { navigate, route } from "../router.js";

const SAVE_DEBOUNCE = 500;
const NAME_REGEX = /^[a-zA-Z0-9-_,.; ]+$/;

const script = ref(null);
const error = ref(null);
const taskQueued = ref(false);
const testJob = ref(null);
const newName = ref(null);
const newNameInput = ref(null);

let saveTimeout = null;

async function save() {
    window.clearTimeout(saveTimeout);
    try {
        await api.put(`/api/user_script/${encodeURIComponent(script.value.name)}`, script.value);
        error.value = null;
    } catch (e) {
        error.value = e.message;
    }
}

function validName(name) {
    return NAME_REGEX.test(name);
}

async function startRename() {
    newName.value = script.value.name;
    await nextTick();
    newNameInput.value.select();
}

async function rename() {
    // Flush pending edits; they would otherwise be saved under the old name
    await save();
    if (error.value !== null) {
        return;
    }

    const name = newName.value;
    try {
        await api.post(`/api/user_script/${encodeURIComponent(script.value.name)}/rename`, { name: name });
    } catch (e) {
        error.value = e.message;
        return;
    }

    window.clearTimeout(saveTimeout);
    newName.value = null;
    navigate(`/user_script/${encodeURIComponent(name)}`);
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
