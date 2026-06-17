#pragma once

// Framework-global settings, persisted to mods/modframework.cfg as simple
// "key value" integer lines. Distinct from per-mod config (storage.cfg) and
// from the online hub config; this is for the framework's own UI/QOL options.

void fwsettings_init(void);                       // load from disk (idempotent)
int  fwsettings_get_int(const char* key, int def); // value or def if unset
void fwsettings_set_int(const char* key, int value); // set + persist immediately
