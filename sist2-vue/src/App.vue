<template>
    <div id="app" :class="getClass()" v-if="!authLoading">
        <NavBar></NavBar>
        <router-view v-if="!configLoading"/>
    </div>
    <div class="loading-page" v-else>
        <div class="loading-spinners">
            <b-spinner type="grow" variant="primary"></b-spinner>
            <b-spinner type="grow" variant="primary"></b-spinner>
            <b-spinner type="grow" variant="primary"></b-spinner>
        </div>
        <div class="loading-text">
            Loading • Chargement • 装载 • Wird geladen • Ładowanie
        </div>
    </div>
</template>

<script>
import NavBar from "@/components/NavBar.vue";
import {mapActions, mapGetters, mapMutations} from "vuex";
import Sist2Api from "@/Sist2Api";
import {setupAuth0} from "@/main";
import Sist2ElasticsearchQuery from "@/Sist2ElasticsearchQuery";
import Sist2SqliteQuery from "@/Sist2SqliteQuery";

export default {
    components: {NavBar},
    data() {
        return {
            configLoading: false,
            authLoading: true,
            sist2InfoLoading: true
        }
    },
    computed: {
        ...mapGetters(["optTheme"]),
    },
    mounted() {
        this.$store.dispatch("loadConfiguration").then(() => {
            this.$root.$i18n.locale = this.$store.state.optLang;
        });

        this.$store.subscribe((mutation) => {
            if (mutation.type === "setOptLang") {
                this.$root.$i18n.locale = mutation.payload;
                this.configLoading = true;
                window.setTimeout(() => this.configLoading = false, 10);
            }

            if (mutation.type === "setAuth0Token") {
                this.authLoading = false;
            }
        });

        Sist2Api.getSist2Info().then(data => {

            if (data.auth0Enabled) {
                this.authLoading = true;
                setupAuth0(data.auth0Domain, data.auth0ClientId, data.auth0Audience)

                this.$auth.$watch("loading", loading => {
                    if (loading === false) {

                        if (!this.$auth.isAuthenticated) {
                            this.$auth.loginWithRedirect();
                            return;
                        }

                        // Remove "code" param
                        window.history.replaceState({}, "", "/" + window.location.hash);

                        this.$store.dispatch("loadAuth0Token");
                    }
                });
            } else {
                this.authLoading = false;
            }

            this.setSist2Info(data);
            this.setIndices(data.indices)

            if (Sist2Api.backend() === "sqlite") {
                Sist2Api.init(Sist2SqliteQuery.searchQuery);
                this.$store.commit("setUiSqliteMode", true);
            } else {
                Sist2Api.init(Sist2ElasticsearchQuery.searchQuery);
            }
        });
    },
    methods: {
        ...mapActions(["setSist2Info",]),
        ...mapMutations(["setIndices",]),
        getClass() {
            return {
                "theme-light": this.optTheme === "light",
                "theme-black": this.optTheme === "black",
            }
        }
    }
    ,
}
</script>

<style>
html, body {
    height: 100%;
}

#app {
    /*font-family: Avenir, Helvetica, Arial, sans-serif;*/
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    /*text-align: center;*/
    color: #2c3e50;
    padding-bottom: 1em;
    min-height: 100%;
}

/*Black theme*/
.theme-black {
    background-color: #000;
}

.theme-black .card, .theme-black .modal-content {
    background: #212121;
    color: #e0e0e0;
    border-radius: 1px;
    border: none;
}


.theme-black .table {
    color: #e0e0e0;
}

.theme-black .table td, .theme-black .table th {
    border: none;
}

.theme-black .table thead th {
    border-bottom: 1px solid #646464;
}

.theme-black .custom-select {
    overflow: auto;
    background-color: #37474F;
    border: 1px solid #616161;
    color: #bdbdbd;
}

.theme-black .custom-select:focus {
    border-color: #757575;
    outline: 0;
    box-shadow: 0 0 0 .2rem rgba(0, 123, 255, .25);
}

.theme-black .inspire-tree .selected > .wholerow, .theme-black .inspire-tree .selected > .title-wrap:hover + .wholerow {
    background: none !important;
}

.theme-black .inspire-tree .icon-expand::before, .theme-black .inspire-tree .icon-collapse::before {
    background-color: black !important;
}

.theme-black .inspire-tree .title {
    color: #eee;
}

.theme-black .inspire-tree {
    font-weight: 400;
    font-size: 14px;
    font-family: Helvetica, Nueue, Verdana, sans-serif;
    max-height: 350px;
    overflow: auto;
}

.inspire-tree [type=checkbox] {
    left: 22px !important;
    top: 7px !important;
}

.theme-black .form-control {
    background-color: #37474F;
    border: 1px solid #616161;
    color: #dbdbdb !important;
}

.theme-black .form-control:focus {
    background-color: #546E7A;
    color: #fff;
}

.theme-black .input-group-text, .theme-black .default-input {
    background: #37474F !important;
    border: 1px solid #616161 !important;
    color: #dbdbdb !important;
}

.theme-black ::placeholder {
    color: #BDBDBD !important;
    opacity: 1;
}

.theme-black .nav-tabs .nav-link {
    color: #e0e0e0;
    border-radius: 0;
}

.theme-black .nav-tabs .nav-item.show .nav-link, .theme-black .nav-tabs .nav-link.active {
    background-color: #212121;
    border-color: #616161 #616161 #212121;
    color: #e0e0e0;
}

