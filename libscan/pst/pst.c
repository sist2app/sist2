#include "pst.h"

#include <libpff.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>

#include "rtf.h"

#include "../sub_document.h"
#include "../util.h"

#ifdef _WIN32

#include <io.h>

#else

#include <unistd.h>

#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define PST_SIGNATURE "!BDN"

/* A body is only ever read to be indexed, so no more of it is copied than the index will hold.
 * Markup shrinks on the way to text, hence the factor. */
#define PST_BODY_SIZE_FACTOR 4
#define PST_MIN_BODY_SIZE ((size_t) 64 * 1024)

#define PST_MAX_ATTACHMENT_SIZE ((size64_t) 512 * 1024 * 1024)
#define PST_MAX_SPILL_SIZE ((int64_t) 4 * 1024 * 1024 * 1024)
#define PST_SPILL_BUF_SIZE (1024 * 1024)

/* A folder tree is a handful of levels deep; anything past this was built to be walked forever */
#define PST_MAX_FOLDER_DEPTH 32

/** Bytes of a subject kept in the path of a message */
#define PST_MAX_NAME_LEN 96

// PidTagDisplayCc, PidTagInternetMessageId, PidTagSenderSmtpAddress and
// PidTagSentRepresentingSmtpAddress, which libpff carries no constants for
#define PST_ENTRY_TYPE_MESSAGE_DISPLAY_CC 0x0e03
#define PST_ENTRY_TYPE_MESSAGE_INTERNET_MESSAGE_ID 0x1035
#define PST_ENTRY_TYPE_MESSAGE_SENDER_SMTP_ADDRESS 0x5d01
#define PST_ENTRY_TYPE_MESSAGE_SENT_REPRESENTING_SMTP_ADDRESS 0x5d02

typedef struct {
    scan_pst_ctx_t *ctx;
    vfile_t *f;
    parse_job_t *sub_job;

    /* Identifiers of the messages already indexed: a search folder holds the same message a mail
     * folder does, and walking both would index it twice */
    uint32_t *seen;
    size_t seen_size;
    size_t seen_count;

    int message_count;
} pst_walk_t;

static void log_pff_error(scan_pst_ctx_t *ctx, const char *filepath, const char *what,
                          libpff_error_t **error) {
    char message[512] = {0};

    if (error != NULL && *error != NULL) {
        libpff_error_sprint(*error, message, sizeof(message));
        libpff_error_free(error);
    }

    CTX_LOG_WARNINGF(filepath, "%s: %s", what, message);
}

/** A UTF-8 property of an item, or NULL when it does not carry one. Caller frees. */
static char *item_string(libpff_item_t *item, uint32_t entry_type) {
    size_t size = 0;
    libpff_error_t *error = NULL;

    if (libpff_message_get_entry_value_utf8_string_size(item, entry_type, &size, &error) != 1 ||
        size <= 1) {
        libpff_error_free(&error);
        return NULL;
    }

    char *value = malloc(size);
    if (libpff_message_get_entry_value_utf8_string(item, entry_type, (uint8_t *) value, size,
                                                   &error) != 1) {
        libpff_error_free(&error);
        free(value);
        return NULL;
    }

    return value;
}

/*
 * PidTagSubject may open with a marker — 0x01 and the length of the prefix that follows it,
 * "RE: " or "FW: " — which is not part of the subject anyone reads.
 */
static char *message_subject(libpff_item_t *message) {
    char *subject = item_string(message, LIBPFF_ENTRY_TYPE_MESSAGE_SUBJECT);

    if (subject != NULL && subject[0] == 0x01 && subject[1] != '\0') {
        memmove(subject, subject + 2, strlen(subject + 2) + 1);
    }

    return subject;
}

static uint32_t item_codepage(libpff_item_t *item, uint32_t entry_type) {
    libpff_record_set_t *record_set = NULL;
    libpff_record_entry_t *record_entry = NULL;
    libpff_error_t *error = NULL;
    uint32_t value = 0;

    if (libpff_item_get_record_set_by_index(item, 0, &record_set, &error) != 1) {
        libpff_error_free(&error);
        return 0;
    }

    if (libpff_record_set_get_entry_by_type(record_set, entry_type,
                                            LIBPFF_VALUE_TYPE_INTEGER_32BIT_SIGNED, &record_entry, 0,
                                            &error) == 1) {
        if (libpff_record_entry_get_data_as_32bit_integer(record_entry, &value, &error) != 1) {
            value = 0;
        }
        libpff_record_entry_free(&record_entry, &error);
    }

    libpff_record_set_free(&record_set, &error);
    libpff_error_free(&error);

    return value;
}

