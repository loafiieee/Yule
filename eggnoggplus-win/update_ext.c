#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "update_ext.h"

/*
 * Owner-side release tooling reads this unique marker from the compiled proxy.
 * It prevents a channel such as 1.1 from packaging a DLL that still identifies
 * itself as 1.0 and repeatedly offering the same update after relaunch.
 */
const char g_yule_framework_version_marker[] =
    "YULE_FRAMEWORK_VERSION=" FRAMEWORK_VERSION;

#if defined(UPDATE_EXT_TEST) || defined(UPDATE_EXT_HELPER)
#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...)  ((void)0)
#define LOG_WARN(...)  ((void)0)
#define LOG_ERROR(...) ((void)0)
#else
#include "log.h"
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define UPDATE_DEFAULT_CHANNEL \
    "https://loafiieee.com/yule/releases/latest.json"
#define UPDATE_CFG_REL              "mods\\modframework.cfg"
#define UPDATE_STAGING_REL          "mods\\update_staging"
#define UPDATE_JOURNAL_REL          "mods\\update_staging\\transaction.journal"
#define UPDATE_MANIFEST_MAX_BYTES   (1024u * 1024u)
#define UPDATE_FILE_MAX_BYTES       (128u * 1024u * 1024u)
#define UPDATE_TOTAL_MAX_BYTES      (256u * 1024u * 1024u)
#define UPDATE_CFG_MAX_BYTES        (256u * 1024u)
#define UPDATE_HTTP_TIMEOUT_MS      8000
#define UPDATE_SHUTDOWN_WAIT_MS     20000
#define UPDATE_ABS_CAP              1024
#define UPDATE_STATUS_CAP           192
#define UPDATE_URL_CAP              2048
#define UPDATE_JSON_MAX_DEPTH       16
#define UPDATE_JOURNAL_MAGIC_V1     "YULE_UPDATE_JOURNAL 1"
#define UPDATE_JOURNAL_MAGIC_V2     "YULE_UPDATE_JOURNAL 2"
#define UPDATE_JOURNAL_MAGIC_V3     "YULE_UPDATE_JOURNAL 3"
#define UPDATE_JOURNAL_MAGIC        UPDATE_JOURNAL_MAGIC_V3
#define UPDATE_RECOVERY_SUFFIX      ".update-recovery"
#define UPDATE_UNKNOWN_SHA256       \
    "0000000000000000000000000000000000000000000000000000000000000000"

typedef struct UpdateManifest {
    char version[64];
    char base[UPDATE_URL_CAP];
    UpdateFileSpec files[UPDATE_MAX_FILES];
    size_t file_count;
} UpdateManifest;

typedef struct UpdateState {
    CRITICAL_SECTION lock;
    CRITICAL_SECTION cfg_lock;
    char root[UPDATE_ABS_CAP];
    char channel_url[UPDATE_URL_CAP];
    char latest_version[64];
    char status_line[UPDATE_STATUS_CAP];
    char error[UPDATE_STATUS_CAP];
    UpdateManifest manifest;
    HANDLE worker;
    int worker_active;
} UpdateState;

static INIT_ONCE g_update_once = INIT_ONCE_STATIC_INIT;
static UpdateState g_update;
static volatile LONG g_update_status = UPDATE_IDLE;
static volatile LONG g_update_auto = 1;
static volatile LONG g_update_notice = 0;
static volatile LONG g_update_booted = 0;
static volatile LONG g_update_cancel = 0;
static volatile LONG g_update_apply_requested = 0;
static volatile LONG g_update_recovery_restart = 0;
static volatile LONG g_update_helper_pending = 0;

#ifdef UPDATE_EXT_TEST
/* Simulates Windows keeping a renamed image section alive until process exit. */
static int g_update_test_hold_recovery_quarantine = 0;
static const char* g_update_test_probe_error_path = NULL;
#endif

#if defined(__GNUC__)
static __thread char g_update_tls_latest[64];
static __thread char g_update_tls_status[UPDATE_STATUS_CAP];
#else
__declspec(thread) static char g_update_tls_latest[64];
__declspec(thread) static char g_update_tls_status[UPDATE_STATUS_CAP];
#endif

static size_t update_strnlen(const char* s, size_t cap) {
    size_t n = 0;
    if (!s) return 0;
    while (n < cap && s[n]) n++;
    return n;
}

static int update_copy(char* dst, size_t cap, const char* src) {
    size_t n;
    if (!dst || cap == 0 || !src) return 0;
    n = strlen(src);
    if (n >= cap) return 0;
    memcpy(dst, src, n + 1);
    return 1;
}

static void update_copy_trunc(char* dst, size_t cap, const char* src) {
    size_t n;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int update_join_path(char* out, size_t cap,
                            const char* left, const char* right) {
    size_t a;
    size_t b;
    int separator;
    if (!out || !left || !right) return 0;
    a = strlen(left);
    b = strlen(right);
    separator = (a > 0 && left[a - 1] != '\\' && left[a - 1] != '/');
    if (a + (size_t)separator + b + 1 > cap) return 0;
    memcpy(out, left, a);
    if (separator) out[a++] = '\\';
    memcpy(out + a, right, b + 1);
    return 1;
}

static int update_backup_path(char* out, size_t cap, const char* target) {
    static const char suffix[] = ".old";
    size_t len;
    if (!out || !target) return 0;
    len = strlen(target);
    if (len + sizeof(suffix) > cap) return 0;
    memcpy(out, target, len);
    memcpy(out + len, suffix, sizeof(suffix));
    return 1;
}

static void update_slashes_to_backslashes(char* s) {
    if (!s) return;
    while (*s) {
        if (*s == '/') *s = '\\';
        s++;
    }
}

static int update_get_root(char out[UPDATE_ABS_CAP]) {
    HMODULE module = NULL;
    DWORD n;
    char* slash;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)(uintptr_t)&update_ext_boot, &module)) {
        module = GetModuleHandleA(NULL);
    }
    n = GetModuleFileNameA(module, out, UPDATE_ABS_CAP);
    if (n == 0 || n >= UPDATE_ABS_CAP) return 0;
    slash = strrchr(out, '\\');
    if (!slash) slash = strrchr(out, '/');
    if (!slash) return 0;
    *slash = '\0';
    return out[0] != '\0';
}

static BOOL CALLBACK update_init_once(PINIT_ONCE once, PVOID param, PVOID* ctx) {
    (void)once;
    (void)param;
    (void)ctx;
    memset(&g_update, 0, sizeof(g_update));
    InitializeCriticalSection(&g_update.lock);
    InitializeCriticalSection(&g_update.cfg_lock);
    update_copy_trunc(g_update.channel_url, sizeof(g_update.channel_url),
                      UPDATE_DEFAULT_CHANNEL);
    update_copy_trunc(g_update.status_line, sizeof(g_update.status_line),
                      "not checked");
    if (!update_get_root(g_update.root)) {
        update_copy_trunc(g_update.root, sizeof(g_update.root), ".");
    }
    return TRUE;
}

static void update_ensure_init(void) {
    InitOnceExecuteOnce(&g_update_once, update_init_once, NULL, NULL);
}

static void update_set_status(UpdateStatus status, const char* fmt, ...) {
    va_list ap;
    update_ensure_init();
    EnterCriticalSection(&g_update.lock);
    if (fmt) {
        va_start(ap, fmt);
        vsnprintf(g_update.status_line, sizeof(g_update.status_line), fmt, ap);
        va_end(ap);
        g_update.status_line[sizeof(g_update.status_line) - 1] = '\0';
    }
    /* Publish the enum last, after the corresponding text is complete. */
    InterlockedExchange(&g_update_status, (LONG)status);
    LeaveCriticalSection(&g_update.lock);
}

static void update_set_error(const char* fmt, ...) {
    va_list ap;
    update_ensure_init();
    EnterCriticalSection(&g_update.lock);
    va_start(ap, fmt);
    vsnprintf(g_update.error, sizeof(g_update.error), fmt, ap);
    va_end(ap);
    g_update.error[sizeof(g_update.error) - 1] = '\0';
    snprintf(g_update.status_line, sizeof(g_update.status_line),
             "update failed - see log");
    InterlockedExchange(&g_update_status, UPDATE_ERROR);
    LeaveCriticalSection(&g_update.lock);
}

UpdateStatus update_ext_status(void) {
    return (UpdateStatus)InterlockedCompareExchange(&g_update_status, 0, 0);
}

const char* update_ext_latest_version(void) {
    update_ensure_init();
    EnterCriticalSection(&g_update.lock);
    update_copy_trunc(g_update_tls_latest, sizeof(g_update_tls_latest),
                      g_update.latest_version);
    LeaveCriticalSection(&g_update.lock);
    return g_update_tls_latest;
}

const char* update_ext_status_line(void) {
    update_ensure_init();
    EnterCriticalSection(&g_update.lock);
    update_copy_trunc(g_update_tls_status, sizeof(g_update_tls_status),
                      g_update.status_line);
    LeaveCriticalSection(&g_update.lock);
    return g_update_tls_status;
}

int update_ext_notice_active(void) {
    return InterlockedCompareExchange(&g_update_notice, 0, 0) != 0;
}

void update_ext_dismiss_notice(void) {
    InterlockedExchange(&g_update_notice, 0);
}

/* Numeric comparison without integer overflow.  Each segment is compared by
 * significant digit count and then lexicographically; absent segments are 0. */
static const char* update_version_segment(const char* p, const char** begin,
                                          size_t* digits) {
    const char* start;
    const char* end;
    if (!p) p = "";
    start = p;
    while (*p && *p != '.') p++;
    end = p;
    while (start < end && *start == '0') start++;
    *begin = start;
    *digits = (size_t)(end - start);
    if (*p == '.') p++;
    return p;
}

int update_version_cmp(const char* a, const char* b) {
    const char* pa = a ? a : "";
    const char* pb = b ? b : "";
    for (;;) {
        const char* sa;
        const char* sb;
        size_t na;
        size_t nb;
        int cmp;
        int enda = (*pa == '\0');
        int endb = (*pb == '\0');
        if (enda && endb) return 0;
        pa = update_version_segment(pa, &sa, &na);
        pb = update_version_segment(pb, &sb, &nb);
        if (na != nb) return na > nb ? 1 : -1;
        if (na) {
            cmp = memcmp(sa, sb, na);
            if (cmp != 0) return cmp > 0 ? 1 : -1;
        }
    }
}

static int update_version_valid(const char* version) {
    const unsigned char* p = (const unsigned char*)version;
    int digits = 0;
    int segments = 1;
    size_t len;
    if (!version) return 0;
    len = strlen(version);
    if (len == 0 || len >= 64) return 0;
    while (*p) {
        if (isdigit(*p)) {
            digits++;
        } else if (*p == '.' && digits > 0 && p[1] != '\0') {
            digits = 0;
            if (++segments > 16) return 0;
        } else {
            return 0;
        }
        p++;
    }
    return digits > 0;
}

static int update_sha256_begin(BCRYPT_ALG_HANDLE* alg,
                               BCRYPT_HASH_HANDLE* hash,
                               unsigned char** object) {
    DWORD cb = 0;
    DWORD got = 0;
    NTSTATUS st;
    *alg = NULL;
    *hash = NULL;
    *object = NULL;
    st = BCryptOpenAlgorithmProvider(alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!NT_SUCCESS(st)) return 0;
    st = BCryptGetProperty(*alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cb,
                           sizeof(cb), &got, 0);
    if (!NT_SUCCESS(st) || got != sizeof(cb) || cb == 0) goto fail;
    *object = (unsigned char*)malloc(cb);
    if (!*object) goto fail;
    st = BCryptCreateHash(*alg, hash, *object, cb, NULL, 0, 0);
    if (!NT_SUCCESS(st)) goto fail;
    return 1;
fail:
    if (*hash) BCryptDestroyHash(*hash);
    free(*object);
    if (*alg) BCryptCloseAlgorithmProvider(*alg, 0);
    *alg = NULL;
    *hash = NULL;
    *object = NULL;
    return 0;
}

static void update_sha256_close(BCRYPT_ALG_HANDLE alg,
                                BCRYPT_HASH_HANDLE hash,
                                unsigned char* object) {
    if (hash) BCryptDestroyHash(hash);
    free(object);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
}

static void update_hex_encode(const unsigned char digest[32], char out[65]) {
    static const char hex[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < 32; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[64] = '\0';
}

int update_sha256_hex(const void* data, size_t len, char out_hex[65]) {
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE hash;
    unsigned char* object;
    unsigned char digest[32];
    const unsigned char* p = (const unsigned char*)data;
    size_t remaining = len;
    if (!out_hex || (!data && len != 0)) return 0;
    out_hex[0] = '\0';
    if (!update_sha256_begin(&alg, &hash, &object)) return 0;
    while (remaining) {
        ULONG chunk = remaining > 0x7fffffffu
                          ? 0x7fffffffu
                          : (ULONG)remaining;
        if (!NT_SUCCESS(BCryptHashData(hash, (PUCHAR)p, chunk, 0))) {
            update_sha256_close(alg, hash, object);
            return 0;
        }
        p += chunk;
        remaining -= chunk;
    }
    if (!NT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
        update_sha256_close(alg, hash, object);
        return 0;
    }
    update_sha256_close(alg, hash, object);
    update_hex_encode(digest, out_hex);
    return 1;
}

/* ---- Bounded JSON reader ------------------------------------------------ */

typedef struct UpdateJsonReader {
    const char* p;
    const char* end;
} UpdateJsonReader;

static void update_json_ws(UpdateJsonReader* r) {
    while (r->p < r->end &&
           (*r->p == ' ' || *r->p == '\t' || *r->p == '\r' || *r->p == '\n')) {
        r->p++;
    }
}

static int update_json_take(UpdateJsonReader* r, char ch) {
    update_json_ws(r);
    if (r->p >= r->end || *r->p != ch) return 0;
    r->p++;
    return 1;
}

static int update_json_hex4(const char* p, unsigned* value) {
    unsigned v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        unsigned d;
        unsigned char c = (unsigned char)p[i];
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return 0;
        v = (v << 4) | d;
    }
    *value = v;
    return 1;
}

static int update_json_emit_utf8(char* out, size_t cap, size_t* used,
                                 unsigned cp) {
    unsigned char bytes[4];
    size_t n;
    if (cp == 0 || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu)) return 0;
    if (cp <= 0x7fu) {
        bytes[0] = (unsigned char)cp;
        n = 1;
    } else if (cp <= 0x7ffu) {
        bytes[0] = (unsigned char)(0xc0u | (cp >> 6));
        bytes[1] = (unsigned char)(0x80u | (cp & 0x3fu));
        n = 2;
    } else if (cp <= 0xffffu) {
        bytes[0] = (unsigned char)(0xe0u | (cp >> 12));
        bytes[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3fu));
        bytes[2] = (unsigned char)(0x80u | (cp & 0x3fu));
        n = 3;
    } else {
        bytes[0] = (unsigned char)(0xf0u | (cp >> 18));
        bytes[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3fu));
        bytes[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3fu));
        bytes[3] = (unsigned char)(0x80u | (cp & 0x3fu));
        n = 4;
    }
    if (out) {
        if (*used + n >= cap) return 0;
        memcpy(out + *used, bytes, n);
    }
    *used += n;
    return 1;
}

static int update_json_string(UpdateJsonReader* r, char* out, size_t cap) {
    size_t used = 0;
    if (out && cap == 0) return 0;
    update_json_ws(r);
    if (r->p >= r->end || *r->p++ != '"') return 0;
    while (r->p < r->end) {
        unsigned char c = (unsigned char)*r->p++;
        if (c == '"') {
            if (out) out[used] = '\0';
            return 1;
        }
        if (c < 0x20) return 0;
        if (c == '\\') {
            unsigned cp;
            if (r->p >= r->end) return 0;
            c = (unsigned char)*r->p++;
            switch (c) {
                case '"': cp = '"'; break;
                case '\\': cp = '\\'; break;
                case '/': cp = '/'; break;
                case 'b': cp = '\b'; break;
                case 'f': cp = '\f'; break;
                case 'n': cp = '\n'; break;
                case 'r': cp = '\r'; break;
                case 't': cp = '\t'; break;
                case 'u': {
                    unsigned high;
                    if ((size_t)(r->end - r->p) < 4 ||
                        !update_json_hex4(r->p, &high)) return 0;
                    r->p += 4;
                    if (high >= 0xd800u && high <= 0xdbffu) {
                        unsigned low;
                        if ((size_t)(r->end - r->p) < 6 ||
                            r->p[0] != '\\' || r->p[1] != 'u' ||
                            !update_json_hex4(r->p + 2, &low) ||
                            low < 0xdc00u || low > 0xdfffu) return 0;
                        r->p += 6;
                        cp = 0x10000u + ((high - 0xd800u) << 10) +
                             (low - 0xdc00u);
                    } else {
                        cp = high;
                    }
                    break;
                }
                default: return 0;
            }
            if (!update_json_emit_utf8(out, cap, &used, cp)) return 0;
        } else {
            if (out) {
                if (used + 1 >= cap) return 0;
                out[used] = (char)c;
            }
            used++;
        }
    }
    return 0;
}

static int update_json_number_span(UpdateJsonReader* r,
                                   const char** start, const char** finish) {
    const char* p;
    update_json_ws(r);
    p = r->p;
    *start = p;
    if (p < r->end && *p == '-') p++;
    if (p >= r->end) return 0;
    if (*p == '0') {
        p++;
        if (p < r->end && isdigit((unsigned char)*p)) return 0;
    } else if (*p >= '1' && *p <= '9') {
        do { p++; } while (p < r->end && isdigit((unsigned char)*p));
    } else {
        return 0;
    }
    if (p < r->end && *p == '.') {
        p++;
        if (p >= r->end || !isdigit((unsigned char)*p)) return 0;
        do { p++; } while (p < r->end && isdigit((unsigned char)*p));
    }
    if (p < r->end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < r->end && (*p == '+' || *p == '-')) p++;
        if (p >= r->end || !isdigit((unsigned char)*p)) return 0;
        do { p++; } while (p < r->end && isdigit((unsigned char)*p));
    }
    *finish = p;
    r->p = p;
    return 1;
}

static int update_json_uint64(UpdateJsonReader* r, uint64_t* value) {
    const char* begin;
    const char* end;
    uint64_t v = 0;
    const char* p;
    if (!update_json_number_span(r, &begin, &end) || begin == end ||
        *begin == '-' || memchr(begin, '.', (size_t)(end - begin)) ||
        memchr(begin, 'e', (size_t)(end - begin)) ||
        memchr(begin, 'E', (size_t)(end - begin))) return 0;
    for (p = begin; p < end; p++) {
        unsigned d = (unsigned)(*p - '0');
        if (v > (UINT64_MAX - d) / 10u) return 0;
        v = v * 10u + d;
    }
    *value = v;
    return 1;
}

static int update_json_bool(UpdateJsonReader* r, int* value) {
    update_json_ws(r);
    if ((size_t)(r->end - r->p) >= 4 && memcmp(r->p, "true", 4) == 0) {
        r->p += 4;
        *value = 1;
        return 1;
    }
    if ((size_t)(r->end - r->p) >= 5 && memcmp(r->p, "false", 5) == 0) {
        r->p += 5;
        *value = 0;
        return 1;
    }
    return 0;
}

static int update_json_skip_value(UpdateJsonReader* r, int depth) {
    if (depth > UPDATE_JSON_MAX_DEPTH) return 0;
    update_json_ws(r);
    if (r->p >= r->end) return 0;
    if (*r->p == '"') return update_json_string(r, NULL, 0);
    if (*r->p == '{') {
        r->p++;
        update_json_ws(r);
        if (r->p < r->end && *r->p == '}') {
            r->p++;
            return 1;
        }
        for (;;) {
            if (!update_json_string(r, NULL, 0) || !update_json_take(r, ':') ||
                !update_json_skip_value(r, depth + 1)) return 0;
            update_json_ws(r);
            if (r->p < r->end && *r->p == '}') {
                r->p++;
                return 1;
            }
            if (!update_json_take(r, ',')) return 0;
        }
    }
    if (*r->p == '[') {
        r->p++;
        update_json_ws(r);
        if (r->p < r->end && *r->p == ']') {
            r->p++;
            return 1;
        }
        for (;;) {
            if (!update_json_skip_value(r, depth + 1)) return 0;
            update_json_ws(r);
            if (r->p < r->end && *r->p == ']') {
                r->p++;
                return 1;
            }
            if (!update_json_take(r, ',')) return 0;
        }
    }
    if (*r->p == '-' || isdigit((unsigned char)*r->p)) {
        const char* a;
        const char* b;
        return update_json_number_span(r, &a, &b);
    }
    if ((size_t)(r->end - r->p) >= 4 &&
        (memcmp(r->p, "true", 4) == 0 || memcmp(r->p, "null", 4) == 0)) {
        r->p += 4;
        return 1;
    }
    if ((size_t)(r->end - r->p) >= 5 && memcmp(r->p, "false", 5) == 0) {
        r->p += 5;
        return 1;
    }
    return 0;
}

