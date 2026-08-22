/**
 * The address of an Elasticsearch node, the way a person types it into the admin interface.
 *
 * A scheme is assumed when there is none, the way sist2 itself reads --es-url. A URL that names
 * no port is left alone rather than pointed at 9200: the scan and index jobs will use the same
 * port this test does, so the two have to agree.
 *
 * @returns {URL|null} null when the text is not an address of an Elasticsearch node
 */
export function parseEsUrl(esUrl) {
    if (typeof esUrl !== "string" || esUrl.trim() === "") {
        return null;
    }

    const text = esUrl.trim();
    const withScheme = /^[a-z][a-z0-9+.-]*:\/\//i.test(text) ? text : `http://${text}`;

    let url;
    try {
        url = new URL(withScheme);
    } catch (e) {
        return null;
    }

    if (url.protocol !== "http:" && url.protocol !== "https:") {
        return null;
    }

    if (url.hostname === "") {
        return null;
    }

    return url;
}

/** The port a URL reaches, including the one it does not spell out */
export function esUrlPort(url) {
    if (url.port !== "") {
        return Number(url.port);
    }

    return url.protocol === "https:" ? 443 : 80;
}
