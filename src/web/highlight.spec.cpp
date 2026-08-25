#include <gtest/gtest.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

extern "C" {
#include "src/web/highlight.h"
}

/** Owns the term array for the duration of a test */
class Terms {
public:
    explicit Terms(const char *query) : terms_(highlight_query_terms(query)) {}

    ~Terms() { highlight_free_terms(terms_); }

    operator char *const *() const { return terms_; }

    std::vector<std::string> list() const {
        std::vector<std::string> out;
        for (int i = 0; terms_[i] != nullptr; i++) {
            out.emplace_back(terms_[i]);
        }
        return out;
    }

private:
    char **terms_;
};

/** Free the string highlight_text() returns */
static std::string marked(const char *text, char *const *terms, int context = 0) {
    char *result = highlight_text(text, terms, context);
    if (result == nullptr) {
        return "";
    }

    std::string out(result);
    free(result);
    return out;
}

TEST(HighlightTerms, LowercasesAndSplits) {
    ASSERT_EQ(Terms("Alpha BETA").list(), (std::vector<std::string>{"alpha", "beta"}));
}

TEST(HighlightTerms, DropsOperatorsAndColumnFilters) {
    ASSERT_EQ(Terms("alpha AND beta OR gamma NOT delta").list(),
              (std::vector<std::string>{"alpha", "beta", "gamma", "delta"}));
    ASSERT_EQ(Terms("name:report content:budget").list(),
              (std::vector<std::string>{"report", "budget"}));
}

TEST(HighlightTerms, KeepsPhraseWordsAndPrefixes) {
    ASSERT_EQ(Terms("\"quarterly report\" budg*").list(),
              (std::vector<std::string>{"quarterly", "report", "budg*"}));
}

TEST(HighlightText, MarksEveryOccurrence) {
    Terms terms("fox");

    ASSERT_EQ(marked("the fox and the other fox", terms),
              "the <mark>fox</mark> and the other <mark>fox</mark>");
}

TEST(HighlightText, IsCaseInsensitiveAndKeepsTheOriginalCase) {
    Terms terms("fox");

    ASSERT_EQ(marked("The FOX", terms), "The <mark>FOX</mark>");
}

TEST(HighlightText, MatchesWholeWordsOnly) {
    Terms terms("fox");

    ASSERT_EQ(marked("foxtrot", terms), "foxtrot");
}

TEST(HighlightText, PrefixTermMatchesTheStartOfAWord) {
    Terms terms("fox*");

    ASSERT_EQ(marked("foxtrot", terms), "<mark>foxtrot</mark>");
}

TEST(HighlightText, WindowStartsNearTheFirstMatch) {
    Terms terms("needle");

    const std::string text = "one two three four five six seven eight nine ten needle after";
    const std::string snippet = marked(text.c_str(), terms, 6);

    // The window is six words long and holds the match, not the start of the document
    ASSERT_NE(snippet.find("<mark>needle</mark>"), std::string::npos) << snippet;
    ASSERT_EQ(snippet.find("one two"), std::string::npos) << snippet;
}

TEST(HighlightText, WithoutAMatchReturnsTheStartOfTheText) {
    Terms terms("absent");

    ASSERT_EQ(marked("alpha beta gamma", terms, 2), "alpha beta");
}

TEST(HighlightText, EmptyTextHasNoHighlight) {
    Terms terms("alpha");

    ASSERT_EQ(marked("", terms), "");
}

TEST(HighlightText, KeepsNonAsciiWordsWhole) {
    Terms terms("café");

    ASSERT_EQ(marked("le café noir", terms), "le <mark>café</mark> noir");
}

/*
 * Both inputs come from outside: the query is whatever was typed into the search box, the text is
 * whatever was extracted from a file on disk. These rounds run in the ASan and UBSan binaries too.
 *
 * More rounds: ./build/sist2_a_test --gtest_filter=*Fuzz* --gtest_repeat=100
 */

#define FUZZ_ROUNDS 400
#define MAX_HIGHLIGHT_BYTES 16384

namespace {

