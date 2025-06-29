<template>
  <b-navbar>
    <b-navbar-brand to="/">
      <Sist2Icon></Sist2Icon>
    </b-navbar-brand>
    
    <span class="badge badge-pill version" v-if="$store && $store.state.sist2Info">
      v{{ sist2Version() }}
    </span>
      
    <b-button class="ml-auto" to="/task" variant="link">{{ $t("tasks") }}</b-button>
  </b-navbar>
</template>

<script>
import Sist2Icon from "@/components/icons/Sist2Icon";

export default {
  name: "NavBar",
  components: {Sist2Icon},
  methods: {
    tagline() {
      return this.$store.state.sist2Info.tagline;
    },
    sist2Version() {
      return this.$store.state.sist2Info.version;
    },
    isDebug() {
      return this.$store.state.sist2Info.debug;
    },
    isLegacy() {
      return this.$store.state.sist2Info.esVersionLegacy;
    },
    hideLegacy() {
      return this.$store.state.optHideLegacy;
    }
  }
}
</script>

<style scoped>
.navbar {
  box-shadow: 0 0.125rem 0.25rem rgb(0 0 0 / 8%) !important;
  border-radius: 0;
}

.theme-black .navbar {
  background: #546b7a30;
  border-bottom: none;
}

.navbar-brand {
  color: #222 !important;
  font-size: 1.75rem;
  padding: 0;
}

.navbar-brand:hover {
  color: #000 !important;
}

.version {
  color: #222 !important;
  margin-left: -18px;
  margin-top: -14px;
  font-size: 11px;
  font-family: monospace;
}

.btn-link {
  color: #222;
}
</style>
