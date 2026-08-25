import * as ort from "onnxruntime-web/wasm";
import axios from "axios";

import {WordPieceTokenizer} from "@/ml/WordPieceTokenizer";
import {downloadToBuffer, ORT_WASM_PATH_PREFIX} from "@/ml/mlUtils";
import ModelStore from "@/ml/ModelStore";

/**
 * A sentence-transformers text encoder. The pooling and the normalisation are part of the exported
 * graph, so one query in gives one embedding out, comparable with the ones a user script wrote.
 */
export class TransformerModel {

    _modelUrl = null;
    _tokenizerUrl = null;
    _size = null;
    _model = null;
    _tokenizer = null;

    constructor(modelUrl, tokenizerUrl, size) {
        this._modelUrl = modelUrl;
        this._tokenizerUrl = tokenizerUrl;
        this._size = size;
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
        this._tokenizer = new WordPieceTokenizer(resp.data);
    }

    async predict(text) {
        const ids = this._tokenizer.encode(text);

        // The sequence length is a dynamic axis, so a query is never padded
        const inputs = {
            input_ids: new ort.Tensor("int64", BigInt64Array.from(ids, BigInt), [1, ids.length]),
            attention_mask: new ort.Tensor("int64", new BigInt64Array(ids.length).fill(1n), [1, ids.length])
        };

        const results = await this._model.run(inputs);

        return Array.from(
            Object.values(results)
                .find(result => result.size === this._size).data
        );
    }
}
