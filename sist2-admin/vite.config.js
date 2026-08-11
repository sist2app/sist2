import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";

export default defineConfig({
    root: "frontend",
    base: "/ui/",
    plugins: [vue()],
    build: {
        outDir: "dist",
        emptyOutDir: true
    },
    server: {
        proxy: {
            "/api": "http://localhost:8080"
        }
    }
});
