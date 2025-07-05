<template>

    <b-card>
        <b-card-title>
            <span class="text-monospace">{{ getName() }}</span>
            {{ $t("searchBackendTitle") }}
        </b-card-title>

        <div class="mb-3">
            <b-button variant="danger" @click="deleteBackend()">{{ $t("delete") }}</b-button>
        </div>

        <b-progress v-if="loading" striped animated value="100"></b-progress>
        <b-card-body v-else>

            <label>{{ $t("backendOptions.type") }}</label>
            <b-select :options="backendTypeOptions" v-model="backend.backend_type" @change="update()"></b-select>

            <hr/>

            <template v-if="backend.backend_type === 'elasticsearch'">
                <b-alert :variant="esTestOk ? 'success' : 'danger'" :show="showEsTestAlert" class="mt-1">
                    {{ esTestMessage }}
                </b-alert>

                <label>{{ $t("backendOptions.esUrl") }}</label>
                <b-input-group>
                    <b-form-input v-model="backend.es_url" @change="update()"></b-form-input>
                    <b-input-group-append>
                        <b-button variant="outline-primary" @click="testEs()">{{ $t("test") }}</b-button>
                    </b-input-group-append>
                </b-input-group>

                <b-form-checkbox v-model="backend.es_insecure_ssl" :disabled="!this.backend.es_url.startsWith('https')"
                                 @change="update()">
                    {{ $t("backendOptions.esInsecure") }}
                </b-form-checkbox>

                <label>{{ $t("backendOptions.esIndex") }}</label>
                <b-form-input v-model="backend.es_index" @change="update()"></b-form-input>

                <label>{{ $t("backendOptions.threads") }}</label>
                <b-form-input v-model="backend.threads" type="number" min="1" @change="update()"></b-form-input>

                <label>{{ $t("backendOptions.batchSize") }}</label>
                <b-form-input v-model="backend.batch_size" type="number" min="1" @change="update()"></b-form-input>

                <label>{{ $t("backendOptions.esMappings") }}</label>
                <b-form-textarea v-model="backend.es_mappings" rows="4" @change="update()"></b-form-textarea>

                <label>{{ $t("backendOptions.esSettings") }}</label>
                <b-form-textarea v-model="backend.es_settings" rows="4" @change="update()"></b-form-textarea>
            </template>
            <template v-else>
                <label>{{ $t("backendOptions.searchIndex") }}</label>
                <b-form-input v-model="backend.search_index" disabled></b-form-input>
            </template>
        </b-card-body>

    </b-card>
</template>

<script>
import sist2AdminApi from "@/Sist2AdminApi";
import Sist2AdminApi from "@/Sist2AdminApi";

export default {
    name: "SearchBackend",
    data() {
        return {
            showEsTestAlert: false,
            esTestOk: false,
            esTestMessage: "",
            loading: true,
            backend: null,
            backendTypeOptions: [
                {
                    text: "Elasticsearch",
                    value: "elasticsearch"
                },
                {
                    text: "SQLite",
                    value: "sqlite"
                }
            ]
        }
    },
    mounted() {
        Sist2AdminApi.getSearchBackend(this.getName()).then(resp => {
            this.backend = resp.data;
            this.loading = false;
        });
    },
    methods: {
        getName() {
            return this.$route.params.name;
        },
        testEs() {
            sist2AdminApi.pingEs(this.backend.es_url, this.backend.es_insecure_ssl)
                .then((resp) => {
                    this.showEsTestAlert = true;
                    this.esTestOk = resp.data.ok;
                    this.esTestMessage = resp.data.message;
                });
        },
        update() {
            Sist2AdminApi.updateSearchBackend(this.getName(), this.backend);
        },
        deleteBackend() {
            Sist2AdminApi.deleteBackend(this.getName())
                .then(() => {
                    this.$router.push("/");
                })
                .catch(err => {
                    this.$bvToast.toast("Cannot delete search backend " +
                        "because it is referenced by a job or frontend", {
                        title: "Error",
                        variant: "danger",
                        toaster: "b-toaster-bottom-right"
                    });
                })
        }
    }
}
</script>

<style scoped>

</style>