static int update_json_reader_init(UpdateJsonReader* r, const char* json) {
    size_t len;
    if (!json) return 0;
    len = update_strnlen(json, UPDATE_MANIFEST_MAX_BYTES + 1u);
    if (len == 0 || len > UPDATE_MANIFEST_MAX_BYTES) return 0;
    r->p = json;
    r->end = json + len;
    return 1;
}

int update_json_get_string(const char* json, const char* key,
                           char* out, size_t cap) {
    UpdateJsonReader r;
    char name[128];
    int found = 0;
    if (!key || !*key || strlen(key) >= sizeof(name) || !out || cap == 0 ||
        !update_json_reader_init(&r, json) || !update_json_take(&r, '{')) return 0;
    update_json_ws(&r);
    if (r.p < r.end && *r.p == '}') return 0;
    for (;;) {
        if (!update_json_string(&r, name, sizeof(name)) ||
            !update_json_take(&r, ':')) return 0;
        if (strcmp(name, key) == 0) {
            if (found || !update_json_string(&r, out, cap)) return 0;
            found = 1;
        } else if (!update_json_skip_value(&r, 1)) {
            return 0;
        }
        update_json_ws(&r);
        if (r.p < r.end && *r.p == '}') {
            r.p++;
            break;
        }
        if (!update_json_take(&r, ',')) return 0;
    }
    update_json_ws(&r);
    return found && r.p == r.end;
}

static int update_sha256_valid(char hash[65]) {
    size_t i;
    if (strlen(hash) != 64) return 0;
    for (i = 0; i < 64; i++) {
        unsigned char c = (unsigned char)hash[i];
        if (!isxdigit(c)) return 0;
        hash[i] = (char)tolower(c);
    }
    return 1;
}

static int update_path_segment_reserved(const char* segment, size_t len) {
    char stem[16];
    size_t stem_len = 0;
    size_t i;
    while (stem_len < len && segment[stem_len] != '.') stem_len++;
    if (stem_len == 0 || stem_len >= sizeof(stem)) return 0;
    for (i = 0; i < stem_len; i++) {
        stem[i] = (char)toupper((unsigned char)segment[i]);
    }
    stem[stem_len] = '\0';
    if (strcmp(stem, "CON") == 0 || strcmp(stem, "PRN") == 0 ||
        strcmp(stem, "AUX") == 0 || strcmp(stem, "NUL") == 0) return 1;
    if (stem_len == 4 &&
        ((memcmp(stem, "COM", 3) == 0 || memcmp(stem, "LPT", 3) == 0) &&
         stem[3] >= '1' && stem[3] <= '9')) return 1;
    return 0;
}

static int update_path_is_safe(const char* path) {
    const char* segment;
    const char* p;
    size_t len;
    if (!path) return 0;
    len = strlen(path);
    if (len == 0 || len > UPDATE_MAX_PATH || path[0] == '/' || path[0] == '\\' ||
        path[len - 1] == '/' || path[len - 1] == '\\') return 0;
    segment = path;
    for (p = path;; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '/' || c == '\\' || c == '\0') {
            size_t n = (size_t)(p - segment);
            if (n == 0 || (n == 1 && segment[0] == '.') ||
                (n == 2 && segment[0] == '.' && segment[1] == '.') ||
                segment[n - 1] == '.' ||
                update_path_segment_reserved(segment, n)) return 0;
            if (c == '\0') break;
            segment = p + 1;
        } else if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
            /* Keeping channel paths URL-safe also prevents query/fragment and
             * alternate-data-stream injection on Windows. */
            return 0;
        }
    }
    return 1;
}

static int update_path_has_recovery_suffix(const char* path) {
    size_t path_len;
    size_t suffix_len = sizeof(UPDATE_RECOVERY_SUFFIX) - 1u;
    if (!path) return 0;
    path_len = strlen(path);
    return path_len >= suffix_len &&
           _stricmp(path + path_len - suffix_len,
                    UPDATE_RECOVERY_SUFFIX) == 0;
}

static int update_path_uses_recovery_namespace(const char* path) {
    const char* segment;
    const char* p;
    size_t suffix_len = sizeof(UPDATE_RECOVERY_SUFFIX) - 1u;
    if (!path) return 0;
    segment = path;
    for (p = path;; p++) {
        if (*p == '/' || *p == '\\' || *p == '\0') {
            size_t segment_len = (size_t)(p - segment);
            if (segment_len >= suffix_len &&
                _strnicmp(segment + segment_len - suffix_len,
                          UPDATE_RECOVERY_SUFFIX, suffix_len) == 0) {
                return 1;
            }
            if (*p == '\0') break;
            segment = p + 1;
        }
    }
    return 0;
}

/* Release paths leave room for the updater-owned cleanup suffix. Journals use
 * update_path_is_safe directly so cleanup-only handoff entries remain valid. */
static int update_release_path_is_safe(const char* path) {
    size_t len;
    if (!update_path_is_safe(path) ||
        update_path_uses_recovery_namespace(path)) {
        return 0;
    }
    len = strlen(path);
    return len + sizeof(UPDATE_RECOVERY_SUFFIX) - 1u <= UPDATE_MAX_PATH;
}

static void update_path_normalize(char* path) {
    while (*path) {
        if (*path == '\\') *path = '/';
        path++;
    }
}

static int update_files_have_collision(const UpdateFileSpec* files,
                                       size_t count, const char* path) {
    size_t i;
    char backup[UPDATE_MAX_PATH + 6];
    char other_backup[UPDATE_MAX_PATH + 6];
    snprintf(backup, sizeof(backup), "%s.old", path);
    for (i = 0; i < count; i++) {
        snprintf(other_backup, sizeof(other_backup), "%s.old", files[i].path);
        if (_stricmp(files[i].path, path) == 0 ||
            _stricmp(files[i].path, backup) == 0 ||
            _stricmp(other_backup, path) == 0) return 1;
    }
    return 0;
}

static int update_json_files_array(UpdateJsonReader* r,
                                   UpdateFileSpec* files,
                                   size_t file_cap, size_t* out_count) {
    size_t count = 0;
    if (!update_json_take(r, '[')) return 0;
    update_json_ws(r);
    if (r->p < r->end && *r->p == ']') {
        r->p++;
        *out_count = 0;
        return 1;
    }
    for (;;) {
        UpdateFileSpec file;
        char key[64];
        int have_path = 0;
        int have_sha = 0;
        int have_size = 0;
        int have_overwrite = 0;
        memset(&file, 0, sizeof(file));
        file.overwrite = 1;
        if (count >= file_cap || count >= UPDATE_MAX_FILES ||
            !update_json_take(r, '{')) return 0;
        update_json_ws(r);
        if (r->p < r->end && *r->p == '}') return 0;
        for (;;) {
            if (!update_json_string(r, key, sizeof(key)) ||
                !update_json_take(r, ':')) return 0;
            if (strcmp(key, "path") == 0) {
                if (have_path || !update_json_string(r, file.path,
                                                     sizeof(file.path))) return 0;
                have_path = 1;
            } else if (strcmp(key, "sha256") == 0) {
                if (have_sha || !update_json_string(r, file.sha256,
                                                    sizeof(file.sha256))) return 0;
                have_sha = 1;
            } else if (strcmp(key, "size") == 0) {
                if (have_size || !update_json_uint64(r, &file.size)) return 0;
                have_size = 1;
            } else if (strcmp(key, "overwrite") == 0) {
                if (have_overwrite || !update_json_bool(r, &file.overwrite)) return 0;
                have_overwrite = 1;
            } else if (!update_json_skip_value(r, 2)) {
                return 0;
            }
            update_json_ws(r);
            if (r->p < r->end && *r->p == '}') {
                r->p++;
                break;
            }
            if (!update_json_take(r, ',')) return 0;
        }
        if (!have_path || !have_sha || !have_size ||
            file.size > UPDATE_FILE_MAX_BYTES ||
            !update_release_path_is_safe(file.path) ||
            !update_sha256_valid(file.sha256)) return 0;
        update_path_normalize(file.path);
        if (update_files_have_collision(files, count, file.path)) return 0;
        files[count++] = file;
        update_json_ws(r);
        if (r->p < r->end && *r->p == ']') {
            r->p++;
            *out_count = count;
            return 1;
        }
        if (!update_json_take(r, ',')) return 0;
    }
}

int update_json_scan_files(const char* json, UpdateFileSpec* files,
                           size_t file_cap, size_t* out_count) {
    UpdateJsonReader r;
    char key[128];
    int found = 0;
    size_t count = 0;
    if (!files || !out_count || file_cap == 0 || file_cap > UPDATE_MAX_FILES ||
        !update_json_reader_init(&r, json) || !update_json_take(&r, '{')) return 0;
    update_json_ws(&r);
    if (r.p < r.end && *r.p == '}') return 0;
    for (;;) {
        if (!update_json_string(&r, key, sizeof(key)) ||
            !update_json_take(&r, ':')) return 0;
        if (strcmp(key, "files") == 0) {
            if (found || !update_json_files_array(&r, files, file_cap, &count)) return 0;
            found = 1;
        } else if (!update_json_skip_value(&r, 1)) {
            return 0;
        }
        update_json_ws(&r);
        if (r.p < r.end && *r.p == '}') {
            r.p++;
            break;
        }
        if (!update_json_take(&r, ',')) return 0;
    }
    update_json_ws(&r);
    if (!found || r.p != r.end) return 0;
    *out_count = count;
    return 1;
}

static int update_utf8_to_wide(const char* utf8, wchar_t* wide, size_t cap) {
    int n;
    if (!utf8 || !wide || cap == 0 || cap > INT_MAX) return 0;
    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                            wide, (int)cap);
    return n > 0;
}

typedef struct UpdateCrackedUrl {
    wchar_t host[512];
    wchar_t path[2048];
    wchar_t extra[1024];
    INTERNET_SCHEME scheme;
    INTERNET_PORT port;
} UpdateCrackedUrl;

static int update_crack_url(const char* url, UpdateCrackedUrl* out) {
    wchar_t wide[UPDATE_URL_CAP];
    wchar_t user[8] = {0};
    wchar_t password[8] = {0};
    URL_COMPONENTS uc;
    size_t len;
    if (!url || !out) return 0;
    len = strlen(url);
    if (len == 0 || len >= UPDATE_URL_CAP || strchr(url, '\r') ||
        strchr(url, '\n') || !update_utf8_to_wide(url, wide,
                                                  sizeof(wide) / sizeof(wide[0]))) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = out->host;
    uc.dwHostNameLength = sizeof(out->host) / sizeof(out->host[0]);
    uc.lpszUrlPath = out->path;
    uc.dwUrlPathLength = sizeof(out->path) / sizeof(out->path[0]);
    uc.lpszExtraInfo = out->extra;
    uc.dwExtraInfoLength = sizeof(out->extra) / sizeof(out->extra[0]);
    uc.lpszUserName = user;
    uc.dwUserNameLength = sizeof(user) / sizeof(user[0]);
    uc.lpszPassword = password;
    uc.dwPasswordLength = sizeof(password) / sizeof(password[0]);
    if (!WinHttpCrackUrl(wide, 0, ICU_REJECT_USERPWD, &uc)) return 0;
    if (uc.nScheme != INTERNET_SCHEME_HTTP &&
        uc.nScheme != INTERNET_SCHEME_HTTPS) return 0;
    if (!out->host[0]) return 0;
    out->scheme = uc.nScheme;
    out->port = uc.nPort ? uc.nPort
                         : (uc.nScheme == INTERNET_SCHEME_HTTPS
                                ? INTERNET_DEFAULT_HTTPS_PORT
                                : INTERNET_DEFAULT_HTTP_PORT);
    return 1;
}

static int update_url_allowed(const char* url, int require_plain_base) {
    UpdateCrackedUrl parsed;
    int loopback;
    if (!update_crack_url(url, &parsed)) return 0;
    loopback = (_wcsicmp(parsed.host, L"localhost") == 0 ||
                _wcsicmp(parsed.host, L"127.0.0.1") == 0 ||
                _wcsicmp(parsed.host, L"::1") == 0);
    /* Executables and DLLs must not be fetched over cleartext internet.  HTTP
     * remains available on loopback for the documented local-channel test. */
    if (parsed.scheme != INTERNET_SCHEME_HTTPS && !loopback) return 0;
    if (require_plain_base && parsed.extra[0]) return 0;
    return 1;
}

static int update_manifest_parse(const char* json, UpdateManifest* manifest) {
    UpdateJsonReader r;
    UpdateManifest parsed;
    char key[128];
    int have_channel = 0;
    int have_version = 0;
    int have_base = 0;
    int have_files = 0;
    uint64_t channel_version = 0;
    uint64_t total = 0;
    size_t i;
    if (!manifest || !update_json_reader_init(&r, json) ||
        !update_json_take(&r, '{')) return 0;
    memset(&parsed, 0, sizeof(parsed));
    update_json_ws(&r);
    if (r.p < r.end && *r.p == '}') return 0;
    for (;;) {
        if (!update_json_string(&r, key, sizeof(key)) ||
            !update_json_take(&r, ':')) return 0;
        if (strcmp(key, "channel_version") == 0) {
            if (have_channel || !update_json_uint64(&r, &channel_version)) return 0;
            have_channel = 1;
        } else if (strcmp(key, "version") == 0) {
            if (have_version || !update_json_string(&r, parsed.version,
                                                     sizeof(parsed.version))) return 0;
            have_version = 1;
        } else if (strcmp(key, "base") == 0) {
            if (have_base || !update_json_string(&r, parsed.base,
                                                  sizeof(parsed.base))) return 0;
            have_base = 1;
        } else if (strcmp(key, "files") == 0) {
            if (have_files ||
                !update_json_files_array(&r, parsed.files, UPDATE_MAX_FILES,
                                         &parsed.file_count)) return 0;
            have_files = 1;
        } else if (!update_json_skip_value(&r, 1)) {
            return 0;
        }
        update_json_ws(&r);
        if (r.p < r.end && *r.p == '}') {
            r.p++;
            break;
        }
        if (!update_json_take(&r, ',')) return 0;
    }
    update_json_ws(&r);
    if (r.p != r.end || !have_channel || channel_version != 1 ||
        !have_version || !update_version_valid(parsed.version) ||
        !have_base || !update_url_allowed(parsed.base, 1) ||
        !have_files || parsed.file_count == 0) return 0;
    for (i = 0; i < parsed.file_count; i++) {
        if (UINT64_MAX - total < parsed.files[i].size) return 0;
        total += parsed.files[i].size;
    }
    if (total > UPDATE_TOTAL_MAX_BYTES) return 0;
    *manifest = parsed;
    return 1;
}

/* ---- Config, rooted beside the injected DLL ---------------------------- */

static int update_path_exists(const char* path, int* is_directory) {
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
    if (is_directory) *is_directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return 1;
}

/* Unlike update_path_exists, this distinguishes a genuinely absent path from
 * an attribute-query failure. Recovery must never interpret access denied or
 * an I/O error as evidence that a journal or quarantine is gone. */
static int update_path_query(const char* path, int* out_exists,
                             DWORD* out_attributes) {
    DWORD attrs;
    DWORD error;
    if (!path || !out_exists) return 0;
#ifdef UPDATE_EXT_TEST
    if (g_update_test_probe_error_path &&
        _stricmp(path, g_update_test_probe_error_path) == 0) {
        SetLastError(ERROR_ACCESS_DENIED);
        return 0;
    }
#endif
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            return 0;
        }
        *out_exists = 0;
        if (out_attributes) *out_attributes = INVALID_FILE_ATTRIBUTES;
        return 1;
    }
    *out_exists = 1;
    if (out_attributes) *out_attributes = attrs;
    return 1;
}

static int update_create_directory(const char* path) {
    DWORD attrs;
    if (CreateDirectoryA(path, NULL)) return 1;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    attrs = GetFileAttributesA(path);
    if (attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_DIRECTORY) &&
        !(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return 1;
    return 0;
}

static int update_ensure_mods_dir(void) {
    char path[UPDATE_ABS_CAP];
    return update_join_path(path, sizeof(path), g_update.root, "mods") &&
           update_create_directory(path);
}

static int update_read_file_bounded(const char* path, size_t max_bytes,
                                    unsigned char** out, size_t* out_len) {
    HANDLE file;
    LARGE_INTEGER size;
    unsigned char* data;
    DWORD got;
    size_t offset = 0;
    if (!out || !out_len) return 0;
    *out = NULL;
    *out_len = 0;
    file = CreateFileA(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (uint64_t)size.QuadPart > max_bytes ||
        (uint64_t)size.QuadPart > SIZE_MAX - 1u) {
        CloseHandle(file);
        return 0;
    }
    data = (unsigned char*)malloc((size_t)size.QuadPart + 1u);
    if (!data) {
        CloseHandle(file);
        return 0;
    }
    while (offset < (size_t)size.QuadPart) {
        DWORD want = (DWORD)(((size_t)size.QuadPart - offset) > 0x40000000u
                                 ? 0x40000000u
                                 : ((size_t)size.QuadPart - offset));
        if (!ReadFile(file, data + offset, want, &got, NULL) || got == 0) {
            free(data);
            CloseHandle(file);
            return 0;
        }
        offset += got;
    }
    CloseHandle(file);
    data[offset] = '\0';
    *out = data;
    *out_len = offset;
    return 1;
}

static uint32_t update_root_hash(const char* suffix) {
    uint32_t hash = 2166136261u;
    const unsigned char* p = (const unsigned char*)g_update.root;
    while (*p) {
        hash ^= (unsigned char)tolower(*p++);
        hash *= 16777619u;
    }
    p = (const unsigned char*)suffix;
    while (*p) {
        hash ^= *p++;
        hash *= 16777619u;
    }
    return hash;
}

static HANDLE update_named_mutex(const char* purpose) {
    char name[96];
    snprintf(name, sizeof(name), "Local\\YuleUpdater_%s_%08lx",
             purpose, (unsigned long)update_root_hash(purpose));
    return CreateMutexA(NULL, FALSE, name);
}

static int update_lock_mutex(HANDLE mutex, DWORD timeout_ms) {
    DWORD wait;
    if (!mutex) return 0;
    wait = WaitForSingleObject(mutex, timeout_ms);
    return wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
}

static int update_cfg_key_matches(const char* begin, const char* end,
                                  const char* key, const char** value) {
    size_t key_len = strlen(key);
    const char* p = begin;
    if ((size_t)(end - p) >= 3 &&
        (unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbb &&
        (unsigned char)p[2] == 0xbf) p += 3;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if ((size_t)(end - p) < key_len || _strnicmp(p, key, key_len) != 0) return 0;
    p += key_len;
    if (p < end && *p != ' ' && *p != '\t' && *p != '=' && *p != ':') return 0;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p >= end || (*p != '=' && *p != ':')) return 0;
    p++;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (value) *value = p;
    return 1;
}

static int update_cfg_get(const char* key, char* out, size_t cap) {
    char cfg[UPDATE_ABS_CAP];
    unsigned char* bytes = NULL;
    size_t len = 0;
    const char* p;
    const char* end;
    int found = 0;
    if (!out || cap == 0 ||
        !update_join_path(cfg, sizeof(cfg), g_update.root, UPDATE_CFG_REL) ||
        !update_read_file_bounded(cfg, UPDATE_CFG_MAX_BYTES, &bytes, &len)) return 0;
    p = (const char*)bytes;
    end = p + len;
    while (p < end) {
        const char* line_end = p;
        const char* value;
        const char* value_end;
        size_t n;
        while (line_end < end && *line_end != '\r' && *line_end != '\n') line_end++;
        if (update_cfg_key_matches(p, line_end, key, &value)) {
            value_end = line_end;
            while (value_end > value &&
                   (value_end[-1] == ' ' || value_end[-1] == '\t')) value_end--;
            n = (size_t)(value_end - value);
            if (n >= cap) {
                free(bytes);
                return 0;
            }
            memcpy(out, value, n);
            out[n] = '\0';
            found = 1;
        }
        p = line_end;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }
    free(bytes);
    return found;
}

static int update_write_bytes_atomic(const char* path,
                                     const void* data, size_t len) {
    char tmp[UPDATE_ABS_CAP];
    HANDLE file;
    DWORD wrote;
    size_t offset = 0;
    {
        int n = snprintf(tmp, sizeof(tmp), "%s.tmp.%lu.%lu", path,
                         (unsigned long)GetCurrentProcessId(),
                         (unsigned long)GetCurrentThreadId());
        if (n < 0 || (size_t)n >= sizeof(tmp)) return 0;
    }
    file = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    while (offset < len) {
        DWORD want = (DWORD)((len - offset) > 0x40000000u
                                 ? 0x40000000u
                                 : (len - offset));
        if (!WriteFile(file, (const unsigned char*)data + offset,
                       want, &wrote, NULL) || wrote == 0) {
            CloseHandle(file);
            DeleteFileA(tmp);
            return 0;
        }
        offset += wrote;
    }
    if (!FlushFileBuffers(file)) {
        CloseHandle(file);
        DeleteFileA(tmp);
        return 0;
    }
    CloseHandle(file);
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp);
        return 0;
    }
    return 1;
}