    /**
     * '<' is left out so that the tags in the output can only be the ones highlight_text() wrote,
     * which is what makes the invariants below checkable. LiteralMarkupInTextIsNotTouched covers
     * the other case.
     */
    char random_byte(std::mt19937 &rng) {
        std::uniform_int_distribution<int> byte(1, 255);

        while (true) {
            const int c = byte(rng);
            if (c != '<') {
                return (char) c;
            }
        }
    }

    std::string random_text(std::mt19937 &rng, size_t max_len) {
        std::uniform_int_distribution<size_t> length(0, max_len);
        std::uniform_int_distribution<int> shape(0, 3);

        std::string out;
        const size_t len = length(rng);

        for (size_t i = 0; i < len; i++) {
            switch (shape(rng)) {
                case 0:
                    out += random_byte(rng);
                    break;
                case 1:
                    // Words and spaces, the shape real text has
                    out += (char) ('a' + (rng() % 26));
                    break;
                case 2:
                    out += " \t\n.,;:!?\"'()[]{}*-/"[rng() % 19];
                    break;
                default:
                    // A UTF-8 sequence, sometimes cut short
                    out += (char) (0xC0 | (rng() % 0x20));
                    if (rng() % 4 != 0) {
                        out += (char) (0x80 | (rng() % 0x40));
                    }
                    break;
            }
        }

        return out;
    }

    /** The marked-up output, with the tags taken back out. Fails the test if they are malformed. */
    std::string strip_marks(const std::string &out, std::vector<std::string> *marked) {
        std::string text;
        size_t i = 0;
        bool open = false;

        while (i < out.size()) {
            if (out.compare(i, 6, "<mark>") == 0) {
                EXPECT_FALSE(open) << "nested <mark>: " << out;
                open = true;
                i += 6;
                marked->emplace_back();
                continue;
            }

            if (out.compare(i, 7, "</mark>") == 0) {
                EXPECT_TRUE(open) << "unopened </mark>: " << out;
                open = false;
                i += 7;
                continue;
            }

            EXPECT_NE(out[i], '<') << "stray '<' in output: " << out;

            if (open) {
                marked->back() += out[i];
            }
            text += out[i];
            i += 1;
        }

        EXPECT_FALSE(open) << "unclosed <mark>: " << out;

        return text;
    }

    /** The test's own reading of the matching rule, kept independent of the implementation */
    bool matches_a_term(const std::string &word, const std::vector<std::string> &terms) {
        for (std::string term: terms) {
            const bool prefix = !term.empty() && term.back() == '*';
            if (prefix) {
                term.pop_back();
            }
            if (term.empty()) {
                continue;
            }

            if (prefix ? word.size() >= term.size() : word.size() == term.size()) {
                if (strncasecmp(word.c_str(), term.c_str(), term.size()) == 0) {
                    return true;
                }
            }
        }

        return false;
    }
}

TEST(FuzzHighlight, RandomInputKeepsTheTextIntact) {
    std::mt19937 rng(20260813);

    for (int round = 0; round < FUZZ_ROUNDS; round++) {
        const std::string text = random_text(rng, 400);
        const std::string query = random_text(rng, 40);
        const int context = (int) (rng() % 40) - 5;

        Terms terms(query.c_str());
        char *result = highlight_text(text.c_str(), terms, context);

        if (result == nullptr) {
            continue;
        }

        const std::string out(result);
        free(result);

        std::vector<std::string> marked;
        const std::string stripped = strip_marks(out, &marked);

        // Nothing invented, nothing lost: what comes out is a run of what went in
        ASSERT_NE(text.find(stripped), std::string::npos)
                                    << "text: " << text << "\nquery: " << query << "\nout: " << out;
        ASSERT_LE(out.size(), (size_t) MAX_HIGHLIGHT_BYTES + 13);

        for (const std::string &word: marked) {
            ASSERT_TRUE(matches_a_term(word, terms.list()))
                                        << "marked '" << word << "' for query: " << query;
        }
    }
}