typedef int (*get_time_t)(libpff_item_t *, uint64_t *, libpff_error_t **);

static int64_t message_timestamp(libpff_item_t *message, get_time_t get_time) {
    uint64_t filetime = 0;
    libpff_error_t *error = NULL;

    if (get_time(message, &filetime, &error) != 1 || filetime == 0) {
        libpff_error_free(&error);
        return 0;
    }

    // FILETIME counts 100ns intervals since 1601-01-01
    return (int64_t) (filetime / 10000000) - 11644473600;
}

/** The time the message was received, or failing that the time it was sent or written */
static int64_t message_date(libpff_item_t *message) {
    int64_t timestamp = message_timestamp(message, libpff_message_get_delivery_time);

    if (timestamp == 0) {
        timestamp = message_timestamp(message, libpff_message_get_client_submit_time);
    }
    if (timestamp == 0) {
        timestamp = message_timestamp(message, libpff_message_get_creation_time);
    }

    return timestamp;
}

/** The charset labels libenvelope knows, for the codepages a message body is written in */
static const char *charset_name(uint32_t codepage) {
    switch (codepage) {
        case 65001:
            return "utf-8";
        case 1250:
            return "windows-1250";
        case 1251:
            return "windows-1251";
        case 1252:
            return "windows-1252";
        case 1253:
            return "windows-1253";
        case 1254:
            return "windows-1254";
        case 1255:
            return "windows-1255";
        case 1256:
            return "windows-1256";
        case 1257:
            return "windows-1257";
        case 1258:
            return "windows-1258";
        case 20866:
            return "koi8-r";
        case 21866:
            return "koi8-u";
        case 28591:
            return "iso-8859-1";
        case 28592:
            return "iso-8859-2";
        case 28595:
            return "iso-8859-5";
        case 28597:
            return "iso-8859-7";
        case 28599:
            return "iso-8859-9";
        case 28605:
            return "iso-8859-15";
        default:
            return "utf-8";
    }
}

static void format_date(int64_t timestamp, char *buf, size_t buf_size) {
    static const char *days[] = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    int64_t seconds_of_day = timestamp % 86400;
    int64_t days_since_epoch = timestamp / 86400;
    if (seconds_of_day < 0) {
        seconds_of_day += 86400;
        days_since_epoch -= 1;
    }

    // Civil date from a day count, shifting the era to start on a leap year (0000-03-01)
    const int64_t z = days_since_epoch + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t day_of_era = z - era * 146097;
    const int64_t year_of_era =
            (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const int64_t shifted_month = (5 * day_of_year + 2) / 153;

    const int day = (int) (day_of_year - (153 * shifted_month + 2) / 5 + 1);
    const int month = (int) (shifted_month < 10 ? shifted_month + 3 : shifted_month - 9);
    const int64_t year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);

    const int weekday = (int) (((days_since_epoch % 7) + 7) % 7);

    snprintf(buf, buf_size, "%s, %d %s %" PRId64 " %02d:%02d:%02d +0000", days[weekday], day,
             months[month - 1], year, (int) (seconds_of_day / 3600),
             (int) (seconds_of_day / 60 % 60), (int) (seconds_of_day % 60));
}

/*
 * A header value written by whoever wrote the message: a newline in it would turn the rest into
 * headers of its own, and a NUL would cut the message short.
 */
static void append_header(dyn_buffer_t *buf, const char *name, const char *value) {
    if (value == NULL || *value == '\0') {
        return;
    }

    dyn_buffer_append_string(buf, name);
    dyn_buffer_append_string(buf, ": ");

    for (const char *p = value; *p != '\0'; p++) {
        const unsigned char c = (unsigned char) *p;
        dyn_buffer_write_char(buf, (c < 0x20 || c == 0x7f) ? ' ' : *p);
    }

    dyn_buffer_append_string(buf, "\r\n");
}

