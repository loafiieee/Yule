/* Runs inside UPDATE_EXT_TEST with disposable roots, never the installed DLL. */
static void update_test_channels(void) {
    static const char stable_bytes[] = "YULE_CHANNEL_SWITCH=1\0YULE_FRAMEWORK_VERSION=" FRAMEWORK_VERSION "\0stable";
    static const char beta_bytes[] = "YULE_CHANNEL_SWITCH=1\0YULE_FRAMEWORK_VERSION=" FRAMEWORK_VERSION "\0beta";
    static const char older_bytes[] = "YULE_CHANNEL_SWITCH=1\0YULE_FRAMEWORK_VERSION=1.0\0older stable";
    char cwd[UPDATE_ABS_CAP], root[UPDATE_ABS_CAP], cache[UPDATE_ABS_CAP], path[UPDATE_ABS_CAP], relative[96];
    char error[UPDATE_STATUS_CAP], json[UPDATE_PROFILE_CAP];
    UpdateManifest beta, stable, loaded;
    UpdateFileSpec* spec;
    uint16_t port;
    int tls;
    assert(GetCurrentDirectoryA(sizeof(cwd), cwd));
    snprintf(relative, sizeof(relative), "build\\update_ext_test_tmp_channels_%lu", (unsigned long)GetCurrentProcessId());
    assert(update_join_path(root, sizeof(root), cwd, relative));
    (void)update_remove_tree(root);
    assert(update_create_directory(root));
    update_copy_trunc(g_update.root, sizeof(g_update.root), root);
    memset(&g_update.active, 0, sizeof(g_update.active));
    g_update.active_invalid = 0;
    InterlockedExchange(&g_update_cancel, 0);
    InterlockedExchange(&g_update_recovery_restart, 0);
    InterlockedExchange(&g_update_helper_pending, 0);
    assert(update_prepare_relative_path(root, "SDL2.dll", path, sizeof(path), 1));
    assert(update_write_bytes_atomic(path, stable_bytes, sizeof(stable_bytes)));
    assert(update_prepare_relative_path(root, "lua51.dll", path, sizeof(path), 1));
    update_test_write(path, "stable Lua runtime");

    memset(&beta, 0, sizeof(beta));
    update_copy_trunc(beta.version, sizeof(beta.version), FRAMEWORK_VERSION);
    update_copy_trunc(beta.base, sizeof(beta.base), "https://unreachable.invalid/");
    update_copy_trunc(beta.release_channel, sizeof(beta.release_channel), "beta");
    beta.channel_switch = 1;
    update_copy_trunc(beta.server_host, sizeof(beta.server_host), "beta.loafiieee.com");
    beta.server_port = 47782;
    beta.server_tls = 1;
    beta.file_count = 2;
    spec = &beta.files[0];
    update_copy_trunc(spec->path, sizeof(spec->path), "SDL2.dll");
    spec->size = sizeof(beta_bytes); spec->overwrite = 1;
    assert(update_sha256_hex(beta_bytes, sizeof(beta_bytes), spec->sha256));
    spec = &beta.files[1];
    update_copy_trunc(spec->path, sizeof(spec->path), "lua51.dll");
    spec->size = strlen("beta Lua runtime"); spec->overwrite = 1;
    assert(update_sha256_hex("beta Lua runtime", (size_t)spec->size, spec->sha256));
    assert(update_manifest_encode(&beta, json, sizeof(json)));
    assert(update_manifest_parse(json, &loaded));
    loaded.server_tls = 0;
    assert(update_manifest_encode(&loaded, json, sizeof(json)));
    assert(!update_manifest_parse(json, &loaded));
    loaded = beta;
    update_copy_trunc(loaded.server_host, sizeof(loaded.server_host), "eggnogg.loafiieee.com");
    loaded.server_port = 47778;
    assert(update_manifest_encode(&loaded, json, sizeof(json)));
    assert(!update_manifest_parse(json, &loaded));
    loaded = beta;
    update_copy_trunc(loaded.files[1].path, sizeof(loaded.files[1].path), UPDATE_ACTIVE_REL);
    assert(update_manifest_encode(&loaded, json, sizeof(json)));
    assert(!update_manifest_parse(json, &loaded));

    // A complete cached beta set can switch with an unreachable download base.
    assert(update_cache_root("beta", cache));
    assert(update_prepare_relative_path(cache, "SDL2.dll", path, sizeof(path), 1));
    assert(update_write_bytes_atomic(path, beta_bytes, sizeof(beta_bytes)));
    assert(update_prepare_relative_path(cache, "lua51.dll", path, sizeof(path), 1));
    update_test_write(path, "beta Lua runtime");
    assert(update_cache_publish(&beta, cache));
    assert(update_cache_load("beta", &loaded));
    assert(!update_cache_load("stable", &loaded));
    assert(update_apply_manifest(&beta, error, sizeof(error)));
    assert(update_manifest_files_match(&beta, root));
    assert(update_cache_load("stable", &stable));
    assert(update_recover_journal());
    update_load_active();
    assert(!g_update.active_invalid && strcmp(update_ext_channel(), "beta") == 0);
    assert(update_ext_channel_server(path, sizeof(path), &port, &tls) == 1);
    assert(strcmp(path, "beta.loafiieee.com") == 0 && port == 47782 && tls == 1);
    assert(update_ext_beta_unlocked());

    // Stable return and another beta switch both use their verified cache.
    assert(update_apply_manifest(&stable, error, sizeof(error)));
    assert(update_recover_journal());
    update_load_active();
    assert(!g_update.active_invalid && strcmp(update_ext_channel(), "stable") == 0);
    assert(update_ext_channel_server(path, sizeof(path), &port, &tls) == 0);
    assert(update_manifest_files_match(&stable, root));
    update_copy_trunc(g_update.switch_target, sizeof(g_update.switch_target), "beta");
    update_run_switch();
    assert(update_ext_status() == UPDATE_RESTART_PENDING);
    assert(update_recover_journal());
    update_load_active();
    assert(strcmp(update_ext_channel(), "beta") == 0);

    // A lower stable version is a valid explicit switch, never an update loop.
    update_copy_trunc(stable.version, sizeof(stable.version), "1.0");
    stable.files[0].size = sizeof(older_bytes);
    assert(update_sha256_hex(older_bytes, sizeof(older_bytes), stable.files[0].sha256));
    assert(update_cache_root("stable", cache));
    assert(update_prepare_relative_path(cache, "SDL2.dll", path, sizeof(path), 0));
    assert(update_write_bytes_atomic(path, older_bytes, sizeof(older_bytes)));
    assert(update_cache_publish(&stable, cache));
    update_copy_trunc(g_update.switch_target, sizeof(g_update.switch_target), "stable");
    update_run_switch();
    assert(update_ext_status() == UPDATE_RESTART_PENDING);
    assert(update_manifest_files_match(&stable, root));
    assert(update_recover_journal());
    assert(update_prepare_relative_path(root, UPDATE_ACTIVE_REL, path, sizeof(path), 0));
    assert(update_read_manifest_path(path, &loaded));
    assert(strcmp(loaded.version, "1.0") == 0 && strcmp(loaded.release_channel, "stable") == 0);

    // Corrupt/torn caches cannot activate; corrupt profile fails online closed.
    assert(update_prepare_relative_path(cache, "SDL2.dll", path, sizeof(path), 0));
    update_test_write(path, "broken download");
    assert(!update_cache_load("stable", &loaded));
    assert(!update_cache_root("../escape", cache));
    assert(update_prepare_relative_path(root, UPDATE_ACTIVE_REL, path, sizeof(path), 0));
    update_test_write(path, "broken channel state");
    update_load_active();
    assert(strcmp(update_ext_channel(), "invalid") == 0);
    assert(update_ext_channel_server(path, sizeof(path), &port, &tls) == -1);
    assert(!update_ext_switch_channel("beta"));
    assert(update_remove_tree(root));
    memset(&g_update.active, 0, sizeof(g_update.active));
    g_update.active_invalid = 0;
    puts("channel cache, equal-version swaps, downgrade, profile isolation and corruption: OK");
}

