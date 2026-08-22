#include "libwpd_c_api.h"
#include "libwpd/libwpd.h"
#include "libwpd/WPXProperty.h"
#include "libwpd-stream/libwpd-stream.h"

class StringDocument : public WPXDocumentInterface {

private:
    text_buffer_t *tex;
    document_t *doc;
    bool is_full;
public:

    StringDocument(text_buffer_t *tex, document_t *doc) {
        this->tex = tex;
        this->doc = doc;
        this->is_full = false;
    }

    void setDocumentMetaData(const WPXPropertyList &propList) override {

        WPXPropertyList::Iter propIter(propList);
        for (propIter.rewind(); propIter.next();) {
            wpd_set_meta(this->doc, propIter.key(), propIter()->getStr().cstr());
        }
    }

    void endDocument() override {
        text_buffer_terminate_string(this->tex);
    }

    void closeParagraph() override {
        if (!this->is_full) {
            if (text_buffer_append_char(tex, ' ') == TEXT_BUF_FULL) {
                this->is_full = true;
            };
        }
    }

    void closeSpan() override {
        if (!this->is_full) {
            if (text_buffer_append_char(tex, ' ') == TEXT_BUF_FULL) {
                this->is_full = true;
            };
        }
    }

    void closeSection() override {
        if (!this->is_full) {
            if (text_buffer_append_char(tex, ' ') == TEXT_BUF_FULL) {
                this->is_full = true;
            };
        }
    }

    void insertTab() override {
        if (!this->is_full) {
            if (text_buffer_append_char(tex, ' ') == TEXT_BUF_FULL) {
                this->is_full = true;
            };
        }
    }

    void insertSpace() override {
        if (!this->is_full) {
            if (text_buffer_append_char(tex, ' ') == TEXT_BUF_FULL) {
                this->is_full = true;
            };
        }
    }

    void insertText(const WPXString &text) override {
        if (!this->is_full) {
            if (text_buffer_append_string0(tex, text.cstr()) == TEXT_BUF_FULL) {
                this->is_full = true;
            };
        }
    }

    void insertLineBreak() override {
        if (!this->is_full) {
            if (text_buffer_append_char(tex, ' ') == TEXT_BUF_FULL) {
                this->is_full = true;
            };
        }
    }

    void definePageStyle(const WPXPropertyList &) override { /* noop */ }

    void closePageSpan() override { /* noop */ }

    void openHeader(const WPXPropertyList &) override { /* noop */ }

    void closeHeader() override { /* noop */ }

    void openFooter(const WPXPropertyList &) override { /* noop */ }

    void closeFooter() override { /* noop */ }

    void
    defineParagraphStyle(const WPXPropertyList &, const WPXPropertyListVector &) override { /* noop */ }

    void openParagraph(const WPXPropertyList &, const WPXPropertyListVector &) override { /* noop */ }

    void defineCharacterStyle(const WPXPropertyList &) override { /* noop */ }

    void openSpan(const WPXPropertyList &) override { /* noop */ }

    void
    defineSectionStyle(const WPXPropertyList &, const WPXPropertyListVector &) override { /* noop */ }

    void openSection(const WPXPropertyList &, const WPXPropertyListVector &) override { /* noop */ }

    void insertField(const WPXString &, const WPXPropertyList &) override { /* noop */ }

    void defineOrderedListLevel(const WPXPropertyList &) override { /* noop */ }

    void defineUnorderedListLevel(const WPXPropertyList &) override { /* noop */ }

    void openOrderedListLevel(const WPXPropertyList &) override { /* noop */ }

    void openUnorderedListLevel(const WPXPropertyList &) override { /* noop */ }

    void closeOrderedListLevel() override { /* noop */ }

    void closeUnorderedListLevel() override { /* noop */ }

    void openListElement(const WPXPropertyList &, const WPXPropertyListVector &) override { /* noop */ }

    void closeListElement() override { /* noop */ }

    void openFootnote(const WPXPropertyList &) override { /* noop */ }

    void closeFootnote() override { /* noop */ }

    void openEndnote(const WPXPropertyList &) override { /* noop */ }

    void closeEndnote() override { /* noop */ }

    void openComment(const WPXPropertyList &) override { /* noop */ }

    void closeComment() override { /* noop */ }

    void openTextBox(const WPXPropertyList &) override { /* noop */ }

    void closeTextBox() override { /* noop */ }

    void openTable(const WPXPropertyList &, const WPXPropertyListVector &) override { /* noop */ }

    void openTableRow(const WPXPropertyList &) override { /* noop */ }

    void closeTableRow() override { /* noop */ }

    void openTableCell(const WPXPropertyList &) override { /* noop */ }

    void closeTableCell() override { /* noop */ }

    void insertCoveredTableCell(const WPXPropertyList &) override { /* noop */ }

    void closeTable() override { /* noop */ }

    void openFrame(const WPXPropertyList &) override { /* noop */ }

    void closeFrame() override { /* noop */ }

    void insertBinaryObject(const WPXPropertyList &, const WPXBinaryData &) override { /* noop */ }

    void insertEquation(const WPXPropertyList &, const WPXString &) override { /* noop */ }

    void openPageSpan(const WPXPropertyList &) override { /* noop */ }

    void startDocument() override { /* noop */ };
};


wpd_stream_t wpd_memory_stream_create(const unsigned char *buf, size_t buf_len) {
    auto *input = new WPXStringStream(buf, buf_len);
    return input;
}

wpd_confidence_t wpd_is_file_format_supported(wpd_stream_t ptr) {
    auto *stream = (WPXStringStream *) ptr;
    WPDConfidence confidence = WPDocument::isFileFormatSupported(stream);

    return (wpd_confidence_t) confidence;
}

wpd_result_t wpd_parse(wpd_stream_t ptr, text_buffer_t *tex, document_t *doc) {
    auto *stream = (WPXStringStream *) ptr;

    auto myDoc = StringDocument(tex, doc);
    WPDResult result2 = WPDocument::parse(stream, &myDoc, nullptr);

    return (wpd_result_t) result2;
}

void wpd_memory_stream_destroy(wpd_stream_t ptr) {
    auto *stream = (WPXStringStream *) ptr;
    delete stream;
}
