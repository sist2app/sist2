export function mergeTooltips(slider, threshold, separator, fixTooltips) {

    const isMobile = window.innerWidth <= 650;
    if (isMobile) {
        threshold = 25;
    }

    const textIsRtl = getComputedStyle(slider).direction === 'rtl';
    const isRtl = slider.noUiSlider.options.direction === 'rtl';
    const isVertical = slider.noUiSlider.options.orientation === 'vertical';
    const tooltips = slider.noUiSlider.getTooltips();
    const origins = slider.noUiSlider.getOrigins();

    // Move tooltips into the origin element. The default stylesheet handles this.
    tooltips.forEach(function (tooltip, index) {
        if (tooltip) {
            origins[index].appendChild(tooltip);
        }
    });

    slider.noUiSlider.on('update', function (values, handle, unencoded, tap, positions) {

        const pools = [[]];
        const poolPositions = [[]];
        const poolValues = [[]];
        let atPool = 0;

        // Assign the first tooltip to the first pool, if the tooltip is configured
        if (tooltips[0]) {
            pools[0][0] = 0;
            poolPositions[0][0] = positions[0];
            poolValues[0][0] = values[0];
        }

        for (let i = 1; i < positions.length; i++) {
            if (!tooltips[i] || (positions[i] - positions[i - 1]) > threshold) {
                atPool++;
                pools[atPool] = [];
                poolValues[atPool] = [];
                poolPositions[atPool] = [];
            }

            if (tooltips[i]) {
                pools[atPool].push(i);
                poolValues[atPool].push(values[i]);
                poolPositions[atPool].push(positions[i]);
            }
        }

        pools.forEach(function (pool, poolIndex) {
            const handlesInPool = pool.length;

            for (let j = 0; j < handlesInPool; j++) {
                const handleNumber = pool[j];

                if (j === handlesInPool - 1) {
                    let offset = 0;

                    poolPositions[poolIndex].forEach(function (value) {
                        offset += 1000 - 10 * value;
                    });

                    const direction = isVertical ? 'bottom' : 'right';
                    const last = isRtl ? 0 : handlesInPool - 1;
                    const lastOffset = 1000 - 10 * poolPositions[poolIndex][last];
                    offset = (textIsRtl && !isVertical ? 100 : 0) + (offset / handlesInPool) - lastOffset;

                    // Center this tooltip over the affected handles
                    tooltips[handleNumber].innerHTML = poolValues[poolIndex].join(separator);
                    tooltips[handleNumber].style.display = 'block';

                    tooltips[handleNumber].style[direction] = offset + '%';
                } else {
                    // Hide this tooltip
                    tooltips[handleNumber].style.display = 'none';
                }
            }
        });

        if (fixTooltips) {
            const isMobile = window.innerWidth <= 650;
            const len = isMobile ? 20 : 5;

            if (positions[0] < len) {
                tooltips[0].style.right = `${(1 - ((positions[0]) / len)) * -35}px`
            } else {
                tooltips[0].style.right = "0"
            }

            if (positions[1] > (100 - len)) {
                tooltips[1].style.right = `${((positions[1] - (100 - len)) / len) * 35}px`
            } else {
                tooltips[1].style.right = "0"
            }
        }
    });
}


export function burrow(table, addSelfDir, rootName) {
    const root = {};
    table.forEach(row => {
        let layer = root;

        row.taxonomy.forEach(key => {
            layer[key] = key in layer ? layer[key] : {};
            layer = layer[key];
        });
        if (Object.keys(layer).length === 0) {
            layer["$size$"] = row.size;
        } else if (addSelfDir) {
            layer["."] = {
                "$size$": row.size,
            };
        }
    });

    const descend = function (obj, depth) {
        return Object.keys(obj).filter(k => k !== "$size$").map(k => {
            const child = {
                name: k,
                depth: depth,
                value: 0,
                children: descend(obj[k], depth + 1)
            };
            if ("$size$" in obj[k]) {
                child.value = obj[k]["$size$"];
            }
            return child;
        });
    };

    return {
        name: rootName,
        children: descend(root, 1),
        value: 0,
        depth: 0,
    }
}


export function ext(hit) {
    return srcExt(hit._source)
}

export function srcExt(src) {
    return Object.prototype.hasOwnProperty.call(src, "extension")
    && src["extension"] !== "" ? "." + src["extension"] : "";
}

export function strUnescape(str) {
    let result = "";

    for (let i = 0; i < str.length; i++) {
        const c = str[i];
        const next = str[i + 1];

        if (c === "]") {
            if (next === "]") {
                result += c;
                i += 1;
            } else {
                result += String.fromCharCode(parseInt(str.slice(i, i + 2), 16));
                i += 2;
            }
        } else {
            result += c;
        }
    }
    return result;
}