static void update_test_beta_http(const char* base) {
    static const char current[] = "YULE_CHANNEL_SWITCH=1\0YULE_FRAMEWORK_VERSION=" FRAMEWORK_VERSION "\0stable fixture";
    char cwd[UPDATE_ABS_CAP], root[UPDATE_ABS_CAP], path[UPDATE_ABS_CAP], relative[96], url[UPDATE_URL_CAP];
    UpdateManifest installed, cached;
    HANDLE worker;
    assert(GetCurrentDirectoryA(sizeof(cwd), cwd));
    snprintf(relative, sizeof(relative), "build\\update_ext_http_tmp_beta_%lu", (unsigned long)GetCurrentProcessId());
    assert(update_join_path(root, sizeof(root), cwd, relative));
    (void)update_remove_tree(root);
    assert(update_create_directory(root));
    update_copy_trunc(g_update.root, sizeof(g_update.root), root);
    memset(&g_update.active, 0, sizeof(g_update.active));
    g_update.active_invalid = 0;
    InterlockedExchange(&g_update_cancel, 0);
    InterlockedExchange(&g_update_recovery_restart, 0);
    InterlockedExchange(&g_update_helper_pending, 0);
    InterlockedExchange(&g_update_status, UPDATE_IDLE);
    assert(update_prepare_relative_path(root, "SDL2.dll", path, sizeof(path), 1));
    assert(update_write_bytes_atomic(path, current, sizeof(current)));
    assert(update_build_download_url(base, "beta.json", url, sizeof(url)));
    assert(update_ext_config_set("update_beta_url", url));
    assert(update_ext_switch_channel("beta"));
    EnterCriticalSection(&g_update.lock);
    worker = g_update.worker;
    LeaveCriticalSection(&g_update.lock);
    assert(worker && WaitForSingleObject(worker, 20000) == WAIT_OBJECT_0);
    assert(update_ext_status() == UPDATE_RESTART_PENDING);
    assert(strcmp(update_ext_latest_version(), "99.0") == 0);
    assert(update_ext_beta_unlocked());
    assert(!update_ext_switch_channel("beta"));
    // The running process retains its identity until restarted, even in the
    // test-only apply path that simulates closed-game file replacement.
    assert(strcmp(update_ext_channel(), "stable") == 0);
    assert(update_prepare_relative_path(root, UPDATE_ACTIVE_REL, path, sizeof(path), 0));
    assert(update_read_manifest_path(path, &installed));
    assert(strcmp(installed.release_channel, "beta") == 0);
    assert(update_manifest_files_match(&installed, root));
    assert(update_cache_load("beta", &cached));
    assert(update_cache_load("stable", &cached));
    assert(update_prepare_relative_path(root, "NewTransportRuntime.dll", path, sizeof(path), 0));
    update_test_expect_file(path, "New beta runtime fixture.");
    assert(update_recover_journal());
    assert(update_remove_tree(root));
    puts("first beta download, future DLL install and asynchronous opt-in: OK");
}