/*
 * The mail address of a sender. A message that never left the organisation carries the X.500
 * address of the sender's mailbox instead of a mail address, and the mail address of it, when the
 * message kept one at all, is a property of its own.
 */
static char *sender_address(libpff_item_t *message, uint32_t address_type_entry,
                            uint32_t address_entry, uint32_t smtp_address_entry) {
    char *address_type = item_string(message, address_type_entry);
    const int is_smtp = address_type != NULL && strcmp(address_type, "SMTP") == 0;
    free(address_type);

    if (is_smtp) {
        return item_string(message, address_entry);
    }

    return item_string(message, smtp_address_entry);
}

/** From: as an address when the message carries one, and as a display name otherwise */
static void append_from(dyn_buffer_t *buf, libpff_item_t *message) {
    char *name = item_string(message, LIBPFF_ENTRY_TYPE_MESSAGE_SENDER_NAME);
    char *address = sender_address(message, LIBPFF_ENTRY_TYPE_MESSAGE_SENDER_ADDRESS_TYPE,
                                   LIBPFF_ENTRY_TYPE_MESSAGE_SENDER_EMAIL_ADDRESS,
                                   PST_ENTRY_TYPE_MESSAGE_SENDER_SMTP_ADDRESS);

    if (name == NULL && address == NULL) {
        name = item_string(message, LIBPFF_ENTRY_TYPE_MESSAGE_SENT_REPRESENTING_NAME);
        address = sender_address(message,
                                 LIBPFF_ENTRY_TYPE_MESSAGE_SENT_REPRESENTING_ADDRESS_TYPE,
                                 LIBPFF_ENTRY_TYPE_MESSAGE_SENT_REPRESENTING_EMAIL_ADDRESS,
                                 PST_ENTRY_TYPE_MESSAGE_SENT_REPRESENTING_SMTP_ADDRESS);
    }

    if (name != NULL && address != NULL) {
        char from[1024];
        snprintf(from, sizeof(from), "%s <%s>", name, address);
        append_header(buf, "From", from);
    } else {
        append_header(buf, "From", name != NULL ? name : address);
    }

    free(name);
    free(address);
}

/**
 * The body of a message, and the media type to hand it over as. Plain text is preferred over HTML,
 * which is preferred over the RTF a message written in Outlook may carry instead of either.
 */
static char *message_body(libpff_item_t *message, size_t max_size, char *content_type,
                          size_t content_type_size, size_t *out_size) {
    libpff_error_t *error = NULL;
    size_t size = 0;

    if (libpff_message_get_plain_text_body_size(message, &size, &error) == 1 && size > 1) {
        char *body = malloc(size);
        if (libpff_message_get_plain_text_body(message, (uint8_t *) body, size, &error) == 1) {
            snprintf(content_type, content_type_size, "text/plain; charset=utf-8");
            *out_size = MIN(size - 1, max_size);
            return body;
        }
        free(body);
    }
    libpff_error_free(&error);

    if (libpff_message_get_html_body_size(message, &size, &error) == 1 && size > 1) {
        char *body = malloc(size);
        if (libpff_message_get_html_body(message, (uint8_t *) body, size, &error) == 1) {
            const uint32_t codepage =
                    item_codepage(message, LIBPFF_ENTRY_TYPE_MESSAGE_BODY_CODEPAGE);
            snprintf(content_type, content_type_size, "text/html; charset=%s",
                     charset_name(codepage));

            *out_size = MIN(size - 1, max_size);
            return body;
        }
        free(body);
    }
    libpff_error_free(&error);

    if (libpff_message_get_rtf_body_size(message, &size, &error) == 1 && size > 1) {
        char *rtf = malloc(size);
        if (libpff_message_get_rtf_body(message, (uint8_t *) rtf, size, &error) == 1) {
            char *body = rtf_to_text(rtf, size - 1, max_size, out_size);
            free(rtf);

            snprintf(content_type, content_type_size, "text/plain; charset=utf-8");
            return body;
        }
        free(rtf);
    }
    libpff_error_free(&error);

    *out_size = 0;
    return NULL;
}

/*
 * A message as an RFC 5322 message, which is what the mail parser reads. A PST holds properties
 * rather than a message, so the headers are written back out of them; the transport headers a
 * received message kept are not reused as they stand, because they describe MIME parts that only
 * exist inside the PST.
 */
