import { reactive } from "vue";

const ROUTES = [
    { regex: /^\/job\/(.+)$/, name: "job", paramNames: ["name"] },
    { regex: /^\/frontend\/(.+)$/, name: "frontend", paramNames: ["name"] },
    { regex: /^\/search_backend\/(.+)$/, name: "searchBackend", paramNames: ["name"] },
    { regex: /^\/user_script\/(.+)$/, name: "userScript", paramNames: ["name"] },
    { regex: /^\/tasks$/, name: "tasks", paramNames: [] },
    { regex: /^\/log\/(.+)$/, name: "tail", paramNames: ["taskId"] },
    { regex: /^\/(\d+)$/, name: "home", paramNames: ["tab"] },
    { regex: /^\/?$/, name: "home", paramNames: [] }
];

export const route = reactive({
    name: "home",
    params: {}
});

function parseHash() {
    let hash = window.location.hash.slice(1);
    if (hash === "") {
        hash = "/";
    }

    for (const candidate of ROUTES) {
        const match = candidate.regex.exec(hash);
        if (match === null) {
            continue;
        }
        const params = {};
        for (const [index, name] of candidate.paramNames.entries()) {
            params[name] = decodeURIComponent(match[index + 1]);
        }
        route.name = candidate.name;
        route.params = params;
        return;
    }

    route.name = "home";
    route.params = {};
}

export function navigate(path) {
    window.location.hash = path;
}

window.addEventListener("hashchange", parseHash);
parseHash();
