<template>
    <div id="app">
        <NavBar></NavBar>
        <b-container class="pt-4">
            <b-alert show dismissible variant="info">
                This is a beta version of sist2-admin. Please submit bug reports, usability issues and feature requests
                to the <a href="https://github.com/sist2app/sist2/issues/new/choose" target="_blank">issue tracker on
                Github</a>. Thank you!
            </b-alert>
            <router-view v-if="$store.state.sist2AdminInfo"/>
        </b-container>
    </div>
</template>

<script>
import NavBar from "@/components/NavBar";
import Sist2AdminApi from "@/Sist2AdminApi";

export default {
    components: {NavBar},
    data() {
        return {
            socket: null
        }
    },
    mounted() {
        Sist2AdminApi.getSist2AdminInfo()
            .then(resp => this.$store.commit("setSist2AdminInfo", resp.data));
        this.$store.dispatch("loadBrowserSettings");
        this.connectNotifications();
        // this.socket.onclose = this.connectNotifications;
    },
    methods: {
        connectNotifications() {
            if (window.location.protocol === "https:") {
                this.socket = new WebSocket(`wss://${window.location.host}/notifications`);
            } else {
                this.socket = new WebSocket(`ws://${window.location.host}/notifications`);
            }
            this.socket.onopen = () => {
                this.socket.send("Hello from client");
            }

            this.socket.onmessage = e => {
                const notification = JSON.parse(e.data);
                if (notification.message) {
                    notification.messageString = this.$t(notification.message).toString();
                }

                this.$store.dispatch("notify", notification)
            }
        }
    }
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

label {
    margin-top: 0.5rem;
    margin-bottom: 0;
}
</style>
