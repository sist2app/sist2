<template>
    <div>
        <b-btn style="float:right;margin-bottom: 10px" @click="downloadTreemap()" variant="primary">
            {{ $t("download") }}
        </b-btn>
        <svg id="treemap"></svg>
    </div>
</template>

<script>
import * as d3 from "d3";
import {humanFileSize, burrow} from "@/util";
import Sist2Api from "@/Sist2Api";
import domtoimage from "dom-to-image";


const TILING_MODES = {
    "squarify": d3.treemapSquarify,
    "binary": d3.treemapBinary,
    "sliceDice": d3.treemapSliceDice,
    "slice": d3.treemapSlice,
    "dice": d3.treemapDice,
};

const COLORS = {
    "PuBuGn": d3.interpolatePuBuGn,
    "PuRd": d3.interpolatePuRd,
    "PuBu": d3.interpolatePuBu,
    "YlOrBr": d3.interpolateYlOrBr,
    "YlOrRd": d3.interpolateYlOrRd,
    "YlGn": d3.interpolateYlGn,
    "YlGnBu": d3.interpolateYlGnBu,
    "Plasma": d3.interpolatePlasma,
    "Magma": d3.interpolateMagma,
    "Inferno": d3.interpolateInferno,
    "Viridis": d3.interpolateViridis,
    "Turbo": d3.interpolateTurbo,
};

const SIZES = {
    "small": [800, 600],
    "medium": [1300, 750],
    "large": [1900, 900],
    "x-large": [2800, 1700],
    "xx-large": [3600, 2000],
};


const uids = {};

function uid(name) {
    let id = uids[name] || 0;
    uids[name] = id + 1;
    return name + id;
}

function cascade(root, offset) {
    const x = new Map;
    const y = new Map;
    return root.eachAfter(d => {
        if (d.children && d.children.length !== 0) {
            x.set(d, 1 + d3.max(d.children, c => c.x1 === d.x1 - offset ? x.get(c) : NaN));
            y.set(d, 1 + d3.max(d.children, c => c.y1 === d.y1 - offset ? y.get(c) : NaN));
        } else {
            x.set(d, 0);
            y.set(d, 0);
        }
    }).eachBefore(d => {
        d.x1 -= 2 * offset * x.get(d);
        d.y1 -= 2 * offset * y.get(d);
    });
}

function cascadeTreemap(data, svg, width, height, tilingMode, treemapColor) {
    const root = cascade(
        d3.treemap()
            .size([width, height])
            .tile(TILING_MODES[tilingMode])
            .paddingOuter(3)
            .paddingTop(16)
            .paddingInner(1)
            .round(true)(
                d3.hierarchy(data)
                    .sum(d => d.value)
                    .sort((a, b) => b.value - a.value)
            ),
        3 // treemap.paddingOuter
    );

    const maxDepth = Math.max(...root.descendants().map(d => d.depth));
    const color = d3.scaleSequential([maxDepth, -1], COLORS[treemapColor]);

    svg.append("filter")
        .attr("id", "shadow")
        .append("feDropShadow")
        .attr("flood-opacity", 0.3)
        .attr("dx", 0)
        .attr("stdDeviation", 3);

    const node = svg.selectAll("g")
        .data(
            d3.groups(root.descendants(), d => d.depth)
                .sort(([depthA], [depthB]) => d3.ascending(depthA, depthB))
        )
        .join("g")
        .attr("filter", "url(#shadow)")
        .selectAll("g")
        .data(([, nodes]) => nodes)
        .join("g")
        .attr("transform", d => `translate(${d.x0},${d.y0})`);

    node.append("title")
        .text(d => `${d.ancestors().reverse().splice(1).map(d => d.data.name).join("/")}\n${humanFileSize(d.value)}`);

    node.append("rect")
        .attr("id", d => (d.nodeUid = uid("node")))
        .attr("fill", d => color(d.depth))
        .attr("width", d => d.x1 - d.x0)
        .attr("height", d => d.y1 - d.y0);

    node.append("clipPath")
        .attr("id", d => (d.clipUid = uid("clip")))
        .append("use")
        .attr("href", d => `#${d.nodeUid}`);

    node.append("text")
        .attr("fill", d => d3.hsl(color(d.depth)).l > .5 ? "#333" : "#eee")
        .attr("clip-path", d => `url(#${d.clipUid})`)
        .selectAll("tspan")
        .data(d => [d.data.name, humanFileSize(d.value)])
        .join("tspan")
        .text(d => d);

    node.filter(d => d.children).selectAll("tspan")
        .attr("dx", 3)
        .attr("y", 13);

    node.filter(d => !d.children).selectAll("tspan")
        .attr("x", 3)
        .attr("y", (d, i) => `${i === 0 ? 1.1 : 2.3}em`);
}

