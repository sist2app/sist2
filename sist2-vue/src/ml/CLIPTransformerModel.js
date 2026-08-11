import * as ort from "onnxruntime-web/wasm";
import {BPETokenizer} from "@/ml/BPETokenizer";
import axios from "axios";
import {downloadToBuffer, ORT_WASM_PATH_PREFIX} from "@/ml/mlUtils";
import ModelStore from "@/ml/ModelStore";

export class CLIPTransformerModel {

    _modelUrl = null;
    _tokenizerUrl = null;
    _model = null;
    _tokenizer = null;

    constructor(modelUrl, tokenizerUrl) {
        this._modelUrl = modelUrl;
        this._tokenizerUrl = tokenizerUrl;
    }

    async init(onProgress) {
        await Promise.all([this.loadTokenizer(), this.loadModel(onProgress)]);
    }

    async loadModel(onProgress) {
        ort.env.wasm.wasmPaths = ORT_WASM_PATH_PREFIX;
        if (window.crossOriginIsolated) {
            ort.env.wasm.numThreads = 2;
        }

        let buf = await ModelStore.get(this._modelUrl);
        if (!buf) {
            buf = await downloadToBuffer(this._modelUrl, onProgress);
            await ModelStore.set(this._modelUrl, buf);
        }

        this._model = await ort.InferenceSession.create(buf.buffer, {
            executionProviders: ["wasm"],
        });
    }

    async loadTokenizer() {
        const resp = await axios.get(this._tokenizerUrl);
        this._tokenizer = new BPETokenizer(resp.data.encoder, resp.data.bpe_ranks)
    }

    async predict(text) {
        const tokenized = this._tokenizer.encode(text);

        const inputs = {
            input_ids: new ort.Tensor("int32", tokenized, [1, 77])
        };

        const results = await this._model.run(inputs);

        return Array.from(
            Object.values(results)
                .find(result => result.size === 512).data
        );
    }
}
