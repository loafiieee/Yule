#pragma once
#include <stddef.h>
#include <stdint.h>
#define PREVIEW_HTTP_HEADER_MAX 8192u
enum PreviewHttpMethod {PREVIEW_HTTP_GET=1,PREVIEW_HTTP_POST=2,PREVIEW_HTTP_OPTIONS=3};
typedef struct PreviewHttpRequest {int method;char token[33];uint32_t length;size_t body_offset;} PreviewHttpRequest;
/* 1 complete valid headers, 0 incomplete, -1 rejected. Output is atomic.
 * Only literal IPv4-loopback Host and /preview/<token>, no transfer encoding. */
int preview_http_parse(const void* bytes,size_t size,unsigned port,PreviewHttpRequest* out);