function flatTreemap(data, svg, width, height, groupingDepth, tilingMode, fillOpacity) {
    const ordinalColor = d3.scaleOrdinal(d3.schemeCategory10);

    const root = d3.treemap()
        .tile(TILING_MODES[tilingMode])
        .size([width, height])
        .padding(1)
        .round(true)(
            d3.hierarchy(data)
                .sum(d => d.value)
                .sort((a, b) => b.value - a.value)
        );

    const leaf = svg.selectAll("g")
        .data(root.leaves())
        .join("g")
        .attr("transform", d => `translate(${d.x0},${d.y0})`);

    leaf.append("title")
        .text(d => `${d.ancestors().reverse().map(d => d.data.name).join("/")}\n${humanFileSize(d.value)}`);

    leaf.append("rect")
        .attr("id", d => (d.leafUid = uid("leaf")))
        .attr("fill", d => {
            while (d.depth > groupingDepth) d = d.parent;
            return ordinalColor(d.data.name);
        })
        .attr("fill-opacity", fillOpacity)
        .attr("width", d => d.x1 - d.x0)
        .attr("height", d => d.y1 - d.y0);

    leaf.append("clipPath")
        .attr("id", d => (d.clipUid = uid("clip")))
        .append("use")
        .attr("href", d => `#${d.leafUid}`);

    leaf.append("text")
        .attr("clip-path", d => `url(#${d.clipUid})`)
        .selectAll("tspan")
        .data(d => {
            if (d.data.name === ".") {
                d = d.parent;
            }
            return [d.data.name, humanFileSize(d.value)]
        })
        .join("tspan")
        .attr("x", 2)
        .attr("y", (d, i) => `${i === 0 ? 1.1 : 2.3}em`)
        .text(d => d);
}

function exportTreemap(indexName, width, height) {
    domtoimage.toBlob(document.getElementById("treemap"), {width: width, height: height})
        .then(function (blob) {
            let a = document.createElement("a");
            let url = URL.createObjectURL(blob);

            a.href = url;
            a.download = `${indexName}_treemap.png`;
            document.body.appendChild(a);
            a.click();
            setTimeout(function () {
                document.body.removeChild(a);
                window.URL.revokeObjectURL(url);
            }, 0);
        });
}

export default {
    name: "D3Treemap",
    props: ["indexId"],
    watch: {
        indexId: function () {
            this.update(this.indexId);
        }
    },
    mounted() {
        this.update(this.indexId);
    },
    methods: {
        update(indexId) {
            const width = SIZES[this.$store.state.optTreemapSize][0];
            const height = SIZES[this.$store.state.optTreemapSize][1];
            const tilingMode = this.$store.state.optTreemapTiling;
            const groupingDepth = this.$store.state.optTreemapColorGroupingDepth;
            const treemapColor = this.$store.state.optTreemapColor;
            const treemapType = this.$store.state.optTreemapType;

            const treemapSvg = d3.select("#treemap");

            treemapSvg.selectAll("*").remove();
            treemapSvg.attr("viewBox", [0, 0, width, height])
                .attr("xmlns", "http://www.w3.org/2000/svg")
                .attr("xmlns:xlink", "http://www.w3.org/1999/xlink")
                .attr("version", "1.1")
                .style("overflow", "visible")
                .style("font", "10px sans-serif");

            d3.json(Sist2Api.getTreemapStat(indexId)).then(tabularData => {
                tabularData.forEach(row => {
                    row.taxonomy = row.path.split("/");
                    row.size = Number(row.size);
                });

                if (treemapType === "cascaded") {
                    const data = burrow(tabularData, false);
                    cascadeTreemap(data, treemapSvg, width, height, tilingMode, treemapColor);
                } else {
                    const data = burrow(tabularData.sort((a, b) => b.taxonomy.length - a.taxonomy.length), true);
                    const fillOpacity = this.$store.state.optTheme === "black" ? 0.9 : 0.6;
                    flatTreemap(data, treemapSvg, width, height, groupingDepth, tilingMode, fillOpacity);
                }
            });
        },
        downloadTreemap() {
            const width = SIZES[this.$store.state.optTreemapSize][0];
            const height = SIZES[this.$store.state.optTreemapSize][1];

            exportTreemap(this.indexId, width, height);
        }
    }
}
</script>