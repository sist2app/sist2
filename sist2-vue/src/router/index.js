import Vue from "vue"
import VueRouter from "vue-router"
import StatsPage from "../views/StatsPage.vue"
import Configuration from "../views/Configuration.vue"
import SearchPage from "@/views/SearchPage.vue";
import FilePage from "@/views/FilePage.vue";
import {authGuard as auth0AuthGuard} from "@/router/auth0";

Vue.use(VueRouter)

let USE_AUTH0 = false
export function setUseAuth0(val) {
    USE_AUTH0 = val;
}

const authGuard = (to, from, next) => {
    if (USE_AUTH0) {
        return auth0AuthGuard(to, from, next);
    }

    next();
}

const routes = [
    {
        path: "/",
        name: "SearchPage",
        component: SearchPage,
        beforeEnter: authGuard
    },
    {
        path: "/stats",
        name: "Stats",
        component: StatsPage
    },
    {
        path: "/config",
        name: "Configuration",
        component: Configuration
    },
    {
        path: "/file",
        name: "File",
        component: FilePage
    }
]

const router = new VueRouter({
    mode: "hash",
    base: import.meta.env.BASE_URL,
    routes,
    scrollBehavior(to, from, savedPosition) {
        // return desired position
    }
})

export default router