static char *build_message(scan_pst_ctx_t *ctx, libpff_item_t *message, size_t *out_size) {
    dyn_buffer_t buf = dyn_buffer_create();

    char *subject = message_subject(message);
    char *to = item_string(message, LIBPFF_ENTRY_TYPE_MESSAGE_DISPLAY_TO);
    char *cc = item_string(message, PST_ENTRY_TYPE_MESSAGE_DISPLAY_CC);
    char *message_id = item_string(message, PST_ENTRY_TYPE_MESSAGE_INTERNET_MESSAGE_ID);

    const int64_t timestamp = message_date(message);

    append_from(&buf, message);
    append_header(&buf, "To", to);
    append_header(&buf, "Cc", cc);
    append_header(&buf, "Subject", subject);
    append_header(&buf, "Message-ID", message_id);

    if (timestamp != 0) {
        char date[64];
        format_date(timestamp, date, sizeof(date));
        append_header(&buf, "Date", date);
    }

    size_t max_body_size = 0;
    if (ctx->content_size > 0) {
        max_body_size = MAX((size_t) ctx->content_size * PST_BODY_SIZE_FACTOR, PST_MIN_BODY_SIZE);
    }

    char content_type[64];
    snprintf(content_type, sizeof(content_type), "text/plain; charset=utf-8");

    size_t body_size = 0;
    char *body = max_body_size > 0
                 ? message_body(message, max_body_size, content_type, sizeof(content_type),
                                &body_size)
                 : NULL;

    append_header(&buf, "MIME-Version", "1.0");
    append_header(&buf, "Content-Type", content_type);
    dyn_buffer_append_string(&buf, "\r\n");

    if (body != NULL) {
        dyn_buffer_write(&buf, body, body_size);
        free(body);
    }

    free(subject);
    free(to);
    free(cc);
    free(message_id);

    *out_size = buf.cur;
    return buf.buf;
}

/** Truncates on a codepoint boundary, so that a name cut short is still valid UTF-8 */
static void truncate_utf8(char *str, size_t max_len) {
    size_t len = strlen(str);

    if (len <= max_len) {
        return;
    }

    while (max_len > 0 && ((unsigned char) str[max_len] & 0xc0) == 0x80) {
        max_len -= 1;
    }

    str[max_len] = '\0';
}

/*
 * The name a message is indexed under. Subjects repeat and are not unique inside a folder, so the
 * identifier of the message — which is what the PST itself keys it on — is part of the name.
 */
static void message_name(libpff_item_t *message, uint32_t identifier, char *buf, size_t buf_size) {
    char *subject = message_subject(message);

    if (subject == NULL) {
        snprintf(buf, buf_size, "message-%" PRIu32 ".eml", identifier);
        return;
    }

    char safe_subject[PATH_MAX];
    sub_document_sanitize_name(subject, safe_subject, sizeof(safe_subject));
    truncate_utf8(safe_subject, PST_MAX_NAME_LEN);
    free(subject);

    if (*safe_subject == '\0') {
        snprintf(buf, buf_size, "message-%" PRIu32 ".eml", identifier);
        return;
    }

    snprintf(buf, buf_size, "%s (%" PRIu32 ").eml", safe_subject, identifier);
}

static uint32_t item_identifier(libpff_item_t *item) {
    uint32_t identifier = 0;
    libpff_error_t *error = NULL;

    if (libpff_item_get_identifier(item, &identifier, &error) != 1) {
        libpff_error_free(&error);
        return 0;
    }

    return identifier;
}

static uint8_t item_type(libpff_item_t *item) {
    uint8_t type = LIBPFF_ITEM_TYPE_UNDEFINED;
    libpff_error_t *error = NULL;

    if (libpff_item_get_type(item, &type, &error) != 1) {
        libpff_error_free(&error);
        return LIBPFF_ITEM_TYPE_UNDEFINED;
    }

    return type;
}

