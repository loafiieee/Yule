#pragma once

#include <stddef.h>
#include <stdint.h>

/* This is the version of the injected framework, not the base game.  Release
 * manifests use dot-separated numeric versions (for example 1.12.3). */
#define FRAMEWORK_VERSION "1.91"

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

typedef enum UpdateHelperResult {
    UPDATE_HELPER_BLOCKED = 0,
    UPDATE_HELPER_READY = 1,
    UPDATE_HELPER_UPDATED = 2,
    UPDATE_HELPER_ROLLED_BACK = 3
} UpdateHelperResult;

/* Starts a non-blocking channel check.  Safe to call more than once. */
void update_ext_boot(void);

/* Manually retries the non-blocking channel check after an idle, completed, or
 * failed check.  It is a no-op while a check/apply is already running, while
 * an update is available, or after an update has been installed. */
void update_ext_begin_check(void);

/* Optional orderly shutdown hook.  Call this from normal application code,
 * never while the Windows loader lock is held (for example, not from DllMain).
 * A process exit is also safe: verified staging remains journaled for the
 * one-shot updater helper, which waits for this game process to exit before
 * applying or recovering it and relaunching the game. */
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

/* Reads one config value under the same serialization as config writes.
 * Returns 1 when the key exists and fits in out, otherwise 0. */
int update_ext_config_get(const char* key, char* out, size_t cap);

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

/* One-shot updater helper entry point. It applies a pristine staged V3
 * transaction or recovers any interrupted V1/V2/V3 transaction under the
 * installation mutex. The module containing this function must live in the
 * installation root. It performs no network access and never launches a
 * process. */
UpdateHelperResult update_ext_helper_service(char* status, size_t status_cap);

#ifdef UPDATE_EXT_HELPER_TEST
int update_ext_helper_set_root_for_test(const char* root);
#endif
