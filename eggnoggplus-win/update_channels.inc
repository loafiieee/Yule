/* Private channel/cache helpers, included after the updater's verified I/O.
 * The active profile is installed LAST by the same V3 transaction as its DLLs.
 * YuleUpdater stays shared: it never imports a replaceable channel runtime. */

static int update_manifest_encode(const UpdateManifest* m, char* out, size_t cap) {
    size_t used = 0, i;
    int n;
    /* All emitted strings are numeric versions, safe relative paths, hashes,
     * hostnames, fixed channel names, or URLs. Escape URL quote/backslash below. */
    n = snprintf(out, cap,
        "{\"channel_version\":1,\"version\":\"%s\",\"release_channel\":\"%s\","
        "\"channel_switch\":1,\"base\":\"", m->version, m->release_channel);
    if (n < 0 || (size_t)n >= cap) return 0;
    used = (size_t)n;
    for (i = 0; m->base[i]; i++) {
        unsigned char c = (unsigned char)m->base[i];
        if (c < 32 || used + 3 >= cap) return 0;
        if (c == '"' || c == '\\') out[used++] = '\\';
        out[used++] = (char)c;
    }
    n = snprintf(out + used, cap - used, "\"");
    if (n < 0 || (size_t)n >= cap - used) return 0;
    used += (size_t)n;
    if (m->server_host[0]) {
        n = snprintf(out + used, cap - used,
            ",\"server_host\":\"%s\",\"server_port\":%u,\"server_tls\":%s",
            m->server_host, (unsigned)m->server_port, m->server_tls ? "true" : "false");
        if (n < 0 || (size_t)n >= cap - used) return 0;
        used += (size_t)n;
    }
    n = snprintf(out + used, cap - used, ",\"files\":[");
    if (n < 0 || (size_t)n >= cap - used) return 0;
    used += (size_t)n;
    for (i = 0; i < m->file_count; i++) {
        n = snprintf(out + used, cap - used,
            "%s{\"path\":\"%s\",\"sha256\":\"%s\",\"size\":%llu,\"overwrite\":true}",
            i ? "," : "", m->files[i].path, m->files[i].sha256,
            (unsigned long long)m->files[i].size);
        if (n < 0 || (size_t)n >= cap - used) return 0;
        used += (size_t)n;
    }
    n = snprintf(out + used, cap - used, "]}\n");
    return n >= 0 && (size_t)n < cap - used;
}

static int update_channel_name_valid(const char* name) {
    return name && (strcmp(name, "stable") == 0 || strcmp(name, "beta") == 0);
}

static int update_cache_root(const char* name, char out[UPDATE_ABS_CAP]) {
    char relative[UPDATE_MAX_PATH + 1];
    char probe[UPDATE_ABS_CAP];
    char* slash;
    if (!update_channel_name_valid(name)) return 0;
    snprintf(relative, sizeof(relative), UPDATE_CACHE_REL "/%s/manifest.json", name);
    if (!update_prepare_relative_path(g_update.root, relative, probe, sizeof(probe), 1)) return 0;
    slash = strrchr(probe, '\\');
    if (!slash) return 0;
    *slash = '\0';
    return update_copy(out, UPDATE_ABS_CAP, probe);
}

static int update_read_manifest_path(const char* path, UpdateManifest* m) {
    unsigned char* body = NULL;
    size_t length = 0;
    int ok = update_read_file_bounded(path, UPDATE_PROFILE_CAP, &body, &length) &&
             length && update_manifest_parse((const char*)body, m);
    free(body);
    return ok;
}

static int update_manifest_files_match(const UpdateManifest* m, const char* root) {
    size_t i;
    for (i = 0; i < m->file_count; i++) {
        char file[UPDATE_ABS_CAP];
        if (!update_prepare_relative_path(root, m->files[i].path, file, sizeof(file), 0) ||
            !update_file_matches(file, &m->files[i])) return 0;
    }
    return 1;
}

static int update_blob_has(const unsigned char* bytes, size_t len, const char* text) {
    size_t i, n = strlen(text);
    for (i = 0; i + n < len; i++) if (memcmp(bytes + i, text, n) == 0 && bytes[i + n] == 0) return 1;
    return 0;
}

