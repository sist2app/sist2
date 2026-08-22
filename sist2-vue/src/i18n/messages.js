export default {
    en: {
        filePage: {
            notFound: "Not found"
        },
        searchBar: {
            simple: "Search",
            advanced: "Advanced search",
            fuzzy: "Fuzzy"
        },
        addTag: "Tag",
        deleteTag: "Delete",
        download: "Download",
        and: "and",
        page: "page",
        pages: "pages",
        mimeTypes: "Media types",
        tags: "Tags",
        tagFilter: "Filter tags",
        forExample: "For example:",
        embeddingsSearchPlaceholder: "Embeddings search",
        help: {
            simpleSearch: "Simple search",
            advancedSearch: "Advanced search",
            help: "Help",
            term: "<TERM>",
            and: "AND operator",
            or: "OR operator",
            not: "negates a single term",
            quotes: "will match the enclosed sequence of terms in that specific order",
            prefix: "will match any term with a given prefix when used at the end of a word",
            parens: "used to group expressions",
            tildeTerm: "match a term with a given edit distance",
            tildePhrase: "match a phrase with a given number of allowed intervening unmatched words",
            example1:
                "For example: <code>\"fried eggs\" +(eggplant | potato) -frittata</code> will match the " +
                "phrase <i>fried eggs</i> and either <i>eggplant</i> or <i>potato</i>, but will ignore results " +
                "containing <i>frittata</i>.",
            defaultOperator:
                "When neither <code>+</code> nor <code>|</code> is specified, the default operator is " +
                "<code>+</code> (and).",
            fuzzy:
                "When the <b>Fuzzy</b> option is checked, partial matches based on 3-grams are also returned.",
            moreInfoSimple: "For more information, see <a target=\"_blank\" " +
                "rel=\"noreferrer\" href=\"//www.elastic.co/guide/en/elasticsearch/reference/current/query-dsl-simple-query-string-query.html\">Elasticsearch documentation</a>",
            moreInfoAdvanced: "For documentation about the advanced search mode, see <a target=\"_blank\" rel=\"noreferrer\" href=\"//www.elastic.co/guide/en/elasticsearch/reference/current/query-dsl-query-string-query.html#query-string-syntax\">Elasticsearch documentation</a>"
        },
        config: "Configuration",
        configDescription: "Configuration is saved in real time for this browser.",
        configReset: "Reset configuration",
        searchOptions: "Search options",
        treemapOptions: "Treemap options",
        displayOptions: "Display options",
        opt: {
            lang: "Language",
            highlight: "Enable highlighting",
            fuzzy: "Set fuzzy search by default",
            searchInPath: "Enable matching query against document path",
            suggestPath: "Enable auto-complete in path filter bar",
            fragmentSize: "Highlight context size",
            fragmentCount: "Number of highlight snippets",
            queryMode: "Search mode",
            displayMode: "Display",
            columns: "Column count",
            treemapType: "Treemap type",
            treemapTiling: "Treemap tiling",
            treemapColorGroupingDepth: "Treemap color grouping depth (flat)",
            treemapColor: "Treemap color (cascaded)",
            treemapSize: "Treemap size",
            theme: "Theme",
            lightboxLoadOnlyCurrent: "Do not preload full-size images for adjacent slides in image viewer.",
            slideDuration: "Slide duration",
            resultSize: "Number of results per page",
            tagOrOperator: "Use OR operator when specifying multiple tags.",
            hideDuplicates: "Hide duplicate results based on checksum",
            hideLegacy: "Hide the 'legacyES' Elasticsearch notice",
            updateMimeMap: "Update the Media Types tree in real time",
            useDatePicker: "Use a Date Picker component rather than a slider",
            vidPreviewInterval: "Video preview frame duration in ms",
            simpleLightbox: "Disable animations in image viewer",
            showTagPickerFilter: "Display the tag filter bar",
            featuredFields: "Featured fields Javascript template string. Will appear in the search results.",
            featuredFieldsList: "Available variables",
        },
        queryMode: {
            simple: "Simple",
            advanced: "Advanced",
        },
        lang: {
            en: "English",
            de: "Deutsch",
            fr: "Français",
            pl: "Polski",
            "zh-CN": "简体中文",
        },
        displayMode: {
            grid: "Grid",
            list: "List",
        },
        columns: {
            auto: "Auto"
        },
        treemapType: {
            cascaded: "Cascaded",
            flat: "Flat (compact)"
        },
        treemapSize: {
            small: "Small",
            medium: "Medium",
            large: "Large",
            xLarge: "xLarge",
            xxLarge: "xxLarge",
            custom: "Custom",
        },
        treemapTiling: {
            binary: "Binary",
            squarify: "Squarify",
            slice: "Slice",
            dice: "Dice",
            sliceDice: "Slice & Dice",
        },
        theme: {
            light: "Light",
            black: "Black"
        },
        hit: "hit",
        hits: "hits",
        details: "Details",
        stats: "Stats",
        queryTime: "Query time",
        totalSize: "Total size",
        pathBar: {
            placeholder: "Filter path",
            modalTitle: "Select path"
        },
        debug: "Debug information",
        debugDescription: "Information useful for debugging. If you encounter bugs or have suggestions for" +
            " new features, please submit a new issue <a href='https://github.com/sist2app/sist2/issues/new/choose'>here</a>.",
        tagline: "Tagline",
        toast: {
            esConnErrTitle: "Elasticsearch connection error",
            esConnErr: "sist2 web module encountered an error while connecting to Elasticsearch." +
                " See server logs for more information.",
            loadErr: "sist2 could not load the search page. Check that the search backend is reachable;" +
                " see server logs for more information.",
            esQueryErrTitle: "Query error",
            esQueryErr: "Could not parse or execute query, please check the Advanced search documentation. " +
                "See server logs for more information.",
            dupeTagTitle: "Duplicate tag",
            dupeTag: "This tag already exists for this document.",
            invalidTagTitle: "Invalid tag",
            invalidTag: "Tag names cannot contain the '#' character.",
            copiedToClipboard: "Copied to clipboard"
        },
        saveTagModalTitle: "Add tag",
        saveTagPlaceholder: "Tag name",
        confirm: "Confirm",
        indexPickerPlaceholder: "Select an index",
        sort: {
            relevance: "Relevance",
            dateAsc: "Date (Older first)",
            dateDesc: "Date (Newer first)",
            sizeAsc: "Size (Smaller first)",
            sizeDesc: "Size (Larger first)",
            nameAsc: "Name (A-z)",
            nameDesc: "Name (Z-a)",
            random: "Random",
        },
        d3: {
            mimeCount: "File count distribution by media type",
            mimeSize: "Size distribution by media type",
            dateHistogram: "File modification time distribution",
            sizeHistogram: "File size distribution",
            noStats: "No statistics for this index.",
        },
        mimePicker: {
            selectAll: "Select All",
            selectNone: "Select None",
            invert: "Invert",
        },
        indexPicker: {
            selectNone: "Select None",
            selectAll: "Select All",
            selectedIndex: "selected index",
            selectedIndices: "selected indices",
        }
    },
    de: {
        filePage: {
            notFound: "Nicht gefunden"
        },
        searchBar: {
            simple: "Suche",
            advanced: "Erweiterte Suche",
            fuzzy: "Fuzzy"
        },
        addTag: "Tag",
        deleteTag: "Löschen",
        download: "Herunterladen",
        and: "und",
        page: "Seite",
        pages: "Seiten",
        mimeTypes: "Medientypen",
        tags: "Tags",
        tagFilter: "Tags filtern",
        forExample: "Zum Beispiel:",
        embeddingsSearchPlaceholder: "Embeddings-Suche",
        help: {
            simpleSearch: "Einfache Suche",
            advancedSearch: "Erweiterte Suche",
            help: "Hilfe",
            term: "<BEGRIFF>",
            and: "UND Operator",
            or: "ODER Operator",
            not: "negiert einen einzelnen Begriff",
            quotes: "liefert Treffer, wenn die Abfolge in der genauen Reihenfolge gefunden wird",
            prefix: "liefert Treffer, wenn die Abfolge einen solchen Präfix hat",
            parens: "gruppiert Ausdrücke",
            tildeTerm: "liefert Treffer, im gegebenen 'Editierabstand'",
            tildePhrase: "liefert Treffer, mit dem Ausdruck. Erfolgt die gegebene Anzahl zwischenstehende Nicht-Treffer-Wörter.",
            example1:
                "Zum Beispiel: <code>\"fried eggs\" +(eggplant | potato) -frittata</code> wird " +
                "<i>fried eggs</i> und <i>eggplant</i> oder <i>potato</i> finden, aber keine Ergebnisse, " +
                "die <i>frittata</i> enthalten.",
            defaultOperator:
                "Wenn weder <code>+</code> noch <code>|</code> angegeben sind, ist " +
                "<code>+</code> (und) der Standard.",
            fuzzy:
                "Wenn <b>Fuzzy</b> aktiviert ist, werden Teil-Treffer (3-grams) ebenfalls akzeptiert.",
            moreInfoSimple: "Für weitere Informationen s. <a target=\"_blank\" " +
                "rel=\"noreferrer\" href=\"//www.elastic.co/guide/en/elasticsearch/reference/current/query-dsl-simple-query-string-query.html\">Elasticsearch Dokumentation</a>",
            moreInfoAdvanced: "Für die Dokumentation der erweiterten Suche s. <a target=\"_blank\" rel=\"noreferrer\" href=\"//www.elastic.co/guide/en/elasticsearch/reference/current/query-dsl-query-string-query.html#query-string-syntax\">Elasticsearch Dokumentation</a>"
        },
        config: "Konfiguration",
        configDescription: "Konfiguration wird in Echtzeit für diesen Browser gespeichert.",
        configReset: "Konfiguration zurücksetzen",
        searchOptions: "Such-Optionen",
        treemapOptions: "Kacheldiagramm-Optionen",
        displayOptions: "Anzeige-Optionen",
        opt: {
            lang: "Sprache",
            highlight: "Aktiviere Hervorhebung von Treffern",
            fuzzy: "Aktiviere Fuzzy-Suche standardmäßig",
            searchInPath: "Abgleich der Abfrage mit dem Dokumentpfad aktivieren",
            suggestPath: "Aktiviere Auto-Vervollständigung in Pfadfilter-Leiste",
            fragmentSize: "Kontextgröße",
            fragmentCount: "Anzahl der hervorgehobenen Snippets",
            queryMode: "Such-Modus",
            displayMode: "Ansicht",
            columns: "Anzahl Spalten",
            treemapType: "Kacheldiagramm Typ",
            treemapTiling: "Kacheldiagramm Tiling",
            treemapColorGroupingDepth: "Kacheldiagramm Farbgruppierungstiefe (flach)",
            treemapColor: "Kacheldiagramm Farbe (kaskadiert)",
            treemapSize: "Kacheldiagramm Größe",
            theme: "Theme",
            lightboxLoadOnlyCurrent: "keine Bilder in voller Größe für benachbarte Slides im Image-Viewer vorab laden.",
            slideDuration: "Slide Dauer",
            resultSize: "Anzahl Treffer pro Seite",
            tagOrOperator: "Verwende ODER Operator bei der Angabe mehrerer Tags.",
            hideDuplicates: "Verstecke Duplikate basierend auf der Prüfsumme",
            hideLegacy: "Verstecke die 'legacyES' Elasticsearch Notiz",
            updateMimeMap: "Aktualisiere Medientyp-Baum in Echtzeit",
            useDatePicker: "Benutze Datumswähler statt Schieber",
            vidPreviewInterval: "Videovorschau Framedauer in ms",
            simpleLightbox: "Schalte Animationen im Image-Viewer ab",
            showTagPickerFilter: "Zeige die Tag-Filter-Leiste",
            featuredFields: "Variablen, welche zusätzlich in den Suchergebnissen angezeigt werden können.",
            featuredFieldsList: "verfügbare Variablen"
        },
        queryMode: {
            simple: "Einfach",
            advanced: "Erweitert",
        },
        lang: {
            en: "English",
            de: "Deutsch",
            fr: "Français",
            pl: "Polski",
            "zh-CN": "简体中文",
        },
        displayMode: {
            grid: "Gitter",
            list: "Liste",
        },
        columns: {
            auto: "Auto"
        },
        treemapType: {
            cascaded: "kaskadiert",
            flat: "flach (kompakt)"
        },
        treemapSize: {
            small: "klein",
            medium: "mittel",
            large: "groß",
            xLarge: "sehr groß",
            xxLarge: "riesig",
            custom: "eigene",
        },
        treemapTiling: {
            binary: "binär",
            squarify: "quadratisch",
            slice: "Slice",
            dice: "Dice",
            sliceDice: "Slice & Dice",
        },
        theme: {
            light: "Hell",
            black: "Dunkel"
        },
        hit: "Treffer",
        hits: "Treffer",
        details: "Details",
        stats: "Statistiken",
        queryTime: "Abfragedauer",
        totalSize: "Gesamtgröße",
        pathBar: {
            placeholder: "Filter Pfad",
            modalTitle: "Wähle Pfad"
        },
        debug: "Debug Informationen",
        debugDescription: "Informationen für das Debugging. Wenn du Bugs gefunden oder Anregungen für " +
            "neue Features hast, poste sie bitte <a href='https://github.com/sist2app/sist2/issues/new/choose'>hier</a>.",
        tagline: "Tagline",
        toast: {
            esConnErrTitle: "Elasticsearch Verbindungsfehler",
            esConnErr: "sist2 Web-Modul stellte einen Fehler beim Verbinden mit Elasticsearch fest. " +
                "Schau in die Server-Logs für weitere Informationen.",
            esQueryErrTitle: "Query Fehler",
            esQueryErr: "Konnte Query nicht verarbeiten/ausführen, bitte schaue in die Dokumentation zur erweiterten Suche. " +
                "Schau in die Server-Logs für weitere Informationen.",
            dupeTagTitle: "Tag Duplikat",
            dupeTag: "Dieser Tag existiert bereits für das Dokument.",
            invalidTagTitle: "Ungültiger Tag",
            invalidTag: "Tag-Namen dürfen kein '#'-Zeichen enthalten.",
            copiedToClipboard: "In die Zwischenablage kopiert."
        },
        saveTagModalTitle: "Tag hinzufügen",
        saveTagPlaceholder: "Tag Name",
        confirm: "Bestätigen",
        indexPickerPlaceholder: "Index auswählen",
        sort: {
            relevance: "Relevanz",
            dateAsc: "Datum (älteste zuerst)",
            dateDesc: "Datum (neuste zuerst)",
            sizeAsc: "Größe (kleinste zuerst)",
            sizeDesc: "Größe (größte zuerst)",
            nameAsc: "Name (A-z)",
            nameDesc: "Name (Z-a)",
            random: "zufällig",
        },
        d3: {
            mimeCount: "Anzahl nach Medientyp",
            mimeSize: "Größen nach Medientyp",
            dateHistogram: "Änderungszeiten",
            sizeHistogram: "Dateigrößen",
            noStats: "Keine Statistiken für diesen Index.",
        },
        mimePicker: {
            selectAll: "alle auswählen",
            selectNone: "keinen auswählen",
            invert: "umkehren",
        },
        indexPicker: {
            selectNone: "keinen auswählen",
            selectAll: "alle auswählen",
            selectedIndex: "ausgewählter Index",
            selectedIndices: "ausgewählte Indizes",
        },
    },
    fr: {
        filePage: {
            notFound: "Fichier introuvable"
        },
        searchBar: {
            simple: "Recherche",
            advanced: "Recherche avancée",
            fuzzy: "Approximatif"
        },
        addTag: "Taguer",
        deleteTag: "Supprimer",
        download: "Télécharger",
        and: "et",
        page: "page",
        pages: "pages",
        mimeTypes: "Types de médias",
        tags: "Tags",
        tagFilter: "Filtrer les tags",
        forExample: "Par exemple:",
        embeddingsSearchPlaceholder: "Recherche par embeddings",
        help: {
            simpleSearch: "Recherche simple",
            advancedSearch: "Recherche avancée",
            help: "Aide",
            term: "<TERME>",
            and: "opérateur ET",
            or: "opérateur OU",
            not: "exclut un terme",
            quotes: "recherche la séquence de termes dans cet ordre spécifique.",
            prefix: "lorsqu'utilisé à la fin d'un mot, recherche tous les termes avec le préfixe donné.",
            parens: "utilisé pour regrouper des expressions",
            tildeTerm: "recherche un terme avec une distance d'édition donnée",
            tildePhrase: "recherche une phrase avec un nombre donné de mots intermédiaires tolérés",
            example1:
                "Par exemple: <code>\"fried eggs\" +(eggplant | potato) -frittata</code> va rechercher la " +
                "phrase <i>fried eggs</i> et soit <i>eggplant</i> ou <i>potato</i>, mais va exclure les résultats " +
                "qui contiennent <i>frittata</i>.",
            defaultOperator:
                "Lorsqu'aucun des opérateurs <code>+</code> ou <code>|</code> n'est spécifié, l'opérateur par défaut " +
                "est <code>+</code> (ET).",
            fuzzy:
                "Lorsque l'option <b>Approximatif</b> est activée, les résultats partiels basés sur les trigrammes sont" +
                " également inclus.",
            moreInfoSimple: "Pour plus d'information, voir <a target=\"_blank\" " +
                "rel=\"noreferrer\" href=\"//www.elastic.co/guide/en/elasticsearch/reference/current/query-dsl-simple-query-string-query.html\">documentation Elasticsearch</a>",
            moreInfoAdvanced: "Pour plus d'information sur la recherche avancée, voir <a target=\"_blank\" rel=\"noreferrer\" href=\"//www.elastic.co/guide/en/elasticsearch/reference/current/query-dsl-query-string-query.html#query-string-syntax\">documentation Elasticsearch</a>"
        },
        config: "Configuration",
        configDescription: "La configuration est enregistrée en temps réel pour ce navigateur.",
        configReset: "Réinitialiser la configuration",
        searchOptions: "Options de recherche",
        treemapOptions: "Options du Treemap",
        displayOptions: "Options d'affichage",
        opt: {
            lang: "Langue",
            highlight: "Activer le surlignage",
            fuzzy: "Activer la recherche approximative par défaut",
            searchInPath: "Activer la recherche dans le chemin des documents",
            suggestPath: "Activer l'autocomplétion dans la barre de filtre de chemin",
            fragmentSize: "Longueur du contexte de surlignage",
            fragmentCount: "Nombre d'extraits surlignés",
            queryMode: "Mode de recherche",
            displayMode: "Affichage",
            columns: "Nombre de colonnes",
            treemapType: "Type de Treemap",
            treemapTiling: "Treemap tiling",
            treemapColorGroupingDepth: "Groupage de couleur du Treemap (plat)",
            treemapColor: "Couleur du Treemap (en cascade)",
            treemapSize: "Taille du Treemap",
            theme: "Thème",
            lightboxLoadOnlyCurrent: "Désactiver le chargement des diapositives adjacentes pour le visualiseur d'images",
            slideDuration: "Durée des diapositives",
            resultSize: "Nombre de résultats par page",
            tagOrOperator: "Utiliser l'opérateur OU lors de la spécification de plusieurs tags",
            hideDuplicates: "Masquer les résultats en double",
            hideLegacy: "Masquer la notice 'legacyES' Elasticsearch",
            updateMimeMap: "Mettre à jour l'arbre de Types de médias en temps réel",
            useDatePicker: "Afficher un composant « Date Picker » plutôt qu'un slider",
            vidPreviewInterval: "Durée des images d'aperçu vidéo en millisecondes",
            simpleLightbox: "Désactiver les animations du visualiseur d'images",
            showTagPickerFilter: "Afficher le filtre dans l'onglet Tags",
            featuredFields: "Expression Javascript pour les variables mises en évidence. Sera affiché dans les résultats de recherche.",
            featuredFieldsList: "Variables disponibles"
        },
        queryMode: {
            simple: "Simple",
            advanced: "Avancé",
        },
        lang: {
            en: "English",
            de: "Deutsch",
            fr: "Français",
            pl: "Polski",
            "zh-CN": "简体中文",
        },
        displayMode: {
            grid: "Grille",
            list: "Liste",
        },
        columns: {
            auto: "Auto"
        },
        treemapType: {
            cascaded: "En cascade",
            flat: "Plat (compact)"
        },
        treemapSize: {
            small: "Petit",
            medium: "Moyen",
            large: "Grand",
            xLarge: "xGrand",
            xxLarge: "xxGrand",
            custom: "Personnalisé",
        },
        treemapTiling: {
            binary: "Binary",
            squarify: "Squarify",
            slice: "Slice",
            dice: "Dice",
            sliceDice: "Slice & Dice",
        },
        theme: {
            light: "Clair",
            black: "Noir"
        },
        hit: "résultat",
        hits: "résultats",
        details: "Détails",
        stats: "Stats",
        queryTime: "Durée de la requête",
        totalSize: "Taille totale",
        pathBar: {
            placeholder: "Filtrer le chemin",
            modalTitle: "Sélectionner le chemin"
        },
        debug: "Information de débogage",
        debugDescription: "Informations utiles pour le débogage\n" +
            "Si vous rencontrez des bogues ou si vous avez des suggestions pour de nouvelles fonctionnalités," +
            " veuillez soumettre un nouvel Issue <a href='https://github.com/sist2app/sist2/issues/new/choose'>ici</a>.",
        tagline: "Tagline",
        toast: {
            esConnErrTitle: "Erreur de connexion Elasticsearch",
            esConnErr: "Le module web a rencontré une erreur lors de la connexion à Elasticsearch." +
                " Consultez les journaux du serveur pour plus d'informations.",
            esQueryErrTitle: "Erreur de requête",
            esQueryErr: "Impossible d'analyser ou d'exécuter la requête, veuillez consulter la documentation sur la " +
                "recherche avancée. Voir les journaux du serveur pour plus d'informations.",
            dupeTagTitle: "Tag en double",
            dupeTag: "Ce tag existe déjà pour ce document.",
            invalidTagTitle: "Tag invalide",
            invalidTag: "Les noms de tags ne peuvent pas contenir le caractère '#'.",
            copiedToClipboard: "Copié dans le presse-papier"
        },
        saveTagModalTitle: "Ajouter un tag",
        saveTagPlaceholder: "Nom du tag",
        confirm: "Confirmer",
        indexPickerPlaceholder: "Sélectionner un index",
        sort: {
            relevance: "Pertinence",
            dateAsc: "Date (Plus ancien)",
            dateDesc: "Date (Plus récent)",
            sizeAsc: "Taille (Plus petit)",
            sizeDesc: "Taille (Plus grand)",
            nameAsc: "Nom (A-z)",
            nameDesc: "Nom (Z-a)",
            random: "Aléatoire",
        },
        d3: {
            mimeCount: "Distribution du nombre de fichiers par type de média",
            mimeSize: "Distribution des tailles de fichiers par type de média",
            dateHistogram: "Distribution des dates de modification",
            sizeHistogram: "Distribution des tailles de fichier",
            noStats: "Aucune statistique pour cet index.",
        },
        mimePicker: {
            selectAll: "Sélectionner tout",
            selectNone: "Ne rien sélectionner",
            invert: "Inverser",
        },
        indexPicker: {
            selectNone: "Ne rien sélectionner",
            selectAll: "Sélectionner tout",
            selectedIndex: "index sélectionné",
            selectedIndices: "index sélectionnés",
        },
    },
    "zh-CN": {
        filePage: {
            notFound: "未找到"
        },
        searchBar: {
            simple: "搜索",
            advanced: "高级搜索",
            fuzzy: "模糊搜索"
        },
        addTag: "签条",
        deleteTag: "删除",
        download: "下载",
        and: "与",
        page: "页",
        pages: "页",
        mimeTypes: "文件类型",
        tags: "标签",
        tagFilter: "筛选标签",
        forExample: "例如：",
        embeddingsSearchPlaceholder: "向量搜索",
        help: {
            simpleSearch: "简易搜索",
            advancedSearch: "高级搜索",
            help: "帮助",
            term: "<关键词>",
            and: "与操作",
            or: "或操作",
            not: "反选单个关键词",
            quotes: "括起来的部分视为一个关键词，保序",
            prefix: "在词尾使用时，匹配前缀",
            parens: "表达式编组",
            tildeTerm: "匹配编辑距离以内的关键词",
            tildePhrase: "匹配短语，容忍一些非匹配词",
            example1:
                "例如：<code>\"番茄\" +(炒蛋 | 牛腩) -饭</code> 将匹配" +
                "短语 <i>番茄炒蛋</i>、<i>炒蛋</i> 或者 <i>牛腩</i>，而忽略任何带有" +
                "<i>饭</i>的关键词。",
            defaultOperator:
                "表达式中无<code>+</code>或者<code>|</code>时，默认使用" +
                "<code>+</code>（与操作）。",
            fuzzy:
                "选中<b>模糊搜索</b>选项时，返回部分匹配的结果（3-grams）。",
            moreInfoSimple: "详细信息：<a target=\"_blank\" " +
                "rel=\"noreferrer\" href=\"//www.elastic.co/guide/en/elasticsearch/reference/current/query-dsl-simple-query-string-query.html\">Elasticsearch文档</a>",
            moreInfoAdvanced: "高级搜索模式文档：<a target=\"_blank\" rel=\"noreferrer\" href=\"//www.elastic.co/guide/en/elasticsearch/reference/current/query-dsl-query-string-query.html#query-string-syntax\">Elasticsearch文档</a>"
        },
        config: "配置",
        configDescription: "配置在此浏览器中实时保存。",
        configReset: "重置所有设置",
        searchOptions: "搜索选项",
        treemapOptions: "树状图选项",
        displayOptions: "显示选项",
        opt: {
            lang: "语言",
            highlight: "启用高亮",
            fuzzy: "默认使用模糊搜索",
            searchInPath: "匹配文档路径",
            suggestPath: "搜索框启用自动补全",
            fragmentSize: "高亮上下文大小",
            fragmentCount: "突出显示的项目数",
            queryMode: "搜索模式",
            displayMode: "显示",
            columns: "列数",
            treemapType: "树状图类型",
            treemapTiling: "树状图平铺",
            treemapColorGroupingDepth: "树状图颜色编组深度（展开）",
            treemapColor: "树状图颜色（折叠）",
            treemapSize: "树状图大小",
            theme: "主题",
            lightboxLoadOnlyCurrent: "在图片查看器中，不要预读相邻的全图",
            slideDuration: "幻灯片时长",
            resultSize: "每页结果数",
            tagOrOperator: "使用或操作（OR）匹配多个标签。",
            hideDuplicates: "使用校验码隐藏重复结果",
            hideLegacy: "隐藏 'legacyES' Elasticsearch 通知",
            updateMimeMap: "媒体类型树的实时更新",
            useDatePicker: "使用日期选择器组件而不是滑块",
            vidPreviewInterval: "视频预览帧的持续时间，以毫秒为单位",
            simpleLightbox: "在图片查看器中，禁用动画",
            showTagPickerFilter: "显示标签过滤栏",
            featuredFields: "特色领域的Javascript模板字符串。将出现在搜索结果中。",
            featuredFieldsList: "可利用的变量"
        },
        queryMode: {
            simple: "简单",
            advanced: "高级",
        },
        lang: {
            en: "English",
            de: "Deutsch",
            fr: "Français",
            pl: "Polski",
            "zh-CN": "简体中文",
        },
        displayMode: {
            grid: "网格",
            list: "列表",
        },
        columns: {
            auto: "自动"
        },
        treemapType: {
            cascaded: "折叠",
            flat: "平铺（紧凑）"
        },
        treemapSize: {
            small: "小",
            medium: "中",
            large: "大",
            xLarge: "加大",
            xxLarge: "加加大",
            custom: "自订",
        },
        treemapTiling: {
            binary: "Binary",
            squarify: "Squarify",
            slice: "Slice",
            dice: "Dice",
            sliceDice: "Slice & Dice",
        },
        theme: {
            light: "亮",
            black: "暗"
        },
        hit: "命中",
        hits: "命中",
        details: "详细信息",
        stats: "统计信息",
        queryTime: "查询时间",
        totalSize: "总大小",
        pathBar: {
            placeholder: "过滤路径",
            modalTitle: "选择路径"
        },
        debug: "调试信息",
        debugDescription: "对调试除错有用的信息。 若您遇到bug或者想建议新功能，请提交新Issue到" +
            "<a href='https://github.com/sist2app/sist2/issues/new/choose'>这里</a>。",
        tagline: "标签栏",
        toast: {
            esConnErrTitle: "Elasticsearch连接错误",
            esConnErr: "sist2 web 模块连接Elasticsearch出错。" +
                "查看服务日志以获取更多信息。",
            esQueryErrTitle: "查询错误",
            esQueryErr: "无法识别或执行查询，请查阅高级搜索文档。" +
                "查看服务日志以获取更多信息。",
            dupeTagTitle: "重复标签",
            dupeTag: "该标签已存在于此文档。",
            invalidTagTitle: "无效标签",
            invalidTag: "标签名不能包含 '#' 字符。",
            copiedToClipboard: "复制到剪贴板"
        },
        saveTagModalTitle: "增加标签",
        saveTagPlaceholder: "标签名",
        confirm: "确认",
        indexPickerPlaceholder: "选择一个索引",
        sort: {
            relevance: "相关度",
            dateAsc: "日期（由旧到新）",
            dateDesc: "日期（由新到旧）",
            sizeAsc: "大小（从小到大）",
            sizeDesc: "大小（从大到小）",
            nameAsc: "名字（A-z）",
            nameDesc: "名字 （Z-a）",
            random: "随机",
        },
        d3: {
            mimeCount: "各类文件数量分布",
            mimeSize: "各类文件大小分布",
            dateHistogram: "文件修改时间分布",
            sizeHistogram: "文件大小分布",
            noStats: "此索引没有统计数据。",
        },
        mimePicker: {
            selectAll: "全选",
            selectNone: "清空",
            invert: "反选",
        },
        indexPicker: {
            selectNone: "清空",
            selectAll: "全选",
            selectedIndex: "选中索引",
            selectedIndices: "选中索引",
        },
    },
    pl: {
        filePage: {
            notFound: "Nie znaleziono"
        },
        searchBar: {
            simple: "Szukaj",
            advanced: "Zaawansowane szukanie",
            fuzzy: "Również podobne"
        },
        addTag: "Tag",
        deleteTag: "Usuń",
        download: "Pobierz",
        and: "i",
        page: "strona",
        pages: "stron",
        mimeTypes: "Typy danych",
        tags: "Tagi",
        tagFilter: "Filtruj tagi",
        forExample: "Na przykład:",
        embeddingsSearchPlaceholder: "Szukanie wektorowe",
        help: {
            simpleSearch: "Proste szukanie",
            advancedSearch: "Zaawansowane szukanie",
            help: "Pomoc",
            term: "<WYRAZ>",
            and: "operator I",
            or: "operator LUB",
            not: "zabrania danego wyrazu",
            quotes: "znajdzie objętą sekwencję wyrazów w podanej kolejności",
            prefix: "znajdzie dowolny wyraz rozpoczynający się na takie litery, jeśli zastosowane na końcu wyrazu",
            parens: "używane do grupowania wyrażeń",
            tildeTerm: "znajdzie wyraz w podanej odległości",
            tildePhrase: "znajdzie frazę przeplecioną podaną liczbą niepasujących wyrazów",
            example1:
                "Na przykład: <code>\"pieczone jajko\" +(kiełbasa | ziemniak) -frytki</code> znajdzie frazę " +
                "<i>pieczone jajko</i> gdzie występuje też: <i>kiełbasa</i> albo <i>ziemniak</i>, ale zignoruje rezultat " +
                "zawierający <i>frytki</i>.",
            defaultOperator:
                "Kiedy nie podano ani <code>+</code>, ani <code>|</code>, to domyślnym operatorem jest " +
                "<code>+</code> (i).",
            fuzzy:
                "Kiedy opcja <b>Również podobne</b> jest zaznaczona, częściowo zgodne wyrazy są również znajdywane.",
            moreInfoSimple: "Po więcej informacji sięgnij do <a target=\"_blank\" " +
                "rel=\"noreferrer\" href=\"//www.elastic.co/guide/en/elasticsearch/reference/current/query-dsl-simple-query-string-query.html\">dokumentacji Elasticsearch</a>",
            moreInfoAdvanced: "Aby uzyskać więcej informacji o zaawansowanym szukaniu, przeczytaj <a target=\"_blank\" rel=\"noreferrer\" href=\"//www.elastic.co/guide/en/elasticsearch/reference/current/query-dsl-query-string-query.html#query-string-syntax\">dokumentację Elasticsearch</a>"
        },
        config: "Ustawienia",
        configDescription: "Ustawienia są zapisywane na żywo w tej przeglądarce.",
        configReset: "Zresetuj ustawienia",
        searchOptions: "Opcje szukania",
        treemapOptions: "Opcje mapy",
        displayOptions: "Opcje wyświetlania",
        opt: {
            lang: "Język",
            highlight: "Zaznaczaj znalezione fragmenty",
            fuzzy: "Ustaw szukanie również podobnych jako domyślne",
            searchInPath: "Włącz szukanie również w ścieżce dokumentu",
            suggestPath: "Włącz auto-uzupełnianie w filtrze ścieżek",
            fragmentSize: "Podświetl wielkość kontekstu w znakach",
            fragmentCount: "Liczba wyróżnionych fragmentów",
            queryMode: "Tryb szukania",
            displayMode: "Wyświetlanie",
            columns: "Liczba kolumn",
            treemapType: "Typ mapy",
            treemapTiling: "Układanie mapy",
            treemapColorGroupingDepth: "Jak głęboko grupować kolory mapy (na płasko)",
            treemapColor: "Kolor mapy (kaskadowo)",
            treemapSize: "Wielkość mapy",
            theme: "Styl graficzny",
            lightboxLoadOnlyCurrent: "Nie pobieraj od razu obrazów w pełnej wielkości dla sąsiednich obrazów podglądu.",
            slideDuration: "Czas trwania jednego slajdu w pokazie slajdów",
            resultSize: "Liczba wyników na stronę",
            tagOrOperator: "Użyj operatora LUB przy wyborze kilku tagów",
            hideDuplicates: "Ukryj zduplikowane wyniki (według sumy kontrolnej)",
            hideLegacy: "Ukryj powiadomienie Elasticsearch 'legacyES'",
            updateMimeMap: "Uaktualniaj drzewo typów mediów na żywo",
            useDatePicker: "Używaj kalendarza do wyboru dat, zamiast suwaka",
            vidPreviewInterval: "Czas trwania jednej klatki w podglądzie wideo (w ms)",
            simpleLightbox: "Wyłącz animacje w podglądzie obrazów",
            showTagPickerFilter: "Pokazuj pole filtrowania tagów",
            featuredFields: "Wybrane pola szablonu Javascript. Będą pojawiać się przy wynikach wyszukiwania.",
            featuredFieldsList: "Dostępne zmienne",
        },
        queryMode: {
            simple: "Proste",
            advanced: "Zaawansowane",
        },
        lang: {
            en: "English",
            de: "Deutsch",
            fr: "Français",
            pl: "Polski",
            "zh-CN": "简体中文",
        },
        displayMode: {
            grid: "Siatka",
            list: "Lista",
        },
        columns: {
            auto: "Automatyczna"
        },
        treemapType: {
            cascaded: "Kaskadowa",
            flat: "Płaska (kompaktowa)"
        },
        treemapSize: {
            small: "Mała",
            medium: "Średnia",
            large: "Duża",
            xLarge: "Bardzo duża",
            xxLarge: "Ogromna",
            custom: "Inna",
        },
        treemapTiling: {
            binary: "Binarnie",
            squarify: "Kwadratowo",
            slice: "Wycinek",
            dice: "Kostka",
            sliceDice: "Wycinek i kostka",
        },
        theme: {
            light: "Jasny",
            black: "Czarny"
        },
        hit: "trafienie",
        hits: "trafień",
        details: "Szczegóły",
        stats: "Statystyki",
        queryTime: "Czas szukania",
        totalSize: "Całkowita wielkość",
        pathBar: {
            placeholder: "Filtruj ścieżki",
            modalTitle: "Wybierz ścieżkę"
        },
        debug: "Informacje dla programistów",
        debugDescription: "Informacje przydatne do znajdowania błędów w oprogramowaniu. Jeśli napotkasz błąd lub masz" +
            " propozycje zmian, zgłoś to proszę <a href='https://github.com/sist2app/sist2/issues/new/choose'>tutaj</a>.",
        tagline: "Slogan",
        toast: {
            esConnErrTitle: "Problem z połączeniem z Elasticsearch",
            esConnErr: "Moduł strony internetowej sist2 napotkał problem przy połączeniu z Elasticsearch." +
                " Zobacz logi serwera, aby uzyskać więcej informacji.",
            esQueryErrTitle: "Problem z kwerendą",
            esQueryErr: "Kwerenda szukania jest niezrozumiała albo nie udało się jej przesłać. Sprawdź dokumentację zaawansowanego szukania. " +
                "Zobacz logi serwera, aby uzyskać więcej informacji.",
            dupeTagTitle: "Zduplikowany tag",
            dupeTag: "Ten dokument już ma taki tag.",
            invalidTagTitle: "Nieprawidłowy tag",
            invalidTag: "Nazwa tagu nie może zawierać znaku '#'.",
            copiedToClipboard: "Skopiowano do schowka"
        },
        saveTagModalTitle: "Dodaj tag",
        saveTagPlaceholder: "Nazwa",
        confirm: "Zatwierdź",
        indexPickerPlaceholder: "Wybierz indeks",
        sort: {
            relevance: "Zgodność z szukanym",
            dateAsc: "Data (najpierw starsze)",
            dateDesc: "Data (najpierw nowsze)",
            sizeAsc: "Wielkość (najpierw mniejsze)",
            sizeDesc: "Wielkość (najpierw większe)",
            nameAsc: "Nazwa (A-z)",
            nameDesc: "Nazwa (Z-a)",
            random: "Losowo",
        },
        d3: {
            mimeCount: "Dystrybucja liczby plików według typów mediów",
            mimeSize: "Dystrybucja wielkości plików według typów mediów",
            dateHistogram: "Dystrybucja dat modyfikacji plików",
            sizeHistogram: "Dystrybucja wielkości plików",
        },
        indexPicker: {
            selectNone: "Odznacz wszystko",
            selectAll: "Zaznacz wszystko",
            selectedIndex: "wybrany indeks",
            selectedIndices: "wybrane indeksy",
        }
    }
}
