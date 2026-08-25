<template>
    <div>
        <b-progress v-if="modelLoading && [0, 1].includes(modelLoadingProgress)" max="1" class="mb-1" variant="primary"
                    striped animated :value="1">
        </b-progress>
        <b-progress v-else-if="modelLoading" :value="modelLoadingProgress" max="1" class="mb-1" variant="warning"
                    show-progress>
        </b-progress>
        <div style="display: flex">
            <b-select :options="modelOptions()" class="mr-2 input-prepend" :value="modelName"
                      @change="onModelChange($event)"></b-select>

            <b-input-group>
                <b-form-input :value="embeddingText"
                              :placeholder="$store.state.embeddingDoc ? ' ' : $t('embeddingsSearchPlaceholder')"
                              @input="onInput($event)"
                              :disabled="modelLoading"
                              :style="{'pointer-events': $store.state.embeddingDoc ? 'none' : undefined}"
                ></b-form-input>
                <b-badge v-if="$store.state.embeddingDoc" pill variant="primary" class="overlay-badge" href="#"
                         @click="onBadgeClick()">{{ docName }}
                </b-badge>

                <template #prepend>
                </template>

                <template #append>
                    <b-input-group-text>
                        <MLIcon class="ml-append" big></MLIcon>
                    </b-input-group-text>
                </template>

            </b-input-group>
        </div>

    </div>
</template>

<script>
import {mapGetters, mapMutations} from "vuex";
import {CLIPTransformerModel} from "@/ml/CLIPTransformerModel"
import {TransformerModel} from "@/ml/TransformerModel"
import {debounce as _debounce} from "@/util";
import MLIcon from "@/components/icons/MlIcon.vue";
import Sist2AdminApi from "@/Sist2Api";

export default {
    components: {MLIcon},
    data() {
        return {
            modelLoading: false,
            modelLoadingProgress: 0,
            modelLoaded: false,
            model: null,
            modelName: null,
            modelError: null
        }
    },
    computed: {
        ...mapGetters({
            optQueryMode: "optQueryMode",
            embeddingText: "embeddingText",
            fuzzy: "fuzzy",
        }),
        docName() {
            const ext = this.$store.state.embeddingDoc._source.extension;
            return this.$store.state.embeddingDoc._source.name +
                (ext ? "." + ext : "")
        }
    },
    mounted() {
        // Set default model
        this.modelName = Sist2AdminApi.models()[0].name;
        this.onModelChange(this.modelName);

        this.onInput = _debounce(this._onInput, 450);
    },
    methods: {
        ...mapMutations({
            setEmbeddingText: "setEmbeddingText",
            setEmbedding: "setEmbedding",
            setEmbeddingModel: "setEmbeddingsModel",
        }),
        async loadModel() {
            if (this.modelError) {
                throw new Error(this.modelError);
            }

            this.modelLoading = true;

            await this.model.init(async progress => {
                this.modelLoadingProgress = progress;
            });
            this.modelLoading = false;
            this.modelLoaded = true;
        },
        async _onInput(text) {
            try {

                if (!this.modelLoaded) {
                    await this.loadModel();
                }

                if (text.length === 0) {
                    this.setEmbeddingText("");
                    this.setEmbedding(null);
                    return;
                }

                const embeddings = await this.model.predict(text);

                this.setEmbeddingText(text);
                this.setEmbedding(embeddings);
            } catch (e) {
                alert(e)
            }
        },
        modelOptions() {
            return Sist2AdminApi.models().map(model => model.name);
        },
        onModelChange(name) {
            this.modelLoaded = false;
            this.modelLoadingProgress = 0;

            const modelInfo = Sist2AdminApi.models().find(m => m.name === name);

            this.setEmbeddingModel(modelInfo.id);

            // Runs while the bar is mounting, so a model that cannot be loaded is reported when
            // the user types rather than thrown into the render
            if (!modelInfo.url) {
                this.model = null;
                this.modelError = `The ${name} model was registered without a URL, so its ` +
                    "embeddings cannot be searched from here. The user script that wrote them has " +
                    "to point at an .onnx text encoder.";
                return;
            }

            this.modelError = null;

            // Every model keeps its tokenizer next to it
            const tokenizerUrl = new URL("./tokenizer.json", modelInfo.url).href;

            this.model = modelInfo.name === "CLIP"
                ? new CLIPTransformerModel(modelInfo.url, tokenizerUrl)
                : new TransformerModel(modelInfo.url, tokenizerUrl, modelInfo.size);
        },
        onBadgeClick() {
            this.$store.commit("setEmbedding", null);
            this.$store.commit("setEmbeddingDoc", null);
        }
    }
}
</script>
<style>
.progress-bar {
    transition: none;
}

.overlay-badge {
    position: absolute;
    z-index: 1;
    left: 0.375rem;
    top: 8px;
    line-height: 1.1rem;
    overflow: hidden;
    max-width: 200px;
    text-overflow: ellipsis;
}

.input-prepend {
    max-width: 100px;
}

.theme-black .ml-append {
    filter: brightness(0.95) !important;
}
</style>