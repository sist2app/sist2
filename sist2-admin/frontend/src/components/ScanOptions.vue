<template>
    <div class="card mb-3">
        <div class="card-header">Scanning options</div>
        <div class="card-body">
            <div class="mb-2">
                <label class="form-label">Path</label>
                <input class="form-control" v-model="options.path">
            </div>
            <div class="mb-2">
                <label class="form-label">Number of threads</label>
                <input class="form-control" type="number" min="1" v-model.number="options.threads">
            </div>
            <div class="mb-2">
                <label class="form-label">Scan up to this many subdirectories deep</label>
                <input class="form-control" type="number" v-model.number="options.depth">
            </div>
            <div class="mb-2">
                <label class="form-label">Thumbnail quality, on a scale of 0 to 100, 100 being the best</label>
                <input class="form-control" type="number" min="0" max="100"
                       v-model.number="options.thumbnail_quality">
            </div>
            <div class="mb-2">
                <label class="form-label">Number of thumbnails to generate. Set a value &gt; 1 to create video
                    previews, set to 0 to disable thumbnails.</label>
                <input class="form-control" type="number" min="0" v-model.number="options.thumbnail_count">
            </div>
            <div class="mb-2">
                <label class="form-label">Thumbnail size, in pixels</label>
                <input class="form-control" type="number" min="32" v-model.number="options.thumbnail_size">
            </div>
            <div class="mb-2">
                <label class="form-label">Number of bytes to be extracted from text documents. Set to 0 to
                    disable</label>
                <input class="form-control" type="number" min="0" v-model.number="options.content_size">
            </div>
            <div class="mb-2">
                <label class="form-label">Serve files from this url instead of from disk</label>
                <input class="form-control" v-model="options.rewrite_url">
            </div>
            <div class="mb-2">
                <label class="form-label">Archive file mode</label>
                <select class="form-select" v-model="options.archive">
                    <option value="skip">skip</option>
                    <option value="list">list</option>
                    <option value="shallow">shallow</option>
                    <option value="recurse">recurse</option>
                </select>
            </div>
            <div class="mb-2">
                <label class="form-label">Passphrase for encrypted archive files</label>
                <input class="form-control" v-model="options.archive_passphrase">
            </div>

            <div class="form-check form-switch">
                <input class="form-check-input" type="checkbox" id="ocrEbooks" v-model="options.ocr_ebooks">
                <label class="form-check-label" for="ocrEbooks">Enable OCR'ing of ebook files</label>
            </div>
            <div class="form-check form-switch mb-2">
                <input class="form-check-input" type="checkbox" id="ocrImages" v-model="options.ocr_images">
                <label class="form-check-label" for="ocrImages">Enable OCR'ing of image files</label>
            </div>
            <div class="mb-2" v-if="options.ocr_ebooks || options.ocr_images">
                <label class="form-label">Tesseract language</label>
                <div class="alert alert-warning" v-if="selectedLangs.length === 0">
                    You must select at least one language
                </div>
                <div class="ps-2">
                    <div class="form-check form-check-inline" v-for="lang in store.info.tesseract_langs" :key="lang">
                        <input class="form-check-input" type="checkbox" :id="`lang-${lang}`" :value="lang"
                               :checked="selectedLangs.includes(lang)" @change="toggleLang(lang)">
                        <label class="form-check-label" :for="`lang-${lang}`">{{ lang }}</label>
                    </div>
                </div>
            </div>

            <div class="mb-2">
                <label class="form-label">Files that match this regex will not be scanned</label>
                <input class="form-control" v-model="options.exclude" placeholder="Exclude">
            </div>

            <div class="form-check form-switch">
                <input class="form-check-input" type="checkbox" id="fast" v-model="options.fast">
                <label class="form-check-label" for="fast">Only index file names &amp; mime type</label>
            </div>
            <div class="form-check form-switch">
                <input class="form-check-input" type="checkbox" id="checksums" v-model="options.checksums">
                <label class="form-check-label" for="checksums">Calculate file checksums when scanning</label>
            </div>
            <div class="form-check form-switch">
                <input class="form-check-input" type="checkbox" id="readSubtitles" v-model="options.read_subtitles">
                <label class="form-check-label" for="readSubtitles">Read subtitles from media files</label>
            </div>
            <div class="form-check form-switch mb-2">
                <input class="form-check-input" type="checkbox" id="optimizeIndex" v-model="options.optimize_index">
                <label class="form-check-label" for="optimizeIndex">Defragment index file after scan to reduce its
                    file size.</label>
            </div>

            <div class="mb-2">
                <label class="form-label">Maximum memory buffer size per thread in MiB for files inside
                    archives</label>
                <input class="form-control" type="number" min="0" v-model.number="options.mem_buffer">
            </div>
            <div class="form-check form-switch mb-2">
                <input class="form-check-input" type="checkbox" id="noStats" v-model="options.no_stats">
                <label class="form-check-label" for="noStats">Skip the stats generation step. The stats page will
                    have nothing to show for this index.</label>
            </div>

            <div class="mb-2">
                <label class="form-label">Relative size threshold for treemap</label>
                <input class="form-control" type="number" step="0.0001" min="0"
                       v-model.number="options.treemap_threshold">
            </div>
        </div>
    </div>
</template>

<script setup>
import { computed } from "vue";

import { store } from "../store.js";

const props = defineProps({
    options: { type: Object, required: true }
});

const selectedLangs = computed(() => {
    if (!props.options.ocr_lang) {
        return [];
    }
    return props.options.ocr_lang.split("+");
});

function toggleLang(lang) {
    const langs = new Set(selectedLangs.value);
    if (langs.has(lang)) {
        langs.delete(lang);
    } else {
        langs.add(lang);
    }

    if (langs.size === 0) {
        props.options.ocr_lang = null;
    } else {
        props.options.ocr_lang = [...langs].join("+");
    }
}
</script>
