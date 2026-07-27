#define UPDATE_EXT_HELPER
#define UPDATE_EXT_HELPER_TEST
#include "../update_ext.c"

#include <assert.h>

static void helper_write(const char* path, const char* text) {
    assert(update_write_bytes_atomic(path, text, strlen(text)));
}

static void helper_expect(const char* path, const char* text) {
    unsigned char* bytes = NULL;
    size_t len = 0u;
    assert(update_read_file_bounded(path, 4096u, &bytes, &len));
    assert(len == strlen(text));
    assert(memcmp(bytes, text, len) == 0);
    free(bytes);
}

static void helper_remove_file(const char* path) {
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        assert(update_remove_tree(path));
    } else {
        assert(DeleteFileA(path));
    }
}

static void helper_entry(UpdateApplyEntry* entry,
                         const char* root,
                         const char* relative,
                         const char* original,
                         const char* replacement) {
    char staging_root[UPDATE_ABS_CAP];
    memset(entry, 0, sizeof(*entry));
    assert(update_copy(entry->spec.path, sizeof(entry->spec.path), relative));
    entry->spec.size = strlen(replacement);
    entry->spec.overwrite = 1;
    assert(update_sha256_hex(replacement, strlen(replacement),
                             entry->spec.sha256));
    assert(update_prepare_relative_path(root, relative,
                                        entry->target,
                                        sizeof(entry->target), 1));
    assert(update_join_path(staging_root, sizeof(staging_root), root,
                            UPDATE_STAGING_REL));
    assert(update_create_directory(staging_root));
    assert(update_prepare_relative_path(staging_root, relative,
                                        entry->staged,
                                        sizeof(entry->staged), 1));
    assert(update_backup_path(entry->backup, sizeof(entry->backup),
                              entry->target));
    assert(update_recovery_quarantine_path(entry->quarantine,
                                           entry->target));
    helper_remove_file(entry->target);
    helper_remove_file(entry->staged);
    helper_remove_file(entry->backup);
    helper_remove_file(entry->quarantine);
    if (original) {
        helper_write(entry->target, original);
        entry->had_original = 1;
        entry->original_size = strlen(original);
        assert(update_sha256_hex(original, strlen(original),
                                 entry->original_sha256));
    } else {
        entry->had_original = 0;
        entry->original_size = 0u;
        assert(update_copy(entry->original_sha256,
                           sizeof(entry->original_sha256),
                           UPDATE_UNKNOWN_SHA256));
    }
    entry->has_original_integrity = 1;
    helper_write(entry->staged, replacement);
}

static void helper_reset_root(const char* root) {
    char mods[UPDATE_ABS_CAP];
    char staging[UPDATE_ABS_CAP];
    assert(update_remove_tree(root));
    assert(update_create_directory(root));
    assert(update_ext_helper_set_root_for_test(root));
    assert(update_join_path(mods, sizeof(mods), root, "mods"));
    assert(update_create_directory(mods));
    assert(update_join_path(staging, sizeof(staging), root,
                            UPDATE_STAGING_REL));
    assert(update_create_directory(staging));
    InterlockedExchange(&g_update_recovery_restart, 0);
    InterlockedExchange(&g_update_helper_pending, 0);
}

