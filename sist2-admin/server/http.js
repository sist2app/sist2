import fs from "node:fs";
import path from "node:path";

import { FRONTEND_DIST } from "./config.js";
import { logger } from "./log.js";

const MIME_TYPES = {
    ".html": "text/html",
    ".js": "application/javascript",
    ".css": "text/css",
    ".ico": "image/x-icon",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".woff2": "font/woff2",
    ".map": "application/json"
};

const SSE_HEARTBEAT_INTERVAL = 15000;

export class HttpError extends Error {
    constructor(status, message) {
        super(message);
        this.status = status;
    }
}

export class Router {
    constructor() {

        this._routes = [];
    }

    add(method, pattern, handler) {
        const paramNames = [];
        const regexSource = pattern.replace(/:([a-zA-Z_]+)/g, (match, name) => {
            paramNames.push(name);
            return "([^/]+)";
        });
        this._routes.push({
            method: method,
            regex: new RegExp(`^${regexSource}$`),
            paramNames: paramNames,
            handler: handler
        });
    }

    get(pattern, handler) {
        this.add("GET", pattern, handler);
    }

    post(pattern, handler) {
        this.add("POST", pattern, handler);
    }

    put(pattern, handler) {
        this.add("PUT", pattern, handler);
    }

    delete(pattern, handler) {
        this.add("DELETE", pattern, handler);
    }

    match(method, pathname) {
        for (const route of this._routes) {
            if (route.method !== method) {
                continue;
            }
            const match = route.regex.exec(pathname);
            if (match === null) {
                continue;
            }
            const params = {};
            for (const [index, name] of route.paramNames.entries()) {
                params[name] = decodeURIComponent(match[index + 1]);
            }
            return {
                handler: route.handler,
                params: params
            };
        }
        return null;
    }
}

function readBody(req) {
    return new Promise((resolve, reject) => {
        const chunks = [];
        req.on("data", (chunk) => chunks.push(chunk));
        req.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
        req.on("error", reject);
    });
}

export async function handleRequest(router, req, res) {
    const url = new URL(req.url, "http://localhost");

    res.setHeader("Access-Control-Allow-Origin", "*");
    res.setHeader("Access-Control-Allow-Methods", "*");
    res.setHeader("Access-Control-Allow-Headers", "*");

    if (req.method === "OPTIONS") {
        res.writeHead(204);
        res.end();
        return;
    }

    if (req.method === "GET" && (url.pathname === "/" || url.pathname === "/ui")) {
        res.writeHead(302, { location: "/ui/" });
        res.end();
        return;
    }

    if (req.method === "GET" && url.pathname.startsWith("/ui/")) {
        serveStatic(res, url.pathname);
        return;
    }

    const match = router.match(req.method, url.pathname);
    if (match === null) {
        sendJson(res, 404, { error: "not found" });
        return;
    }

    let body = null;
    if (req.method === "PUT" || req.method === "POST") {
        const raw = await readBody(req);
        if (raw !== "") {
            try {
                body = JSON.parse(raw);
            } catch (e) {
                sendJson(res, 400, { error: "invalid JSON body" });
                return;
            }
        }
    }

    try {
        const result = await match.handler({
            req: req,
            res: res,
            params: match.params,
            query: url.searchParams,
            body: body
        });

        if (res.writableEnded || result === RESPONSE_HANDLED) {
            return;
        }
        if (result === undefined) {
            sendJson(res, 200, "ok");
        } else {
            sendJson(res, 200, result);
        }
    } catch (e) {
        if (e instanceof HttpError) {
            sendJson(res, e.status, { error: e.message });
            return;
        }
        logger.error(`Unhandled error for ${req.method} ${url.pathname}: ${e.stack}`);
        sendJson(res, 500, { error: "internal server error" });
    }
}

export const RESPONSE_HANDLED = Symbol("response handled");

export function sendJson(res, status, payload) {
    const data = JSON.stringify(payload);
    res.writeHead(status, {
        "content-type": "application/json",
        "content-length": Buffer.byteLength(data)
    });
    res.end(data);
}

function serveStatic(res, urlPath) {
    let relative = urlPath.slice("/ui/".length);
    if (relative === "") {
        relative = "index.html";
    }

    let filePath = path.normalize(path.join(FRONTEND_DIST, relative));
    if (!filePath.startsWith(FRONTEND_DIST)) {
        sendJson(res, 404, { error: "not found" });
        return;
    }

    if (!fs.existsSync(filePath) || fs.statSync(filePath).isDirectory()) {
        filePath = path.join(FRONTEND_DIST, "index.html");
    }

    if (!fs.existsSync(filePath)) {
        sendJson(res, 404, { error: "frontend is not built (run npm run build)" });
        return;
    }

    const extension = path.extname(filePath);
    const mimeType = MIME_TYPES[extension] === undefined ? "application/octet-stream" : MIME_TYPES[extension];

    res.writeHead(200, { "content-type": mimeType });
    fs.createReadStream(filePath).pipe(res);
}

export function openSse(res) {
    res.writeHead(200, {
        "content-type": "text/event-stream",
        "cache-control": "no-cache",
        "connection": "keep-alive"
    });
    res.write(": connected\n\n");

    const heartbeat = setInterval(() => {
        res.write(": heartbeat\n\n");
    }, SSE_HEARTBEAT_INTERVAL);

    res.on("close", () => {
        clearInterval(heartbeat);
    });

    return {
        send(payload) {
            res.write(`data: ${JSON.stringify(payload)}\n\n`);
        },
        onClose(callback) {
            res.on("close", callback);
        },
        close() {
            clearInterval(heartbeat);
            res.end();
        }
    };
}
