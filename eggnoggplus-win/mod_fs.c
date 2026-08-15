#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER _WIN32_WINNT
#endif

#include "mod_fs.h"

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <luajit-2.1/lauxlib.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FS_PICKER_MAX_FILTERS 16
#define FS_PICKER_MAX_PATTERNS 16
#define FS_PICKER_FILTER_WCHARS 4096
#define FS_PICKER_PATH_WCHARS 32768
#define FS_PICKER_TITLE_BYTES 512
#define FS_PICKER_LABEL_BYTES 160
#define FS_PICKER_PATTERN_BYTES 96

static int fs_picker_owner_guard(lua_State* L, const char* api_name) {
    const int* owner_enabled =
        (const int*)lua_touserdata(L, lua_upvalueindex(1));
    if (owner_enabled && *owner_enabled) return 1;

    lua_pushnil(L);
    lua_pushfstring(L, "%s is unavailable because its mod is disabled",
                    api_name ? api_name : "mod.fs picker");
    return 0;
}

static WCHAR* fs_picker_utf8_to_wide(const char* text, size_t bytes) {
    WCHAR* wide;
    int chars;

    if (!text || bytes > (size_t)INT_MAX || strlen(text) != bytes) return NULL;
    chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)bytes,
                                NULL, 0);
    if (chars <= 0 && bytes != 0) return NULL;

    wide = (WCHAR*)calloc((size_t)chars + 1u, sizeof(WCHAR));
    if (!wide) return NULL;
    if (chars > 0 &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)bytes,
                            wide, chars) != chars) {
        free(wide);
        return NULL;
    }
    wide[chars] = L'\0';
    return wide;
}

static int fs_picker_append_filter_part(WCHAR* filter, size_t filter_cap,
                                        size_t* used, const char* text,
                                        size_t bytes) {
    int chars;

    if (!filter || !used || !text || bytes == 0 || bytes > (size_t)INT_MAX ||
        strlen(text) != bytes) {
        return 0;
    }
    chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)bytes,
                                NULL, 0);
    if (chars <= 0 || *used + (size_t)chars + 1u >= filter_cap) return 0;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)bytes,
                            filter + *used, chars) != chars) {
        return 0;
    }
    *used += (size_t)chars;
    filter[(*used)++] = L'\0';
    return 1;
}

static int fs_picker_pattern_valid(const char* pattern, size_t bytes) {
    size_t i;

    if (!pattern || bytes == 0 || bytes > FS_PICKER_PATTERN_BYTES ||
        strlen(pattern) != bytes) {
        return 0;
    }
    for (i = 0; i < bytes; ++i) {
        unsigned char c = (unsigned char)pattern[i];
        if (c < 0x20u || c == 0x7fu || c == '\\' || c == '/' || c == ':' ||
            c == ';' || c == '"' || c == '<' || c == '>' || c == '|') {
            return 0;
        }
    }
    return 1;
}

static int fs_picker_label_valid(const char* label, size_t bytes) {
    size_t i;

    if (!label || bytes == 0 || bytes > FS_PICKER_LABEL_BYTES ||
        strlen(label) != bytes) {
        return 0;
    }
    for (i = 0; i < bytes; ++i) {
        unsigned char c = (unsigned char)label[i];
        if (c < 0x20u || c == 0x7fu) return 0;
    }
    return 1;
}