int main(void) {
    char cwd[UPDATE_ABS_CAP];
    char root[UPDATE_ABS_CAP];
    char journal_path[UPDATE_ABS_CAP];
    char status[UPDATE_STATUS_CAP];
    UpdateApplyEntry one;
    UpdateApplyEntry batch[2];
    DWORD cwd_len = GetCurrentDirectoryA(sizeof(cwd), cwd);
    assert(cwd_len > 0u && cwd_len < sizeof(cwd));
    assert(snprintf(root, sizeof(root),
                    "%s\\build\\update_helper_test_tmp_%lu",
                    cwd, (unsigned long)GetCurrentProcessId()) > 0);

    /* Pristine V3 staging is reverified, applied, committed, and cleaned before
     * any importing process exists. */
    helper_reset_root(root);
    helper_entry(&one, root, "sample.dll", "original", "replacement");
    assert(update_journal_write(&one, 1u, 0));
    assert(update_ext_helper_service(status, sizeof(status)) ==
           UPDATE_HELPER_UPDATED);
    helper_expect(one.target, "replacement");
    assert(!update_path_exists(one.backup, NULL));
    assert(!update_path_exists(one.staged, NULL));
    assert(update_journal_path(journal_path));
    assert(!update_path_exists(journal_path, NULL));

    /* The V3 original digest closes the stage-to-restart TOCTOU window. */
    helper_reset_root(root);
    helper_entry(&one, root, "sample.dll", "original", "replacement");
    assert(update_journal_write(&one, 1u, 0));
    helper_write(one.target, "externally-changed");
    assert(update_ext_helper_service(status, sizeof(status)) ==
           UPDATE_HELPER_BLOCKED);
    helper_expect(one.target, "externally-changed");
    helper_expect(one.staged, "replacement");
    assert(update_path_exists(journal_path, NULL));

    /* Power cut after target -> .old rolls the original back. */
    helper_reset_root(root);
    helper_entry(&one, root, "sample.dll", "original", "replacement");
    assert(update_journal_write(&one, 1u, 0));
    assert(MoveFileExA(one.target, one.backup, MOVEFILE_WRITE_THROUGH));
    assert(update_ext_helper_service(status, sizeof(status)) ==
           UPDATE_HELPER_ROLLED_BACK);
    helper_expect(one.target, "original");
    assert(!update_path_exists(one.backup, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* Partial multi-file install is whole-transaction rollback. */
    helper_reset_root(root);
    helper_entry(&batch[0], root, "one.dll",
                 "original-one", "replacement-one");
    helper_entry(&batch[1], root, "bin/two.dll",
                 "original-two", "replacement-two");
    assert(update_journal_write(batch, 2u, 0));
    assert(MoveFileExA(batch[0].target, batch[0].backup,
                       MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(batch[0].staged, batch[0].target,
                       MOVEFILE_WRITE_THROUGH));
    assert(update_ext_helper_service(status, sizeof(status)) ==
           UPDATE_HELPER_ROLLED_BACK);
    helper_expect(batch[0].target, "original-one");
    helper_expect(batch[1].target, "original-two");

    /* Power cut after the final target move but before commit completes
     * forward, preserving the verified release. */
    helper_reset_root(root);
    helper_entry(&one, root, "sample.dll", "original", "replacement");
    assert(update_journal_write(&one, 1u, 0));
    assert(MoveFileExA(one.target, one.backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(one.staged, one.target, MOVEFILE_WRITE_THROUGH));
    assert(update_ext_helper_service(status, sizeof(status)) ==
           UPDATE_HELPER_UPDATED);
    helper_expect(one.target, "replacement");
    assert(!update_path_exists(one.backup, NULL));
    assert(!update_path_exists(journal_path, NULL));

    /* A durable commit is still rehashed: an exact target completes forward,
     * while a corrupt committed target rolls the complete transaction back. */
    helper_reset_root(root);
    helper_entry(&one, root, "sample.dll", "original", "replacement");
    assert(MoveFileExA(one.target, one.backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(one.staged, one.target, MOVEFILE_WRITE_THROUGH));
    assert(update_journal_write(&one, 1u, 1));
    assert(update_ext_helper_service(status, sizeof(status)) ==
           UPDATE_HELPER_UPDATED);
    helper_expect(one.target, "replacement");

    helper_reset_root(root);
    helper_entry(&one, root, "sample.dll", "original", "replacement");
    assert(MoveFileExA(one.target, one.backup, MOVEFILE_WRITE_THROUGH));
    assert(MoveFileExA(one.staged, one.target, MOVEFILE_WRITE_THROUGH));
    assert(update_journal_write(&one, 1u, 1));
    helper_write(one.target, "corrupt");
    assert(update_ext_helper_service(status, sizeof(status)) ==
           UPDATE_HELPER_ROLLED_BACK);
    helper_expect(one.target, "original");

    /* A newly introduced target has an explicit absence precondition. */
    helper_reset_root(root);
    helper_entry(&one, root, "new.dll", NULL, "new-file");
    assert(update_journal_write(&one, 1u, 0));
    assert(update_ext_helper_service(status, sizeof(status)) ==
           UPDATE_HELPER_UPDATED);
    helper_expect(one.target, "new-file");

    /* Missing/corrupt staging with no swap evidence is blocked, not guessed
     * through or silently discarded. */
    helper_reset_root(root);
    helper_entry(&one, root, "sample.dll", "original", "replacement");
    assert(update_journal_write(&one, 1u, 0));
    helper_write(one.staged, "corrupt");
    assert(update_ext_helper_service(status, sizeof(status)) ==
           UPDATE_HELPER_BLOCKED);
    helper_expect(one.target, "original");
    assert(update_path_exists(journal_path, NULL));

    /* Corrupt journal and an unsafe journal-as-directory both fail closed. */
    helper_reset_root(root);
    assert(update_journal_path(journal_path));
    helper_write(journal_path, "not a journal");
    assert(update_ext_helper_service(status, sizeof(status)) ==
           UPDATE_HELPER_BLOCKED);
    helper_reset_root(root);
    assert(update_journal_path(journal_path));
    assert(CreateDirectoryA(journal_path, NULL));
    assert(update_ext_helper_service(status, sizeof(status)) ==
           UPDATE_HELPER_BLOCKED);

    /* With no journal, a closed-game missing target can safely consume the
     * legacy adjacent backup before launch. */
    helper_reset_root(root);
    helper_entry(&one, root, "SDL2.dll", "old-framework", "unused");
    helper_remove_file(one.staged);
    assert(MoveFileExA(one.target, one.backup, MOVEFILE_WRITE_THROUGH));
    assert(update_ext_helper_service(status, sizeof(status)) ==
           UPDATE_HELPER_READY);
    helper_expect(one.target, "old-framework");
    assert(!update_path_exists(one.backup, NULL));

    assert(update_remove_tree(root));
    printf("update helper tests: ALL OK\n");
    return 0;
}
