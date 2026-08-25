#include <gtest/gtest.h>

#include "tests/support/subprocess.h"

#include <string>
#include <vector>

extern "C" {
#include "src/worker/protocol.h"
}

// Wraps an encoder's output in a frame the decoders can be pointed at.
// The encoder is invoked from the constructor body: passing the buffer and the length it writes as
// two arguments would leave their evaluation order unspecified.
struct Encoded {
    char *payload;
    uint32_t len = 0;

    template<typename Encode>
    explicit Encoded(Encode encode) { payload = encode(&len); }

    ~Encoded() { free(payload); }

    frame_t frame(const uint32_t type) const { return frame_t{type, len, payload}; }

    // Same frame with the payload cut short, to check that the decoders reject it
    frame_t truncated(const uint32_t type, const uint32_t bytes_removed) const {
        return frame_t{type, len - bytes_removed, payload};
    }
};

TEST(Protocol, JobRoundTrip) {
    proto_job_t job = {};
    strcpy(job.path, "/some/dir/a file.txt");
    job.mtime = 1754870400;
    job.size = 8589934592L;

    const Encoded encoded([&](uint32_t *len) { return proto_encode_job(&job, len); });

    proto_job_t decoded = {};
    const frame_t frame = encoded.frame(FRAME_JOB);
    ASSERT_EQ(proto_decode_job(&frame, &decoded), 0);

    ASSERT_STREQ(decoded.path, job.path);
    ASSERT_EQ(decoded.mtime, job.mtime);
    ASSERT_EQ(decoded.size, job.size);
}

TEST(Protocol, JobRejectsTruncatedPayload) {
    proto_job_t job = {};
    strcpy(job.path, "/some/dir/a file.txt");

    const Encoded encoded([&](uint32_t *len) { return proto_encode_job(&job, len); });

    for (uint32_t removed = 1; removed <= encoded.len; removed++) {
        proto_job_t decoded = {};
        frame_t frame = encoded.truncated(FRAME_JOB, removed);
        ASSERT_EQ(proto_decode_job(&frame, &decoded), -1) << "removed " << removed << " bytes";
    }
}

TEST(Protocol, DocRoundTrip) {
    proto_doc_t doc = {};
    strcpy(doc.path, "archive.zip#/inner/document.pdf");
    strcpy(doc.parent, "archive.zip");
    doc.mime = 0x0a0001;
    doc.mtime = 1754870400;
    doc.size = 123456;
    doc.thumbnail_count = 3;
    doc.json = strdup(R"({"name":"document","content":"hello"})");

    const Encoded encoded([&](uint32_t *len) { return proto_encode_doc(&doc, len); });

    proto_doc_t decoded = {};
    const frame_t frame = encoded.frame(FRAME_DOC);
    ASSERT_EQ(proto_decode_doc(&frame, &decoded), 0);

    ASSERT_STREQ(decoded.path, doc.path);
    ASSERT_STREQ(decoded.parent, doc.parent);
    ASSERT_EQ(decoded.mime, doc.mime);
    ASSERT_EQ(decoded.mtime, doc.mtime);
    ASSERT_EQ(decoded.size, doc.size);
    ASSERT_EQ(decoded.thumbnail_count, doc.thumbnail_count);
    ASSERT_STREQ(decoded.json, doc.json);

    free(doc.json);
    free(decoded.json);
}

TEST(Protocol, DocWithoutJsonRoundTrip) {
    // The placeholder row written for an archive before its children are parsed
    proto_doc_t doc = {};
    strcpy(doc.path, "archive.zip");
    doc.json = nullptr;

    const Encoded encoded([&](uint32_t *len) { return proto_encode_doc(&doc, len); });

    proto_doc_t decoded = {};
    const frame_t frame = encoded.frame(FRAME_DOC);
    ASSERT_EQ(proto_decode_doc(&frame, &decoded), 0);

    ASSERT_STREQ(decoded.path, doc.path);
    ASSERT_STREQ(decoded.parent, "");
    ASSERT_EQ(decoded.json, nullptr);
}

TEST(Protocol, DocRejectsTruncatedPayload) {
    proto_doc_t doc = {};
    strcpy(doc.path, "file.txt");
    doc.json = strdup("{}");

    const Encoded encoded([&](uint32_t *len) { return proto_encode_doc(&doc, len); });

    for (uint32_t removed = 1; removed <= encoded.len; removed++) {
        proto_doc_t decoded = {};
        frame_t frame = encoded.truncated(FRAME_DOC, removed);
        ASSERT_EQ(proto_decode_doc(&frame, &decoded), -1) << "removed " << removed << " bytes";
    }

    free(doc.json);
}

