async function request(method, path, body) {
    const options = { method: method };
    if (body !== undefined) {
        options.headers = { "content-type": "application/json" };
        options.body = JSON.stringify(body);
    }

    const response = await fetch(path, options);

    if (!response.ok) {
        let message = `HTTP ${response.status}`;
        try {
            const payload = await response.json();
            if (payload.error !== undefined) {
                message = payload.error;
            }
        } catch (e) {
            // Not JSON; keep the status message.
        }
        throw new Error(message);
    }

    return response.json();
}

export const api = {
    get(path) {
        return request("GET", path);
    },
    post(path, body) {
        return request("POST", path, body);
    },
    put(path, body) {
        return request("PUT", path, body);
    },
    delete(path) {
        return request("DELETE", path);
    },
    events(path) {
        return new EventSource(path);
    }
};
