#pragma once

#include <stddef.h>
#include <stdint.h>

/* This is the version of the injected framework, not the base game.  Release
 * manifests use dot-separated numeric versions (for example 1.12.3). */
#define FRAMEWORK_VERSION "1.0"

#define UPDATE_MAX_FILES 32
#define UPDATE_MAX_PATH  240

typedef enum UpdateStatus {
    UPDATE_IDLE = 0,
    UPDATE_CHECKING,
    UPDATE_UP_TO_DATE,
    UPDATE_AVAILABLE,
    UPDATE_APPLYING,
    UPDATE_RESTART_PENDING,
    UPDATE_ERROR
} UpdateStatus;

typedef struct UpdateFileSpec {
    char path[UPDATE_MAX_PATH + 1];
    char sha256[65];
    uint64_t size;
    int overwrite;
} UpdateFileSpec;

/* Starts a non-blocking channel check.  Safe to call more than once. */
void update_ext_boot(void);

/* Manually retries the non-blocking channel check after an idle, completed, or
 * failed check.  It is a no-op while a check/apply is already running, while
 * an update is available, or after an update has been installed. */
void update_ext_begin_check(void);

/* Optional orderly shutdown hook.  Call this from normal application code,
 * never while the Windows loader lock is held (for example, not from DllMain).
 * A process exit is also safe: the on-disk transaction journal is recovered on
 * the next boot if the process exits during a swap. */
void update_ext_shutdown(void);

UpdateStatus update_ext_status(void);
const char* update_ext_latest_version(void);
const char* update_ext_status_line(void);

int update_ext_auto(void);
void update_ext_set_auto(int enabled);
void update_ext_begin_apply(void);

/* Atomically updates one installation-rooted modframework.cfg key while
 * preserving unrelated lines/comments. Keys are 1..63 ASCII bytes limited to
 * letters, digits, '.', '_', and '-'; values may be empty but may not contain
 * CR/LF and are capped at 4096 bytes. Cross-thread and cross-process writes are
 * serialized. Returns 1 on persisted success, 0 on failure. This does not
 * update a caller subsystem's live in-memory state. */
int update_ext_config_set(const char* key, const char* value);

int update_ext_notice_active(void);
void update_ext_dismiss_notice(void);

/* Small, dependency-light helpers kept public for the standalone regression
 * test and release tooling. */
int update_version_cmp(const char* a, const char* b);
int update_sha256_hex(const void* data, size_t len, char out_hex[65]);
int update_json_get_string(const char* json, const char* key,
                           char* out, size_t cap);
int update_json_scan_files(const char* json, UpdateFileSpec* files,
                           size_t file_cap, size_t* out_count);
