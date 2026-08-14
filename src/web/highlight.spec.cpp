#include <gtest/gtest.h>

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