static int update_channel_runtime_valid(const UpdateManifest* m, const char* root) {
    size_t i;
    for (i = 0; i < m->file_count; i++) {
        if (_stricmp(m->files[i].path, "SDL2.dll") == 0) {
            char path[UPDATE_ABS_CAP], version_marker[96];
            unsigned char* bytes = NULL;
            size_t len = 0;
            int ok;
            snprintf(version_marker, sizeof(version_marker), "YULE_FRAMEWORK_VERSION=%s", m->version);
            if (!update_prepare_relative_path(root, "SDL2.dll", path, sizeof(path), 0) ||
                !update_file_matches(path, &m->files[i]) ||
                !update_read_file_bounded(path, UPDATE_FILE_MAX_BYTES, &bytes, &len)) return 0;
            ok = update_blob_has(bytes, len, "YULE_CHANNEL_SWITCH=1") &&
                 update_blob_has(bytes, len, version_marker);
            free(bytes);
            return ok;
        }
    }
    return 0;
}

static void update_load_active(void) {
    char path[UPDATE_ABS_CAP];
    DWORD attrs;
    if (!update_prepare_relative_path(g_update.root, UPDATE_ACTIVE_REL, path, sizeof(path), 0)) {
        /* A new install need not have mods/ yet. */
        char mods[UPDATE_ABS_CAP];
        if (update_join_path(mods, sizeof(mods), g_update.root, "mods") &&
            GetFileAttributesA(mods) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) return;
        g_update.active_invalid = 1;
        return;
    }
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) return;
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !update_read_manifest_path(path, &g_update.active) ||
        !g_update.active.channel_switch ||
        strcmp(g_update.active.version, FRAMEWORK_VERSION) != 0 ||
        !update_channel_runtime_valid(&g_update.active, g_update.root)) {
        memset(&g_update.active, 0, sizeof(g_update.active));
        g_update.active_invalid = 1;
    }
}

static INIT_ONCE g_channel_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK update_channel_init_once(PINIT_ONCE once, PVOID param, PVOID* context) {
    (void)once; (void)param; (void)context;
    update_load_active();
    return TRUE;
}

static void update_ensure_channel(void) {
    /* Config reads during hooks_init must remain safe under the loader lock.
     * Runtime hashing is deferred to the menu/updater service, outside it. */
    update_ensure_init();
    InitOnceExecuteOnce(&g_channel_once, update_channel_init_once, NULL, NULL);
}

const char* update_ext_channel(void) {
    update_ensure_channel();
    if (g_update.active_invalid) return "invalid";
    return g_update.active.release_channel[0] ? g_update.active.release_channel : "stable";
}

int update_ext_beta_unlocked(void) {
    char value[8];
    return strcmp(update_ext_channel(), "beta") == 0 ||
        (update_ext_config_get("beta_unlocked", value, sizeof(value)) && strcmp(value, "1") == 0);
}

int update_ext_channel_server(char* host, size_t cap, uint16_t* port, int* tls) {
    update_ensure_channel();
    if (g_update.active_invalid) return -1;
    if (!g_update.active.server_host[0]) return 0;
    if (!host || !port || !tls || !update_copy(host, cap, g_update.active.server_host)) return -1;
    *port = g_update.active.server_port;
    *tls = g_update.active.server_tls;
    return 1;
}

static int update_cache_load(const char* name, UpdateManifest* m) {
    char root[UPDATE_ABS_CAP], path[UPDATE_ABS_CAP];
    return update_cache_root(name, root) &&
        update_prepare_relative_path(root, "manifest.json", path, sizeof(path), 0) &&
        update_read_manifest_path(path, m) && m->channel_switch &&
        strcmp(m->release_channel, name) == 0 &&
        update_manifest_files_match(m, root) && update_channel_runtime_valid(m, root);
}

static int update_copy_verified(const char* source, const char* destination,
                                const UpdateFileSpec* spec) {
    unsigned char* bytes = NULL;
    size_t len = 0;
    DWORD attrs = GetFileAttributesA(destination);
    int ok;
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return 0;
    if (update_file_matches(destination, spec)) return 1;
    if (!update_file_matches(source, spec) ||
        !update_read_file_bounded(source, UPDATE_FILE_MAX_BYTES, &bytes, &len)) return 0;
    ok = update_write_bytes_atomic(destination, bytes, len) && update_file_matches(destination, spec);
    free(bytes);
    return ok;
}