.theme-black .nav-tabs .nav-link:focus, .theme-black .nav-tabs .nav-link:hover {
    border-color: #e0e0e0 #e0e0e0 #212121;
    color: #e0e0e0;
}

.theme-black .nav-tabs {
    border-bottom: #616161;
}

.theme-black a:hover, .theme-black .btn:hover {
    color: #fff;
}

.theme-black .b-dropdown a:hover {
    color: inherit;
}

.theme-black .btn {
    color: #eee;
}

.theme-black .modal-header .close {
    color: #e0e0e0;
    text-shadow: none;
}

.theme-black .modal-header {
    border-bottom: 1px solid #646464;
}

/* -------------------------- */

#nav {
    padding: 30px;
}

#nav a {
    font-weight: bold;
    color: #2c3e50;
}

#nav a.router-link-exact-active {
    color: #42b983;
}

.mobile {
    display: none;
}

.container {
    padding-top: 1em;
}

@media (max-width: 650px) {
    .mobile {
        display: initial;
    }

    .not-mobile {
        display: none;
    }

    .grid-single-column .fit {
        max-height: none !important;
    }

    .container {
        padding-left: 0;
        padding-right: 0;
        padding-top: 0
    }

    .lightbox-caption {
        display: none;
    }
}

.info-icon {
    width: 1rem;
    min-width: 1rem;
    margin-right: 0.2rem;
    cursor: pointer;
    line-height: 1rem;
    height: 1rem;
    min-height: 1rem;
    background-image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHhtbG5zOnhsaW5rPSJodHRwOi8vd3d3LnczLm9yZy8xOTk5L3hsaW5rIiB4PSIwcHgiIHk9IjBweCIKICAgICB2aWV3Qm94PSIwIDAgNDI2LjY2NyA0MjYuNjY3IiBzdHlsZT0iZW5hYmxlLWJhY2tncm91bmQ6bmV3IDAgMCA0MjYuNjY3IDQyNi42Njc7IiBmaWxsPSIjZmZmIj4KPGc+CiAgICA8Zz4KICAgICAgICA8Zz4KICAgICAgICAgICAgPHJlY3QgeD0iMTkyIiB5PSIxOTIiIHdpZHRoPSI0Mi42NjciIGhlaWdodD0iMTI4Ii8+CiAgICAgICAgICAgIDxwYXRoIGQ9Ik0yMTMuMzMzLDBDOTUuNDY3LDAsMCw5NS40NjcsMCwyMTMuMzMzczk1LjQ2NywyMTMuMzMzLDIxMy4zMzMsMjEzLjMzM1M0MjYuNjY3LDMzMS4yLDQyNi42NjcsMjEzLjMzMwogICAgICAgICAgICAgICAgUzMzMS4yLDAsMjEzLjMzMywweiBNMjEzLjMzMywzODRjLTk0LjA4LDAtMTcwLjY2Ny03Ni41ODctMTcwLjY2Ny0xNzAuNjY3UzExOS4yNTMsNDIuNjY3LDIxMy4zMzMsNDIuNjY3CiAgICAgICAgICAgICAgICBTMzg0LDExOS4yNTMsMzg0LDIxMy4zMzNTMzA3LjQxMywzODQsMjEzLjMzMywzODR6Ii8+CiAgICAgICAgICAgIDxyZWN0IHg9IjE5MiIgeT0iMTA2LjY2NyIgd2lkdGg9IjQyLjY2NyIgaGVpZ2h0PSI0Mi42NjciLz4KICAgICAgICA8L2c+CiAgICA8L2c+CjwvZz4KPC9zdmc+Cg==);
    filter: brightness(45%);
    display: block;
}

.theme-black .info-icon {
    filter: brightness(80%);
}

.tabs {
    margin-top: 10px;
}

.modal-title {
    text-overflow: ellipsis;
    overflow: hidden;
    white-space: nowrap;
}

@media screen and (min-width: 1500px) {
    .container {
        max-width: 1440px;
    }
}

.noUi-connects {
    border-radius: 1px !important;
}

mark {
    background: #fff217;
    border-radius: 0;
    padding: 1px 0;
    color: inherit;
}

.theme-black mark {
    background: rgba(251, 191, 41, 0.25);
    border-radius: 0;
    padding: 1px 0;
    color: inherit;
}

.theme-black .content-div mark {
    background: rgba(251, 191, 41, 0.40);
    color: white;
}

.content-div mark.current-hit {
    outline: 2px solid #FF9800;
}

.content-div {
    font-family: SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
    font-size: 13px;
    padding: 1em;
    background-color: #f5f5f5;
    border: 1px solid #ccc;
    border-radius: 4px;
    margin: 3px;
    white-space: normal;
    color: #000;
    overflow: hidden;
}

.theme-black .content-div {
    background-color: #37474F;
    border: 1px solid #616161;
    color: #E0E0E0FF;
}

.graph {
    display: inline-block;
    width: 40%;
}

.pointer {
    cursor: pointer;
}

.loading-page {
    display: flex;
    justify-content: center;
    align-items: center;
    flex-direction: column;
    height: 100%;
    gap: 15px
}

.loading-spinners {
    display: flex;
    gap: 10px;
}

.loading-text {
    text-align: center;
}
</style>