static int update_cfg_key_valid(const char* key) {
    size_t i;
    size_t len;
    if (!key) return 0;
    len = strlen(key);
    if (len == 0 || len >= 64) return 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
            return 0;
        }
    }
    return 1;
}

static int update_cfg_value_valid(const char* value) {
    size_t len;
    if (!value) return 0;
    len = update_strnlen(value, 4097u);
    return len <= 4096u && value[len] == '\0' &&
           strchr(value, '\r') == NULL && strchr(value, '\n') == NULL;
}

static int update_cfg_set_locked(const char* key, const char* value) {
    char cfg[UPDATE_ABS_CAP];
    unsigned char* old = NULL;
    size_t old_len = 0;
    size_t key_len = strlen(key);
    size_t value_len = strlen(value);
    size_t cap;
    char* output;
    size_t used = 0;
    const char* p;
    const char* end;
    int replaced = 0;
    int ok = 0;
    int cfg_is_dir = 0;
    int cfg_exists;
    HANDLE mutex;
    if (!update_ensure_mods_dir() ||
        !update_join_path(cfg, sizeof(cfg), g_update.root, UPDATE_CFG_REL)) return 0;
    mutex = update_named_mutex("Config");
    if (!update_lock_mutex(mutex, 5000)) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }
    cfg_exists = update_path_exists(cfg, &cfg_is_dir);
    if (cfg_is_dir ||
        (cfg_exists && !update_read_file_bounded(cfg, UPDATE_CFG_MAX_BYTES,
                                                 &old, &old_len))) goto done;
    cap = old_len + (key_len + value_len + 4u) * 4u + 4u;
    if (cap > UPDATE_CFG_MAX_BYTES + 1024u) goto done;
    output = (char*)malloc(cap);
    if (!output) goto done;
    p = old ? (const char*)old : "";
    end = p + old_len;
    while (p < end) {
        const char* content_end = p;
        const char* newline_end;
        while (content_end < end && *content_end != '\r' && *content_end != '\n') {
            content_end++;
        }
        newline_end = content_end;
        if (newline_end < end && *newline_end == '\r') newline_end++;
        if (newline_end < end && *newline_end == '\n') newline_end++;
        if (update_cfg_key_matches(p, content_end, key, NULL)) {
            int n = snprintf(output + used, cap - used, "%s=%s", key, value);
            if (n < 0 || (size_t)n >= cap - used) {
                free(output);
                goto done;
            }
            used += (size_t)n;
            if (content_end < newline_end) {
                size_t newline_len = (size_t)(newline_end - content_end);
                memcpy(output + used, content_end, newline_len);
                used += newline_len;
            }
            replaced = 1;
        } else {
            size_t line_len = (size_t)(newline_end - p);
            if (used + line_len > cap) {
                free(output);
                goto done;
            }
            memcpy(output + used, p, line_len);
            used += line_len;
        }
        p = newline_end;
    }
    if (!replaced) {
        int n;
        if (used && output[used - 1] != '\n' && output[used - 1] != '\r') {
            if (used + 1 > cap) {
                free(output);
                goto done;
            }
            output[used++] = '\n';
        }
        n = snprintf(output + used, cap - used, "%s=%s\n", key, value);
        if (n < 0 || (size_t)n >= cap - used) {
            free(output);
            goto done;
        }
        used += (size_t)n;
    }
    ok = update_write_bytes_atomic(cfg, output, used);
    free(output);
done:
    free(old);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return ok;
}

int update_ext_config_set(const char* key, const char* value) {
    int ok;
    update_ensure_init();
    if (!update_cfg_key_valid(key) || !update_cfg_value_valid(value)) return 0;
    EnterCriticalSection(&g_update.cfg_lock);
    ok = update_cfg_set_locked(key, value);
    LeaveCriticalSection(&g_update.cfg_lock);
    return ok;
}

int update_ext_config_get(const char* key, char* out, size_t cap) {
    int ok;
    update_ensure_init();
    if (!update_cfg_key_valid(key) || !out || cap == 0u) return 0;
    out[0] = '\0';
    EnterCriticalSection(&g_update.cfg_lock);
    ok = update_cfg_get(key, out, cap);
    LeaveCriticalSection(&g_update.cfg_lock);
    return ok;
}

static void update_read_config(void) {
    char value[UPDATE_URL_CAP];
    int auto_update = 1;
    EnterCriticalSection(&g_update.cfg_lock);
    if (update_cfg_get("auto_update", value, sizeof(value))) {
        char* end = NULL;
        long parsed = strtol(value, &end, 10);
        while (end && (*end == ' ' || *end == '\t')) end++;
        if (end && *end == '\0') auto_update = parsed != 0;
    }
    InterlockedExchange(&g_update_auto, auto_update);
    if (update_cfg_get("update_channel_url", value, sizeof(value))) {
        if (update_url_allowed(value, 0)) {
            EnterCriticalSection(&g_update.lock);
            update_copy_trunc(g_update.channel_url, sizeof(g_update.channel_url), value);
            LeaveCriticalSection(&g_update.lock);
        } else {
            LOG_WARN("update: ignoring unsafe/invalid update_channel_url");
        }
    }
    LeaveCriticalSection(&g_update.cfg_lock);
}

int update_ext_auto(void) {
    return InterlockedCompareExchange(&g_update_auto, 0, 0) != 0;
}

void update_ext_set_auto(int enabled) {
    char value[2];
    update_ensure_init();
    enabled = enabled ? 1 : 0;
    EnterCriticalSection(&g_update.cfg_lock);
    InterlockedExchange(&g_update_auto, enabled);
    value[0] = enabled ? '1' : '0';
    value[1] = '\0';
    if (!update_cfg_set_locked("auto_update", value)) {
        LOG_WARN("update: could not persist auto_update=%d", enabled);
    }
    LeaveCriticalSection(&g_update.cfg_lock);
    if (enabled && update_ext_status() == UPDATE_AVAILABLE) {
        update_ext_begin_apply();
    }
}

/* ---- HTTP and files ----------------------------------------------------- */

static int update_cancelled(void) {
    return InterlockedCompareExchange(&g_update_cancel, 0, 0) != 0;
}

static int update_http_get(const char* url, size_t max_bytes,
                           unsigned char** out, size_t* out_len,
                           char* error, size_t error_cap) {
    UpdateCrackedUrl parsed;
    HINTERNET session = NULL;
    HINTERNET connection = NULL;
    HINTERNET request = NULL;
    wchar_t object[3072];
    DWORD flags;
    DWORD timeout = UPDATE_HTTP_TIMEOUT_MS;
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    DWORD content_length = 0;
    DWORD content_length_size = sizeof(content_length);
    unsigned char* bytes = NULL;
    size_t cap = 0;
    size_t len = 0;
    int ok = 0;
    if (!out || !out_len || !error || error_cap == 0 || max_bytes == 0) return 0;
    *out = NULL;
    *out_len = 0;
    error[0] = '\0';
    if (!update_url_allowed(url, 0) || !update_crack_url(url, &parsed)) {
        snprintf(error, error_cap, "invalid or unsafe URL");
        return 0;
    }
    {
        int object_len = swprintf(object, sizeof(object) / sizeof(object[0]),
                                  L"%ls%ls",
                                  parsed.path[0] ? parsed.path : L"/",
                                  parsed.extra);
        if (object_len < 0 ||
            (size_t)object_len >= sizeof(object) / sizeof(object[0])) {
            snprintf(error, error_cap, "URL path too long");
            return 0;
        }
    }
    session = WinHttpOpen(L"Yule-Framework/" FRAMEWORK_VERSION,
                          WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        snprintf(error, error_cap, "WinHttpOpen failed (%lu)",
                 (unsigned long)GetLastError());
        goto done;
    }
    WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);
    (void)WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY,
                           &redirect_policy, sizeof(redirect_policy));
    connection = WinHttpConnect(session, parsed.host, parsed.port, 0);
    if (!connection) {
        snprintf(error, error_cap, "WinHttpConnect failed (%lu)",
                 (unsigned long)GetLastError());
        goto done;
    }
    flags = parsed.scheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    request = WinHttpOpenRequest(connection, L"GET", object, NULL,
                                 WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        snprintf(error, error_cap, "WinHttpOpenRequest failed (%lu)",
                 (unsigned long)GetLastError());
        goto done;
    }
    if (update_cancelled()) {
        snprintf(error, error_cap, "cancelled");
        goto done;
    }
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) {
        snprintf(error, error_cap, "request failed (%lu)",
                 (unsigned long)GetLastError());
        goto done;
    }
    if (!WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
            WINHTTP_NO_HEADER_INDEX) || status != 200) {
        snprintf(error, error_cap, "HTTP %lu", (unsigned long)status);
        goto done;
    }
    if (WinHttpQueryHeaders(request,
            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &content_length,
            &content_length_size, WINHTTP_NO_HEADER_INDEX) &&
        (size_t)content_length > max_bytes) {
        snprintf(error, error_cap, "response exceeds size limit");
        goto done;
    }
    cap = content_length ? (size_t)content_length + 1u : 8192u;
    if (cap > max_bytes + 1u) cap = max_bytes + 1u;
    if (cap < 2u) cap = 2u;
    bytes = (unsigned char*)malloc(cap);
    if (!bytes) {
        snprintf(error, error_cap, "out of memory");
        goto done;
    }
    for (;;) {
        DWORD available = 0;
        DWORD got = 0;
        size_t need;
        unsigned char* grown;
        if (update_cancelled()) {
            snprintf(error, error_cap, "cancelled");
            goto done;
        }
        if (!WinHttpQueryDataAvailable(request, &available)) {
            snprintf(error, error_cap, "response read failed (%lu)",
                     (unsigned long)GetLastError());
            goto done;
        }
        if (available == 0) break;
        if ((size_t)available > max_bytes - len) {
            snprintf(error, error_cap, "response exceeds size limit");
            goto done;
        }
        need = len + (size_t)available + 1u;
        if (need > cap) {
            size_t new_cap = cap;
            while (new_cap < need) {
                size_t next = new_cap < 1024u * 1024u
                                  ? new_cap * 2u
                                  : new_cap + 1024u * 1024u;
                if (next <= new_cap || next > max_bytes + 1u) {
                    new_cap = max_bytes + 1u;
                    break;
                }
                new_cap = next;
            }
            if (new_cap < need) {
                snprintf(error, error_cap, "response exceeds size limit");
                goto done;
            }
            grown = (unsigned char*)realloc(bytes, new_cap);
            if (!grown) {
                snprintf(error, error_cap, "out of memory");
                goto done;
            }
            bytes = grown;
            cap = new_cap;
        }
        if (!WinHttpReadData(request, bytes + len, available, &got) || got == 0) {
            snprintf(error, error_cap, "response read failed (%lu)",
                     (unsigned long)GetLastError());
            goto done;
        }
        len += got;
    }
    bytes[len] = '\0';
    *out = bytes;
    *out_len = len;
    bytes = NULL;
    ok = 1;
done:
    free(bytes);
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

static int update_remove_tree(const char* path) {
    char pattern[UPDATE_ABS_CAP];
    WIN32_FIND_DATAA data;
    HANDLE find;
    int is_dir = 0;
    int ok = 1;
    if (!update_path_exists(path, &is_dir)) return 1;
    if (!is_dir) return DeleteFileA(path) != 0;
    {
        DWORD root_attrs = GetFileAttributesA(path);
        if (root_attrs != INVALID_FILE_ATTRIBUTES &&
            (root_attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
            return RemoveDirectoryA(path) != 0;
        }
    }
    if (snprintf(pattern, sizeof(pattern), "%s\\*", path) < 0 ||
        strlen(pattern) >= sizeof(pattern) - 1) return 0;
    find = FindFirstFileA(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            char child[UPDATE_ABS_CAP];
            if (strcmp(data.cFileName, ".") == 0 ||
                strcmp(data.cFileName, "..") == 0) continue;
            if (!update_join_path(child, sizeof(child), path, data.cFileName)) {
                ok = 0;
                break;
            }
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    if (!RemoveDirectoryA(child)) ok = 0;
                } else if (!update_remove_tree(child)) {
                    ok = 0;
                }
            } else if (!DeleteFileA(child)) {
                ok = 0;
            }
        } while (ok && FindNextFileA(find, &data));
        FindClose(find);
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
        ok = 0;
    }
    return ok && RemoveDirectoryA(path) != 0;
}

