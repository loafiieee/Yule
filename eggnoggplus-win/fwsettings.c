#include "fwsettings.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define FW_PATH        "mods/modframework.cfg"
#define FW_MAX_ENTRIES 32
#define FW_KEY_MAX     64

typedef struct {
    char key[FW_KEY_MAX];
    int  value;
} FwEntry;

static FwEntry g_entries[FW_MAX_ENTRIES];
static int     g_count  = 0;
static int     g_loaded = 0;

static int fw_find(const char* key) {
    for (int i = 0; i < g_count; i++) {
        if (_stricmp(g_entries[i].key, key) == 0) return i;
    }
    return -1;
}

void fwsettings_init(void) {
    g_count = 0;
    g_loaded = 1;

    FILE* f = fopen(FW_PATH, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == '\0' || *p == '\n' || *p == '\r') continue;

        char key[FW_KEY_MAX];
        int  value = 0;
        if (sscanf(p, "%63s %d", key, &value) == 2 && g_count < FW_MAX_ENTRIES) {
            strncpy(g_entries[g_count].key, key, FW_KEY_MAX - 1);
            g_entries[g_count].key[FW_KEY_MAX - 1] = '\0';
            g_entries[g_count].value = value;
            g_count++;
        }
    }
    fclose(f);
}

int fwsettings_get_int(const char* key, int def) {
    if (!g_loaded) fwsettings_init();
    if (!key || !key[0]) return def;
    int i = fw_find(key);
    return (i >= 0) ? g_entries[i].value : def;
}

static void fwsettings_save(void) {
    CreateDirectoryA("mods", NULL);
    FILE* f = fopen(FW_PATH, "w");
    if (!f) return;
    fprintf(f, "# Eggnogg+ mod framework settings (key value, one per line)\n");
    for (int i = 0; i < g_count; i++) {
        fprintf(f, "%s %d\n", g_entries[i].key, g_entries[i].value);
    }
    fclose(f);
}

void fwsettings_set_int(const char* key, int value) {
    if (!g_loaded) fwsettings_init();
    if (!key || !key[0]) return;

    int i = fw_find(key);
    if (i < 0) {
        if (g_count >= FW_MAX_ENTRIES) return;
        i = g_count++;
        strncpy(g_entries[i].key, key, FW_KEY_MAX - 1);
        g_entries[i].key[FW_KEY_MAX - 1] = '\0';
    }
    g_entries[i].value = value;
    fwsettings_save();
}
