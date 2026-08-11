<template>
    <div v-if="backend !== null">
        <BackButton to="/0"/>
        <div class="d-flex justify-content-between align-items-center mb-3">
            <h4 class="mb-0">[{{ backend.name }}] search backend configuration</h4>
            <button class="btn btn-danger" @click="deleteBackend()">Delete</button>
        </div>

        <div class="alert alert-danger" v-if="error">{{ error }}</div>

        <div class="card mb-3">
            <div class="card-header">Search backend options</div>
            <div class="card-body">
                <div class="mb-2">
                    <label class="form-label">Search backend type</label>
                    <select class="form-select" v-model="backend.backend_type">
                        <option value="elasticsearch">elasticsearch</option>
                        <option value="sqlite">sqlite</option>
                    </select>
                </div>

                <template v-if="backend.backend_type === 'elasticsearch'">
                    <div class="mb-2">
                        <label class="form-label">Elasticsearch URL</label>
                        <div class="input-group">
                            <input class="form-control" v-model="backend.es_url">
                            <button class="btn btn-outline-primary" type="button" @click="testEs()">Test</button>
                        </div>
                    </div>
                    <div class="alert mt-2" :class="pingOk ? 'alert-success' : 'alert-danger'"
                         v-if="pingMessage !== null">
                        {{ pingMessage }}
                    </div>
                    <div class="form-check form-switch mb-2">
                        <input class="form-check-input" type="checkbox" id="esInsecure"
                               v-model="backend.es_insecure_ssl"
                               :disabled="!backend.es_url.startsWith('https:')">
                        <label class="form-check-label" for="esInsecure">
                            Do not verify SSL connections to Elasticsearch.
                        </label>
                    </div>
                    <div class="mb-2">
                        <label class="form-label">Elasticsearch index name</label>
                        <input class="form-control" v-model="backend.es_index">
                    </div>
                    <div class="row">
                        <div class="col-md-6 mb-2 d-flex flex-column justify-content-end">
                            <label class="form-label">Number of threads</label>
                            <input class="form-control" type="number" min="1" v-model.number="backend.threads">
                        </div>
                        <div class="col-md-6 mb-2 d-flex flex-column justify-content-end">
                            <label class="form-label">Index batch size</label>
                            <input class="form-control" type="number" min="1" v-model.number="backend.batch_size">
                        </div>
                    </div>
                    <div class="mb-2">
                        <label class="form-label">Elasticsearch mappings file override</label>
                        <textarea class="form-control font-monospace" rows="4"
                                  v-model="backend.es_mappings"></textarea>
                    </div>
                    <div class="mb-2">
                        <label class="form-label">Elasticsearch settings file override</label>
                        <textarea class="form-control font-monospace" rows="4"
                                  v-model="backend.es_settings"></textarea>
                    </div>
                </template>

                <template v-else>
                    <div class="mb-2">
                        <label class="form-label">Search index file location</label>
                        <input class="form-control" :value="backend.search_index" readonly>
                    </div>
                </template>
            </div>
        </div>
    </div>
</template>

<script setup>
import { onMounted, ref, watch } from "vue";

import BackButton from "../components/BackButton.vue";
import { api } from "../api.js";
import { navigate, route } from "../router.js";

const SAVE_DEBOUNCE = 500;

const backend = ref(null);
const error = ref(null);
const pingOk = ref(false);
const pingMessage = ref(null);

let saveTimeout = null;

async function save() {
    try {
        await api.put(`/api/search_backend/${encodeURIComponent(backend.value.name)}`, backend.value);
        error.value = null;
    } catch (e) {
        error.value = e.message;
    }
}

async function testEs() {
    const url = encodeURIComponent(backend.value.es_url);
    const insecure = backend.value.es_insecure_ssl;
    const result = await api.get(`/api/ping_es?url=${url}&insecure=${insecure}`);
    pingOk.value = result.ok;
    pingMessage.value = result.message;
}

async function deleteBackend() {
    if (!window.confirm(`Delete search backend ${backend.value.name}?`)) {
        return;
    }
    try {
        await api.delete(`/api/search_backend/${encodeURIComponent(backend.value.name)}`);
        navigate("/");
    } catch (e) {
        error.value = e.message;
    }
}

onMounted(async () => {
    backend.value = await api.get(`/api/search_backend/${encodeURIComponent(route.params.name)}`);

    watch(
        backend,
        () => {
            window.clearTimeout(saveTimeout);
            saveTimeout = window.setTimeout(save, SAVE_DEBOUNCE);
        },
        { deep: true }
    );
});
</script>