TEST(FuzzHighlight, TermsOfRandomQueriesAreWellFormed) {
    std::mt19937 rng(987654321);

    for (int round = 0; round < FUZZ_ROUNDS; round++) {
        const std::string query = random_text(rng, 200);

        Terms terms(query.c_str());
        const std::vector<std::string> list = terms.list();

        ASSERT_LE(list.size(), 64u);

        for (const std::string &term: list) {
            ASSERT_FALSE(term.empty());
            ASSERT_EQ(term.find(':'), std::string::npos) << term;
            ASSERT_NE(term, "*");

            for (size_t i = 0; i + 1 < term.size(); i++) {
                ASSERT_FALSE(isupper((unsigned char) term[i])) << term;
            }
        }
    }
}

/** A query whose terms are taken from the text, so that matches actually happen */
TEST(FuzzHighlight, WordsAreMarkedIfAndOnlyIfTheyMatch) {
    std::mt19937 rng(13572468);

    for (int round = 0; round < FUZZ_ROUNDS; round++) {
        std::string text;
        std::vector<std::string> words;

        for (int i = 0; i < 1 + (int) (rng() % 20); i++) {
            std::string word;
            for (int j = 0; j < 1 + (int) (rng() % 6); j++) {
                word += (char) ('a' + (rng() % 4));
            }
            words.push_back(word);
            text += word + " ";
        }

        std::string query = words[rng() % words.size()];
        if (rng() % 3 == 0) {
            query += "*";
        }

        Terms terms(query.c_str());
        char *result = highlight_text(text.c_str(), terms, 1000);
        ASSERT_NE(result, nullptr);

        const std::string out(result);
        free(result);

        std::vector<std::string> marked;
        const std::string stripped = strip_marks(out, &marked);
        ASSERT_EQ(stripped, text);

        // Every word of the window is marked exactly when the test's own rule says it matches
        size_t marked_index = 0;
        for (const std::string &word: words) {
            if (matches_a_term(word, terms.list())) {
                ASSERT_LT(marked_index, marked.size()) << "missing highlight for " << word;
                ASSERT_EQ(marked[marked_index++], word);
            }
        }
        ASSERT_EQ(marked_index, marked.size());
    }
}

TEST(HighlightText, PunctuationOnlyDocumentIsCapped) {
    Terms terms("alpha");

    const std::string text(1024 * 1024, '.');
    char *result = highlight_text(text.c_str(), terms, 30);

    ASSERT_NE(result, nullptr);
    ASSERT_LE(strlen(result), (size_t) MAX_HIGHLIGHT_BYTES);
    free(result);
}

TEST(HighlightText, OneEnormousWordIsCapped) {
    Terms terms("alpha");

    const std::string text(1024 * 1024, 'a');
    char *result = highlight_text(text.c_str(), terms, 30);

    // The single word does not fit under the cap, so there is nothing to show
    ASSERT_EQ(result, nullptr);
}

/** Markup in the document is text like any other: it is copied, not interpreted or escaped */
TEST(HighlightText, MarkupInTextIsCopiedVerbatim) {
    Terms terms("alpha");

    char *result = highlight_text("alpha <script>x</script>", terms, 30);

    ASSERT_NE(result, nullptr);
    ASSERT_STREQ(result, "<mark>alpha</mark> <script>x</script>");
    free(result);
}

/**
 * A document that contains mark tags of its own ends up indistinguishable from a highlight, and
 * the frontend renders both. It escapes everything else (see sist2-vue util.spec.js), so the worst
 * a document can do is highlight itself.
 */
TEST(HighlightText, MarkTagsInTextCannotBeToldApartFromHighlights) {
    Terms terms("beta");

    char *result = highlight_text("beta <mark>gamma</mark>", terms, 30);

    ASSERT_NE(result, nullptr);
    ASSERT_STREQ(result, "<mark>beta</mark> <mark>gamma</mark>");
    free(result);
}

TEST(HighlightText, TruncatedUtf8IsCopiedAsItIs) {
    Terms terms("alpha");

    // A lead byte with its continuation byte cut off
    const std::string text = "alpha \xC3";
    char *result = highlight_text(text.c_str(), terms, 30);

    ASSERT_NE(result, nullptr);
    ASSERT_STREQ(result, "<mark>alpha</mark> \xC3");
    free(result);
}

TEST(HighlightTerms, StopsAtSixtyFourTerms) {
    std::string query;
    for (int i = 0; i < 500; i++) {
        query += "term" + std::to_string(i) + " ";
    }

    ASSERT_EQ(Terms(query.c_str()).list().size(), 64u);
}

