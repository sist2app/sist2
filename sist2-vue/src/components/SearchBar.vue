<template>
  <div>
    <b-input-group>
      <b-form-input :value="searchText"
                    :placeholder="advanced() ? $t('searchBar.advanced') : $t('searchBar.simple')"
                    @input="setSearchText($event)"></b-form-input>

      <template #prepend>
        <b-input-group-text v-if="fuzzyAvailable">
          <b-form-checkbox :checked="fuzzy" title="Toggle fuzzy searching" @change="setFuzzy($event)">
            {{ $t("searchBar.fuzzy") }}
          </b-form-checkbox>
        </b-input-group-text>
      </template>

      <template #append>
        <b-button variant="outline-secondary" @click="$emit('show-help')">{{$t("help.help")}}</b-button>
      </template>
    </b-input-group>
  </div>
</template>

<script>
import {mapGetters, mapMutations} from "vuex";

export default {
  computed: {
    ...mapGetters({
      optQueryMode: "optQueryMode",
      searchText: "searchText",
      fuzzy: "fuzzy",
      fuzzyAvailable: "fuzzyAvailable",
    }),
  },
  methods: {
    ...mapMutations({
      setSearchText: "setSearchText",
      setFuzzy: "setFuzzy"
    }),
    advanced() {
      return this.optQueryMode === "advanced"
    }
  }
}
</script>
<style>
</style>