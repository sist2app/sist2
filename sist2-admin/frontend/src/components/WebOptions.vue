<template>
    <div class="card mb-3">
        <div class="card-header">Web options</div>
        <div class="card-body">
            <div class="row">
                <div class="col-md-6 mb-2 d-flex flex-column justify-content-end">
                    <label class="form-label">UI Language</label>
                    <select class="form-select" v-model="options.lang">
                        <option value="en">en</option>
                        <option value="fr">fr</option>
                        <option value="zh-CN">zh-CN</option>
                        <option value="pl">pl</option>
                        <option value="de">de</option>
                    </select>
                </div>
                <div class="col-md-6 mb-2 d-flex flex-column justify-content-end">
                    <label class="form-label">Listen address</label>
                    <input class="form-control" v-model="options.bind">
                </div>
            </div>
            <div class="mb-2">
                <label class="form-label">Tagline in navbar</label>
                <input class="form-control" v-model="options.tagline">
            </div>
            <div class="mb-2">
                <label class="form-label">Basic auth in user:password format</label>
                <input class="form-control" v-model="options.auth" :disabled="!!options.tag_auth">
            </div>
            <div class="mb-2">
                <label class="form-label">Basic auth in user:password format for tagging</label>
                <input class="form-control" v-model="options.tag_auth" :disabled="!!options.auth">
            </div>
            <div class="form-check form-switch mb-2">
                <input class="form-check-input" type="checkbox" id="webVerbose" v-model="options.verbose">
                <label class="form-check-label" for="webVerbose">Verbose logs</label>
            </div>

            <div class="row">
                <div class="col-md-6 mb-2 d-flex flex-column justify-content-end">
                    <label class="form-label">Auth0 audience</label>
                    <input class="form-control" v-model="options.auth0_audience">
                </div>
                <div class="col-md-6 mb-2 d-flex flex-column justify-content-end">
                    <label class="form-label">Auth0 domain</label>
                    <input class="form-control" v-model="options.auth0_domain">
                </div>
            </div>
            <div class="mb-2">
                <label class="form-label">Auth0 client ID</label>
                <input class="form-control" v-model="options.auth0_client_id">
            </div>
            <div class="mb-2">
                <label class="form-label">Auth0 public key</label>
                <textarea class="form-control" rows="3" v-model="options.auth0_public_key"></textarea>
            </div>
        </div>
    </div>
</template>

<script setup>
defineProps({
    options: { type: Object, required: true }
});
</script>