TEST(HighlightTerms, QueryOfOnlyOperatorsHasNoTerms) {
    ASSERT_TRUE(Terms("AND OR NOT NEAR").list().empty());
    ASSERT_TRUE(Terms("*** \"\" () ^ - :").list().empty());
    ASSERT_TRUE(Terms("").list().empty());
}

TEST(HighlightTerms, NullQueryHasNoTerms) {
    ASSERT_TRUE(Terms(nullptr).list().empty());
}

TEST(HighlightText, NullInputsAreRejected) {
    Terms terms("alpha");

    ASSERT_EQ(highlight_text(nullptr, terms, 30), nullptr);
    ASSERT_EQ(highlight_text("alpha", nullptr, 30), nullptr);
}

TEST(HighlightText, ContextLargerThanTheCapIsClamped) {
    Terms terms("alpha");

    std::string text = "alpha";
    for (int i = 0; i < 5000; i++) {
        text += " word" + std::to_string(i);
    }

    char *result = highlight_text(text.c_str(), terms, 1000000);

    ASSERT_NE(result, nullptr);
    // 1000 words at most, and never past the byte cap
    ASSERT_LE(strlen(result), (size_t) MAX_HIGHLIGHT_BYTES);
    free(result);
}

TEST(HighlightTerms, FreeingNullIsAllowed) {
    highlight_free_terms(nullptr);
}

/** Owns the page break array for the duration of a test */
class PageBreaks {
public:
    explicit PageBreaks(const char *csv) : breaks_(highlight_parse_page_breaks(csv, &count_)) {}

    ~PageBreaks() { free(breaks_); }

    const size_t *get() const { return breaks_; }

    int count() const { return count_; }

    std::vector<size_t> list() const { return {breaks_, breaks_ + count_}; }

private:
    int count_ = 0;
    size_t *breaks_;
};

TEST(PageBreaksParse, ReadsTheOffsetsAScanWrote) {
    ASSERT_EQ(PageBreaks("0,31,1036").list(), (std::vector<size_t>{0, 31, 1036}));
}

TEST(PageBreaksParse, EmptyForADocumentThatIsNotPaginated) {
    ASSERT_EQ(PageBreaks("").get(), nullptr);
    ASSERT_EQ(PageBreaks(nullptr).get(), nullptr);
}

TEST(FragmentPage, FindsThePageAFragmentCameFrom) {
    const char *text = "page one text page two text page three text";
    const PageBreaks breaks("0,14,28");

    ASSERT_EQ(highlight_fragment_page(text, "page <mark>one</mark>", breaks.get(), breaks.count()), 1);
    ASSERT_EQ(highlight_fragment_page(text, "page <mark>two</mark>", breaks.get(), breaks.count()), 2);
    ASSERT_EQ(highlight_fragment_page(text, "page <mark>three</mark>", breaks.get(), breaks.count()), 3);
}

/** The excerpt starts a few words before the match, which can be on the page before it */
TEST(FragmentPage, AnswersWithThePageTheMatchIsOn) {
    const char *text = "page one text page two text";
    const PageBreaks breaks("0,14");

    ASSERT_EQ(highlight_fragment_page(text, "one text page <mark>two</mark>", breaks.get(), breaks.count()), 2);
}

TEST(FragmentPage, CountsCodePointsRatherThanBytes) {
    // Two code points, four bytes, before the second page starts
    const char *text = "éé page two";
    const PageBreaks breaks("0,3");

    ASSERT_EQ(highlight_fragment_page(text, "<mark>page</mark>", breaks.get(), breaks.count()), 2);
}

TEST(FragmentPage, ZeroWhenTheFragmentIsNotPartOfTheText) {
    const PageBreaks breaks("0,14");

    ASSERT_EQ(highlight_fragment_page("page one text", "<mark>elsewhere</mark>", breaks.get(), breaks.count()), 0);
}

TEST(FragmentPage, ZeroForADocumentThatIsNotPaginated) {
    ASSERT_EQ(highlight_fragment_page("page one text", "<mark>one</mark>", nullptr, 0), 0);
}