/** The item types that are mail; a calendar, contact or task item is not indexed */
static int is_message_item(uint8_t type) {
    switch (type) {
        case LIBPFF_ITEM_TYPE_COMMON:
        case LIBPFF_ITEM_TYPE_CONFLICT_MESSAGE:
        case LIBPFF_ITEM_TYPE_EMAIL:
        case LIBPFF_ITEM_TYPE_EMAIL_SMIME:
        case LIBPFF_ITEM_TYPE_FAX:
        case LIBPFF_ITEM_TYPE_MEETING:
        case LIBPFF_ITEM_TYPE_MMS:
        case LIBPFF_ITEM_TYPE_POSTING_NOTE:
        case LIBPFF_ITEM_TYPE_RSS_FEED:
        case LIBPFF_ITEM_TYPE_SHARING:
        case LIBPFF_ITEM_TYPE_SMS:
        case LIBPFF_ITEM_TYPE_TASK_REQUEST:
        case LIBPFF_ITEM_TYPE_VOICEMAIL:
            return TRUE;
        default:
            return FALSE;
    }
}

/** Returns FALSE when the identifier was already indexed */
static int mark_seen(pst_walk_t *walk, uint32_t identifier) {
    if (walk->seen_count * 2 >= walk->seen_size) {
        const size_t size = walk->seen_size * 2;
        uint32_t *seen = calloc(size, sizeof(uint32_t));

        for (size_t i = 0; i < walk->seen_size; i++) {
            if (walk->seen[i] == 0) {
                continue;
            }
            size_t slot = walk->seen[i] & (size - 1);
            while (seen[slot] != 0) {
                slot = (slot + 1) & (size - 1);
            }
            seen[slot] = walk->seen[i];
        }

        free(walk->seen);
        walk->seen = seen;
        walk->seen_size = size;
    }

    size_t slot = identifier & (walk->seen_size - 1);
    while (walk->seen[slot] != 0) {
        if (walk->seen[slot] == identifier) {
            return FALSE;
        }
        slot = (slot + 1) & (walk->seen_size - 1);
    }

    walk->seen[slot] = identifier;
    walk->seen_count += 1;

    return TRUE;
}

static void parse_message(pst_walk_t *walk, libpff_item_t *message, const char *prefix,
                          const char *separator);

/**
 * Every attachment of a message as a document of its own, parented to the message. An attachment
 * that is a message itself is written back out and walked like any other.
 */
static void parse_attachments(pst_walk_t *walk, libpff_item_t *message, const char *message_path) {
    scan_pst_ctx_t *ctx = walk->ctx;

    int count = 0;
    libpff_error_t *error = NULL;

    if (libpff_message_get_number_of_attachments(message, &count, &error) != 1) {
        libpff_error_free(&error);
        return;
    }

    for (int i = 0; i < count; i++) {
        libpff_item_t *attachment = NULL;

        if (libpff_message_get_attachment(message, i, &attachment, &error) != 1) {
            log_pff_error(ctx, walk->f->filepath, "libpff_message_get_attachment() failed", &error);
            continue;
        }

        int type = LIBPFF_ATTACHMENT_TYPE_UNDEFINED;
        if (libpff_attachment_get_type(attachment, &type, &error) != 1) {
            libpff_error_free(&error);
            libpff_item_free(&attachment, &error);
            continue;
        }

        if (type == LIBPFF_ATTACHMENT_TYPE_ITEM) {
            libpff_item_t *attached_item = NULL;

            if (libpff_attachment_get_item(attachment, &attached_item, &error) == 1) {
                if (is_message_item(item_type(attached_item))) {
                    // A message attached to a message is a document below it, not beside it
                    parse_message(walk, attached_item, message_path, "#/");
                }
                libpff_item_free(&attached_item, &error);
            } else {
                libpff_error_free(&error);
            }

            libpff_item_free(&attachment, &error);
            continue;
        }

        if (type != LIBPFF_ATTACHMENT_TYPE_DATA) {
            libpff_item_free(&attachment, &error);
            continue;
        }

        size64_t size = 0;
        if (libpff_attachment_get_data_size(attachment, &size, &error) != 1 || size == 0) {
            libpff_error_free(&error);
            libpff_item_free(&attachment, &error);
            continue;
        }

        char attachment_name[PATH_MAX];
        char *filename = item_string(attachment, LIBPFF_ENTRY_TYPE_ATTACHMENT_FILENAME_LONG);
        if (filename == NULL) {
            filename = item_string(attachment, LIBPFF_ENTRY_TYPE_ATTACHMENT_FILENAME_SHORT);
        }

        if (filename != NULL) {
            sub_document_sanitize_name(filename, attachment_name, sizeof(attachment_name));
            free(filename);
        }

        if (filename == NULL || *attachment_name == '\0') {
            snprintf(attachment_name, sizeof(attachment_name), "attachment-%d", i);
        }

        char name[PATH_MAX * 3];
        const int name_len = snprintf(name, sizeof(name), "%s#/%s", message_path, attachment_name);

        if (name_len < 0 || name_len >= (int) sizeof(name)) {
            CTX_LOG_ERRORF(walk->f->filepath, "Skipped %s, path too long", attachment_name);
            libpff_item_free(&attachment, &error);
            continue;
        }

        if (size > PST_MAX_ATTACHMENT_SIZE) {
            CTX_LOG_WARNINGF(walk->f->filepath, "Skipped %s, attachment is %" PRIu64 " bytes", name,
                             (uint64_t) size);
            libpff_item_free(&attachment, &error);
            continue;
        }

        char *data = malloc((size_t) size);
        if (data == NULL) {
            CTX_LOG_ERRORF(walk->f->filepath, "Skipped %s, out of memory", name);
            libpff_item_free(&attachment, &error);
            continue;
        }

        const ssize_t read = libpff_attachment_data_read_buffer(attachment, (uint8_t *) data,
                                                                (size_t) size, &error);
        if (read < 0) {
            log_pff_error(ctx, walk->f->filepath, "libpff_attachment_data_read_buffer() failed",
                          &error);
        } else {
            sub_document_submit(ctx->parse, walk->f, walk->sub_job, name, data, (size_t) read);
        }

        free(data);
        libpff_item_free(&attachment, &error);
    }
}