const HTML_ESCAPES = {
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    "\"": "&quot;",
    "'": "&#39;"
};

export function escapeHtml(str) {
    return String(str).replace(/[&<>"']/g, (c) => HTML_ESCAPES[c]);
}

// Both backends wrap matches in <mark>; the rest of the fragment is untrusted file data.
export function highlightHtml(str) {
    return escapeHtml(str)
        .replaceAll("&lt;mark&gt;", "<mark>")
        .replaceAll("&lt;/mark&gt;", "</mark>");
}

const thresh = 1000;
const units = ["k", "M", "G", "T", "P", "E", "Z", "Y"];

export function humanFileSize(bytes) {
    if (bytes === 0) {
        return "0 B"
    }

    if (Math.abs(bytes) < thresh) {
        return bytes + ' B';
    }
    let u = -1;
    do {
        bytes /= thresh;
        ++u;
    } while (Math.abs(bytes) >= thresh && u < units.length - 1);

    return bytes.toFixed(1) + units[u];
}

export function humanTime(sec_num) {
    sec_num = Math.floor(sec_num);
    const hours = Math.floor(sec_num / 3600);
    const minutes = Math.floor((sec_num - (hours * 3600)) / 60);
    const seconds = sec_num - (hours * 3600) - (minutes * 60);

    if (sec_num < 60) {
        return `${sec_num}s`
    }

    if (sec_num < 3600) {
        return `${minutes < 10 ? "0" : ""}${minutes}:${seconds < 10 ? "0" : ""}${seconds}`;
    }

    return `${hours < 10 ? "0" : ""}${hours}:${minutes < 10 ? "0" : ""}${minutes}:${seconds < 10 ? "0" : ""}${seconds}`;
}

export function humanDate(numMilis) {
    const date = (new Date(numMilis * 1000));
    return date.getUTCFullYear() + "-" + ("0" + (date.getUTCMonth() + 1)).slice(-2) + "-" + ("0" + date.getUTCDate()).slice(-2)

}

export function lum(c) {
    c = c.substring(1);
    const rgb = parseInt(c, 16);
    const r = (rgb >> 16) & 0xff;
    const g = (rgb >> 8) & 0xff;
    const b = (rgb >> 0) & 0xff;

    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}


export function getSelectedTreeNodes(tree) {
    const selectedNodes = new Set();

    const selected = tree.selected();

    for (let i = 0; i < selected.length; i++) {

        if (selected[i].id === "any") {
            return ["any"]
        }

        //Only get children
        if (selected[i].text.indexOf("(") !== -1) {
            if (selected[i].values) {
                selectedNodes.add(selected[i].values.slice(-1)[0]);
            } else {
                selectedNodes.add(selected[i].id);
            }
        }
    }

    return Array.from(selectedNodes);
}

export function getTreeNodeAttributes(tree) {
    const nodes = tree.selectable();
    const attributes = {};

    for (let i = 0; i < nodes.length; i++) {

        let id = null;

        if (nodes[i].text.indexOf("(") !== -1 && nodes[i].values) {
            id = nodes[i].values.slice(-1)[0];
        } else {
            id = nodes[i].id
        }

        attributes[id] = {
            checked: nodes[i].itree.state.checked,
            collapsed: nodes[i].itree.state.collapsed,
        }
    }

    return attributes;
}


export function serializeMimes(mimes) {
    if (mimes.length === 0) {
        return undefined;
    }
    return mimes.map(mime => compressMime(mime)).join("");
}

export function deserializeMimes(mimeString) {
    return mimeString
        .replaceAll(/([IVATUF])/g, "$$$&")
        .split("$")
        .map(mime => decompressMime(mime))
        .slice(1) // Ignore the first (empty) token
}

export function compressMime(mime) {
    return mime
        .replace("image/", "I")
        .replace("video/", "V")
        .replace("application/", "A")
        .replace("text/", "T")
        .replace("audio/", "U")
        .replace("font/", "F")
        .replace("+", ",")
        .replace("x-", "X")
}

export function decompressMime(mime) {
    return mime
        .replace("I", "image/")
        .replace("V", "video/")
        .replace("A", "application/")
        .replace("T", "text/")
        .replace("U", "audio/")
        .replace("F", "font/")
        .replace(",", "+")
        .replace("X", "x-")
}

export function debounce(fn, wait) {
    let timeout = null;

    return function (...args) {
        window.clearTimeout(timeout);
        timeout = window.setTimeout(() => fn.apply(this, args), wait);
    };
}

export function randomSeed() {
    return Math.round(Math.random() * 100000);
}

export function sid(doc) {
    if (doc._id.includes(".")) {
        return doc._id
    }

    const num = BigInt(doc._id);

    const indexId = (num >> BigInt(32));
    const docId = num & BigInt(0xFFFFFFFF);

    return indexId.toString(16).padStart(8, "0") + "." + docId.toString(16).padStart(8, "0");
}

/** One bucket of the date histogram covers a month, the way the scan aggregates mtimes */
const DATE_HISTOGRAM_BUCKET_SECONDS = 2629800;

/**
 * Bars of the file modification time histogram, oldest first. Every bucket the index reports is
 * a bar: dropping the shorter ones leaves a chart that is empty whenever the counts are level.
 */
export function dateHistogramBins(data) {
    return data
        .map(d => ({
            length: Number(d.count),
            x0: Number(d.bucket),
            x1: Number(d.bucket) + DATE_HISTOGRAM_BUCKET_SECONDS
        }))
        .sort((a, b) => a.x0 - b.x0);
}

const MAX_TERMS = 64;
const DEFAULT_CONTEXT_WORDS = 30;
const MAX_CONTEXT_WORDS = 1000;
// Only words count towards the window, so a passage that is one long run of punctuation would
// otherwise be copied whole, once per hit on the page
const MAX_EXCERPT_LENGTH = 16384;

const QUERY_OPERATORS = ["AND", "OR", "NOT", "NEAR"];

// Word characters, as close to the Elasticsearch tokenizer as this gets: every non-ASCII code point
// belongs to a word, so the split never lands inside one
const WORD = /[0-9A-Za-z_\u{80}-\u{10FFFF}]+/gu;

/**
 * The words of a search query, lowercased, a trailing `*` kept as the prefix match it is. Mirrors
 * highlight_query_terms() in src/web/highlight.c, so that an excerpt the browser marks up reads the
 * way one the SQLite backend marked up does.
 */
export function queryTerms(query) {
    const terms = [];

    if (!query) {
        return terms;
    }

    const re = new RegExp(WORD.source, WORD.flags);

    let match;
    while ((match = re.exec(query)) !== null && terms.length < MAX_TERMS) {
        const next = query[re.lastIndex];

        // `name:term` filters the field, and the field name is not part of the query
        if (next === ":" || QUERY_OPERATORS.includes(match[0])) {
            continue;
        }

        terms.push(match[0].toLowerCase() + (next === "*" ? "*" : ""));
    }

    return terms;
}

function wordMatches(word, terms) {
    const lower = word.toLowerCase();

    return terms.some(term => term.endsWith("*")
        ? term.length > 1 && lower.startsWith(term.slice(0, -1))
        : term.length > 0 && lower === term);
}

/**
 * A window of `contextWords` words of `text`, starting a third of it before the first word that
 * matches, with the matches wrapped in <mark>. Mirrors highlight_text() in src/web/highlight.c: the
 * Elasticsearch backend has no server-side highlighter for the passage an embeddings search
 * matched, because the passage comes back from the kNN inner hit rather than from a query.
 */
export function excerptText(text, terms, contextWords) {
    if (!text) {
        return null;
    }

    if (!contextWords || contextWords <= 0) {
        contextWords = DEFAULT_CONTEXT_WORDS;
    } else if (contextWords > MAX_CONTEXT_WORDS) {
        contextWords = MAX_CONTEXT_WORDS;
    }

    // Separators land on the even indices, words on the odd ones
    const parts = text.split(new RegExp(`(${WORD.source})`, "u"));

    const lead = Math.floor(contextWords / 3);
    let firstWord = 0;

    for (let i = 1; i < parts.length; i += 2) {
        if (wordMatches(parts[i], terms)) {
            firstWord = Math.max(0, (i - 1) / 2 - lead);
            break;
        }
    }

    let out = "";
    let words = 0;

    for (let i = firstWord * 2 + 1; i < parts.length && words < contextWords; i++) {
        const isWord = i % 2 === 1;

        if (!isWord) {
            if (out.length + parts[i].length > MAX_EXCERPT_LENGTH) {
                break;
            }
            out += parts[i];
            continue;
        }

        const matched = wordMatches(parts[i], terms);

        // The word and the tags around it go in together, so the output never ends mid-markup
        if (out.length + parts[i].length + (matched ? 13 : 0) > MAX_EXCERPT_LENGTH) {
            break;
        }

        out += matched ? `<mark>${parts[i]}</mark>` : parts[i];
        words += 1;
    }

    return out === "" ? null : out;
}
