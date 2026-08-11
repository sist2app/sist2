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
            modelName: null
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

            if (modelInfo.name === "CLIP") {
                const tokenizerUrl = new URL("./tokenizer.json", modelInfo.url).href;
                this.model = new CLIPTransformerModel(modelInfo.url, tokenizerUrl)
                this.setEmbeddingModel(modelInfo.id);
            } else {
                throw new Error("Unknown model: " + name);
            }
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