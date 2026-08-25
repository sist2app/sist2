<template>
  <b-modal :visible="show" size="lg" :hide-footer="true" static :title="$t('help.help')"
           @close="$emit('close')"
           @hide="$emit('close')"
  >
    <h2>{{$t("help.simpleSearch")}}</h2>

    <table class="table">
      <tbody>
      <tr>
        <td><code>+</code></td>
        <td>{{$t("help.and")}}</td>
      </tr>
      <tr>
        <td><code>|</code></td>
        <td>{{$t("help.or")}}</td>
      </tr>
      <tr>
        <td><code>-</code></td>
        <td>{{$t("help.not")}}</td>
      </tr>
      <tr>
        <td><code>""</code></td>
        <td>{{$t("help.quotes")}}</td>
      </tr>
      <tr>
        <td><code>{{$t("help.term")}}*</code></td>
        <td>{{$t("help.prefix")}}</td>
      </tr>
      <tr>
        <td><code>(</code> {{$t("and")}} <code>)</code></td>
        <td>{{$t("help.parens")}}</td>
      </tr>
      <tr>
        <td><code>{{$t("help.term")}}~N</code></td>
        <td>{{$t("help.tildeTerm")}}</td>
      </tr>
      <tr>
        <td><code>"..."~N</code></td>
        <td>{{$t("help.tildePhrase")}}</td>
      </tr>
      </tbody>
    </table>

    <p v-html="$t('help.example1')"></p>

    <p v-html="$t('help.defaultOperator')"></p>

    <p v-html="$store.state.uiSqliteMode ? $t('help.fuzzySqlite') : $t('help.fuzzy')"></p>

    <br>

    <p v-html="$t('help.moreInfoSimple')"></p>

    <p></p>

    <h2>{{$t("help.advancedSearch")}}</h2>
    <p v-html="$t('help.moreInfoAdvanced')"></p>

  </b-modal>
</template>

<script>
export default {
  name: "HelpDialog",
  props: ["show"]
}
</script>

<style scoped>

</style>