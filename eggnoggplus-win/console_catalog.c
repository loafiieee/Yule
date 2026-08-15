#include "console_catalog.h"

#include <ctype.h>

static const char* const k_commands[] = {
    "help", "commands", "clear", "history", "echo", "find",
    "console.find", "console.stats", "console.copy",
    "state", "state.last", "state.return", "state.switch", "sys.info",
    "ui.size", "time.scale", "framework.api", "discord.app",
    "music.status", "music.scan", "music.rescan", "music.play",
    "music.output_rate",
    "mods.count", "mods.list", "mods.find", "mods.info", "mods.trace",
    "mods.enable", "mods.disable", "mods.toggle", "mods.config",
    "mods.config.find", "mods.config.get", "mods.config.set",
    "mods.config.action", "mods.cfg", "mods.cfg.find", "mods.cfg.get",
    "mods.cfg.set", "mods.cfg.action",
    "binds.list", "binds.find", "binds.set", "binds.clear",
    "reload.mods", "mods.reload", "reload.assets",
    "online", "online.hub", "online.troubleshoot", "net.diag",
    "net.trouble",
    "ggpo.net", "ggpo.roundtrip", "ggpo.selftest", "ggpo.local",
    "log.level", "log.tail", "input.show", "input.override",
    "input.clear", "lua", "eval", "lua.mod", "eval.mod", "lua.file",
    "exit", "quit", "dev",
};

static const char* const k_developer_commands[] = {
    "ggpo.net", "ggpo.roundtrip", "ggpo.selftest", "ggpo.local",
    "state.switch", "input.show", "input.override", "input.clear",
    "lua", "eval", "lua.mod", "eval.mod", "lua.file", "log.tail",
};

static int ascii_case_equal(const char* left, const char* right) {
    unsigned char a;
    unsigned char b;

    if (!left || !right) return 0;
    do {
        a = (unsigned char)*left++;
        b = (unsigned char)*right++;
        if (tolower(a) != tolower(b)) return 0;
    } while (a != '\0');
    return 1;
}

static int contains(const char* command, const char* const* commands,
                    size_t count) {
    size_t index;

    if (!command || !command[0]) return 0;
    for (index = 0; index < count; ++index) {
        if (ascii_case_equal(command, commands[index])) return 1;
    }
    return 0;
}

size_t console_catalog_count(void) {
    return sizeof(k_commands) / sizeof(k_commands[0]);
}

const char* console_catalog_at(size_t index) {
    if (index >= console_catalog_count()) return NULL;
    return k_commands[index];
}

int console_catalog_is_known(const char* command) {
    return contains(command, k_commands, console_catalog_count());
}

int console_catalog_is_developer(const char* command) {
    return contains(command, k_developer_commands,
                    sizeof(k_developer_commands) /
                        sizeof(k_developer_commands[0]));
}