/** One message as a document, and everything attached to it as documents of its own */
static void parse_message(pst_walk_t *walk, libpff_item_t *message, const char *prefix,
                          const char *separator) {
    scan_pst_ctx_t *ctx = walk->ctx;

    const uint32_t identifier = item_identifier(message);

    char name[PATH_MAX];
    message_name(message, identifier, name, sizeof(name));

    char message_path[PATH_MAX * 2];
    const int path_len = snprintf(message_path, sizeof(message_path), "%s%s%s", prefix,
                                  *prefix == '\0' ? "" : separator, name);
    if (path_len < 0 || path_len >= (int) sizeof(message_path)) {
        CTX_LOG_ERRORF(walk->f->filepath, "Skipped %s, path too long", name);
        return;
    }

    size_t size = 0;
    char *eml = build_message(ctx, message, &size);

    const int64_t timestamp = message_date(message);

    const int mtime = walk->sub_job->vfile.mtime;
    if (timestamp > 0) {
        walk->sub_job->vfile.mtime = (int) timestamp;
    }

    const int submitted =
            sub_document_submit(ctx->parse, walk->f, walk->sub_job, message_path, eml, size);

    walk->sub_job->vfile.mtime = mtime;
    free(eml);

    if (!submitted) {
        return;
    }
    walk->message_count += 1;

    if (sub_document_depth(walk->sub_job->filepath) >= SUB_DOCUMENT_MAX_DEPTH) {
        CTX_LOG_ERRORF(walk->f->filepath, "Attachments of %s skipped, messages nested more than "
                                          "%d deep", message_path, SUB_DOCUMENT_MAX_DEPTH);
        return;
    }

    // The attachments are parented to the message, which has just been written
    char parent[PATH_MAX * 2 + 1];
    strcpy(parent, walk->sub_job->parent);

    const int parent_len = snprintf(walk->sub_job->parent, sizeof(walk->sub_job->parent), "%s#/%s",
                                    walk->f->filepath, message_path);

    if (parent_len > 0 && parent_len < (int) sizeof(walk->sub_job->parent)) {
        parse_attachments(walk, message, message_path);
    } else {
        CTX_LOG_ERRORF(walk->f->filepath, "Attachments of %s skipped, path too long", message_path);
    }

    strcpy(walk->sub_job->parent, parent);
}

