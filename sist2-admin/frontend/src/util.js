const SECOND = 1000;
const MINUTE = 60 * SECOND;
const HOUR = 60 * MINUTE;
const DAY = 24 * HOUR;

export function formatDate(isoString) {
    if (!isoString) {
        return "";
    }
    return new Date(isoString).toLocaleString();
}

export function fromNow(isoString) {
    if (!isoString) {
        return "never";
    }

    const elapsed = Date.now() - new Date(isoString).getTime();
    if (elapsed < MINUTE) {
        return "just now";
    }
    if (elapsed < HOUR) {
        return `${Math.floor(elapsed / MINUTE)} minutes ago`;
    }
    if (elapsed < DAY) {
        return `${Math.floor(elapsed / HOUR)} hours ago`;
    }
    return `${Math.floor(elapsed / DAY)} days ago`;
}

export function humanDuration(startIso, endIso) {
    if (!startIso || !endIso) {
        return "";
    }

    const elapsed = new Date(endIso).getTime() - new Date(startIso).getTime();
    if (elapsed < SECOND) {
        return "<1s";
    }
    if (elapsed < MINUTE) {
        return `${Math.round(elapsed / SECOND)}s`;
    }
    if (elapsed < HOUR) {
        return `${Math.floor(elapsed / MINUTE)}m ${Math.round((elapsed % MINUTE) / SECOND)}s`;
    }
    return `${Math.floor(elapsed / HOUR)}h ${Math.round((elapsed % HOUR) / MINUTE)}m`;
}

export function humanSize(bytes) {
    if (bytes === 0 || bytes === undefined || bytes === null) {
        return "0 B";
    }

    const units = ["B", "kB", "MB", "GB", "TB"];
    const exponent = Math.min(Math.floor(Math.log(bytes) / Math.log(1000)), units.length - 1);
    const value = bytes / Math.pow(1000, exponent);
    return `${value.toFixed(exponent === 0 ? 0 : 1)} ${units[exponent]}`;
}
