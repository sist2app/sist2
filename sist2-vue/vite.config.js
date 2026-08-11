import {fileURLToPath, URL} from "node:url";
import {defaultClientConditions, defineConfig} from "vite";
import vue from "@vitejs/plugin-vue2";

// The C binary embeds a fixed set of files from dist/ (see src/web/web_util.c), so the
// output names must stay stable: index.html, js/index.js, js/chunk-vendors.js, css/index.css.
export default defineConfig({
    base: "./",
    plugins: [vue()],
    resolve: {
        alias: {
            "@": fileURLToPath(new URL("./src", import.meta.url))
        },
        // Without this condition, onnxruntime-web resolves to the build that embeds the
        // ~70MB of wasm runtimes in the bundle instead of fetching them from ORT_WASM_PATH_PREFIX
        conditions: ["onnxruntime-web-use-extern-wasm", ...defaultClientConditions]
    },
    server: {
        // Forward the API routes of `sist2 web` (see src/web/serve.c) so that the dev
        // server is usable against a running instance on the default port
        proxy: Object.fromEntries(
            ["^/i$", "^/es$", "^/status$", "^/favicon.ico$", "^/f/", "^/t/", "^/s/", "^/e/", "^/tag/", "^/fts/"]
                .map(route => [route, "http://127.0.0.1:4090"])
        )
    },
    build: {
        outDir: "dist",
        emptyOutDir: true,
        sourcemap: false,
        cssCodeSplit: false,
        // Everything must end up in the four embedded files: inline any other asset
        assetsInlineLimit: Number.MAX_SAFE_INTEGER,
        minify: "terser",
        terserOptions: {
            compress: {
                passes: 2,
                module: true,
                hoist_funs: true,
                // https://github.com/microsoft/onnxruntime/issues/16984
                unused: false
            },
            mangle: true
        },
        rollupOptions: {
            output: {
                entryFileNames: "js/[name].js",
                chunkFileNames: "js/[name].js",
                assetFileNames: assetInfo =>
                    assetInfo.names.some(name => name.endsWith(".css"))
                        ? "css/index.css"
                        : "assets/[name][extname]",
                manualChunks: id => id.includes("node_modules") ? "chunk-vendors" : undefined
            }
        }
    }
});