static void walk_folder(pst_walk_t *walk, libpff_item_t *folder, const char *path, int depth) {
    scan_pst_ctx_t *ctx = walk->ctx;
    libpff_error_t *error = NULL;

    int count = 0;
    if (libpff_folder_get_number_of_sub_messages(folder, &count, &error) == 1) {
        for (int i = 0; i < count; i++) {
            libpff_item_t *message = NULL;

            if (libpff_folder_get_sub_message(folder, i, &message, &error) != 1) {
                log_pff_error(ctx, walk->f->filepath, "libpff_folder_get_sub_message() failed",
                              &error);
                continue;
            }

            const uint32_t identifier = item_identifier(message);

            if (is_message_item(item_type(message)) && mark_seen(walk, identifier)) {
                parse_message(walk, message, path, "/");
            }

            libpff_item_free(&message, &error);
        }
    } else {
        libpff_error_free(&error);
    }

    if (depth >= PST_MAX_FOLDER_DEPTH) {
        CTX_LOG_ERRORF(walk->f->filepath, "Skipped the folders below %s, nested more than %d deep",
                       path, PST_MAX_FOLDER_DEPTH);
        return;
    }

    count = 0;
    if (libpff_folder_get_number_of_sub_folders(folder, &count, &error) != 1) {
        libpff_error_free(&error);
        return;
    }

    for (int i = 0; i < count; i++) {
        libpff_item_t *sub_folder = NULL;

        if (libpff_folder_get_sub_folder(folder, i, &sub_folder, &error) != 1) {
            log_pff_error(ctx, walk->f->filepath, "libpff_folder_get_sub_folder() failed", &error);
            continue;
        }

        char name[PATH_MAX] = {0};
        size_t name_size = 0;

        if (libpff_folder_get_utf8_name_size(sub_folder, &name_size, &error) == 1 &&
            name_size > 1 && name_size <= sizeof(name)) {
            char folder_name[PATH_MAX];

            if (libpff_folder_get_utf8_name(sub_folder, (uint8_t *) folder_name, name_size,
                                            &error) == 1) {
                sub_document_sanitize_name(folder_name, name, sizeof(name));
                truncate_utf8(name, PST_MAX_NAME_LEN);
            }
        }
        libpff_error_free(&error);

        if (*name == '\0') {
            snprintf(name, sizeof(name), "folder-%" PRIu32, item_identifier(sub_folder));
        }

        char sub_path[PATH_MAX * 2];
        const int path_len = snprintf(sub_path, sizeof(sub_path), "%s%s%s", path,
                                      *path == '\0' ? "" : "/", name);

        if (path_len > 0 && path_len < (int) sizeof(sub_path)) {
            walk_folder(walk, sub_folder, sub_path, depth + 1);
        } else {
            CTX_LOG_ERRORF(walk->f->filepath, "Skipped %s, path too long", name);
        }

        libpff_item_free(&sub_folder, &error);
    }
}

static const char *temp_directory() {
#ifdef _WIN32
    return sist_temp_dir();
#else
    const char *directory = getenv("TMPDIR");
    return (directory != NULL && *directory != '\0') ? directory : "/tmp";
#endif
}

/**
 * A copy of the file on disk, which is the only thing libpff can open: a PST reached through an
 * archive or a message has no path of its own, and is read through a stream that cannot seek.
 * Returns the path of the copy, which the caller removes, or NULL.
 */
static char *spill_to_temp_file(scan_pst_ctx_t *ctx, vfile_t *f) {
    char *path = malloc(PATH_MAX);
    int fd;

#ifdef _WIN32
    unsigned char random[8];
    if (sist_random_bytes(random, sizeof(random)) != 0) {
        free(path);
        return NULL;
    }

    char name[sizeof(random) * 2 + 1];
    for (size_t i = 0; i < sizeof(random); i++) {
        snprintf(name + i * 2, 3, "%02x", random[i]);
    }
    snprintf(path, PATH_MAX, "%s/sist2-pst-%s.tmp", temp_directory(), name);

    fd = sist_open(path, O_CREAT | O_EXCL | O_RDWR | O_BINARY);
#else
    snprintf(path, PATH_MAX, "%s/sist2-pst-XXXXXX", temp_directory());
    fd = mkstemp(path);
#endif

    if (fd < 0) {
        CTX_LOG_ERRORF(f->filepath, "Could not create a temporary file in %s", temp_directory());
        free(path);
        return NULL;
    }

    FILE *file = fdopen(fd, "wb");
    if (file == NULL) {
        close(fd);
        remove(path);
        free(path);
        return NULL;
    }

    char *buf = malloc(PST_SPILL_BUF_SIZE);
    int failed = FALSE;

    while (TRUE) {
        const int bytes_read = f->read(f, buf, PST_SPILL_BUF_SIZE);

        if (bytes_read < 0) {
            CTX_LOG_ERROR(f->filepath, "read() failed");
            failed = TRUE;
            break;
        }
        if (bytes_read == 0) {
            break;
        }
        if (fwrite(buf, 1, bytes_read, file) != (size_t) bytes_read) {
            CTX_LOG_ERRORF(f->filepath, "Could not write %s, out of space?", path);
            failed = TRUE;
            break;
        }
    }

    free(buf);
    fclose(file);

    if (failed) {
        remove(path);
        free(path);
        return NULL;
    }

    return path;
}

