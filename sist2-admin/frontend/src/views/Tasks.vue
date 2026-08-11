<template>
    <div>
        <BackButton to="/"/>
        <h4>Running tasks</h4>
        <p class="text-muted" v-if="running.length === 0">No running tasks.</p>
        <div class="card mb-3" v-for="task in running" :key="task.id">
            <div class="card-body">
                <div class="d-flex justify-content-between align-items-center">
                    <a v-if="task.started !== null" :href="`#/log/${task.id}`">{{ task.display_name }}</a>
                    <span v-else>{{ task.display_name }}</span>
                    <div>
                        <span class="badge text-bg-secondary me-2" v-if="task.started === null">queued</span>
                        <button class="btn btn-sm btn-danger" v-if="task.started !== null"
                                @click="kill(task)">Kill</button>
                    </div>
                </div>
                <div class="progress mt-2" v-if="task.started !== null">
                    <div class="progress-bar" :class="{'progress-bar-striped': !task.progress.waiting}"
                         :style="{width: `${percent(task)}%`}">
                        {{ formatCount(task.progress.done) }} / {{ formatCount(task.progress.count) }}
                    </div>
                </div>
                <small class="text-muted" v-if="task.progress.index_size > 0">
                    index: {{ humanSize(task.progress.index_size) }},
                    store: {{ humanSize(task.progress.store_size) }}
                </small>
            </div>
        </div>

        <h4>Task history</h4>
        <table class="table table-sm">
            <thead>
            <tr>
                <th>Task name</th>
                <th>Started</th>
                <th>Duration</th>
                <th>Status</th>
                <th>Logs</th>
            </tr>
            </thead>
            <tbody>
            <tr v-for="row in page" :key="row.id" :class="{'table-danger': row.return_code !== 0}">
                <td>{{ row.name }}</td>
                <td>{{ formatDate(row.started) }}</td>
                <td>{{ humanDuration(row.started, row.ended) }}</td>
                <td>{{ row.return_code === 0 ? "ok" : `failed (${row.return_code})` }}</td>
                <td>
                    <template v-if="row.has_logs === 1">
                        <a class="btn btn-sm btn-outline-primary" :href="`#/log/${row.id}`">View</a>
                        <button class="btn btn-sm btn-outline-danger ms-1" @click="deleteLogs(row)">Delete</button>
                    </template>
                </td>
            </tr>
            </tbody>
        </table>

        <nav v-if="pageCount > 1">
            <ul class="pagination pagination-sm">
                <li class="page-item" :class="{active: index === pageIndex}"
                    v-for="index in pageIndices" :key="index">
                    <a class="page-link" href="#" @click.prevent="pageIndex = index">{{ index + 1 }}</a>
                </li>
            </ul>
        </nav>
    </div>
</template>

<script setup>
import { computed, onMounted, onUnmounted, ref } from "vue";

import BackButton from "../components/BackButton.vue";
import { api } from "../api.js";
import { formatDate, humanDuration, humanSize } from "../util.js";

const POLL_INTERVAL = 1000;
const PAGE_SIZE = 10;

const running = ref([]);
const history = ref([]);
const pageIndex = ref(0);

let pollInterval = null;

const pageCount = computed(() => Math.ceil(history.value.length / PAGE_SIZE));
const pageIndices = computed(() => [...Array(pageCount.value).keys()]);
const page = computed(() => {
    return history.value.slice(pageIndex.value * PAGE_SIZE, (pageIndex.value + 1) * PAGE_SIZE);
});

function percent(task) {
    if (task.progress.count === 0) {
        return 0;
    }
    return Math.round((task.progress.done / task.progress.count) * 100);
}

function formatCount(value) {
    return value.toLocaleString();
}

async function poll() {
    running.value = await api.get("/api/task");
    history.value = await api.get("/api/task/history");
}

async function kill(task) {
    if (!window.confirm("Send SIGTERM signal to sist2 process?")) {
        return;
    }
    await api.post(`/api/task/${task.id}/kill`);
}

async function deleteLogs(row) {
    await api.post(`/api/task/${row.id}/delete_logs`);
    await poll();
}

onMounted(() => {
    poll();
    pollInterval = window.setInterval(poll, POLL_INTERVAL);
});

onUnmounted(() => {
    window.clearInterval(pollInterval);
});
</script>