static int fs_picker_run(lua_State* L, const WCHAR* title,
                         const WCHAR* filter, DWORD filter_index) {
    WCHAR* path = (WCHAR*)calloc(FS_PICKER_PATH_WCHARS, sizeof(WCHAR));
    OPENFILENAMEW dialog;
    DWORD dialog_error;
    int utf8_bytes;
    char* utf8_path;

    if (!path) {
        lua_pushnil(L);
        lua_pushstring(L, "out of memory");
        return 2;
    }

    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFile = path;
    dialog.nMaxFile = FS_PICKER_PATH_WCHARS;
    dialog.lpstrTitle = title;
    dialog.lpstrFilter = filter;
    dialog.nFilterIndex = filter_index;
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                   OFN_NOCHANGEDIR;
#ifdef OFN_DONTADDTORECENT
    dialog.Flags |= OFN_DONTADDTORECENT;
#endif
    if (!GetOpenFileNameW(&dialog)) {
        char error_message[64];

        dialog_error = CommDlgExtendedError();
        free(path);
        lua_pushnil(L);
        if (dialog_error == 0) {
            lua_pushstring(L, "cancelled");
        } else {
            snprintf(error_message, sizeof(error_message),
                     "file dialog failed (0x%08lx)",
                     (unsigned long)dialog_error);
            lua_pushstring(L, error_message);
        }
        return 2;
    }

    utf8_bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1,
                                     NULL, 0, NULL, NULL);
    if (utf8_bytes <= 0) {
        free(path);
        lua_pushnil(L);
        lua_pushstring(L, "selected path could not be encoded as UTF-8");
        return 2;
    }

    utf8_path = (char*)malloc((size_t)utf8_bytes);
    if (!utf8_path ||
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, utf8_path,
                            utf8_bytes, NULL, NULL) != utf8_bytes) {
        free(utf8_path);
        free(path);
        lua_pushnil(L);
        lua_pushstring(L, "out of memory while reading selected path");
        return 2;
    }

    free(path);
    lua_pushlstring(L, utf8_path, (size_t)utf8_bytes - 1u);
    free(utf8_path);
    return 1;
}

