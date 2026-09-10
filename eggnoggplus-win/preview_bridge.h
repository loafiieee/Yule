#pragma once
#include <stddef.h>
#ifndef PREVIEW_BRIDGE_PORT
#define PREVIEW_BRIDGE_PORT 31785u
#endif
/* Main-thread lifecycle. Session bytes transfer to caller on take; free them.
 * The socket thread never calls native gameplay or evaluates uploaded scripts. */
int preview_bridge_begin(const char* token,char* error,size_t capacity);
int preview_bridge_take(char token[33],unsigned char** bytes,size_t* size);
void preview_bridge_finish(const char* token,int success);
void preview_bridge_cancel(void);
void preview_bridge_shutdown(void);
unsigned preview_bridge_port(void);
