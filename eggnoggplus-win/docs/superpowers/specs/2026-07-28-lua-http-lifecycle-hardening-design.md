# Lua HTTP Lifecycle Hardening Design

**Date:** 2026-07-28
**Status:** Source implementation, live lifecycle tests, and static coverage complete
**Primary code:** `mod_http.c/.h`, owner lifecycle calls in `lua_manager.c`,
`tests/mod_http_test.c`, `tests/lua_http_lifecycle_static_test.py`

## Problem

The original `mod.http` table used eight process-global array indices as public
handles. `cancel()` closed the Win32 thread handle, freed a published body, and marked
the array slot reusable even though closing a thread handle does not stop the worker.
The detached worker could then write into a request belonging to another mod. Responses
also had no byte ceiling and requests survived owner unload without a defined cleanup
path.

## Ownership and handles

Every request records its `LoadedMod` owner and a monotonically changing positive opaque
handle. `get`, `poll`, and `cancel` are owner-bound Lua closures. A mod can resolve only
the live handle it created; raw slot indices are no longer accepted. Mod unload marks
every owned request canceled before the mod allocation is released.

## Cancellation

Cancellation is asynchronous and non-blocking. It atomically marks the slot but does not
close the thread handle, free memory, or make the slot reusable. The worker keeps its
response buffer local, checks cancellation while streaming, publishes its body only at
completion, and writes `done` last. A main-thread reaper releases canceled slots only
after that final atomic completion state. The reaper runs during API calls, every normal
frame, and owner unload.

## Bounds

There remain at most eight concurrent process-wide workers. Resolve, connect, send, and
receive timeouts remain ten seconds. A declared `Content-Length` above 8 MiB is rejected
before body allocation, and the streaming loop independently enforces the same 8 MiB
ceiling when the header is missing or false. HTTP status 200 remains the only success
status in API 1.

## Response metadata and redirects

API revision 3 adds the `http.response_metadata` capability. A successful terminal
poll remains `"done", body` for existing callers and appends a response table as its
third value. A non-200 response remains `"error", "HTTP <status>"` and appends the
same table; transport failures without a response retain the two-value error shape.
The table reports the final status/status text, final URL, redirect flag/count, and a
bounded allowlist of `content-type`, `content-length`, `etag`, `last-modified`,
`cache-control`, and `location`. It never exposes cookies or authentication headers.

WinHTTP automatic redirects are limited to five and HTTPS-to-HTTP downgrades are
refused. Redirect callbacks only increment an atomic counter; all strings remain in
fixed request-slot buffers and are published before the terminal `done` write.

## URL and request-target correctness

The Lua boundary rejects empty/embedded-NUL/invalid-UTF-8 URLs. The worker accepts only
HTTP and HTTPS and rejects embedded username/password credentials. `WinHttpCrackUrl`
returns the query and fragment through `lpszExtraInfo`, separately from `lpszUrlPath`;
the worker now appends that extra component to the request target, while truncating at
`#` because fragments are client-only. A missing path becomes `/`. All path/query
concatenation is checked against the fixed wide request-target buffer before opening
the request.

## Compatibility and coverage

Handles remain opaque integers and the existing leading polling return values are
unchanged, so callers that read one or two values continue to work. Cancellation still
does not synchronously terminate WinHTTP. The live loopback test fills all eight slots,
checks cross-owner isolation, cancellation and unload, waits for deferred cleanup,
reuses retired slots, rejects stale handles, and exercises both redirected success and
HTTP-error metadata.
