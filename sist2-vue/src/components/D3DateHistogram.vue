<template>
  <div class="graph">
    <svg id="date-histogram"></svg>
  </div>
</template>

<script>
import * as d3 from "d3";
import Sist2Api from "@/Sist2Api";
import {dateHistogramBins} from "@/util";

const formatSI = d3.format("~s");


function dateHistogram(data, svg, title) {
  const bins = dateHistogramBins(data);

  const margin = {
    top: 50,
    right: 20,
    bottom: 70,
    left: 40
  };

  const width = 550;
  const height = 450;

  svg.selectAll("*").remove();
  svg.attr("viewBox", [0, 0, width, height]);

  const y = d3.scaleLinear()
      .domain([0, d3.max(bins, d => d.length)]).nice()
      .range([height - margin.bottom, margin.top]);

  const x = d3.scaleLinear()
      .domain(d3.extent(bins, d => d.x0)).nice()
      .range([margin.left, width - margin.right]);

  svg.append("g")
      .attr("fill", "steelblue")
      .selectAll("rect")
      .data(bins)
      .join("rect")
      .attr("x", d => x(d.x0) + 1)
      .attr("width", d => Math.max(1, x(d.x1) - x(d.x0) - 1))
      .attr("y", d => y(d.length))
      .attr("height", d => y(0) - y(d.length))
      .call(g => g
          .append("title")
          .text(d => d.length)
      );

  svg.append("g")
      .attr("transform", `translate(0,${height - margin.bottom})`)
      .call(
          d3.axisBottom(x)
              .ticks(width / 30)
              .tickSizeOuter(0)
              .tickFormat(t => d3.timeFormat("%Y-%m-%d")(d3.utcParse("%s")(t)))
      )
      .call(g => g
          .selectAll("text")
          .style("text-anchor", "end")
          .attr("dx", "-.8em")
          .attr("dy", ".15em")
          .attr("transform", "rotate(-65)")
      )
      .call(g => g.append("text")
          .attr("x", width - margin.right)
          .attr("y", -4)
          .attr("fill", "currentColor")
          .attr("font-weight", "bold")
          .attr("text-anchor", "end")
          .text("mtime")
      );

  svg.append("g")
      .attr("transform", `translate(${margin.left},0)`)
      .call(
          d3.axisLeft(y)
              .ticks(height / 40)
              .tickFormat(t => formatSI(t))
      )
      .call(g => g.select(".domain").remove())
      .call(g => g.select(".tick:last-of-type text").clone()
          .attr("x", 4)
          .attr("text-anchor", "start")
          .attr("font-weight", "bold")
          .text("File count"));

  svg.append("text")
      .attr("x", (width / 2))
      .attr("y", (margin.top / 2))
      .attr("text-anchor", "middle")
      .style("font-size", "16px")
      .text(title);
}

export default {
  name: "D3DateHistogram",
  props: ["indexId"],
  mounted() {
    this.update(this.indexId);
  },
  watch: {
    indexId: function () {
      this.update(this.indexId);
    },
  },
  methods: {
    update(indexId) {
      const svg = d3.select("#date-histogram");

      d3.json(Sist2Api.getDateStat(indexId)).then(tabularData => {
        dateHistogram(tabularData.slice(), svg, this.$t("d3.dateHistogram"));
      });
    }
  }
}
</script>