static int lua_fs_pick_file(lua_State* L) {
    WCHAR filter[FS_PICKER_FILTER_WCHARS];
    size_t filter_used = 0;
    const char* title_utf8 = "Select file";
    size_t title_bytes = strlen(title_utf8);
    WCHAR* title;
    size_t filter_count;
    size_t i;
    int allow_all = 0;
    int result;
    DWORD filter_index = 1;

    if (!fs_picker_owner_guard(L, "mod.fs.pick_file")) return 2;
    luaL_checktype(L, 1, LUA_TTABLE);
    memset(filter, 0, sizeof(filter));

    lua_getfield(L, 1, "title");
    if (!lua_isnil(L, -1)) {
        title_utf8 = luaL_checklstring(L, -1, &title_bytes);
        if (title_bytes == 0 || title_bytes > FS_PICKER_TITLE_BYTES ||
            strlen(title_utf8) != title_bytes) {
            return luaL_error(L,
                              "mod.fs.pick_file title must be 1-%d UTF-8 bytes",
                              FS_PICKER_TITLE_BYTES);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "allow_all");
    if (!lua_isnil(L, -1)) {
        luaL_checktype(L, -1, LUA_TBOOLEAN);
        allow_all = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "filter_index");
    if (!lua_isnil(L, -1)) {
        lua_Integer requested = luaL_checkinteger(L, -1);
        if (requested < 1 || requested > FS_PICKER_MAX_FILTERS + 1) {
            return luaL_error(L,
                              "mod.fs.pick_file filter_index is out of range");
        }
        filter_index = (DWORD)requested;
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "filters");
    if (lua_isnil(L, -1)) {
        filter_count = 0;
    } else {
        luaL_checktype(L, -1, LUA_TTABLE);
        filter_count = lua_objlen(L, -1);
        if (filter_count > FS_PICKER_MAX_FILTERS) {
            return luaL_error(L,
                              "mod.fs.pick_file accepts at most %d filters",
                              FS_PICKER_MAX_FILTERS);
        }
    }

    for (i = 1; i <= filter_count; ++i) {
        const char* label;
        size_t label_bytes;
        size_t pattern_count;
        size_t j;
        char patterns[
            (FS_PICKER_PATTERN_BYTES + 1) * FS_PICKER_MAX_PATTERNS
        ];
        size_t patterns_used = 0;

        lua_rawgeti(L, -1, (int)i);
        if (!lua_istable(L, -1)) {
            return luaL_error(L,
                              "mod.fs.pick_file filters[%d] must be a table",
                              (int)i);
        }

        lua_getfield(L, -1, "name");
        label = luaL_checklstring(L, -1, &label_bytes);
        if (!fs_picker_label_valid(label, label_bytes) ||
            !fs_picker_append_filter_part(filter, FS_PICKER_FILTER_WCHARS,
                                          &filter_used, label, label_bytes)) {
            return luaL_error(
                L, "mod.fs.pick_file filters[%d].name is invalid or too long",
                (int)i);
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "patterns");
        luaL_checktype(L, -1, LUA_TTABLE);
        pattern_count = lua_objlen(L, -1);
        if (pattern_count == 0 || pattern_count > FS_PICKER_MAX_PATTERNS) {
            return luaL_error(
                L,
                "mod.fs.pick_file filters[%d].patterns must contain 1-%d items",
                (int)i, FS_PICKER_MAX_PATTERNS);
        }
        for (j = 1; j <= pattern_count; ++j) {
            const char* pattern;
            size_t pattern_bytes;

            lua_rawgeti(L, -1, (int)j);
            pattern = luaL_checklstring(L, -1, &pattern_bytes);
            if (!fs_picker_pattern_valid(pattern, pattern_bytes) ||
                patterns_used + pattern_bytes + (j > 1 ? 1u : 0u) >=
                    sizeof(patterns)) {
                return luaL_error(
                    L,
                    "mod.fs.pick_file filters[%d].patterns[%d] is invalid",
                    (int)i, (int)j);
            }
            if (j > 1) patterns[patterns_used++] = ';';
            memcpy(patterns + patterns_used, pattern, pattern_bytes);
            patterns_used += pattern_bytes;
            lua_pop(L, 1);
        }
        patterns[patterns_used] = '\0';
        if (!fs_picker_append_filter_part(filter, FS_PICKER_FILTER_WCHARS,
                                          &filter_used, patterns,
                                          patterns_used)) {
            return luaL_error(L, "mod.fs.pick_file filter data is too large");
        }
        lua_pop(L, 2);
    }
    lua_pop(L, 1);

    if (filter_count == 0 || allow_all) {
        if (!fs_picker_append_filter_part(filter, FS_PICKER_FILTER_WCHARS,
                                          &filter_used, "All files (*.*)",
                                          15) ||
            !fs_picker_append_filter_part(filter, FS_PICKER_FILTER_WCHARS,
                                          &filter_used, "*.*", 3)) {
            return luaL_error(L, "mod.fs.pick_file filter data is too large");
        }
        ++filter_count;
    }
    if (filter_index > (DWORD)filter_count) {
        return luaL_error(
            L, "mod.fs.pick_file filter_index exceeds filter count");
    }
    filter[filter_used] = L'\0';

    title = fs_picker_utf8_to_wide(title_utf8, title_bytes);
    if (!title) {
        return luaL_error(L, "mod.fs.pick_file title is not valid UTF-8");
    }
    result = fs_picker_run(L, title, filter, filter_index);
    free(title);
    return result;
}

static int lua_fs_pick_character_file(lua_State* L) {
    const char* title_utf8;
    size_t title_bytes;
    WCHAR* title;
    int result;
    static const WCHAR filter[] =
        L"Character Packages (*.zip;*.json)\0*.zip;*.json\0"
        L"ZIP Files (*.zip)\0*.zip\0"
        L"JSON Files (*.json)\0*.json\0"
        L"All Files (*.*)\0*.*\0\0";

    if (!fs_picker_owner_guard(L, "mod.fs.pick_character_file")) return 2;
    title_utf8 = luaL_optlstring(
        L, 1, "Import character package", &title_bytes);
    if (title_bytes == 0 || title_bytes > FS_PICKER_TITLE_BYTES ||
        strlen(title_utf8) != title_bytes) {
        return luaL_error(
            L,
            "mod.fs.pick_character_file title must be 1-%d UTF-8 bytes",
            FS_PICKER_TITLE_BYTES);
    }
    title = fs_picker_utf8_to_wide(title_utf8, title_bytes);
    if (!title) {
        return luaL_error(
            L, "mod.fs.pick_character_file title is not valid UTF-8");
    }
    result = fs_picker_run(L, title, filter, 1);
    free(title);
    return result;
}

static int lua_fs_pick_folder(lua_State* L) {
    const char* title_utf8;
    size_t title_bytes;
    WCHAR* title;
    WCHAR path[MAX_PATH];
    BROWSEINFOW browser;
    LPITEMIDLIST selected;
    int utf8_bytes;
    char* utf8_path;

    if (!fs_picker_owner_guard(L, "mod.fs.pick_folder")) return 2;
    title_utf8 = luaL_optlstring(L, 1, "Select folder", &title_bytes);
    if (title_bytes == 0 || title_bytes > FS_PICKER_TITLE_BYTES ||
        strlen(title_utf8) != title_bytes) {
        return luaL_error(L,
                          "mod.fs.pick_folder title must be 1-%d UTF-8 bytes",
                          FS_PICKER_TITLE_BYTES);
    }
    title = fs_picker_utf8_to_wide(title_utf8, title_bytes);
    if (!title) {
        return luaL_error(L, "mod.fs.pick_folder title is not valid UTF-8");
    }

    memset(path, 0, sizeof(path));
    memset(&browser, 0, sizeof(browser));
    browser.hwndOwner = GetActiveWindow();
    browser.lpszTitle = title;
    browser.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    selected = SHBrowseForFolderW(&browser);
    free(title);
    if (selected) {
        int ok = SHGetPathFromIDListW(selected, path);
        CoTaskMemFree(selected);
        if (ok && path[0]) {
            utf8_bytes = WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, NULL, 0, NULL, NULL);
            if (utf8_bytes <= 0) {
                lua_pushnil(L);
                lua_pushstring(
                    L, "selected folder could not be encoded as UTF-8");
                return 2;
            }
            utf8_path = (char*)malloc((size_t)utf8_bytes);
            if (!utf8_path ||
                WideCharToMultiByte(
                    CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, utf8_path,
                    utf8_bytes, NULL, NULL) != utf8_bytes) {
                free(utf8_path);
                lua_pushnil(L);
                lua_pushstring(
                    L, "out of memory while reading selected folder");
                return 2;
            }
            lua_pushlstring(L, utf8_path, (size_t)utf8_bytes - 1u);
            free(utf8_path);
            return 1;
        }
    }

    lua_pushnil(L);
    lua_pushstring(L, "cancelled");
    return 2;
}