TEST(Protocol, ThumbRoundTrip) {
    std::vector<char> data(64 * 1024);
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = (char) (i & 0xFF);
    }

    const Encoded encoded([&](uint32_t *len) { return proto_encode_thumb(2, data.data(), data.size(), len); });

    proto_thumb_t decoded = {};
    const frame_t frame = encoded.frame(FRAME_THUMB);
    ASSERT_EQ(proto_decode_thumb(&frame, &decoded), 0);

    ASSERT_EQ(decoded.index, 2);
    ASSERT_EQ(decoded.size, data.size());
    ASSERT_EQ(memcmp(decoded.data, data.data(), data.size()), 0);
}

TEST(Protocol, MarkRoundTrip) {
    proto_mark_t mark = {};
    strcpy(mark.path, "sub/dir/file.txt");
    mark.mtime = 1754870400;

    const Encoded encoded([&](uint32_t *len) { return proto_encode_mark(&mark, len); });

    proto_mark_t decoded = {};
    const frame_t frame = encoded.frame(FRAME_REQ_MARK);
    ASSERT_EQ(proto_decode_mark(&frame, &decoded), 0);

    ASSERT_STREQ(decoded.path, mark.path);
    ASSERT_EQ(decoded.mtime, mark.mtime);
}

TEST(Protocol, Int32RoundTrip) {
    const Encoded encoded([&](uint32_t *len) { return proto_encode_i32(TRUE, len); });

    int32_t decoded = -1;
    const frame_t frame = encoded.frame(FRAME_RSP_MARK);
    ASSERT_EQ(proto_decode_i32(&frame, &decoded), 0);

    ASSERT_EQ(decoded, TRUE);
}

TEST(Protocol, Int32RejectsEmptyPayload) {
    const frame_t frame = {FRAME_RSP_MARK, 0, nullptr};

    int32_t decoded;
    ASSERT_EQ(proto_decode_i32(&frame, &decoded), -1);
}

/* Blocking IO over a pipe */

TEST(Protocol, WriteReadOverPipe) {
    int fds[2];
    ASSERT_EQ(sist2::test::make_pipe(fds), 0);

    const std::string json = R"({"content":"some text"})";
    ASSERT_EQ(frame_write(fds[1], FRAME_DOC, json.data(), json.size()), 0);
    ASSERT_EQ(frame_write(fds[1], FRAME_DONE, nullptr, 0), 0);
    close(fds[1]);

    frame_t frame = {};
    ASSERT_EQ(frame_read(fds[0], &frame), 0);
    ASSERT_EQ(frame.type, (uint32_t) FRAME_DOC);
    ASSERT_EQ(frame.len, json.size());
    ASSERT_EQ(memcmp(frame.payload, json.data(), json.size()), 0);
    frame_free(&frame);

    ASSERT_EQ(frame_read(fds[0], &frame), 0);
    ASSERT_EQ(frame.type, (uint32_t) FRAME_DONE);
    ASSERT_EQ(frame.len, 0u);
    ASSERT_EQ(frame.payload, nullptr);
    frame_free(&frame);

    // Clean EOF
    ASSERT_EQ(frame_read(fds[0], &frame), 1);

    close(fds[0]);
}

TEST(Protocol, ReadReportsTruncatedStreamAsError) {
    int fds[2];
    ASSERT_EQ(sist2::test::make_pipe(fds), 0);

    // A header promising 32 bytes, followed by only 4 and then EOF
    constexpr uint32_t header[2] = {FRAME_DOC, 32};
    ASSERT_EQ(write(fds[1], header, sizeof(header)), (ssize_t) sizeof(header));
    ASSERT_EQ(write(fds[1], "abcd", 4), 4);
    close(fds[1]);

    frame_t frame = {};
    ASSERT_EQ(frame_read(fds[0], &frame), -1);

    close(fds[0]);
}

TEST(Protocol, ReadRejectsOversizedFrame) {
    int fds[2];
    ASSERT_EQ(sist2::test::make_pipe(fds), 0);

    const uint32_t header[2] = {FRAME_DOC, FRAME_MAX_PAYLOAD + 1};
    ASSERT_EQ(write(fds[1], header, sizeof(header)), (ssize_t) sizeof(header));
    close(fds[1]);

    frame_t frame = {};
    ASSERT_EQ(frame_read(fds[0], &frame), -1);

    close(fds[0]);
}

/* Incremental decoding */

