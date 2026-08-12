<template>
  <b-table :items="tableItems" small borderless responsive="md" thead-class="hidden" class="mb-0 mt-4">
    <template #cell(value)="data">
      <span v-if="'html' in data.item" v-html="data.item.html"></span>
      <span v-else>{{ data.value }}</span>
    </template>
  </b-table>
</template>

<script>
import {humanDate, humanFileSize} from "@/util";

function makeGpsLink(latitude, longitude) {

  const lat = Number(latitude);
  const lon = Number(longitude);

  if (!isFinite(lat) || !isFinite(lon)) {
    return "";
  }

  return `<a target="_blank" href="https://maps.google.com/?q=${lat},${lon}&ll=${lat},${lon}&t=k&z=17">${lat}, ${lon}</a>`;
}

function dmsToDecimal(dms, ref) {
  const tokens = dms.split(",")

  const d = Number(tokens[0].trim().split(":")[0]) / Number(tokens[0].trim().split(":")[1])
  const m = Number(tokens[1].trim().split(":")[0]) / Number(tokens[1].trim().split(":")[1])
  const s = Number(tokens[2].trim().split(":")[0]) / Number(tokens[2].trim().split(":")[1])

  return (d + (m / 60) + (s / 3600)) * (ref === "S" || ref === "W" ? -1 : 1)
}

export default {
  name: "InfoTable",
  props: ["doc"],
  data() {
    return {
      indexName: "loading..."
    }
  },
  computed: {
    tableItems() {
      this.indexName;
      const src = this.doc._source;

      const items = [
        {key: "index", value: `[${this.indexName}]`},
        {key: "mtime", value: humanDate(src.mtime)},
        {key: "mime", value: src.mime},
        {key: "size", value: humanFileSize(src.size)},
        {key: "path", value: src.path},
      ];

      if ("width" in this.doc._source) {
        items.push({
          key: "image size",
          value: `${src.width}x${src.height}`
        })
      }

      const fields = [
        "title", "duration", "audioc", "videoc",
        "bitrate", "artist", "album", "album_artist", "genre", "font_name", "author", "media_comment",
        "modified_by", "pages", "tag",
        "exif_make", "exif_software", "exif_exposure_time", "exif_fnumber", "exif_focal_length",
        "exif_user_comment", "exif_iso_speed_ratings", "exif_model", "exif_datetime",
        "checksum"
      ];

      fields.forEach(field => {
        if (field in src) {
          items.push({key: field, value: src[field]});
        }
      });

      Object.keys(src).forEach(key => {
        if (key.startsWith("mt_") || key.startsWith("int_")) {
          items.push({key: key, value: src[key]});
        }
      });

      // Exif GPS
      if ("exif_gps_longitude_dec" in src) {
        items.push({
          key: "Exif GPS",
          html: makeGpsLink(src["exif_gps_latitude_dec"], src["exif_gps_longitude_dec"]),
        });
      } else if ("exif_gps_longitude_dms" in src) {
        items.push({
          key: "Exif GPS",
          html: makeGpsLink(
              dmsToDecimal(src["exif_gps_latitude_dms"], src["exif_gps_latitude_ref"]),
              dmsToDecimal(src["exif_gps_longitude_dms"], src["exif_gps_longitude_ref"]),
          ),
        });
      }

      return items;
    }
  },
  mounted() {
    if (this.$store.getters.indexMap[this.doc.index]) {
      this.indexName = this.$store.getters.indexMap[this.doc._source.index].name
    }

    window.setTimeout(() => {
      this.indexName = this.$store.getters.indexMap[this.doc._source.index].name
    }, 500)
  },
}
</script>

<style scoped>

</style>