import { reactive } from "vue";

import { api } from "./api.js";

const SETTINGS_KEY = "sist2-admin-settings";

const NOTIFICATION_MESSAGES = {
    "notifications.indexCompleted": "Task completed for [$JOB$]",
    "notifications.indexFailed": "Task failed for [$JOB$]"
};

function loadSettings() {
    try {
        const raw = window.localStorage.getItem(SETTINGS_KEY);
        if (raw !== null) {
            return JSON.parse(raw);
        }
    } catch (e) {
        // Corrupt settings; fall through to defaults.
    }
    return { jobDesktopNotifications: {} };
}

export const store = reactive({
    info: null,
    settings: loadSettings()
});

export async function loadInfo() {
    store.info = await api.get("/api");
}

export function jobNotificationsEnabled(jobName) {
    return store.settings.jobDesktopNotifications[jobName] === true;
}

export function setJobNotifications(jobName, enabled) {
    store.settings.jobDesktopNotifications[jobName] = enabled;
    window.localStorage.setItem(SETTINGS_KEY, JSON.stringify(store.settings));

    if (enabled && window.Notification && Notification.permission !== "granted") {
        Notification.requestPermission();
    }
}

export function startNotificationStream() {
    const events = api.events("/api/notifications");

    events.onmessage = (event) => {
        const notification = JSON.parse(event.data);

        if (!jobNotificationsEnabled(notification.job)) {
            return;
        }
        if (!window.Notification || Notification.permission !== "granted") {
            return;
        }

        const template = NOTIFICATION_MESSAGES[notification.message];
        let text = notification.message;
        if (template !== undefined) {
            text = template.replace("$JOB$", notification.job);
        }
        new Notification("sist2-admin", { body: text });
    };
}
