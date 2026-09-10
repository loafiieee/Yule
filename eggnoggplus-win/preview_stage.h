#pragma once
#include <stddef.h>
/* Stage a validated packet below maps_root/_greggnogg_previews/token.
 * Never replaces an existing session or follows a reparse-point directory.
 * On failure only files created by this call are removed. */
int preview_stage_package(const char* maps_root,const char* token,
                          const void* bytes,size_t size,char* error,size_t capacity);

/* Main-thread collection. A true callback keeps a session whose files are still
 * referenced by a loaded or engine-pinned map. NULL keeps all sessions. Only
 * files staged by this process and still matching their original identity are
 * removed; unrelated files and earlier-process caches are never swept. */
typedef int (*PreviewStageInUse)(const char* token);
void preview_stage_collect(PreviewStageInUse in_use);
