export async function downloadToBuffer(url, onProgress) {
    const resp = await fetch(url);

    const contentLength = +resp.headers.get("Content-Length");
    const buf = new Uint8ClampedArray(contentLength);
    const reader = resp.body.getReader();
    let cursor = 0;

    if (onProgress) {
        onProgress(0);
    }

    while (true) {
        const {done, value} = await reader.read();

        if (done) {
            break;
        }

        buf.set(value, cursor);
        cursor += value.length;

        if (onProgress) {
            onProgress(cursor / contentLength);
        }
    }

    return buf;
}

// The .wasm/.mjs runtime files are not embedded in the sist2 binary, they are fetched from a CDN.
// Must match the onnxruntime-web version in package.json.
export const ORT_WASM_PATH_PREFIX = "https://cdn.jsdelivr.net/npm/onnxruntime-web@1.27.0/dist/";