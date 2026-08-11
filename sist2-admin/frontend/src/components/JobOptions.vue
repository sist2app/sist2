<template>
    <div class="card mb-3">
        <div class="card-header">Job options</div>
        <div class="card-body">
            <div class="form-check form-switch">
                <input class="form-check-input" type="checkbox" id="desktopNotifications"
                       :checked="notificationsEnabled" @change="toggleNotifications($event.target.checked)">
                <label class="form-check-label" for="desktopNotifications">Desktop notifications</label>
            </div>
            <div class="form-check form-switch mb-2">
                <input class="form-check-input" type="checkbox" id="scheduleEnabled"
                       v-model="job.schedule_enabled">
                <label class="form-check-label" for="scheduleEnabled">Enable scheduled re-scan</label>
            </div>
            <div class="mb-2">
                <label class="form-label">Job schedule</label>
                <input class="form-control" :class="{'is-invalid': !cronValid}" v-model="job.cron_expression">
                <div class="invalid-feedback">Invalid cron expression</div>
            </div>
            <div class="mb-2">
                <label class="form-label">Keep last N log files. Set to -1 to keep all logs.</label>
                <div class="input-group">
                    <input class="form-control" type="number" min="-1" v-model.number="job.keep_last_n_logs">
                    <button class="btn btn-outline-danger" type="button" @click="deleteLogsNow()">Delete now</button>
                </div>
            </div>
        </div>
    </div>
</template>

<script setup>
import { computed } from "vue";

import { api } from "../api.js";
import { jobNotificationsEnabled, setJobNotifications } from "../store.js";

const CRON_FIELD_REGEX = /^[0-9*,/-]+$/;

const props = defineProps({
    job: { type: Object, required: true }
});

const notificationsEnabled = computed(() => jobNotificationsEnabled(props.job.name));

const cronValid = computed(() => {
    const fields = props.job.cron_expression.trim().split(/\s+/);
    return fields.length === 5 && fields.every((field) => CRON_FIELD_REGEX.test(field));
});

function toggleNotifications(enabled) {
    setJobNotifications(props.job.name, enabled);
}

async function deleteLogsNow() {
    const n = props.job.keep_last_n_logs;
    const rows = await api.get(`/api/job/${encodeURIComponent(props.job.name)}/logs_to_delete?n=${n}`);

    if (rows.length === 0) {
        window.alert("No log files to delete.");
        return;
    }
    if (!window.confirm(`Delete ${rows.length} log file(s)?`)) {
        return;
    }

    for (const row of rows) {
        await api.post(`/api/task/${row.id}/delete_logs`);
    }
}
</script>
