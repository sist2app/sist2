// The ranges BERT's basic tokenizer treats as CJK, and splits one character per token
const CJK_RANGES = [
    [0x4E00, 0x9FFF], [0x3400, 0x4DBF], [0x20000, 0x2A6DF], [0x2A700, 0x2B73F],
    [0x2B740, 0x2B81F], [0x2B820, 0x2CEAF], [0xF900, 0xFAFF], [0x2F800, 0x2FA1F]
];

// BERT counts the ASCII symbols as punctuation as well as everything unicode calls punctuation,
// but not the maths and emoji symbols
const PUNCTUATION = /[!-/:-@[-`{-~]|\p{P}/u;

const CONTROL = /\p{C}/u;

function isCjk(codePoint) {
    return CJK_RANGES.some(([from, to]) => codePoint >= from && codePoint <= to);
}

/**
 * Uncased WordPiece, as BertTokenizer(do_lower_case=True) does it: control characters dropped,
 * CJK split per character, accents stripped, punctuation split off, then greedy longest-match
 * against the vocabulary with ## marking a continuation.
 */
export class WordPieceTokenizer {

    _vocab = null;
    _unkToken = null;
    _clsToken = null;
    _sepToken = null;
    _lowerCase = true;
    _maxLength = 256;

    constructor(data) {
        this._vocab = data.vocab;
        this._unkToken = data.unk_token;
        this._clsToken = data.cls_token;
        this._sepToken = data.sep_token;
        this._lowerCase = data.do_lower_case;
        this._maxLength = data.max_length;
    }

    cleanText(text) {
        let out = "";

        for (const char of text) {
            const codePoint = char.codePointAt(0);

            if (codePoint === 0 || codePoint === 0xFFFD) {
                continue;
            }
            if (/\s/.test(char)) {
                out += " ";
            } else if (CONTROL.test(char)) {
                continue;
            } else if (isCjk(codePoint)) {
                out += ` ${char} `;
            } else {
                out += char;
            }
        }

        return out;
    }

    splitOnPunctuation(token) {
        const parts = [];
        let current = "";

        for (const char of token) {
            if (PUNCTUATION.test(char)) {
                if (current !== "") {
                    parts.push(current);
                    current = "";
                }
                parts.push(char);
            } else {
                current += char;
            }
        }

        if (current !== "") {
            parts.push(current);
        }

        return parts;
    }

    /** The pieces of one whitespace-delimited word, or the unknown token if it has none */
    wordPieces(word) {
        const pieces = [];
        let start = 0;

        while (start < word.length) {
            let end = word.length;
            let piece = null;

            // Longest match first, every piece after the first written as a continuation
            while (start < end) {
                const candidate = start === 0 ? word.slice(start, end) : `##${word.slice(start, end)}`;
                if (candidate in this._vocab) {
                    piece = candidate;
                    break;
                }
                end -= 1;
            }

            if (piece === null) {
                return [this._unkToken];
            }

            pieces.push(piece);
            start = end;
        }

        return pieces;
    }

    tokenize(text) {
        const words = this.cleanText(text).split(" ").filter(word => word !== "");
        const tokens = [];

        for (let word of words) {
            if (this._lowerCase) {
                word = word.toLowerCase().normalize("NFD").replace(/\p{Mn}/gu, "");
            }

            for (const part of this.splitOnPunctuation(word)) {
                tokens.push(...this.wordPieces(part));
            }
        }

        return tokens;
    }

    /** Token ids of text, between [CLS] and [SEP], truncated to what the model takes */
    encode(text) {
        const tokens = this.tokenize(text).slice(0, this._maxLength - 2);

        return [this._clsToken, ...tokens, this._sepToken].map(token => this._vocab[token]);
    }
}
