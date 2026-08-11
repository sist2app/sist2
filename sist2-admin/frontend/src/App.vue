<template>
    <NavBar/>
    <div class="container mt-3 mb-5">
        <component v-if="store.info !== null" :is="currentView" :key="viewKey"/>
    </div>
</template>

<style>
#app {
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    color: #2c3e50;
    padding-bottom: 1em;
    min-height: 100%;
}

@media screen and (min-width: 1500px) {
    .container {
        max-width: 1440px;
    }
}

.form-label {
    margin-top: 0.5rem;
    margin-bottom: 0;
}
</style>

<script setup>
import { computed, onMounted } from "vue";

import NavBar from "./components/NavBar.vue";
import Home from "./views/Home.vue";
import Job from "./views/Job.vue";
import Frontend from "./views/Frontend.vue";
import SearchBackend from "./views/SearchBackend.vue";
import UserScript from "./views/UserScript.vue";
import Tasks from "./views/Tasks.vue";
import Tail from "./views/Tail.vue";
import { route } from "./router.js";
import { loadInfo, startNotificationStream, store } from "./store.js";

const VIEWS = {
    home: Home,
    job: Job,
    frontend: Frontend,
    searchBackend: SearchBackend,
    userScript: UserScript,
    tasks: Tasks,
    tail: Tail
};

const currentView = computed(() => VIEWS[route.name]);
const viewKey = computed(() => JSON.stringify([route.name, route.params]));

onMounted(() => {
    loadInfo();
    startNotificationStream();
});
</script>