static int update_prepare_relative_path(const char* base, const char* relative,
                                        char* out, size_t cap,
                                        int create_parents) {
    char rel[UPDATE_MAX_PATH + 1];
    size_t base_len;
    char* p;
    if (!update_path_is_safe(relative) ||
        !update_copy(rel, sizeof(rel), relative)) return 0;
    update_slashes_to_backslashes(rel);
    if (!update_join_path(out, cap, base, rel)) return 0;
    base_len = strlen(base);
    for (p = out + base_len + 1; *p; p++) {
        if (*p == '\\') {
            DWORD attrs;
            *p = '\0';
            attrs = GetFileAttributesA(out);
            if (attrs == INVALID_FILE_ATTRIBUTES) {
                if (!create_parents || !CreateDirectoryA(out, NULL)) {
                    *p = '\\';
                    return 0;
                }
            } else if (!(attrs & FILE_ATTRIBUTE_DIRECTORY) ||
                       (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
                *p = '\\';
                return 0;
            }
            *p = '\\';
        }
    }
    return 1;
}

static int update_sha256_file(const char* path, uint64_t expected_size,
                              char out_hex[65]) {
    HANDLE file;
    LARGE_INTEGER size;
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE hash;
    unsigned char* object;
    unsigned char digest[32];
    unsigned char buffer[64 * 1024];
    DWORD got;
    int ok = 0;
    out_hex[0] = '\0';
    file = CreateFileA(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL |
                           FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (uint64_t)size.QuadPart != expected_size) {
        CloseHandle(file);
        return 0;
    }
    if (!update_sha256_begin(&alg, &hash, &object)) {
        CloseHandle(file);
        return 0;
    }
    for (;;) {
        if (!ReadFile(file, buffer, sizeof(buffer), &got, NULL)) goto done;
        if (got == 0) break;
        if (!NT_SUCCESS(BCryptHashData(hash, buffer, got, 0))) goto done;
    }
    if (!NT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) goto done;
    update_hex_encode(digest, out_hex);
    ok = 1;
done:
    update_sha256_close(alg, hash, object);
    CloseHandle(file);
    return ok;
}

static int update_file_matches(const char* path, const UpdateFileSpec* spec) {
    char hash[65];
    return update_sha256_file(path, spec->size, hash) &&
           _stricmp(hash, spec->sha256) == 0;
}

static int update_capture_regular_file_integrity(const char* path,
                                                 uint64_t* out_size,
                                                 char out_sha256[65]) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    uint64_t size;
    if (!path || !out_size || !out_sha256 ||
        !GetFileAttributesExA(path, GetFileExInfoStandard, &info) ||
        (info.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        return 0;
    }
    size = ((uint64_t)info.nFileSizeHigh << 32) |
           (uint64_t)info.nFileSizeLow;
    if (size > UPDATE_FILE_MAX_BYTES ||
        !update_sha256_file(path, size, out_sha256)) {
        return 0;
    }
    *out_size = size;
    return 1;
}

static int update_build_download_url(const char* base, const char* relative,
                                     char* out, size_t cap) {
    size_t a = strlen(base);
    size_t b = strlen(relative);
    int separator = a > 0 && base[a - 1] != '/';
    if (!update_release_path_is_safe(relative) ||
        a + (size_t)separator + b + 1u > cap) return 0;
    memcpy(out, base, a);
    if (separator) out[a++] = '/';
    memcpy(out + a, relative, b + 1u);
    while (out[a]) {
        if (out[a] == '\\') out[a] = '/';
        a++;
    }
    return update_url_allowed(out, 0);
}

static int update_download_verified(const UpdateManifest* manifest,
                                    const UpdateFileSpec* spec,
                                    const char* staging_root,
                                    char* staged_out, size_t staged_cap,
                                    char* error, size_t error_cap) {
    char url[UPDATE_URL_CAP];
    int attempt;
    if (!update_build_download_url(manifest->base, spec->path,
                                   url, sizeof(url)) ||
        !update_prepare_relative_path(staging_root, spec->path,
                                      staged_out, staged_cap, 1)) {
        snprintf(error, error_cap, "unsafe or overlong path for %s", spec->path);
        return 0;
    }
    for (attempt = 1; attempt <= 2; attempt++) {
        unsigned char* body = NULL;
        size_t body_len = 0;
        char http_error[160];
        if (update_cancelled()) {
            snprintf(error, error_cap, "cancelled");
            return 0;
        }
        if (update_http_get(url, (size_t)(spec->size ? spec->size : 1u),
                            &body, &body_len, http_error, sizeof(http_error)) &&
            body_len == spec->size &&
            update_write_bytes_atomic(staged_out, body, body_len) &&
            update_file_matches(staged_out, spec)) {
            free(body);
            return 1;
        }
        free(body);
        DeleteFileA(staged_out);
        if (attempt == 1) {
            LOG_WARN("update: verify/download failed for %s; retrying once",
                     spec->path);
        } else {
            snprintf(error, error_cap, "%s failed verification (%s)",
                     spec->path, http_error[0] ? http_error : "hash/size mismatch");
        }
    }
    return 0;
}

/* ---- Crash-safe transaction journal ----------------------------------- */

typedef struct UpdateApplyEntry {
    UpdateFileSpec spec;
    char target[UPDATE_ABS_CAP];
    char staged[UPDATE_ABS_CAP];
    char backup[UPDATE_ABS_CAP];
    char quarantine[UPDATE_ABS_CAP];
    char original_sha256[65];
    uint64_t original_size;
    int had_original;
    int has_original_integrity;
    int swapped;
} UpdateApplyEntry;

typedef struct UpdateJournal {
    int version;
    int committed;
    size_t count;
    struct {
        char path[UPDATE_MAX_PATH + 1];
        char sha256[65];
        uint64_t size;
        int had_original;
        int has_integrity;
        char original_sha256[65];
        uint64_t original_size;
        int has_original_integrity;
    } files[UPDATE_MAX_FILES];
} UpdateJournal;

static int update_entry_original_matches(const UpdateApplyEntry* entry) {
    int exists = 0;
    DWORD attrs = INVALID_FILE_ATTRIBUTES;
    char hash[65];
    if (!entry || !entry->has_original_integrity ||
        !update_path_query(entry->target, &exists, &attrs) ||
        (exists && (attrs &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) ||
        exists != (entry->had_original ? 1 : 0)) {
        return 0;
    }
    if (!exists) {
        return entry->original_size == 0u &&
               _stricmp(entry->original_sha256,
                        UPDATE_UNKNOWN_SHA256) == 0;
    }
    return update_sha256_file(entry->target, entry->original_size, hash) &&
           _stricmp(hash, entry->original_sha256) == 0;
}

static int update_delete_file_if_present(const char* path) {
    DWORD attrs = INVALID_FILE_ATTRIBUTES;
    int exists = 0;
    if (!update_path_query(path, &exists, &attrs)) return 0;
    if (!exists) return 1;
    if (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetLastError(ERROR_DIRECTORY);
        return 0;
    }
    if (DeleteFileA(path)) return 1;
    return GetLastError() == ERROR_FILE_NOT_FOUND;
}

static int update_journal_path(char out[UPDATE_ABS_CAP]) {
    return update_join_path(out, UPDATE_ABS_CAP, g_update.root,
                            UPDATE_JOURNAL_REL);
}

static int update_recovery_quarantine_path(char out[UPDATE_ABS_CAP],
                                           const char* target) {
    size_t len;
    if (!out || !target) return 0;
    len = strlen(target);
    if (len + sizeof(UPDATE_RECOVERY_SUFFIX) > UPDATE_ABS_CAP) return 0;
    memcpy(out, target, len);
    memcpy(out + len, UPDATE_RECOVERY_SUFFIX,
           sizeof(UPDATE_RECOVERY_SUFFIX));
    return 1;
}

static int update_delete_recovery_quarantine(const char* path) {
    DWORD attrs = INVALID_FILE_ATTRIBUTES;
    int exists = 0;
    if (!update_path_query(path, &exists, &attrs)) return 0;
    if (!exists) return 1;
    if (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetLastError(ERROR_DIRECTORY);
        return 0;
    }
#ifdef UPDATE_EXT_TEST
    if (g_update_test_hold_recovery_quarantine > 0) {
        g_update_test_hold_recovery_quarantine--;
        SetLastError(ERROR_SHARING_VIOLATION);
        return 0;
    }
#endif
    if (DeleteFileA(path)) return 1;
    return GetLastError() == ERROR_FILE_NOT_FOUND;
}

static int update_journal_write_state(UpdateJournal* journal,
                                      int committed) {
    char path[UPDATE_ABS_CAP];
    char* text;
    size_t cap;
    size_t used = 0;
    size_t i;
    int n;
    int ok;
    const char* magic;
    if (!journal || journal->count == 0 ||
        journal->count > UPDATE_MAX_FILES ||
        !update_journal_path(path)) return 0;
    if (journal->version == 3) magic = UPDATE_JOURNAL_MAGIC_V3;
    else if (journal->version == 2) magic = UPDATE_JOURNAL_MAGIC_V2;
    else return 0;
    cap = 128u + journal->count * (UPDATE_MAX_PATH + 224u);
    text = (char*)malloc(cap);
    if (!text) return 0;
    n = snprintf(text + used, cap - used,
                 "%s\nphase=%s\ncount=%lu\n",
                 magic,
                 committed ? "committed" : "applying",
                 (unsigned long)journal->count);
    if (n < 0 || (size_t)n >= cap - used) {
        free(text);
        return 0;
    }
    used += (size_t)n;
    for (i = 0; i < journal->count; i++) {
        if (!journal->files[i].has_integrity ||
            !update_sha256_valid(journal->files[i].sha256) ||
            journal->files[i].size > UPDATE_FILE_MAX_BYTES ||
            !update_path_is_safe(journal->files[i].path)) {
            free(text);
            return 0;
        }
        if (journal->version >= 3) {
            if (!journal->files[i].has_original_integrity ||
                !update_sha256_valid(
                    journal->files[i].original_sha256) ||
                journal->files[i].original_size > UPDATE_FILE_MAX_BYTES ||
                (journal->files[i].had_original
                    ? _stricmp(journal->files[i].original_sha256,
                               UPDATE_UNKNOWN_SHA256) == 0
                    : (journal->files[i].original_size != 0u ||
                       _stricmp(journal->files[i].original_sha256,
                                UPDATE_UNKNOWN_SHA256) != 0))) {
                free(text);
                return 0;
            }
            n = snprintf(text + used, cap - used,
                         "file=%d:%llu:%s:%llu:%s:%s\n",
                         journal->files[i].had_original ? 1 : 0,
                         (unsigned long long)journal->files[i].size,
                         journal->files[i].sha256,
                         (unsigned long long)journal->files[i].original_size,
                         journal->files[i].original_sha256,
                         journal->files[i].path);
        } else {
            n = snprintf(text + used, cap - used,
                         "file=%d:%llu:%s:%s\n",
                         journal->files[i].had_original ? 1 : 0,
                         (unsigned long long)journal->files[i].size,
                         journal->files[i].sha256,
                         journal->files[i].path);
        }
        if (n < 0 || (size_t)n >= cap - used) {
            free(text);
            return 0;
        }
        used += (size_t)n;
    }
    ok = update_write_bytes_atomic(path, text, used);
    free(text);
    return ok;
}

static int update_journal_write(const UpdateApplyEntry* entries, size_t count,
                                int committed) {
    UpdateJournal journal;
    size_t i;
    if (!entries || count == 0 || count > UPDATE_MAX_FILES) return 0;
    memset(&journal, 0, sizeof(journal));
    journal.version = 3;
    journal.committed = committed ? 1 : 0;
    journal.count = count;
    for (i = 0; i < count; i++) {
        if (!update_copy(journal.files[i].path,
                         sizeof(journal.files[i].path),
                         entries[i].spec.path) ||
            !update_copy(journal.files[i].sha256,
                         sizeof(journal.files[i].sha256),
                         entries[i].spec.sha256)) return 0;
        journal.files[i].size = entries[i].spec.size;
        journal.files[i].had_original = entries[i].had_original ? 1 : 0;
        journal.files[i].has_integrity = 1;
        if (entries[i].has_original_integrity) {
            journal.files[i].original_size = entries[i].original_size;
            journal.files[i].has_original_integrity = 1;
            if (!update_copy(journal.files[i].original_sha256,
                             sizeof(journal.files[i].original_sha256),
                             entries[i].original_sha256)) return 0;
#ifdef UPDATE_EXT_TEST
        } else if (entries[i].had_original) {
            if (!update_capture_regular_file_integrity(
                    entries[i].target,
                    &journal.files[i].original_size,
                    journal.files[i].original_sha256)) return 0;
            journal.files[i].has_original_integrity = 1;
        } else {
            journal.files[i].original_size = 0u;
            journal.files[i].has_original_integrity = 1;
            if (!update_copy(journal.files[i].original_sha256,
                             sizeof(journal.files[i].original_sha256),
                             UPDATE_UNKNOWN_SHA256)) return 0;
#else
        } else {
            return 0;
#endif
        }
    }
    return update_journal_write_state(&journal, committed);
}

static int update_journal_add_cleanup(UpdateJournal* cleanup,
                                      const UpdateJournal* source,
                                      size_t source_index,
                                      const char* quarantine) {
    WIN32_FILE_ATTRIBUTE_DATA file_info;
    uint64_t size;
    size_t index;
    int written;
    if (!cleanup || !source || source_index >= source->count ||
        cleanup->count >= UPDATE_MAX_FILES || !quarantine) return 0;
    index = cleanup->count;
    written = snprintf(cleanup->files[index].path,
                       sizeof(cleanup->files[index].path), "%s%s",
                       source->files[source_index].path,
                       UPDATE_RECOVERY_SUFFIX);
    if (written < 0 ||
        (size_t)written >= sizeof(cleanup->files[index].path) ||
        !update_path_is_safe(cleanup->files[index].path)) return 0;
    if (source->files[source_index].has_integrity) {
        if (!update_copy(cleanup->files[index].sha256,
                         sizeof(cleanup->files[index].sha256),
                         source->files[source_index].sha256)) return 0;
        size = source->files[source_index].size;
    } else {
        if (!GetFileAttributesExA(quarantine, GetFileExInfoStandard,
                                  &file_info) ||
            (file_info.dwFileAttributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            return 0;
        }
        size = ((uint64_t)file_info.nFileSizeHigh << 32) |
               (uint64_t)file_info.nFileSizeLow;
        if (size > UPDATE_FILE_MAX_BYTES ||
            !update_sha256_file(quarantine, size,
                                cleanup->files[index].sha256)) return 0;
    }
    cleanup->files[index].size = size;
    cleanup->files[index].had_original = 0;
    cleanup->files[index].has_integrity = 1;
    cleanup->count++;
    return 1;
}

static int update_journal_add_unresolved(UpdateJournal* unresolved,
                                         const UpdateJournal* source,
                                         size_t source_index) {
    size_t index;
    if (!unresolved || !source || source_index >= source->count ||
        unresolved->count >= UPDATE_MAX_FILES) return 0;
    index = unresolved->count;
    if (!update_copy(unresolved->files[index].path,
                     sizeof(unresolved->files[index].path),
                     source->files[source_index].path)) return 0;
    /* A reduced journal no longer describes the complete release set. Its
     * applying recovery does not need integrity metadata, so use a valid but
     * deliberately non-authorizing sentinel for V1 and V2 alike. This prevents
     * a later retry from promoting a matching subset into a split-version
     * committed transaction. */
    if (!update_copy(unresolved->files[index].sha256,
                     sizeof(unresolved->files[index].sha256),
                     UPDATE_UNKNOWN_SHA256)) return 0;
    unresolved->files[index].size = 0;
    unresolved->files[index].had_original =
        source->files[source_index].had_original;
    unresolved->files[index].has_integrity = 1;
    unresolved->count++;
    return 1;
}

static void update_journal_mark_unresolved(UpdateJournal* unresolved,
                                           const UpdateJournal* source,
                                           size_t source_index,
                                           int* recovery_ok,
                                           int* journal_plan_ok) {
    if (!update_journal_add_unresolved(unresolved, source, source_index)) {
        if (journal_plan_ok) *journal_plan_ok = 0;
    }
    if (recovery_ok) *recovery_ok = 0;
}

static int update_decimal_u64(const char* begin, const char* end,
                              uint64_t* out) {
    uint64_t value = 0;
    const char* p;
    if (!begin || !end || begin >= end || !out) return 0;
    for (p = begin; p < end; p++) {
        unsigned int digit;
        if (*p < '0' || *p > '9') return 0;
        digit = (unsigned int)(*p - '0');
        if (value > (UINT64_MAX - digit) / 10u) return 0;
        value = value * 10u + digit;
    }
    *out = value;
    return 1;
}

static char* update_next_line(char** cursor) {
    char* line;
    char* p;
    if (!cursor || !*cursor || !**cursor) return NULL;
    line = *cursor;
    p = line;
    while (*p && *p != '\r' && *p != '\n') p++;
    if (*p) {
        char newline = *p;
        *p++ = '\0';
        if (newline == '\r' && *p == '\n') p++;
    }
    *cursor = p;
    return line;
}

static int update_journal_load(UpdateJournal* journal) {
    char path[UPDATE_ABS_CAP];
    unsigned char* bytes = NULL;
    size_t len = 0;
    char* cursor;
    char* line;
    char* end_number;
    unsigned long count;
    size_t i;
    if (!journal || !update_journal_path(path) ||
        !update_read_file_bounded(path, 32u * 1024u, &bytes, &len)) return 0;
    memset(journal, 0, sizeof(*journal));
    cursor = (char*)bytes;
    line = update_next_line(&cursor);
    if (!line) goto fail;
    if (strcmp(line, UPDATE_JOURNAL_MAGIC_V3) == 0) journal->version = 3;
    else if (strcmp(line, UPDATE_JOURNAL_MAGIC_V2) == 0) journal->version = 2;
    else if (strcmp(line, UPDATE_JOURNAL_MAGIC_V1) == 0) journal->version = 1;
    else goto fail;
    line = update_next_line(&cursor);
    if (!line || strncmp(line, "phase=", 6) != 0) goto fail;
    if (strcmp(line + 6, "committed") == 0) journal->committed = 1;
    else if (strcmp(line + 6, "applying") != 0) goto fail;
    line = update_next_line(&cursor);
    if (!line || strncmp(line, "count=", 6) != 0) goto fail;
    count = strtoul(line + 6, &end_number, 10);
    if (!end_number || *end_number || count == 0 || count > UPDATE_MAX_FILES) goto fail;
    journal->count = count;
    for (i = 0; i < journal->count; i++) {
        const char* rel;
        const char* size_begin = NULL;
        const char* size_end = NULL;
        const char* hash_begin = NULL;
        const char* hash_end = NULL;
        line = update_next_line(&cursor);
        if (!line || strncmp(line, "file=", 5) != 0 ||
            (line[5] != '0' && line[5] != '1') || line[6] != ':') goto fail;
        if (journal->version == 1) {
            rel = line + 7;
        } else {
            char* colon;
            size_begin = line + 7;
            colon = strchr((char*)size_begin, ':');
            if (!colon) goto fail;
            size_end = colon;
            hash_begin = colon + 1;
            colon = strchr((char*)hash_begin, ':');
            if (!colon || (size_t)(colon - hash_begin) != 64u) goto fail;
            hash_end = colon;
            rel = colon + 1;
            if (!update_decimal_u64(size_begin, size_end,
                                    &journal->files[i].size) ||
                journal->files[i].size > UPDATE_FILE_MAX_BYTES) goto fail;
            memcpy(journal->files[i].sha256, hash_begin,
                   (size_t)(hash_end - hash_begin));
            journal->files[i].sha256[64] = '\0';
            if (!update_sha256_valid(journal->files[i].sha256)) goto fail;
            journal->files[i].has_integrity = 1;
            if (journal->version >= 3) {
                const char* original_size_begin = rel;
                const char* original_size_end;
                const char* original_hash_begin;
                const char* original_hash_end;
                colon = strchr((char*)original_size_begin, ':');
                if (!colon) goto fail;
                original_size_end = colon;
                original_hash_begin = colon + 1;
                colon = strchr((char*)original_hash_begin, ':');
                if (!colon ||
                    (size_t)(colon - original_hash_begin) != 64u) goto fail;
                original_hash_end = colon;
                rel = colon + 1;
                if (!update_decimal_u64(
                        original_size_begin,
                        original_size_end,
                        &journal->files[i].original_size) ||
                    journal->files[i].original_size >
                        UPDATE_FILE_MAX_BYTES) goto fail;
                memcpy(journal->files[i].original_sha256,
                       original_hash_begin,
                       (size_t)(original_hash_end -
                                original_hash_begin));
                journal->files[i].original_sha256[64] = '\0';
                if (!update_sha256_valid(
                        journal->files[i].original_sha256)) goto fail;
                journal->files[i].has_original_integrity = 1;
            }
        }
        if (!update_path_is_safe(rel) ||
            !update_copy(journal->files[i].path,
                         sizeof(journal->files[i].path), rel)) goto fail;
        update_path_normalize(journal->files[i].path);
        {
            size_t j;
            for (j = 0; j < i; j++) {
                if (_stricmp(journal->files[j].path,
                             journal->files[i].path) == 0) goto fail;
            }
        }
        journal->files[i].had_original = line[5] == '1';
        if (journal->version >= 3 &&
            (journal->files[i].had_original
                ? _stricmp(journal->files[i].original_sha256,
                           UPDATE_UNKNOWN_SHA256) == 0
                : (journal->files[i].original_size != 0u ||
                   _stricmp(journal->files[i].original_sha256,
                            UPDATE_UNKNOWN_SHA256) != 0))) {
            goto fail;
        }
    }
    while ((line = update_next_line(&cursor)) != NULL) {
        if (*line) goto fail;
    }
    free(bytes);
    return 1;
fail:
    free(bytes);
    return 0;
}

static int update_regular_file_or_missing(const char* path, int* out_exists) {
    DWORD attrs = INVALID_FILE_ATTRIBUTES;
    if (!path || !out_exists) return 0;
    if (!update_path_query(path, out_exists, &attrs)) return 0;
    if (!*out_exists) return 1;
    if (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetLastError(ERROR_DIRECTORY);
        return 0;
    }
    *out_exists = 1;
    return 1;
}

static int update_revert_cleanup_plan(const UpdateJournal* cleanup,
                                      const int had_original[UPDATE_MAX_FILES]) {
    size_t i;
    if (!cleanup || !had_original) return 0;
    for (i = cleanup->count; i-- > 0;) {
        char original_rel[UPDATE_MAX_PATH + 1];
        char target[UPDATE_ABS_CAP];
        char quarantine[UPDATE_ABS_CAP];
        char backup[UPDATE_ABS_CAP];
        size_t path_len = strlen(cleanup->files[i].path);
        size_t suffix_len = sizeof(UPDATE_RECOVERY_SUFFIX) - 1u;
        if (path_len <= suffix_len ||
            !update_path_has_recovery_suffix(cleanup->files[i].path) ||
            path_len - suffix_len >= sizeof(original_rel)) return 0;
        memcpy(original_rel, cleanup->files[i].path, path_len - suffix_len);
        original_rel[path_len - suffix_len] = '\0';
        if (!update_prepare_relative_path(g_update.root, original_rel,
                                          target, sizeof(target), 0) ||
            !update_prepare_relative_path(g_update.root,
                                          cleanup->files[i].path,
                                          quarantine, sizeof(quarantine), 0)) {
            return 0;
        }
        if (had_original[i]) {
            if (!update_backup_path(backup, sizeof(backup), target) ||
                !MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH)) {
                return 0;
            }
            if (!MoveFileExA(quarantine, target,
                             MOVEFILE_REPLACE_EXISTING |
                                 MOVEFILE_WRITE_THROUGH)) {
                (void)MoveFileExA(backup, target,
                                  MOVEFILE_REPLACE_EXISTING |
                                      MOVEFILE_WRITE_THROUGH);
                return 0;
            }
        } else if (!MoveFileExA(quarantine, target,
                                MOVEFILE_REPLACE_EXISTING |
                                    MOVEFILE_WRITE_THROUGH)) {
            return 0;
        }
    }
    return 1;
}

static int update_journal_file_matches(const char* target,
                                       const UpdateJournal* journal,
                                       size_t index) {
    UpdateFileSpec spec;
    if (!target || !journal || index >= journal->count ||
        !journal->files[index].has_integrity) return 0;
    memset(&spec, 0, sizeof(spec));
    spec.size = journal->files[index].size;
    if (!update_copy(spec.sha256, sizeof(spec.sha256),
                     journal->files[index].sha256)) return 0;
    return update_file_matches(target, &spec);
}

/* A V2/V3 journal is complete only when the entire release set is installed
 * exactly as declared. For an applying journal this is the common crash window
 * after the last target move but before phase=committed reached disk, and it
 * avoids rolling a currently mapped framework DLL backward. Cleanup-only and
 * mixed reduced journals are intentionally ineligible. */
static int update_journal_targets_are_complete(const UpdateJournal* journal,
                                               int* out_complete) {
    size_t i;
    if (!journal || !out_complete) return 0;
    *out_complete = 0;
    if (journal->version < 2) return 1;
    for (i = 0; i < journal->count; i++) {
        char target[UPDATE_ABS_CAP];
        char quarantine[UPDATE_ABS_CAP];
        DWORD target_attrs = INVALID_FILE_ATTRIBUTES;
        int target_exists = 0;
        int quarantine_exists = 0;
        if (!journal->files[i].has_integrity) return 0;
        if (strcmp(journal->files[i].sha256,
                   UPDATE_UNKNOWN_SHA256) == 0) return 1;
        if (!update_release_path_is_safe(journal->files[i].path)) return 1;
        if (!update_prepare_relative_path(g_update.root,
                                          journal->files[i].path,
                                          target, sizeof(target), 0) ||
            !update_recovery_quarantine_path(quarantine, target) ||
            !update_path_query(target, &target_exists, &target_attrs) ||
            !update_path_query(quarantine, &quarantine_exists, NULL)) {
            return 0;
        }
        if (!target_exists ||
            (target_attrs & (FILE_ATTRIBUTE_DIRECTORY |
                             FILE_ATTRIBUTE_REPARSE_POINT)) ||
            quarantine_exists ||
            !update_journal_file_matches(target, journal, i)) {
            return 1;
        }
    }
    *out_complete = 1;
    return 1;
}

static int update_recover_journal(void) {
    char journal_path[UPDATE_ABS_CAP];
    UpdateJournal journal;
    UpdateJournal cleanup;
    UpdateJournal unresolved;
    UpdateJournal reduced;
    int cleanup_had_original[UPDATE_MAX_FILES];
    int journal_exists;
    int is_dir = 0;
    size_t i;
    int ok = 1;
    int journal_plan_ok = 1;
    memset(&cleanup, 0, sizeof(cleanup));
    memset(&unresolved, 0, sizeof(unresolved));
    memset(&reduced, 0, sizeof(reduced));
    memset(cleanup_had_original, 0, sizeof(cleanup_had_original));
    cleanup.version = 2;
    cleanup.committed = 0;
    unresolved.version = 2;
    unresolved.committed = 0;
    reduced.version = 2;
    reduced.committed = 0;
    if (!update_journal_path(journal_path)) return 0;
    {
        DWORD journal_attrs = INVALID_FILE_ATTRIBUTES;
        if (!update_path_query(journal_path, &journal_exists,
                               &journal_attrs)) {
            LOG_ERROR("update: cannot inspect transaction journal winerr=%lu; refusing to update",
                      (unsigned long)GetLastError());
            return 0;
        }
        is_dir = journal_exists &&
                 (journal_attrs & (FILE_ATTRIBUTE_DIRECTORY |
                                   FILE_ATTRIBUTE_REPARSE_POINT));
    }
    if (!journal_exists) return 1;
    if (is_dir || !update_journal_load(&journal)) {
        LOG_ERROR("update: transaction journal is corrupt; refusing to update");
        return 0;
    }
    if (!journal.committed && journal.version >= 2) {
        int targets_complete = 0;
        if (!update_journal_targets_are_complete(&journal,
                                                 &targets_complete)) {
            LOG_ERROR("update: cannot safely inspect applying transaction targets; retaining journal and recovery artifacts winerr=%lu",
                      (unsigned long)GetLastError());
            return 0;
        }
        if (targets_complete) {
            if (!update_journal_write_state(&journal, 1)) {
                LOG_ERROR("update: verified applying targets but cannot commit recovery journal winerr=%lu",
                          (unsigned long)GetLastError());
                return 0;
            }
            journal.committed = 1;
            InterlockedExchange(&g_update_recovery_restart, 1);
            LOG_WARN("update: all interrupted-update targets verified; completing transaction forward");
        }
    }
    if (journal.committed) {
        int targets_valid = 1;
        int rollback_possible = 1;
        if (journal.version < 2) {
            LOG_ERROR("update: legacy committed journal has no installed-file digest; retaining journal and backups");
            return 0;
        }
        /* Verify the complete committed set before deleting even one backup.
         * This avoids turning mere target existence into proof of commit. */
        for (i = 0; i < journal.count; i++) {
            char target[UPDATE_ABS_CAP];
            char backup[UPDATE_ABS_CAP];
            int target_exists = 0;
            int backup_exists = 0;
            if (!journal.files[i].has_integrity ||
                !update_prepare_relative_path(g_update.root,
                                              journal.files[i].path,
                                              target, sizeof(target), 0) ||
                !update_backup_path(backup, sizeof(backup), target) ||
                !update_regular_file_or_missing(target, &target_exists) ||
                !update_regular_file_or_missing(backup, &backup_exists)) {
                LOG_ERROR("update: committed recovery paths are unsafe; retaining journal and backups");
                return 0;
            }
            if (!target_exists ||
                !update_journal_file_matches(target, &journal, i)) {
                LOG_WARN("update: committed recovery target is missing or corrupt file=%s",
                         journal.files[i].path);
                targets_valid = 0;
            }
            if (journal.files[i].had_original && !backup_exists) {
                rollback_possible = 0;
            } else if (!journal.files[i].had_original && backup_exists) {
                rollback_possible = 0;
            }
        }
        if (targets_valid) {
            /* All installed targets were checked before this destructive pass.
             * A cleanup interruption is safe: the committed journal remains and
             * the next launch rechecks every target before deleting the rest. */
            for (i = journal.count; i-- > 0;) {
                char target[UPDATE_ABS_CAP];
                char backup[UPDATE_ABS_CAP];
                int backup_exists = 0;
                if (!update_prepare_relative_path(g_update.root,
                                                  journal.files[i].path,
                                                  target, sizeof(target), 0) ||
                    !update_backup_path(backup, sizeof(backup), target) ||
                    !update_regular_file_or_missing(backup, &backup_exists) ||
                    (backup_exists && !DeleteFileA(backup))) {
                    LOG_ERROR("update: committed cleanup incomplete; retaining journal");
                    return 0;
                }
            }
            if (!DeleteFileA(journal_path)) {
                LOG_ERROR("update: committed targets verified but journal cleanup failed winerr=%lu",
                          (unsigned long)GetLastError());
                return 0;
            }
            return 1;
        }
        if (!rollback_possible) {
            LOG_ERROR("update: committed target is missing or corrupt and a required recovery artifact is unavailable; retaining journal and backups");
            return 0;
        }
        /* Every old target has a backup. Atomically change the phase before
         * touching files so a crash during rollback resumes with the existing
         * interrupted-transaction rules. */
        if (!update_journal_write_state(&journal, 0)) {
            LOG_ERROR("update: cannot journal committed-integrity rollback; retaining recovery artifacts");
            return 0;
        }
        journal.committed = 0;
        LOG_ERROR("update: committed target is missing or corrupt; restoring the pre-update transaction");
    }
    /* Every committed path above either completed verified cleanup, failed
     * without mutation, or atomically changed the phase to applying. The loop
     * below must never use target existence as committed-integrity evidence. */
    if (journal.committed) return 0;
    LOG_WARN("update: recovering interrupted transaction with %lu file(s)",
             (unsigned long)journal.count);
    for (i = journal.count; i-- > 0;) {
        char target[UPDATE_ABS_CAP];
        char backup[UPDATE_ABS_CAP];
        char quarantine[UPDATE_ABS_CAP];
        int target_exists = 0;
        int backup_exists = 0;
        int quarantine_exists = 0;
        int quarantined = 0;
        /* Cleanup-only applying entries are deliberately consumable by both
         * this updater and older recovery code. Older versions already delete
         * had_original=0 targets directly; do the same instead of appending a
         * second reserved suffix. */
        if (update_path_has_recovery_suffix(journal.files[i].path)) {
            if (journal.files[i].had_original ||
                !update_prepare_relative_path(g_update.root,
                                              journal.files[i].path,
                                              target, sizeof(target), 0) ||
                !update_regular_file_or_missing(target, &target_exists)) {
                LOG_ERROR("update: invalid recovery-cleanup entry file=%s winerr=%lu",
                          journal.files[i].path,
                          (unsigned long)GetLastError());
                update_journal_mark_unresolved(&unresolved, &journal, i,
                                               &ok, &journal_plan_ok);
                continue;
            }
            if (target_exists) {
                if (!DeleteFileA(target)) {
                    LOG_ERROR("update: cannot remove released recovery quarantine file=%s winerr=%lu",
                              journal.files[i].path,
                              (unsigned long)GetLastError());
                    update_journal_mark_unresolved(&unresolved, &journal, i,
                                                   &ok, &journal_plan_ok);
                }
            }
            continue;
        }
        if (!update_prepare_relative_path(g_update.root, journal.files[i].path,
                                          target, sizeof(target), 0) ||
            !update_backup_path(backup, sizeof(backup), target) ||
            !update_recovery_quarantine_path(quarantine, target)) {
            LOG_ERROR("update: recovery path validation failed file=%s",
                      journal.files[i].path);
            update_journal_mark_unresolved(&unresolved, &journal, i,
                                           &ok, &journal_plan_ok);
            continue;
        }
        if (!update_regular_file_or_missing(target, &target_exists)) {
            LOG_ERROR("update: recovery cannot inspect target file=%s winerr=%lu",
                      journal.files[i].path, (unsigned long)GetLastError());
            update_journal_mark_unresolved(&unresolved, &journal, i,
                                           &ok, &journal_plan_ok);
            continue;
        }
        if (!update_regular_file_or_missing(backup, &backup_exists)) {
            LOG_ERROR("update: recovery cannot inspect backup file=%s winerr=%lu",
                      journal.files[i].path, (unsigned long)GetLastError());
            update_journal_mark_unresolved(&unresolved, &journal, i,
                                           &ok, &journal_plan_ok);
            continue;
        }
        if (!update_regular_file_or_missing(quarantine, &quarantine_exists)) {
            LOG_ERROR("update: recovery cannot inspect quarantine file=%s winerr=%lu",
                      journal.files[i].path, (unsigned long)GetLastError());
            update_journal_mark_unresolved(&unresolved, &journal, i,
                                           &ok, &journal_plan_ok);
            continue;
        }
        /* A retained quarantine means the previous recovery restored the disk
         * transaction while this process still had the rejected DLL mapped.
         * A clean restart releases it, so remove it before finalizing. */
        if (quarantine_exists) {
            if (!update_delete_recovery_quarantine(quarantine)) {
                LOG_ERROR("update: recovery cannot clean prior quarantine file=%s winerr=%lu",
                          journal.files[i].path, (unsigned long)GetLastError());
                update_journal_mark_unresolved(&unresolved, &journal, i,
                                               &ok, &journal_plan_ok);
                continue;
            }
        }
        if (journal.files[i].had_original) {
            if (backup_exists) {
                /* Renaming a mapped DLL is supported even when deleting it is
                 * denied. Keep the renamed image until process exit, restore
                 * the original pathname, and retain the journal until the
                 * quarantine can be deleted on the next launch. */
                if (target_exists &&
                    !MoveFileExA(target, quarantine, MOVEFILE_WRITE_THROUGH)) {
                    LOG_ERROR("update: recovery cannot quarantine target file=%s winerr=%lu",
                              journal.files[i].path,
                              (unsigned long)GetLastError());
                    update_journal_mark_unresolved(&unresolved, &journal, i,
                                                   &ok, &journal_plan_ok);
                    continue;
                }
                quarantined = target_exists;
                if (quarantined) {
                    InterlockedExchange(&g_update_recovery_restart, 1);
                }
                if (!MoveFileExA(backup, target,
                                 MOVEFILE_REPLACE_EXISTING |
                                     MOVEFILE_WRITE_THROUGH)) {
                    DWORD restore_error = GetLastError();
                    int transaction_state_restored = 1;
                    (void)restore_error;
                    if (quarantined) {
                        if (!MoveFileExA(quarantine, target,
                                         MOVEFILE_REPLACE_EXISTING |
                                             MOVEFILE_WRITE_THROUGH)) {
                            LOG_ERROR("update: recovery also failed to restore quarantined target file=%s winerr=%lu",
                                      journal.files[i].path,
                                      (unsigned long)GetLastError());
                            transaction_state_restored = 0;
                        }
                    }
                    LOG_ERROR("update: recovery cannot restore backup file=%s winerr=%lu",
                              journal.files[i].path,
                              (unsigned long)restore_error);
                    if (transaction_state_restored) {
                        update_journal_mark_unresolved(
                            &unresolved, &journal, i, &ok,
                            &journal_plan_ok);
                    } else {
                        journal_plan_ok = 0;
                        ok = 0;
                    }
                    continue;
                }
                InterlockedExchange(&g_update_recovery_restart, 1);
                if (quarantined &&
                    !update_delete_recovery_quarantine(quarantine)) {
                    size_t cleanup_index = cleanup.count;
                    LOG_WARN("update: recovery quarantine remains mapped file=%s winerr=%lu",
                             journal.files[i].path,
                             (unsigned long)GetLastError());
                    if (!update_journal_add_cleanup(&cleanup, &journal, i,
                                                    quarantine)) {
                        LOG_ERROR("update: cannot journal quarantine cleanup file=%s",
                                  journal.files[i].path);
                        if (MoveFileExA(target, backup,
                                        MOVEFILE_WRITE_THROUGH) &&
                            MoveFileExA(quarantine, target,
                                        MOVEFILE_REPLACE_EXISTING |
                                            MOVEFILE_WRITE_THROUGH)) {
                            update_journal_mark_unresolved(
                                &unresolved, &journal, i, &ok,
                                &journal_plan_ok);
                        } else {
                            journal_plan_ok = 0;
                            ok = 0;
                        }
                    } else {
                        cleanup_had_original[cleanup_index] = 1;
                        InterlockedExchange(&g_update_recovery_restart, 1);
                    }
                }
            } else if (!target_exists) {
                LOG_ERROR("update: recovery target and required backup are both missing file=%s",
                          journal.files[i].path);
                update_journal_mark_unresolved(&unresolved, &journal, i,
                                               &ok, &journal_plan_ok);
            }
            /* No backup means this entry had not moved yet; its target is the
             * untouched original and must be left in place. */
        } else if (target_exists) {
            if (!MoveFileExA(target, quarantine, MOVEFILE_WRITE_THROUGH)) {
                LOG_ERROR("update: recovery cannot quarantine new target file=%s winerr=%lu",
                          journal.files[i].path,
                          (unsigned long)GetLastError());
                update_journal_mark_unresolved(&unresolved, &journal, i,
                                               &ok, &journal_plan_ok);
                continue;
            }
            InterlockedExchange(&g_update_recovery_restart, 1);
            if (!update_delete_recovery_quarantine(quarantine)) {
                size_t cleanup_index = cleanup.count;
                LOG_WARN("update: recovery quarantine remains mapped file=%s winerr=%lu",
                         journal.files[i].path,
                         (unsigned long)GetLastError());
                if (!update_journal_add_cleanup(&cleanup, &journal, i,
                                                quarantine)) {
                    LOG_ERROR("update: cannot journal quarantine cleanup file=%s",
                              journal.files[i].path);
                    if (MoveFileExA(quarantine, target,
                                    MOVEFILE_REPLACE_EXISTING |
                                        MOVEFILE_WRITE_THROUGH)) {
                        update_journal_mark_unresolved(
                            &unresolved, &journal, i, &ok,
                            &journal_plan_ok);
                    } else {
                        journal_plan_ok = 0;
                        ok = 0;
                    }
                } else {
                    cleanup_had_original[cleanup_index] = 0;
                    InterlockedExchange(&g_update_recovery_restart, 1);
                }
            }
        }
    }
    if (unresolved.count + cleanup.count > UPDATE_MAX_FILES) {
        journal_plan_ok = 0;
    }
    if (journal_plan_ok) {
        for (i = 0; i < unresolved.count; i++) {
            reduced.files[reduced.count++] = unresolved.files[i];
        }
        /* Cleanup entries are last so legacy recovery's reverse walk releases
         * quarantines before retrying any unresolved original entries. */
        for (i = 0; i < cleanup.count; i++) {
            reduced.files[reduced.count++] = cleanup.files[i];
        }
    }
    if (!journal_plan_ok) {
        if (cleanup.count > 0 &&
            !update_revert_cleanup_plan(&cleanup, cleanup_had_original)) {
            LOG_ERROR("update: could not restore original journal state after recovery-plan failure");
        }
        LOG_ERROR("update: could not build a safe reduced recovery journal; original journal retained");
        return 0;
    }
    if (reduced.count > 0) {
        /* Publish even when some entries failed. This drops completed work and
         * atomically preserves both cleanup-only quarantines and only the
         * unresolved original entries for old or new updater code. */
        if (!update_journal_write_state(&reduced, 0)) {
            LOG_ERROR("update: cannot publish backward-compatible reduced recovery journal winerr=%lu",
                      (unsigned long)GetLastError());
            if (cleanup.count > 0 &&
                !update_revert_cleanup_plan(&cleanup,
                                            cleanup_had_original)) {
                LOG_ERROR("update: could not restore original journal state after journal-write failure");
            }
            return 0;
        }
        if (unresolved.count > 0) ok = 0;
    } else if (ok && !DeleteFileA(journal_path)) {
        LOG_ERROR("update: recovery cannot finalize transaction journal winerr=%lu",
                  (unsigned long)GetLastError());
        ok = 0;
    } else if (!ok) {
        LOG_ERROR("update: recovery failed without a preservable journal entry");
        return 0;
    }
    if (ok && InterlockedCompareExchange(&g_update_recovery_restart, 0, 0)) {
        LOG_WARN("update: files restored on disk; restart required to finish recovery cleanup");
    }
    if (!ok) LOG_ERROR("update: recovery incomplete; will retry next launch");
    return ok;
}

static void update_reconcile_legacy_backups(void) {
    static const char* const files[] = {
        "SDL2.dll", "lua51.dll", "libgcc_s_dw2-1.dll",
        "libwinpthread-1.dll", "SDL2_mixer.dll"
    };
    size_t i;
    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char target[UPDATE_ABS_CAP];
        char backup[UPDATE_ABS_CAP];
        int target_dir = 0;
        int backup_dir = 0;
        int target_exists;
        int backup_exists;
        if (!update_prepare_relative_path(g_update.root, files[i], target,
                                          sizeof(target), 0) ||
            snprintf(backup, sizeof(backup), "%s.old", target) < 0 ||
            strlen(backup) >= sizeof(backup) - 1) continue;
        target_exists = update_path_exists(target, &target_dir);
        backup_exists = update_path_exists(backup, &backup_dir);
        if (!backup_exists || backup_dir || target_dir) continue;
        if (target_exists) {
            (void)DeleteFileA(backup);
        } else {
            (void)MoveFileExA(backup, target,
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
    }
}

typedef enum UpdateStagedTransactionState {
    UPDATE_STAGED_NONE = 0,
    UPDATE_STAGED_PRISTINE,
    UPDATE_STAGED_INTERRUPTED,
    UPDATE_STAGED_BLOCKED
} UpdateStagedTransactionState;

static int update_journal_entry_paths(const UpdateJournal* journal,
                                      size_t index,
                                      UpdateApplyEntry* entry) {
    char staging_root[UPDATE_ABS_CAP];
    if (!journal || !entry || index >= journal->count ||
        !update_release_path_is_safe(journal->files[index].path) ||
        !journal->files[index].has_integrity ||
        !update_join_path(staging_root, sizeof(staging_root), g_update.root,
                          UPDATE_STAGING_REL)) {
        return 0;
    }
    memset(entry, 0, sizeof(*entry));
    if (!update_copy(entry->spec.path, sizeof(entry->spec.path),
                     journal->files[index].path) ||
        !update_copy(entry->spec.sha256, sizeof(entry->spec.sha256),
                     journal->files[index].sha256) ||
        !update_prepare_relative_path(g_update.root,
                                      journal->files[index].path,
                                      entry->target,
                                      sizeof(entry->target), 0) ||
        !update_prepare_relative_path(staging_root,
                                      journal->files[index].path,
                                      entry->staged,
                                      sizeof(entry->staged), 0) ||
        !update_backup_path(entry->backup, sizeof(entry->backup),
                            entry->target) ||
        !update_recovery_quarantine_path(entry->quarantine,
                                         entry->target)) {
        return 0;
    }
    entry->spec.size = journal->files[index].size;
    entry->spec.overwrite = 1;
    entry->had_original = journal->files[index].had_original ? 1 : 0;
    entry->original_size = journal->files[index].original_size;
    entry->has_original_integrity =
        journal->files[index].has_original_integrity ? 1 : 0;
    if (journal->files[index].has_original_integrity &&
        !update_copy(entry->original_sha256,
                     sizeof(entry->original_sha256),
                     journal->files[index].original_sha256)) {
        return 0;
    }
    return 1;
}

static UpdateStagedTransactionState
update_classify_staged_transaction(UpdateJournal* out_journal,
                                   UpdateApplyEntry* out_entries,
                                   size_t* out_count,
                                   char* error,
                                   size_t error_cap) {
    char journal_path[UPDATE_ABS_CAP];
    UpdateJournal journal;
    UpdateApplyEntry entries[UPDATE_MAX_FILES];
    size_t i;
    int journal_exists = 0;
    int mutation_evidence = 0;
    int invalid_pristine = 0;
    if (out_count) *out_count = 0u;
    if (error && error_cap) error[0] = '\0';
    if (!update_journal_path(journal_path) ||
        !update_path_query(journal_path, &journal_exists, NULL)) {
        if (error && error_cap) {
            snprintf(error, error_cap,
                     "cannot inspect transaction journal");
        }
        return UPDATE_STAGED_BLOCKED;
    }
    if (!journal_exists) return UPDATE_STAGED_NONE;
    if (!update_journal_load(&journal)) {
        if (error && error_cap) {
            snprintf(error, error_cap,
                     "transaction journal is corrupt");
        }
        return UPDATE_STAGED_BLOCKED;
    }
    if (journal.version < 3 || journal.committed) {
        if (out_journal) *out_journal = journal;
        return UPDATE_STAGED_INTERRUPTED;
    }
    for (i = 0; i < journal.count; i++) {
        int target_exists = 0;
        int staged_exists = 0;
        int backup_exists = 0;
        int quarantine_exists = 0;
        int target_is_replacement = 0;
        if (!journal.files[i].has_original_integrity ||
            !update_journal_entry_paths(&journal, i, &entries[i]) ||
            !update_regular_file_or_missing(entries[i].target,
                                            &target_exists) ||
            !update_regular_file_or_missing(entries[i].staged,
                                            &staged_exists) ||
            !update_regular_file_or_missing(entries[i].backup,
                                            &backup_exists) ||
            !update_regular_file_or_missing(entries[i].quarantine,
                                            &quarantine_exists)) {
            if (error && error_cap) {
                snprintf(error, error_cap,
                         "cannot safely inspect staged transaction");
            }
            return UPDATE_STAGED_BLOCKED;
        }
        if (target_exists) {
            target_is_replacement =
                update_file_matches(entries[i].target,
                                    &entries[i].spec);
        }
        if (backup_exists || quarantine_exists ||
            (!staged_exists && target_is_replacement)) {
            mutation_evidence = 1;
        }
        if (!staged_exists ||
            !update_file_matches(entries[i].staged, &entries[i].spec) ||
            !update_entry_original_matches(&entries[i])) {
            invalid_pristine = 1;
        }
    }
    if (mutation_evidence) {
        if (out_journal) *out_journal = journal;
        return UPDATE_STAGED_INTERRUPTED;
    }
    if (invalid_pristine) {
        if (error && error_cap) {
            snprintf(error, error_cap,
                     "staged update or original target changed");
        }
        return UPDATE_STAGED_BLOCKED;
    }
    if (out_journal) *out_journal = journal;
    if (out_entries) memcpy(out_entries, entries, sizeof(entries));
    if (out_count) *out_count = journal.count;
    return UPDATE_STAGED_PRISTINE;
}

static int update_recover_under_mutex(void) {
    HANDLE mutex = update_named_mutex("Install");
    char staging_root[UPDATE_ABS_CAP];
    int ok;
    UpdateStagedTransactionState staged_state;
    char classify_error[UPDATE_STATUS_CAP];
    if (!update_lock_mutex(mutex, 15000)) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }
    staged_state = update_classify_staged_transaction(
        NULL, NULL, NULL, classify_error, sizeof(classify_error));
    if (staged_state == UPDATE_STAGED_PRISTINE) {
        InterlockedExchange(&g_update_helper_pending, 1);
        InterlockedExchange(&g_update_recovery_restart, 1);
        ok = 1;
    } else if (staged_state == UPDATE_STAGED_BLOCKED) {
        LOG_ERROR("update: %s; retaining staged transaction",
                  classify_error[0] ? classify_error :
                      "staged transaction is unsafe");
        ok = 0;
    } else {
        ok = update_recover_journal();
    }
    if (ok && !InterlockedCompareExchange(&g_update_recovery_restart, 0, 0)) {
        update_reconcile_legacy_backups();
        if (!update_join_path(staging_root, sizeof(staging_root), g_update.root,
                              UPDATE_STAGING_REL)) {
            LOG_ERROR("update: recovered transaction but staging path is too long");
            ok = 0;
        } else if (!update_remove_tree(staging_root)) {
            LOG_ERROR("update: recovered transaction but could not clean update staging winerr=%lu",
                      (unsigned long)GetLastError());
            ok = 0;
        }
    }
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return ok;
}

static int update_targets_match(const UpdateManifest* manifest) {
    size_t i;
    for (i = 0; i < manifest->file_count; i++) {
        char target[UPDATE_ABS_CAP];
        char quarantine[UPDATE_ABS_CAP];
        DWORD attrs;
        int quarantine_exists = 0;
        if (!update_prepare_relative_path(g_update.root,
                                          manifest->files[i].path,
                                          target, sizeof(target), 0) ||
            !update_recovery_quarantine_path(quarantine, target) ||
            !update_path_query(quarantine, &quarantine_exists, NULL) ||
            quarantine_exists) return 0;
        attrs = GetFileAttributesA(target);
        if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) ||
            (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
        if (!manifest->files[i].overwrite) continue;
        if (!update_file_matches(target, &manifest->files[i])) return 0;
    }
    return 1;
}

static int update_rollback(UpdateApplyEntry* entries, size_t count) {
    size_t i;
    int ok = 1;
    for (i = count; i-- > 0;) {
        int backup_exists = 0;
        if (!update_regular_file_or_missing(entries[i].backup,
                                            &backup_exists)) {
            ok = 0;
            continue;
        }
        if (entries[i].had_original) {
            if (backup_exists) {
                if (!update_delete_file_if_present(entries[i].target) ||
                    !MoveFileExA(entries[i].backup, entries[i].target,
                                 MOVEFILE_REPLACE_EXISTING |
                                     MOVEFILE_WRITE_THROUGH)) ok = 0;
            }
        } else if (entries[i].swapped &&
                   !update_delete_file_if_present(entries[i].target)) {
            ok = 0;
        }
    }
    return ok;
}

static int update_apply_manifest(const UpdateManifest* manifest,
                                 char* error, size_t error_cap) {
    HANDLE mutex = NULL;
    char staging_root[UPDATE_ABS_CAP];
    char journal_path[UPDATE_ABS_CAP];
    UpdateApplyEntry entries[UPDATE_MAX_FILES];
    size_t count = 0;
    size_t i;
    int journal_written = 0;
    int mutex_locked = 0;
    int ok = 0;
    if (!manifest || !error || error_cap == 0) return 0;
    error[0] = '\0';
    mutex = update_named_mutex("Install");
    if (!update_lock_mutex(mutex, 30000)) {
        snprintf(error, error_cap, "another game instance is updating");
        goto done;
    }
    mutex_locked = 1;
    if (!update_recover_journal()) {
        snprintf(error, error_cap, "an earlier update needs recovery");
        goto done;
    }
    if (InterlockedCompareExchange(&g_update_recovery_restart, 0, 0)) {
        snprintf(error, error_cap,
                 "earlier update restored; restart before installing");
        goto done;
    }
    update_reconcile_legacy_backups();
    if (update_targets_match(manifest)) {
        LOG_INFO("update: release %s is already staged by another process",
                 manifest->version);
        ok = 1;
        goto done;
    }
    if (!update_ensure_mods_dir() ||
        !update_join_path(staging_root, sizeof(staging_root), g_update.root,
                          UPDATE_STAGING_REL) ||
        !update_journal_path(journal_path)) {
        snprintf(error, error_cap, "install path is too long");
        goto done;
    }
    if (!update_remove_tree(staging_root) || !update_create_directory(staging_root)) {
        snprintf(error, error_cap, "cannot prepare update staging directory");
        goto done;
    }
    memset(entries, 0, sizeof(entries));
    for (i = 0; i < manifest->file_count; i++) {
        const UpdateFileSpec* spec = &manifest->files[i];
        DWORD attrs;
        int exists;
        UpdateApplyEntry* entry;
        char download_error[UPDATE_STATUS_CAP];
        char hash[65];
        if (!update_prepare_relative_path(g_update.root, spec->path,
                                          entries[count].target,
                                          sizeof(entries[count].target), 1)) {
            snprintf(error, error_cap, "unsafe target path: %s", spec->path);
            goto fail;
        }
        attrs = GetFileAttributesA(entries[count].target);
        exists = attrs != INVALID_FILE_ATTRIBUTES;
        if (exists && ((attrs & FILE_ATTRIBUTE_DIRECTORY) ||
                       (attrs & FILE_ATTRIBUTE_REPARSE_POINT))) {
            snprintf(error, error_cap, "target is not a regular file: %s", spec->path);
            goto fail;
        }
        if (exists && !spec->overwrite) continue;
        entry = &entries[count];
        entry->spec = *spec;
        entry->had_original = exists;
        {
            int quarantine_exists = 0;
            if (!update_backup_path(entry->backup, sizeof(entry->backup),
                                    entry->target) ||
                !update_recovery_quarantine_path(entry->quarantine,
                                                 entry->target)) {
                snprintf(error, error_cap,
                         "recovery path is too long: %s", spec->path);
                goto fail;
            }
            if (!update_path_query(entry->quarantine, &quarantine_exists,
                                   NULL)) {
                snprintf(error, error_cap,
                         "cannot inspect reserved recovery path: %s",
                         spec->path);
                goto fail;
            }
            if (quarantine_exists) {
                snprintf(error, error_cap,
                         "reserved recovery path already exists: %s",
                         spec->path);
                goto fail;
            }
        }
        /* A stale backup with no journal is reconciled conservatively. */
        if (update_path_exists(entry->backup, NULL)) {
            if (exists) {
                if (!update_delete_file_if_present(entry->backup)) {
                    snprintf(error, error_cap, "cannot remove stale backup: %s",
                             spec->path);
                    goto fail;
                }
            } else if (!MoveFileExA(entry->backup, entry->target,
                                    MOVEFILE_REPLACE_EXISTING |
                                        MOVEFILE_WRITE_THROUGH)) {
                snprintf(error, error_cap, "cannot restore stale backup: %s",
                         spec->path);
                goto fail;
            } else {
                entry->had_original = 1;
            }
        }
        if (entry->had_original) {
            if (!update_capture_regular_file_integrity(
                    entry->target,
                    &entry->original_size,
                    entry->original_sha256)) {
                snprintf(error, error_cap,
                         "cannot fingerprint original target: %s",
                         spec->path);
                goto fail;
            }
        } else {
            entry->original_size = 0u;
            if (!update_copy(entry->original_sha256,
                             sizeof(entry->original_sha256),
                             UPDATE_UNKNOWN_SHA256)) {
                snprintf(error, error_cap,
                         "cannot record missing original target: %s",
                         spec->path);
                goto fail;
            }
        }
        entry->has_original_integrity = 1;
        update_set_status(UPDATE_APPLYING, "downloading %s (%lu/%lu)",
                          spec->path, (unsigned long)(i + 1),
                          (unsigned long)manifest->file_count);
        if (!update_download_verified(manifest, spec, staging_root,
                                      entry->staged, sizeof(entry->staged),
                                      download_error, sizeof(download_error))) {
            snprintf(error, error_cap, "%s", download_error);
            goto fail;
        }
        /* The downloader verifies after writing.  Verify every staged file a
         * second time as a complete set immediately before any target moves. */
        if (!update_sha256_file(entry->staged, spec->size, hash) ||
            _stricmp(hash, spec->sha256) != 0) {
            snprintf(error, error_cap, "staged file changed: %s", spec->path);
            goto fail;
        }
        count++;
    }
    if (count == 0) {
        ok = 1;
        goto done;
    }
    for (i = 0; i < count; i++) {
        if (!update_file_matches(entries[i].staged, &entries[i].spec)) {
            snprintf(error, error_cap, "preflight verification failed: %s",
                     entries[i].spec.path);
            goto fail;
        }
        if (!update_entry_original_matches(&entries[i])) {
            snprintf(error, error_cap,
                     "original target changed while staging: %s",
                     entries[i].spec.path);
            goto fail;
        }
    }
    if (update_cancelled()) {
        snprintf(error, error_cap, "cancelled");
        goto fail;
    }
    if (!update_journal_write(entries, count, 0)) {
        snprintf(error, error_cap, "cannot create transaction journal");
        goto fail;
    }
    journal_written = 1;
#if !defined(UPDATE_EXT_TEST)
    /* The injected DLL never renames a live import. The one-shot updater
     * revalidates this V3 journal after this process closes and owns every
     * write-through swap before it relaunches eggnoggplus.exe. */
    update_set_status(UPDATE_RESTART_PENDING,
                      "verified update ready - restart");
    InterlockedExchange(&g_update_helper_pending, 1);
    ok = 1;
    goto done;
#endif
    update_set_status(UPDATE_APPLYING, "installing verified update...");
    for (i = 0; i < count; i++) {
        if (entries[i].had_original &&
            !MoveFileExA(entries[i].target, entries[i].backup,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            snprintf(error, error_cap, "cannot back up %s (error %lu)",
                     entries[i].spec.path, (unsigned long)GetLastError());
            goto rollback;
        }
        if (!MoveFileExA(entries[i].staged, entries[i].target,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            snprintf(error, error_cap, "cannot install %s (error %lu)",
                     entries[i].spec.path, (unsigned long)GetLastError());
            goto rollback;
        }
        entries[i].swapped = 1;
    }
    for (i = 0; i < count; i++) {
        if (!update_file_matches(entries[i].target, &entries[i].spec)) {
            snprintf(error, error_cap, "post-install verification failed: %s",
                     entries[i].spec.path);
            goto rollback;
        }
    }
    if (!update_journal_write(entries, count, 1)) {
        snprintf(error, error_cap, "cannot commit transaction journal");
        goto rollback;
    }
    ok = 1;
    goto done;

rollback:
    if (!update_rollback(entries, count)) {
        LOG_ERROR("update: rollback incomplete; journal retained for next boot");
    } else {
        (void)DeleteFileA(journal_path);
        journal_written = 0;
    }
fail:
    if (!journal_written) (void)update_remove_tree(staging_root);
done:
    if (mutex_locked) {
        ReleaseMutex(mutex);
    }
    if (mutex) {
        CloseHandle(mutex);
    }
    return ok;
}

static void update_helper_status(char* status, size_t status_cap,
                                 const char* text) {
    if (!status || status_cap == 0u) return;
    update_copy_trunc(status, status_cap, text ? text : "");
}

static UpdateHelperResult
update_helper_apply_pristine(UpdateApplyEntry* entries,
                             size_t count,
                             char* status,
                             size_t status_cap) {
    char journal_path[UPDATE_ABS_CAP];
    char staging_root[UPDATE_ABS_CAP];
    size_t i;
    if (!entries || count == 0u || count > UPDATE_MAX_FILES ||
        !update_journal_path(journal_path) ||
        !update_join_path(staging_root, sizeof(staging_root), g_update.root,
                          UPDATE_STAGING_REL)) {
        update_helper_status(status, status_cap,
                             "staged transaction paths are invalid");
        return UPDATE_HELPER_BLOCKED;
    }
    for (i = 0; i < count; i++) {
        if (!update_file_matches(entries[i].staged, &entries[i].spec) ||
            !update_entry_original_matches(&entries[i])) {
            update_helper_status(status, status_cap,
                                 "staged update changed during final verification");
            return UPDATE_HELPER_BLOCKED;
        }
    }
    for (i = 0; i < count; i++) {
        if (entries[i].had_original &&
            !MoveFileExA(entries[i].target, entries[i].backup,
                         MOVEFILE_REPLACE_EXISTING |
                             MOVEFILE_WRITE_THROUGH)) {
            break;
        }
        if (!MoveFileExA(entries[i].staged, entries[i].target,
                         MOVEFILE_REPLACE_EXISTING |
                             MOVEFILE_WRITE_THROUGH)) {
            break;
        }
        entries[i].swapped = 1;
    }
    if (i != count) {
        if (!update_rollback(entries, count)) {
            update_helper_status(status, status_cap,
                                 "update move failed and rollback is incomplete");
            return UPDATE_HELPER_BLOCKED;
        }
        (void)DeleteFileA(journal_path);
        (void)update_remove_tree(staging_root);
        update_helper_status(status, status_cap,
                             "update failed safely; previous version restored");
        return UPDATE_HELPER_ROLLED_BACK;
    }
    for (i = 0; i < count; i++) {
        if (!update_file_matches(entries[i].target, &entries[i].spec)) {
            if (!update_rollback(entries, count)) {
                update_helper_status(
                    status, status_cap,
                    "installed update failed verification and rollback is incomplete");
                return UPDATE_HELPER_BLOCKED;
            }
            (void)DeleteFileA(journal_path);
            (void)update_remove_tree(staging_root);
            update_helper_status(
                status, status_cap,
                "installed update failed verification; previous version restored");
            return UPDATE_HELPER_ROLLED_BACK;
        }
    }
    if (!update_journal_write(entries, count, 1)) {
        /* The applying V3 journal still names the complete verified target set.
         * Recovery can safely promote this exact cut point forward. */
        if (!update_recover_journal()) {
            update_helper_status(status, status_cap,
                                 "update commit failed and recovery is incomplete");
            return UPDATE_HELPER_BLOCKED;
        }
    } else if (!update_recover_journal()) {
        update_helper_status(status, status_cap,
                             "updated files could not be finalized");
        return UPDATE_HELPER_BLOCKED;
    }
    InterlockedExchange(&g_update_recovery_restart, 0);
    InterlockedExchange(&g_update_helper_pending, 0);
    if (!update_remove_tree(staging_root)) {
        update_helper_status(status, status_cap,
                             "update installed but staging cleanup is blocked");
        return UPDATE_HELPER_BLOCKED;
    }
    update_helper_status(status, status_cap,
                         "verified update installed");
    return UPDATE_HELPER_UPDATED;
}

UpdateHelperResult update_ext_helper_service(char* status, size_t status_cap) {
    HANDLE mutex;
    UpdateJournal journal;
    UpdateApplyEntry entries[UPDATE_MAX_FILES];
    size_t count = 0u;
    UpdateStagedTransactionState staged_state;
    UpdateHelperResult result = UPDATE_HELPER_BLOCKED;
    char classify_error[UPDATE_STATUS_CAP];
    char staging_root[UPDATE_ABS_CAP];
    int journal_exists = 0;
    update_ensure_init();
    update_helper_status(status, status_cap, "");
    mutex = update_named_mutex("Install");
    if (!update_lock_mutex(mutex, 30000)) {
        if (mutex) CloseHandle(mutex);
        update_helper_status(status, status_cap,
                             "another instance is updating");
        return UPDATE_HELPER_BLOCKED;
    }
    staged_state = update_classify_staged_transaction(
        &journal, entries, &count, classify_error, sizeof(classify_error));
    if (staged_state == UPDATE_STAGED_BLOCKED) {
        update_helper_status(status, status_cap,
                             classify_error[0] ? classify_error :
                                 "update recovery is blocked");
        goto done;
    }
    if (staged_state == UPDATE_STAGED_PRISTINE) {
        result = update_helper_apply_pristine(entries, count,
                                              status, status_cap);
        goto done;
    }
    if (staged_state == UPDATE_STAGED_INTERRUPTED) {
        int completed_forward = 0;
        if (journal.version >= 2) {
            if (!update_journal_targets_are_complete(
                    &journal, &completed_forward)) {
                update_helper_status(
                    status, status_cap,
                    "interrupted update targets cannot be inspected");
                goto done;
            }
        }
        if (!update_recover_journal()) {
            update_helper_status(status, status_cap,
                                 "interrupted update recovery is incomplete");
            goto done;
        }
        InterlockedExchange(&g_update_recovery_restart, 0);
        InterlockedExchange(&g_update_helper_pending, 0);
        result = completed_forward
            ? UPDATE_HELPER_UPDATED : UPDATE_HELPER_ROLLED_BACK;
        update_helper_status(
            status, status_cap,
            completed_forward
                ? "interrupted verified update completed"
                : "interrupted update rolled back safely");
    } else {
        result = UPDATE_HELPER_READY;
        update_helper_status(status, status_cap, "ready");
    }
    update_reconcile_legacy_backups();
    if (!update_join_path(staging_root, sizeof(staging_root), g_update.root,
                          UPDATE_STAGING_REL)) {
        update_helper_status(status, status_cap,
                             "update staging path is invalid");
        result = UPDATE_HELPER_BLOCKED;
        goto done;
    }
    if (!update_remove_tree(staging_root)) {
        update_helper_status(status, status_cap,
                             "update staging cleanup is blocked");
        result = UPDATE_HELPER_BLOCKED;
        goto done;
    }
    {
        char journal_path[UPDATE_ABS_CAP];
        if (!update_journal_path(journal_path) ||
            !update_path_query(journal_path, &journal_exists, NULL) ||
            journal_exists) {
            update_helper_status(status, status_cap,
                                 "transaction evidence remains unresolved");
            result = UPDATE_HELPER_BLOCKED;
        }
    }
done:
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return result;
}

#ifdef UPDATE_EXT_HELPER_TEST
int update_ext_helper_set_root_for_test(const char* root) {
    char full[UPDATE_ABS_CAP];
    DWORD n;
    DWORD attrs;
    if (!root || !root[0]) return 0;
    n = GetFullPathNameA(root, sizeof(full), full, NULL);
    if (n == 0u || n >= sizeof(full)) return 0;
    attrs = GetFileAttributesA(full);
    if (attrs == INVALID_FILE_ATTRIBUTES ||
        !(attrs & FILE_ATTRIBUTE_DIRECTORY) ||
        (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return 0;
    }
    update_ensure_init();
    EnterCriticalSection(&g_update.lock);
    n = update_copy(g_update.root, sizeof(g_update.root), full) ? 1u : 0u;
    LeaveCriticalSection(&g_update.lock);
    return n ? 1 : 0;
}
#endif

/* ---- Asynchronous public API ------------------------------------------ */

enum {
    UPDATE_WORK_CHECK = 1,
    UPDATE_WORK_APPLY = 2
};

static int update_run_apply(void) {
    UpdateManifest manifest;
    char error[UPDATE_STATUS_CAP];
    EnterCriticalSection(&g_update.lock);
    manifest = g_update.manifest;
    LeaveCriticalSection(&g_update.lock);
    if (manifest.file_count == 0 || !update_version_valid(manifest.version)) {
        update_set_error("no validated manifest is available");
        LOG_ERROR("update: apply requested without a validated manifest");
        return 0;
    }
    update_set_status(UPDATE_APPLYING, "preparing %s...", manifest.version);
    if (!update_apply_manifest(&manifest, error, sizeof(error))) {
        if (update_cancelled() && strcmp(error, "cancelled") == 0) {
            update_set_status(UPDATE_AVAILABLE, "%s available - press to update",
                              manifest.version);
            return 0;
        }
        update_set_error("%s", error);
        LOG_ERROR("update: %s", error);
        return 0;
    }
    if (InterlockedCompareExchange(&g_update_helper_pending, 0, 0)) {
        update_set_status(UPDATE_RESTART_PENDING, "%s ready - restart",
                          manifest.version);
    } else {
        update_set_status(UPDATE_RESTART_PENDING, "%s installed - restart",
                          manifest.version);
    }
    InterlockedExchange(&g_update_notice, 1);
    LOG_INFO("update: %s %s; restart to load it",
             manifest.version,
             InterlockedCompareExchange(&g_update_helper_pending, 0, 0)
                ? "staged and verified for YuleUpdater"
                : "installed and verified");
    return 1;
}

static int update_run_check(void) {
    char channel[UPDATE_URL_CAP];
    unsigned char* json = NULL;
    size_t json_len = 0;
    char error[UPDATE_STATUS_CAP];
    UpdateManifest manifest;
    int comparison;
    if (!update_recover_under_mutex()) {
        update_set_status(UPDATE_ERROR,
                          "recovery blocked - see modframework.log");
        InterlockedExchange(&g_update_notice, 1);
        return 0;
    }
    if (InterlockedCompareExchange(&g_update_recovery_restart, 0, 0)) {
        update_set_status(
            UPDATE_RESTART_PENDING,
            InterlockedCompareExchange(&g_update_helper_pending, 0, 0)
                ? "verified update ready - restart"
                : "recovery complete - restart");
        InterlockedExchange(&g_update_notice, 1);
        LOG_WARN("update: restart the game to finish recovery");
        return 0;
    }
    if (update_cancelled()) return 0;
    EnterCriticalSection(&g_update.lock);
    update_copy_trunc(channel, sizeof(channel), g_update.channel_url);
    LeaveCriticalSection(&g_update.lock);
    update_set_status(UPDATE_CHECKING, "checking for updates...");
    if (!update_http_get(channel, UPDATE_MANIFEST_MAX_BYTES,
                         &json, &json_len, error, sizeof(error))) {
        if (!update_cancelled()) {
            LOG_WARN("update: channel check unavailable: %s", error);
            /* Offline launch is deliberately quiet in the UI. */
            update_set_status(UPDATE_IDLE, "update check unavailable");
        }
        return 0;
    }
    if (json_len == 0 || !update_manifest_parse((const char*)json, &manifest)) {
        free(json);
        update_set_error("release channel returned an invalid manifest");
        LOG_ERROR("update: rejected malformed or unsafe release manifest");
        return 0;
    }
    free(json);
    comparison = update_version_cmp(manifest.version, FRAMEWORK_VERSION);
    EnterCriticalSection(&g_update.lock);
    update_copy_trunc(g_update.latest_version,
                      sizeof(g_update.latest_version), manifest.version);
    if (comparison > 0) g_update.manifest = manifest;
    else memset(&g_update.manifest, 0, sizeof(g_update.manifest));
    LeaveCriticalSection(&g_update.lock);
    if (comparison <= 0) {
        update_set_status(UPDATE_UP_TO_DATE, "up to date");
        LOG_INFO("update: current (local=%s channel=%s)",
                 FRAMEWORK_VERSION, manifest.version);
        return 1;
    }
    update_set_status(UPDATE_AVAILABLE, "%s available - press to update",
                      manifest.version);
    InterlockedExchange(&g_update_notice, 1);
    LOG_INFO("update: %s available (local=%s)",
             manifest.version, FRAMEWORK_VERSION);
    return 1;
}

static DWORD WINAPI update_worker_thread(LPVOID parameter) {
    int mode = (int)(uintptr_t)parameter;
    if (mode == UPDATE_WORK_CHECK) {
        (void)update_run_check();
        if (!update_cancelled() && update_ext_status() == UPDATE_AVAILABLE &&
            (update_ext_auto() ||
             InterlockedExchange(&g_update_apply_requested, 0) != 0)) {
            (void)update_run_apply();
        }
    } else if (mode == UPDATE_WORK_APPLY) {
        InterlockedExchange(&g_update_apply_requested, 0);
        (void)update_run_apply();
    }

    /* Synchronize the final explicit-apply check with begin_apply.  If the UI
     * races the last few instructions of a check worker, either this worker
     * consumes the request or begin_apply observes worker_active == 0 and
     * starts a fresh worker. */
    if (mode == UPDATE_WORK_CHECK && !update_cancelled()) {
        EnterCriticalSection(&g_update.lock);
        if (update_ext_status() == UPDATE_AVAILABLE &&
            InterlockedExchange(&g_update_apply_requested, 0) != 0) {
            LeaveCriticalSection(&g_update.lock);
            (void)update_run_apply();
            EnterCriticalSection(&g_update.lock);
        }
        g_update.worker_active = 0;
        LeaveCriticalSection(&g_update.lock);
    } else {
        EnterCriticalSection(&g_update.lock);
        g_update.worker_active = 0;
        LeaveCriticalSection(&g_update.lock);
    }
    return 0;
}

static int update_start_worker_locked(int mode) {
    HANDLE thread;
    if (g_update.worker_active || update_cancelled()) return 0;
    if (g_update.worker) {
        CloseHandle(g_update.worker);
        g_update.worker = NULL;
    }
    g_update.worker_active = 1;
    thread = CreateThread(NULL, 0, update_worker_thread,
                          (LPVOID)(uintptr_t)mode, 0, NULL);
    if (!thread) {
        g_update.worker_active = 0;
        return 0;
    }
    g_update.worker = thread;
    return 1;
}

void update_ext_boot(void) {
    int started;
    update_ensure_init();
    if (InterlockedCompareExchange(&g_update_booted, 1, 0) != 0) return;
    InterlockedExchange(&g_update_cancel, 0);
    update_read_config();
    EnterCriticalSection(&g_update.lock);
    LOG_INFO("update: boot local=%s auto=%d channel=%s",
             FRAMEWORK_VERSION, update_ext_auto(), g_update.channel_url);
    started = update_start_worker_locked(UPDATE_WORK_CHECK);
    LeaveCriticalSection(&g_update.lock);
    if (!started) {
        InterlockedExchange(&g_update_booted, 0);
        update_set_status(UPDATE_IDLE, "update check unavailable");
        LOG_WARN("update: could not start background check");
    }
}

static int update_check_status_allows_start(UpdateStatus status) {
    return status == UPDATE_IDLE || status == UPDATE_UP_TO_DATE ||
           status == UPDATE_ERROR;
}

void update_ext_begin_check(void) {
    UpdateStatus status;
    int started = 0;
    int eligible = 0;

    update_ensure_init();
    status = update_ext_status();
    if (!update_check_status_allows_start(status)) return;

    EnterCriticalSection(&g_update.lock);
    status = update_ext_status();
    if (!g_update.worker_active && update_check_status_allows_start(status)) {
        eligible = 1;
        /* A completed worker leaves its handle for the next launch to reap.
         * update_start_worker_locked closes that handle before creating the
         * replacement.  Clear stale terminal state before publishing the new
         * check so two rapid UI activations cannot start two workers. */
        InterlockedExchange(&g_update_cancel, 0);
        InterlockedExchange(&g_update_apply_requested, 0);
        g_update.error[0] = '\0';
        g_update.latest_version[0] = '\0';
        memset(&g_update.manifest, 0, sizeof(g_update.manifest));
        update_copy_trunc(g_update.status_line,
                          sizeof(g_update.status_line),
                          "checking for updates...");
        InterlockedExchange(&g_update_status, UPDATE_CHECKING);
        started = update_start_worker_locked(UPDATE_WORK_CHECK);
        if (started) InterlockedExchange(&g_update_booted, 1);
    }
    LeaveCriticalSection(&g_update.lock);

    if (eligible && !started) {
        update_set_error("could not start update worker");
        LOG_ERROR("update: could not start manual check worker");
    }
}

void update_ext_begin_apply(void) {
    int started = 1;
    update_ensure_init();
    if (update_ext_status() != UPDATE_AVAILABLE || update_cancelled()) return;
    InterlockedExchange(&g_update_apply_requested, 1);
    EnterCriticalSection(&g_update.lock);
    if (!g_update.worker_active) {
        started = update_start_worker_locked(UPDATE_WORK_APPLY);
    }
    LeaveCriticalSection(&g_update.lock);
    if (!started) {
        update_set_error("could not start update worker");
        LOG_ERROR("update: could not start apply worker");
    }
}

void update_ext_shutdown(void) {
    HANDLE worker = NULL;
    DWORD wait;
    update_ensure_init();
    InterlockedExchange(&g_update_cancel, 1);
    EnterCriticalSection(&g_update.lock);
    worker = g_update.worker;
    LeaveCriticalSection(&g_update.lock);
    if (!worker) return;
    wait = WaitForSingleObject(worker, UPDATE_SHUTDOWN_WAIT_MS);
    if (wait == WAIT_OBJECT_0) {
        EnterCriticalSection(&g_update.lock);
        if (g_update.worker == worker) {
            CloseHandle(g_update.worker);
            g_update.worker = NULL;
            g_update.worker_active = 0;
        }
        LeaveCriticalSection(&g_update.lock);
    } else {
        LOG_WARN("update: background worker did not stop before shutdown");
    }
}

#ifdef UPDATE_EXT_TEST
#include <assert.h>

static void update_test_write(const char* path, const char* text) {
    assert(update_write_bytes_atomic(path, text, strlen(text)));
}

static void update_test_expect_file(const char* path, const char* expected) {
    unsigned char* bytes = NULL;
    size_t len = 0;
    assert(update_read_file_bounded(path, 1024, &bytes, &len));
    assert(len == strlen(expected));
    assert(memcmp(bytes, expected, len) == 0);
    free(bytes);
}

static void update_test_set_expected(UpdateApplyEntry* entry,
                                     const char* replacement) {
    size_t len = strlen(replacement);
    assert(entry != NULL);
    entry->spec.size = (uint64_t)len;
    assert(update_sha256_hex(replacement, len, entry->spec.sha256));
}

static void update_test_prepare_swap(const char* target,
                                     const char* staged,
                                     const char* backup,
                                     const char* original,
                                     const char* replacement) {
    /* Each scenario below models a fresh process unless it explicitly resets
     * the flag between two recovery launches. */
    InterlockedExchange(&g_update_recovery_restart, 0);
    g_update_test_probe_error_path = NULL;
    g_update_test_hold_recovery_quarantine = 0;
    assert(update_delete_file_if_present(target));
    assert(update_delete_file_if_present(staged));
    assert(update_delete_file_if_present(backup));
    update_test_write(target, original);
    update_test_write(staged, replacement);
}

/* Models the applying-journal loop shipped before quarantine-aware recovery. */
static int update_test_legacy_applying_recover(void) {
    UpdateJournal journal;
    char journal_path[UPDATE_ABS_CAP];
    size_t i;
    int ok = 1;
    if (!update_journal_path(journal_path) ||
        !update_journal_load(&journal) || journal.committed) return 0;
    for (i = journal.count; i-- > 0;) {
        char target[UPDATE_ABS_CAP];
        char backup[UPDATE_ABS_CAP];
        int target_dir = 0;
        int backup_dir = 0;
        int target_exists;
        int backup_exists;
        if (!update_prepare_relative_path(g_update.root,
                                          journal.files[i].path,
                                          target, sizeof(target), 0) ||
            !update_backup_path(backup, sizeof(backup), target)) {
            ok = 0;
            continue;
        }
        target_exists = update_path_exists(target, &target_dir);
        backup_exists = update_path_exists(backup, &backup_dir);
        if (target_dir || backup_dir) {
            ok = 0;
            continue;
        }
        if (journal.files[i].had_original) {
            if (backup_exists) {
                if (target_exists && !DeleteFileA(target)) {
                    ok = 0;
                } else if (!MoveFileExA(backup, target,
                                        MOVEFILE_REPLACE_EXISTING |
                                            MOVEFILE_WRITE_THROUGH)) {
                    ok = 0;
                }
            } else if (!target_exists) {
                ok = 0;
            }
        } else if (target_exists && !DeleteFileA(target)) {
            ok = 0;
        }
    }
    if (ok && !DeleteFileA(journal_path)) ok = 0;
    return ok;
}

static void update_test_json(void) {
    static const char good[] =
        "{\"channel_version\":1,\"version\":\"1.10\","
        "\"base\":\"http://127.0.0.1:8788/1.10/\","
        "\"notes\":\"line\\nwith \\u263a\",\"files\":["
        "{\"path\":\"SDL2.dll\","
        "\"sha256\":\"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD\","
        "\"size\":3,\"overwrite\":true},"
        "{\"path\":\"data/new.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":0,\"overwrite\":false}] }";
    UpdateFileSpec files[UPDATE_MAX_FILES];
    UpdateManifest manifest;
    size_t count = 0;
    char value[128];
    assert(update_json_get_string(good, "version", value, sizeof(value)));
    assert(strcmp(value, "1.10") == 0);
    assert(update_json_get_string(good, "base", value, sizeof(value)));
    assert(strcmp(value, "http://127.0.0.1:8788/1.10/") == 0);
    assert(update_json_scan_files(good, files, UPDATE_MAX_FILES, &count));
    assert(count == 2);
    assert(strcmp(files[0].path, "SDL2.dll") == 0);
    assert(files[0].size == 3 && files[0].overwrite == 1);
    assert(files[0].sha256[0] == 'b');
    assert(strcmp(files[1].path, "data/new.bin") == 0);
    assert(files[1].overwrite == 0);
    assert(update_manifest_parse(good, &manifest));
    assert(strcmp(manifest.version, "1.10") == 0);

    assert(!update_json_scan_files(
        "{\"files\":[{\"path\":\"../SDL2.dll\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1}]}", files, UPDATE_MAX_FILES, &count));
    assert(!update_json_scan_files(
        "{\"files\":[{\"path\":\"x.dll\","
        "\"sha256\":\"bad\",\"size\":1}]}",
        files, UPDATE_MAX_FILES, &count));
    assert(!update_json_scan_files(
        "{\"files\":[{\"path\":\"x.dll\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1},{\"path\":\"X.DLL\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1}]}", files, UPDATE_MAX_FILES, &count));
    assert(!update_json_scan_files(
        "{\"files\":[{\"path\":\"SDL2.dll.update-recovery\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1}]}", files, UPDATE_MAX_FILES, &count));
    assert(!update_json_scan_files(
        "{\"files\":[{\"path\":\"cache.update-recovery/payload.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1}]}", files, UPDATE_MAX_FILES, &count));
    assert(!update_json_scan_files(
        "{\"files\":[{\"path\":\"cache\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1},{\"path\":\"cache.update-recovery/payload.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1}]}", files, UPDATE_MAX_FILES, &count));
    assert(!update_json_get_string(
        "{\"version\":\"1.0\",\"version\":\"2.0\"}",
        "version", value, sizeof(value)));
    assert(!update_json_get_string("{\"version\":\"unterminated}",
                                   "version", value, sizeof(value)));
    assert(update_path_is_safe("bin/SDL2.dll"));
    assert(update_path_is_safe("SDL2.dll.update-recovery"));
    assert(!update_release_path_is_safe("SDL2.dll.update-recovery"));
    assert(!update_release_path_is_safe(
        "cache.UPDATE-RECOVERY/payload.bin"));
    assert(!update_path_is_safe("C:/escape.dll"));
    assert(!update_path_is_safe("mods//escape.dll"));
    assert(!update_path_is_safe("file.dll:stream"));
    assert(!update_path_is_safe("NUL.dll"));
    assert(!update_path_is_safe("bin/com1.txt"));
    assert(!update_path_is_safe("file.dll."));
}

static void update_test_storage(void) {
    char cwd[UPDATE_ABS_CAP];
    char root[UPDATE_ABS_CAP];
    char mods[UPDATE_ABS_CAP];
    char staging[UPDATE_ABS_CAP];
    char target[UPDATE_ABS_CAP];
    char staged[UPDATE_ABS_CAP];
    char backup[UPDATE_ABS_CAP];
    char quarantine[UPDATE_ABS_CAP];
    char target2[UPDATE_ABS_CAP];
    char staged2[UPDATE_ABS_CAP];
    char backup2[UPDATE_ABS_CAP];
    char quarantine2[UPDATE_ABS_CAP];
    char journal_path[UPDATE_ABS_CAP];
    char cfg[UPDATE_ABS_CAP];
    char value[64];
    char too_long_value[4098];
    UpdateApplyEntry entry;
    UpdateApplyEntry batch[2];
    DWORD cwd_len;

    update_ensure_init();
    cwd_len = GetCurrentDirectoryA(sizeof(cwd), cwd);
    assert(cwd_len > 0 && cwd_len < sizeof(cwd));
    assert(snprintf(root, sizeof(root), "%s\\build\\update_ext_test_tmp_%lu",
                    cwd, (unsigned long)GetCurrentProcessId()) > 0);
    (void)update_remove_tree(root);
    assert(update_create_directory(root));
    EnterCriticalSection(&g_update.lock);
    assert(update_copy(g_update.root, sizeof(g_update.root), root));
    LeaveCriticalSection(&g_update.lock);
    assert(update_join_path(mods, sizeof(mods), root, "mods"));
    assert(update_create_directory(mods));
    assert(update_join_path(staging, sizeof(staging), root,
                            UPDATE_STAGING_REL));
    assert(update_create_directory(staging));
    assert(update_join_path(target, sizeof(target), root, "sample.dll"));
    assert(update_join_path(staged, sizeof(staged), staging, "sample.dll"));
    assert(update_backup_path(backup, sizeof(backup), target));
    assert(update_recovery_quarantine_path(quarantine, target));
    assert(update_join_path(target2, sizeof(target2), root, "sample2.dll"));
    assert(update_join_path(staged2, sizeof(staged2), staging, "sample2.dll"));
    assert(update_backup_path(backup2, sizeof(backup2), target2));
    assert(update_recovery_quarantine_path(quarantine2, target2));
    assert(update_journal_path(journal_path));

    memset(&entry, 0, sizeof(entry));
    assert(update_copy(entry.spec.path, sizeof(entry.spec.path), "sample.dll"));
    assert(update_copy(entry.target, sizeof(entry.target), target));
    assert(update_copy(entry.staged, sizeof(entry.staged), staged));
    assert(update_copy(entry.backup, sizeof(entry.backup), backup));
    entry.had_original = 1;
    update_test_set_expected(&entry, "replacement");

    /* A V3 applying journal whose entire installed set matches is completed
     * forward. This avoids rolling an already-installed mapped DLL backward. */
    update_test_prepare_swap(target, staged, backup,
                             "original", "replacement");
    assert(update_journal_write(&entry, 1, 0));
    {
        UpdateJournal loaded;
        assert(update_journal_load(&loaded));
        assert(loaded.version == 3 && !loaded.committed && loaded.count == 1);
        assert(loaded.files[0].has_integrity);
        assert(loaded.files[0].has_original_integrity);
        assert(loaded.files[0].size == strlen("replacement"));
        assert(loaded.files[0].original_size == strlen("original"));
        assert(strcmp(loaded.files[0].sha256, entry.spec.sha256) == 0);
    }
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    InterlockedExchange(&g_update_recovery_restart, 0);
    assert(update_recover_journal());
    assert(InterlockedCompareExchange(&g_update_recovery_restart, 0, 0));
    update_test_expect_file(target, "replacement");
    assert(!update_path_exists(backup, NULL));
    assert(!update_path_exists(quarantine, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* A mismatched applying target still rolls back, and any target mutation
     * latches restart-required even when quarantine deletion succeeds. */
    update_test_prepare_swap(target, staged, backup,
                             "rollback-original", "replacement");
    assert(update_journal_write(&entry, 1, 0));
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    update_test_write(target, "rollback-corrupt");
    assert(update_recover_journal());
    assert(InterlockedCompareExchange(&g_update_recovery_restart, 0, 0));
    update_test_expect_file(target, "rollback-original");
    assert(!update_path_exists(backup, NULL));
    assert(!update_path_exists(quarantine, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* A pre-existing quarantine makes an otherwise complete V3 set
     * ineligible for roll-forward, so it cannot be orphaned by commit cleanup. */
    update_test_prepare_swap(target, staged, backup,
                             "quarantine-original", "replacement");
    assert(update_journal_write(&entry, 1, 0));
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    update_test_write(quarantine, "stale-quarantine");
    assert(update_recover_journal());
    assert(InterlockedCompareExchange(&g_update_recovery_restart, 0, 0));
    update_test_expect_file(target, "quarantine-original");
    assert(!update_path_exists(backup, NULL));
    assert(!update_path_exists(quarantine, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* Attribute-query failures are not treated as missing durability
     * artifacts. A denied journal probe leaves every transaction file alone. */
    update_test_prepare_swap(target, staged, backup,
                             "probe-original", "replacement");
    assert(update_journal_write(&entry, 1, 0));
    g_update_test_probe_error_path = journal_path;
    assert(!update_recover_journal());
    update_test_expect_file(target, "probe-original");
    assert(!update_path_exists(backup, NULL));
    assert(update_path_exists(journal_path, NULL));
    g_update_test_probe_error_path = NULL;
    assert(DeleteFileA(journal_path));

    /* Immediate rollback deletion uses the same missing-vs-error rule. */
    g_update_test_probe_error_path = target;
    assert(!update_delete_file_if_present(target));
    update_test_expect_file(target, "probe-original");
    g_update_test_probe_error_path = NULL;

    /* Immediate rollback must also retain its journal when `.old` cannot be
     * inspected; inaccessible cannot be collapsed into absent. */
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    update_test_write(target, "replacement");
    assert(update_journal_write(&entry, 1, 0));
    g_update_test_probe_error_path = backup;
    assert(!update_rollback(&entry, 1));
    update_test_expect_file(target, "replacement");
    update_test_expect_file(backup, "probe-original");
    assert(update_path_exists(journal_path, NULL));
    g_update_test_probe_error_path = NULL;
    assert(update_recover_journal());
    assert(update_path_exists(target, NULL));
    assert(!update_path_exists(backup, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* The same fail-closed rule applies to the reserved quarantine probe. */
    update_test_prepare_swap(target, staged, backup,
                             "probe-quarantine-original", "replacement");
    assert(update_journal_write(&entry, 1, 0));
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    g_update_test_probe_error_path = quarantine;
    assert(!update_recover_journal());
    update_test_expect_file(target, "replacement");
    update_test_expect_file(backup, "probe-quarantine-original");
    assert(update_path_exists(journal_path, NULL));
    g_update_test_probe_error_path = NULL;
    assert(update_recover_journal());
    assert(InterlockedCompareExchange(&g_update_recovery_restart, 0, 0));
    update_test_expect_file(target, "replacement");
    assert(!update_path_exists(backup, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* A mapped replacement can be renamed but not deleted until process exit.
     * Recovery restores the original disk pathname immediately, retains the
     * journal/quarantine, and finishes cleanup after the next restart. */
    update_test_prepare_swap(target, staged, backup,
                             "original-mapped", "replacement");
    assert(update_journal_write(&entry, 1, 0));
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    update_test_write(target, "mapped-corrupt");
    g_update_test_hold_recovery_quarantine = 1;
    assert(update_recover_journal());
    assert(InterlockedCompareExchange(&g_update_recovery_restart, 0, 0));
    update_test_expect_file(target, "original-mapped");
    assert(!update_path_exists(backup, NULL));
    assert(update_path_exists(quarantine, NULL));
    assert(update_path_exists(journal_path, NULL));
    {
        UpdateJournal cleanup_journal;
        assert(update_journal_load(&cleanup_journal));
        assert(!cleanup_journal.committed && cleanup_journal.count == 1);
        assert(!cleanup_journal.files[0].had_original);
        assert(strcmp(cleanup_journal.files[0].path,
                      "sample.dll.update-recovery") == 0);
    }
    assert(update_test_legacy_applying_recover());
    update_test_expect_file(target, "original-mapped");
    assert(!update_path_exists(quarantine, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* The restart handoff is transaction-wide: all files are restored before
     * the journal is retained, and the next launch cleans the one simulated
     * mapped quarantine before finalizing the complete set. */
    batch[0] = entry;
    memset(&batch[1], 0, sizeof(batch[1]));
    assert(update_copy(batch[1].spec.path, sizeof(batch[1].spec.path),
                       "sample2.dll"));
    assert(update_copy(batch[1].target, sizeof(batch[1].target), target2));
    assert(update_copy(batch[1].staged, sizeof(batch[1].staged), staged2));
    assert(update_copy(batch[1].backup, sizeof(batch[1].backup), backup2));
    batch[1].had_original = 1;
    update_test_set_expected(&batch[1], "replacement-two");
    update_test_prepare_swap(target, staged, backup,
                             "mapped-original-one", "replacement");
    update_test_prepare_swap(target2, staged2, backup2,
                             "mapped-original-two", "replacement-two");
    assert(update_journal_write(batch, 2, 0));
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(target2, backup2, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged2, target2, MOVEFILE_WRITE_THROUGH));
    update_test_write(target2, "mapped-corrupt-two");
    g_update_test_hold_recovery_quarantine = 2;
    InterlockedExchange(&g_update_recovery_restart, 0);
    assert(update_recover_journal());
    assert(InterlockedCompareExchange(&g_update_recovery_restart, 0, 0));
    update_test_expect_file(target, "mapped-original-one");
    update_test_expect_file(target2, "mapped-original-two");
    assert(!update_path_exists(backup, NULL));
    assert(!update_path_exists(backup2, NULL));
    assert(update_path_exists(quarantine, NULL));
    assert(update_path_exists(quarantine2, NULL));
    assert(update_path_exists(journal_path, NULL));
    {
        UpdateJournal cleanup_journal;
        assert(update_journal_load(&cleanup_journal));
        assert(!cleanup_journal.committed && cleanup_journal.count == 2);
        assert(!cleanup_journal.files[0].had_original);
        assert(!cleanup_journal.files[1].had_original);
        assert(update_path_has_recovery_suffix(cleanup_journal.files[0].path));
        assert(update_path_has_recovery_suffix(cleanup_journal.files[1].path));
    }
    InterlockedExchange(&g_update_recovery_restart, 0);
    assert(update_recover_journal());
    assert(!InterlockedCompareExchange(&g_update_recovery_restart, 0, 0));
    update_test_expect_file(target, "mapped-original-one");
    update_test_expect_file(target2, "mapped-original-two");
    assert(!update_path_exists(quarantine, NULL));
    assert(!update_path_exists(quarantine2, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* Mixed partial recovery must publish a reduced journal even though the
     * overall attempt is blocked: legacy code first deletes the mapped-file
     * quarantine, then retains only the still-failing original work. */
    update_test_prepare_swap(target, staged, backup,
                             "mixed-original-one", "replacement");
    update_test_prepare_swap(target2, staged2, backup2,
                             "mixed-original-two", "replacement-two");
    assert(update_journal_write(batch, 2, 0));
    assert(update_delete_file_if_present(target2));
    assert(CreateDirectoryA(target2, NULL));
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    update_test_write(target, "mixed-corrupt-one");
    g_update_test_hold_recovery_quarantine = 1;
    assert(!update_recover_journal());
    assert(InterlockedCompareExchange(&g_update_recovery_restart, 0, 0));
    update_test_expect_file(target, "mixed-original-one");
    assert(update_path_exists(quarantine, NULL));
    assert(update_path_exists(journal_path, NULL));
    {
        UpdateJournal mixed_journal;
        assert(update_journal_load(&mixed_journal));
        assert(!mixed_journal.committed && mixed_journal.count == 2);
        assert(mixed_journal.files[0].had_original);
        assert(strcmp(mixed_journal.files[0].path, "sample2.dll") == 0);
        assert(strcmp(mixed_journal.files[0].sha256,
                      UPDATE_UNKNOWN_SHA256) == 0);
        assert(!mixed_journal.files[1].had_original);
        assert(strcmp(mixed_journal.files[1].path,
                      "sample.dll.update-recovery") == 0);
    }
    assert(!update_test_legacy_applying_recover());
    assert(!update_path_exists(quarantine, NULL));
    assert(update_path_exists(journal_path, NULL));
    assert(RemoveDirectoryA(target2));
    update_test_write(target2, "mixed-untouched-two");
    assert(update_test_legacy_applying_recover());
    assert(!update_path_exists(journal_path, NULL));
    update_test_expect_file(target, "mixed-original-one");
    update_test_expect_file(target2, "mixed-untouched-two");

    /* A valid committed transaction is rehashed before cleanup, keeps the
     * replacement, and removes both its backup and journal. */
    update_test_prepare_swap(target, staged, backup,
                             "original-valid", "replacement");
    assert(update_journal_write(&entry, 1, 0));
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    assert(update_journal_write(&entry, 1, 1));
    assert(update_recover_journal());
    update_test_expect_file(target, "replacement");
    assert(!update_path_exists(backup, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* The committed preflight covers the whole set before cleanup. Corrupting
     * the second target rolls the first valid target back too, proving its
     * backup was not deleted early. */
    batch[0] = entry;
    memset(&batch[1], 0, sizeof(batch[1]));
    assert(update_copy(batch[1].spec.path, sizeof(batch[1].spec.path),
                       "sample2.dll"));
    assert(update_copy(batch[1].target, sizeof(batch[1].target), target2));
    assert(update_copy(batch[1].staged, sizeof(batch[1].staged), staged2));
    assert(update_copy(batch[1].backup, sizeof(batch[1].backup), backup2));
    batch[1].had_original = 1;
    update_test_set_expected(&batch[1], "replacement-two");
    update_test_prepare_swap(target, staged, backup,
                             "batch-original-one", "replacement");
    update_test_prepare_swap(target2, staged2, backup2,
                             "batch-original-two", "replacement-two");
    assert(update_journal_write(batch, 2, 0));
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(target2, backup2, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged2, target2, MOVEFILE_WRITE_THROUGH));
    assert(update_journal_write(batch, 2, 1));
    update_test_write(target2, "batch-corrupt");
    assert(update_recover_journal());
    update_test_expect_file(target, "batch-original-one");
    update_test_expect_file(target2, "batch-original-two");
    assert(!update_path_exists(backup, NULL));
    assert(!update_path_exists(backup2, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* A corrupt committed target rolls the complete transaction back while
     * its verified recovery artifact is still available. */
    update_test_prepare_swap(target, staged, backup,
                             "original-corrupt", "replacement");
    assert(update_journal_write(&entry, 1, 0));
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    assert(update_journal_write(&entry, 1, 1));
    update_test_write(target, "corrupt");
    assert(update_recover_journal());
    update_test_expect_file(target, "original-corrupt");
    assert(!update_path_exists(backup, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* A missing committed target is also restored from its .old backup. */
    update_test_prepare_swap(target, staged, backup,
                             "original-missing", "replacement");
    assert(update_journal_write(&entry, 1, 0));
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    assert(update_journal_write(&entry, 1, 1));
    assert(DeleteFileA(target));
    assert(update_recover_journal());
    update_test_expect_file(target, "original-missing");
    assert(!update_path_exists(backup, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* If a corrupt commit has also lost its required backup, recovery cannot
     * guess. It retains the journal and target for manual/next-run recovery. */
    update_test_prepare_swap(target, staged, backup,
                             "original-retain", "replacement");
    assert(update_journal_write(&entry, 1, 0));
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    assert(update_journal_write(&entry, 1, 1));
    update_test_write(target, "corrupt-retained");
    assert(DeleteFileA(backup));
    assert(!update_recover_journal());
    update_test_expect_file(target, "corrupt-retained");
    assert(update_path_exists(journal_path, NULL));
    assert(DeleteFileA(journal_path));

    /* A reduced V2 subset cannot be promoted just because its one remaining
     * target matches that release. The complete set is no longer represented. */
    {
        UpdateJournal source;
        UpdateJournal reduced_subset;
        update_test_prepare_swap(target2, staged2, backup2,
                                 "subset-original", "replacement-two");
        assert(MoveFileExA(target2, backup2, MOVEFILE_WRITE_THROUGH));
        assert(MoveFileExA(staged2, target2, MOVEFILE_WRITE_THROUGH));
        memset(&source, 0, sizeof(source));
        memset(&reduced_subset, 0, sizeof(reduced_subset));
        source.version = 2;
        source.count = 2;
        source.files[1].had_original = 1;
        source.files[1].has_integrity = 1;
        source.files[1].size = batch[1].spec.size;
        assert(update_copy(source.files[1].path,
                           sizeof(source.files[1].path), "sample2.dll"));
        assert(update_copy(source.files[1].sha256,
                           sizeof(source.files[1].sha256),
                           batch[1].spec.sha256));
        reduced_subset.version = 2;
        assert(update_journal_add_unresolved(&reduced_subset, &source, 1));
        assert(strcmp(reduced_subset.files[0].sha256,
                      UPDATE_UNKNOWN_SHA256) == 0);
        assert(update_journal_write_state(&reduced_subset, 0));
        assert(update_recover_journal());
        assert(InterlockedCompareExchange(&g_update_recovery_restart, 0, 0));
        update_test_expect_file(target2, "subset-original");
        assert(!update_path_exists(backup2, NULL));
        assert(!update_path_exists(journal_path, NULL));
    }

    /* A V1 entry upgraded into a reduced V2 journal carries a deliberately
     * non-authorizing digest. Even an empty target cannot pass roll-forward. */
    {
        UpdateJournal legacy_source;
        UpdateJournal upgraded;
        update_test_prepare_swap(target, staged, backup,
                                 "sentinel-original", "unused");
        assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
        update_test_write(target, "");
        memset(&legacy_source, 0, sizeof(legacy_source));
        memset(&upgraded, 0, sizeof(upgraded));
        legacy_source.version = 1;
        legacy_source.count = 1;
        legacy_source.files[0].had_original = 1;
        assert(update_copy(legacy_source.files[0].path,
                           sizeof(legacy_source.files[0].path),
                           "sample.dll"));
        upgraded.version = 2;
        assert(update_journal_add_unresolved(&upgraded, &legacy_source, 0));
        assert(strcmp(upgraded.files[0].sha256,
                      UPDATE_UNKNOWN_SHA256) == 0);
        assert(update_journal_write_state(&upgraded, 0));
        assert(update_recover_journal());
        assert(InterlockedCompareExchange(&g_update_recovery_restart, 0, 0));
        update_test_expect_file(target, "sentinel-original");
        assert(!update_path_exists(backup, NULL));
        assert(!update_path_exists(journal_path, NULL));
    }

    /* V1 applying journals remain recoverable. V1 committed journals lack an
     * expected digest, so they fail safely without deleting either artifact. */
    update_test_prepare_swap(target, staged, backup,
                             "legacy-original", "legacy-replacement");
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    update_test_write(journal_path,
                      UPDATE_JOURNAL_MAGIC_V1
                      "\nphase=applying\ncount=1\nfile=1:sample.dll\n");
    assert(update_recover_journal());
    update_test_expect_file(target, "legacy-original");
    assert(!update_path_exists(backup, NULL));
    assert(!update_path_exists(journal_path, NULL));

    update_test_prepare_swap(target, staged, backup,
                             "legacy-original", "legacy-replacement");
    assert(MoveFileExA(target, backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(staged, target, MOVEFILE_WRITE_THROUGH));
    update_test_write(journal_path,
                      UPDATE_JOURNAL_MAGIC_V1
                      "\nphase=committed\ncount=1\nfile=1:sample.dll\n");
    assert(!update_recover_journal());
    update_test_expect_file(target, "legacy-replacement");
    update_test_expect_file(backup, "legacy-original");
    assert(update_path_exists(journal_path, NULL));
    assert(DeleteFileA(journal_path));
    assert(DeleteFileA(backup));

    /* Atomic config editing preserves comments and unrelated settings. */
    assert(update_join_path(cfg, sizeof(cfg), root, UPDATE_CFG_REL));
    update_test_write(cfg, "# keep me\r\nother_setting=yes\r\nauto_update=0\r\n");
    assert(update_ext_config_set("auto_update", "1"));
    assert(update_ext_config_set("main_menu_mode", "online"));
    assert(!update_ext_config_set("bad key", "value"));
    assert(!update_ext_config_set("bad=key", "value"));
    assert(!update_ext_config_set("newline", "one\ntwo"));
    assert(!update_ext_config_set("newline", "one\rtwo"));
    memset(too_long_value, 'x', sizeof(too_long_value));
    too_long_value[sizeof(too_long_value) - 1] = '\0';
    assert(!update_ext_config_set("too_long", too_long_value));
    assert(!update_ext_config_set(NULL, "value"));
    assert(!update_ext_config_set("key", NULL));
    assert(update_cfg_get("auto_update", value, sizeof(value)));
    assert(strcmp(value, "1") == 0);
    assert(update_cfg_get("main_menu_mode", value, sizeof(value)));
    assert(strcmp(value, "online") == 0);
    {
        unsigned char* bytes = NULL;
        size_t len = 0;
        assert(update_read_file_bounded(cfg, UPDATE_CFG_MAX_BYTES, &bytes, &len));
        assert(strstr((char*)bytes, "# keep me\r\n") != NULL);
        assert(strstr((char*)bytes, "other_setting=yes\r\n") != NULL);
        free(bytes);
    }
    assert(update_remove_tree(root));
}

static void update_test_http_apply(const char* base) {
    char cwd[UPDATE_ABS_CAP];
    char root[UPDATE_ABS_CAP];
    char mods[UPDATE_ABS_CAP];
    char target[UPDATE_ABS_CAP];
    char url[UPDATE_URL_CAP];
    char error[UPDATE_STATUS_CAP];
    unsigned char* expected = NULL;
    unsigned char* installed = NULL;
    size_t expected_len = 0;
    size_t installed_len = 0;
    UpdateManifest manifest;
    DWORD cwd_len;
    if (!base || !*base) return;
    assert(update_build_download_url(base, "README.txt", url, sizeof(url)));
    assert(update_http_get(url, UPDATE_FILE_MAX_BYTES,
                           &expected, &expected_len, error, sizeof(error)));
    assert(expected_len > 0);
    cwd_len = GetCurrentDirectoryA(sizeof(cwd), cwd);
    assert(cwd_len > 0 && cwd_len < sizeof(cwd));
    assert(snprintf(root, sizeof(root), "%s\\build\\update_ext_http_tmp_%lu",
                    cwd, (unsigned long)GetCurrentProcessId()) > 0);
    (void)update_remove_tree(root);
    assert(update_create_directory(root));
    EnterCriticalSection(&g_update.lock);
    assert(update_copy(g_update.root, sizeof(g_update.root), root));
    LeaveCriticalSection(&g_update.lock);
    assert(update_join_path(mods, sizeof(mods), root, "mods"));
    assert(update_create_directory(mods));
    assert(update_join_path(target, sizeof(target), root, "README.txt"));
    update_test_write(target, "old");

    memset(&manifest, 0, sizeof(manifest));
    assert(update_copy(manifest.version, sizeof(manifest.version), "9.0"));
    assert(update_copy(manifest.base, sizeof(manifest.base), base));
    manifest.file_count = 1;
    assert(update_copy(manifest.files[0].path,
                       sizeof(manifest.files[0].path), "README.txt"));
    assert(update_sha256_hex(expected, expected_len,
                             manifest.files[0].sha256));
    manifest.files[0].size = expected_len;
    manifest.files[0].overwrite = 1;
    InterlockedExchange(&g_update_cancel, 0);
    assert(update_apply_manifest(&manifest, error, sizeof(error)));
    assert(update_read_file_bounded(target, UPDATE_FILE_MAX_BYTES,
                                    &installed, &installed_len));
    assert(installed_len == expected_len);
    assert(memcmp(installed, expected, expected_len) == 0);
    free(installed);
    free(expected);
    assert(update_recover_journal());
    assert(update_remove_tree(root));
}

int main(void) {
    char hex[65];
    const char* integration_base;
    assert(update_version_cmp("1.0", "1.0") == 0);
    assert(update_version_cmp("1.1", "1.0") > 0);
    assert(update_version_cmp("1.0", "1.1") < 0);
    assert(update_version_cmp("1.10", "1.9") > 0);
    assert(update_version_cmp("2.0", "1.99.99") > 0);
    assert(update_version_cmp("1.0.1", "1.0") > 0);
    assert(update_version_cmp("1.184467440737095516160",
                              "1.184467440737095516159") > 0);
    assert(update_version_cmp("1.0.0", "1") == 0);
    assert(update_check_status_allows_start(UPDATE_IDLE));
    assert(update_check_status_allows_start(UPDATE_UP_TO_DATE));
    assert(update_check_status_allows_start(UPDATE_ERROR));
    assert(!update_check_status_allows_start(UPDATE_CHECKING));
    assert(!update_check_status_allows_start(UPDATE_AVAILABLE));
    assert(!update_check_status_allows_start(UPDATE_APPLYING));
    assert(!update_check_status_allows_start(UPDATE_RESTART_PENDING));
    assert(update_sha256_hex("abc", 3, hex));
    assert(strcmp(hex,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    assert(update_sha256_hex(NULL, 0, hex));
    assert(strcmp(hex,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    update_test_json();
    update_test_storage();
    integration_base = getenv("UPDATE_EXT_TEST_CHANNEL_BASE");
    if (integration_base && *integration_base) {
        update_test_http_apply(integration_base);
    }
    printf("ALL OK\n");
    return 0;
}
#endif