static int has_pst_signature(scan_pst_ctx_t *ctx, const char *path) {
    const int fd = sist_open(path, O_RDONLY | O_BINARY);

    if (fd < 0) {
        CTX_LOG_ERRORF(path, "open(): [%d] %s", errno, strerror(errno));
        return FALSE;
    }

    char signature[4] = {0};
    const int bytes_read = (int) read(fd, signature, sizeof(signature));
    close(fd);

    return bytes_read == (int) sizeof(signature) &&
           memcmp(signature, PST_SIGNATURE, sizeof(signature)) == 0;
}

scan_code_t parse_pst(scan_pst_ctx_t *ctx, vfile_t *f, document_t *doc) {

    if (sub_document_depth(f->filepath) >= SUB_DOCUMENT_MAX_DEPTH) {
        CTX_LOG_ERRORF(f->filepath, "Skipped, mailboxes nested more than %d deep",
                       SUB_DOCUMENT_MAX_DEPTH);
        return SCAN_OK;
    }

    char *temp_path = NULL;

    if (!f->is_fs_file) {
        if ((int64_t) f->st_size > PST_MAX_SPILL_SIZE) {
            CTX_LOG_WARNINGF(f->filepath, "Skipped, a mailbox read from a container is copied to "
                                          "disk first and this one is %" PRId64 " bytes",
                             (int64_t) f->st_size);
            return SCAN_OK;
        }

        temp_path = spill_to_temp_file(ctx, f);
        if (temp_path == NULL) {
            return SCAN_ERR_READ;
        }
    }

    const char *path = temp_path != NULL ? temp_path : f->filepath;

    // The media type libmagic reports is shared with .msg items, which are not PFF files at all
    if (!has_pst_signature(ctx, path)) {
        if (temp_path != NULL) {
            remove(temp_path);
            free(temp_path);
        }
        return SCAN_OK;
    }

    libpff_error_t *error = NULL;
    libpff_file_t *file = NULL;

    if (libpff_file_initialize(&file, &error) != 1) {
        log_pff_error(ctx, f->filepath, "libpff_file_initialize() failed", &error);
        goto err;
    }

    if (libpff_file_open(file, path, LIBPFF_OPEN_READ, &error) != 1) {
        log_pff_error(ctx, f->filepath, "libpff_file_open() failed", &error);
        libpff_file_free(&file, &error);
        goto err;
    }

    libpff_item_t *root = NULL;
    if (libpff_file_get_root_folder(file, &root, &error) != 1) {
        log_pff_error(ctx, f->filepath, "libpff_file_get_root_folder() failed", &error);
        libpff_file_close(file, &error);
        libpff_file_free(&file, &error);
        goto err;
    }

    pst_walk_t walk = {
            .ctx = ctx,
            .f = f,
            .sub_job = sub_document_job_create(f, doc->filepath, ctx->log, ctx->logf),
            .seen = calloc(1024, sizeof(uint32_t)),
            .seen_size = 1024,
    };

    walk_folder(&walk, root, "", 0);

    CTX_LOG_DEBUGF(f->filepath, "Read %d messages", walk.message_count);

    free(walk.sub_job);
    free(walk.seen);

    libpff_item_free(&root, &error);
    libpff_file_close(file, &error);
    libpff_file_free(&file, &error);
    libpff_error_free(&error);

    if (temp_path != NULL) {
        remove(temp_path);
        free(temp_path);
    }

    return SCAN_OK;

    err:
    if (temp_path != NULL) {
        remove(temp_path);
        free(temp_path);
    }

    return SCAN_ERR_READ;
}