struct CollectedFrame {
    uint32_t type;
    std::string payload;
};

static void collect(const frame_t *frame, void *user_data) {
    auto *collected = (std::vector<CollectedFrame> *) user_data;
    collected->push_back({frame->type, std::string(frame->payload == nullptr ? "" : frame->payload, frame->len)});
}

// Serializes frames the same way frame_write() does, into a buffer the parser can be fed
static std::string frame_bytes(const uint32_t type, const std::string &payload) {
    const uint32_t header[2] = {type, (uint32_t) payload.size()};

    std::string bytes((const char *) header, sizeof(header));
    bytes += payload;

    return bytes;
}

TEST(FrameParser, SeveralFramesInOneChunk) {
    const std::string stream = frame_bytes(FRAME_DOC, "first") + frame_bytes(FRAME_THUMB, "second")
                         + frame_bytes(FRAME_DONE, "");

    frame_parser_t *parser = frame_parser_create();
    std::vector<CollectedFrame> collected;

    ASSERT_EQ(frame_parser_feed(parser, stream.data(), stream.size(), collect, &collected), 0);

    ASSERT_EQ(collected.size(), 3u);
    ASSERT_EQ(collected[0].type, (uint32_t) FRAME_DOC);
    ASSERT_EQ(collected[0].payload, "first");
    ASSERT_EQ(collected[1].type, (uint32_t) FRAME_THUMB);
    ASSERT_EQ(collected[1].payload, "second");
    ASSERT_EQ(collected[2].type, (uint32_t) FRAME_DONE);
    ASSERT_EQ(collected[2].payload, "");

    frame_parser_destroy(parser);
}

TEST(FrameParser, ReassemblesFramesFedOneByteAtATime) {
    const std::string big(100 * 1024, 'x');
    const std::string stream = frame_bytes(FRAME_DOC, "first") + frame_bytes(FRAME_THUMB, big);

    frame_parser_t *parser = frame_parser_create();
    std::vector<CollectedFrame> collected;

    for (char byte: stream) {
        ASSERT_EQ(frame_parser_feed(parser, &byte, 1, collect, &collected), 0);
    }

    ASSERT_EQ(collected.size(), 2u);
    ASSERT_EQ(collected[0].payload, "first");
    ASSERT_EQ(collected[1].payload, big);

    frame_parser_destroy(parser);
}

TEST(FrameParser, EmitsNothingForAPartialFrame) {
    const std::string stream = frame_bytes(FRAME_DOC, "payload");

    frame_parser_t *parser = frame_parser_create();
    std::vector<CollectedFrame> collected;

    ASSERT_EQ(frame_parser_feed(parser, stream.data(), stream.size() - 1, collect, &collected), 0);
    ASSERT_TRUE(collected.empty());

    ASSERT_EQ(frame_parser_feed(parser, stream.data() + stream.size() - 1, 1, collect, &collected), 0);
    ASSERT_EQ(collected.size(), 1u);
    ASSERT_EQ(collected[0].payload, "payload");

    frame_parser_destroy(parser);
}

TEST(FrameParser, RejectsOversizedFrame) {
    constexpr uint32_t header[2] = {FRAME_DOC, FRAME_MAX_PAYLOAD + 1};

    frame_parser_t *parser = frame_parser_create();
    std::vector<CollectedFrame> collected;

    ASSERT_EQ(frame_parser_feed(parser, (const char *) header, sizeof(header), collect, &collected), -1);
    ASSERT_TRUE(collected.empty());

    frame_parser_destroy(parser);
}

TEST(FrameParser, RoundTripsEncodedPayloads) {
    proto_job_t job = {};
    strcpy(job.path, "/root/file.txt");
    job.mtime = 42;
    job.size = 1024;

    const Encoded encoded([&](uint32_t *len) { return proto_encode_job(&job, len); });
    const std::string stream = frame_bytes(FRAME_JOB, std::string(encoded.payload, encoded.len));

    frame_parser_t *parser = frame_parser_create();
    std::vector<CollectedFrame> collected;
    ASSERT_EQ(frame_parser_feed(parser, stream.data(), stream.size(), collect, &collected), 0);

    ASSERT_EQ(collected.size(), 1u);

    const frame_t frame = {collected[0].type, (uint32_t) collected[0].payload.size(), collected[0].payload.data()};
    proto_job_t decoded = {};
    ASSERT_EQ(proto_decode_job(&frame, &decoded), 0);
    ASSERT_STREQ(decoded.path, job.path);
    ASSERT_EQ(decoded.size, job.size);

    frame_parser_destroy(parser);
}