static int update_cache_publish(const UpdateManifest* m, const char* source_root) {
    char root[UPDATE_ABS_CAP], path[UPDATE_ABS_CAP], json[UPDATE_PROFILE_CAP];
    size_t i;
    UpdateManifest validated;
    if (!update_cache_root(m->release_channel, root) || !update_manifest_encode(m, json, sizeof(json)) ||
        !update_manifest_parse(json, &validated)) return 0;
    for (i = 0; i < m->file_count; i++) {
        char source[UPDATE_ABS_CAP], dest[UPDATE_ABS_CAP];
        if (!update_prepare_relative_path(source_root, m->files[i].path, source, sizeof(source), 0) ||
            !update_prepare_relative_path(root, m->files[i].path, dest, sizeof(dest), 1) ||
            !update_copy_verified(source, dest, &m->files[i])) return 0;
    }
    if (!update_channel_runtime_valid(m, root) ||
        !update_prepare_relative_path(root, "manifest.json", path, sizeof(path), 0)) return 0;
    /* Publish only once the complete cache verifies. An interrupted copy can
     * invalidate that channel's cache, but can never activate a partial set. */
    return update_write_bytes_atomic(path, json, strlen(json));
}

static int update_snapshot_add(UpdateManifest* m, const char* relative) {
    UpdateFileSpec* spec;
    char path[UPDATE_ABS_CAP];
    DWORD attrs;
    size_t i;
    for (i = 0; i < m->file_count; i++) if (_stricmp(m->files[i].path, relative) == 0) return 1;
    if (!update_path_is_safe(relative) || !update_join_path(path, sizeof(path), g_update.root, relative)) return 0;
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES &&
        (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) return 1;
    if (!update_prepare_relative_path(g_update.root, relative, path, sizeof(path), 0)) return 0;
    if (m->file_count >= UPDATE_MAX_FILES - 1) return 0;
    spec = &m->files[m->file_count];
    if (!update_copy(spec->path, sizeof(spec->path), relative) ||
        !update_capture_regular_file_integrity(path, &spec->size, spec->sha256) ||
        spec->size > UPDATE_FILE_MAX_BYTES) return 0;
    spec->overwrite = 1;
    m->file_count++;
    return 1;
}

static int update_snapshot_current(const UpdateManifest* destination) {
    static const char* const runtime[] = {
        "SDL2.dll", "lua51.dll", "libgcc_s_dw2-1.dll", "libwinpthread-1.dll",
        "SDL2_mixer.dll", "EOSSDK-Win32-Shipping.dll", "eos_client_secret.txt"
    };
    UpdateManifest m = g_update.active;
    size_t i;
    m.file_count = 0;
    m.channel_switch = 1;
    update_copy_trunc(m.release_channel, sizeof(m.release_channel), update_ext_channel());
    update_copy_trunc(m.version, sizeof(m.version), FRAMEWORK_VERSION);
    if (!m.base[0]) update_copy_trunc(m.base, sizeof(m.base), "https://loafiieee.com/yule/releases/");
    for (i = 0; i < sizeof(runtime) / sizeof(runtime[0]); i++) if (!update_snapshot_add(&m, runtime[i])) return 0;
    for (i = 0; i < g_update.active.file_count; i++) if (!update_snapshot_add(&m, g_update.active.files[i].path)) return 0;
    /* Preserve every existing file the destination will replace, including
     * future support DLLs. Player configs are excluded by manifest validation. */
    for (i = 0; i < destination->file_count; i++) if (!update_snapshot_add(&m, destination->files[i].path)) return 0;
    return update_cache_publish(&m, g_update.root);
}

static int update_stage_channel_file(const UpdateManifest* m, const UpdateFileSpec* spec,
                                     const char* staging_root, char* staged, size_t cap,
                                     char* error, size_t error_cap) {
    char cache[UPDATE_ABS_CAP], cached_file[UPDATE_ABS_CAP];
    if (!update_prepare_relative_path(staging_root, spec->path, staged, cap, 1)) return 0;
    if (update_cache_root(m->release_channel, cache) &&
        update_prepare_relative_path(cache, spec->path, cached_file, sizeof(cached_file), 0) &&
        update_copy_verified(cached_file, staged, spec)) return 1;
    return update_download_verified(m, spec, staging_root, staged, cap, error, error_cap);
}