static int fs_find_file_recursive(const char* dir, const char* name, char* out,
                                  size_t out_cap, int depth) {
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE search;
    size_t dir_len;

    if (!dir || !name || !out || out_cap == 0 || depth > 12) return 0;
    dir_len = strlen(dir);
    if (dir_len == 0 || dir_len + 3 >= sizeof(pattern)) return 0;
    snprintf(pattern, sizeof(pattern), "%s%s*", dir,
             (dir[dir_len - 1] == '\\' || dir[dir_len - 1] == '/') ? "" :
                                                                        "\\");
    search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return 0;

    do {
        char child[MAX_PATH];
        int is_directory;

        if (strcmp(found.cFileName, ".") == 0 ||
            strcmp(found.cFileName, "..") == 0) {
            continue;
        }
        if (dir_len + strlen(found.cFileName) + 2 >= sizeof(child)) continue;
        snprintf(
            child, sizeof(child), "%s%s%s", dir,
            (dir[dir_len - 1] == '\\' || dir[dir_len - 1] == '/') ? "" : "\\",
            found.cFileName);

        is_directory =
            (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (is_directory) {
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                fs_find_file_recursive(child, name, out, out_cap, depth + 1)) {
                FindClose(search);
                return 1;
            }
            continue;
        }
        if (_stricmp(found.cFileName, name) == 0) {
            snprintf(out, out_cap, "%s", child);
            FindClose(search);
            return 1;
        }
    } while (FindNextFileA(search, &found));

    FindClose(search);
    return 0;
}

static int lua_fs_find_file(lua_State* L) {
    const char* root = luaL_checkstring(L, 1);
    const char* name = luaL_optstring(L, 2, "character.json");
    char out[MAX_PATH];

    out[0] = '\0';
    if (fs_find_file_recursive(root, name, out, sizeof(out), 0)) {
        lua_pushstring(L, out);
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, "not found");
    return 2;
}

static void fs_register_owner_function(lua_State* L,
                                       const int* owner_enabled,
                                       lua_CFunction function,
                                       const char* name) {
    lua_pushlightuserdata(L, (void*)owner_enabled);
    lua_pushcclosure(L, function, 1);
    lua_setfield(L, -2, name);
}

void mod_fs_lua_push_api(lua_State* L, const int* owner_enabled) {
    lua_newtable(L);
    fs_register_owner_function(
        L, owner_enabled, lua_fs_pick_file, "pick_file");
    fs_register_owner_function(
        L, owner_enabled, lua_fs_pick_character_file, "pick_character_file");
    fs_register_owner_function(
        L, owner_enabled, lua_fs_pick_folder, "pick_folder");
    lua_pushcfunction(L, lua_fs_find_file);
    lua_setfield(L, -2, "find_file");
}
