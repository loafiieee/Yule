# Lua HTTP Lifecycle Hardening Design

**Date:** 2026-07-28  
**Status:** Source implementation and focused static coverage complete  
**Primary code:** `mod_http.c/.h`, owner lifecycle calls in `lua_manager.c`,
`tests/lua_http_lifecycle_static_test.py`

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

## URL and request-target correctness

The Lua boundary rejects empty/embedded-NUL/invalid-UTF-8 URLs. The worker accepts only
HTTP and HTTPS and rejects embedded username/password credentials. `WinHttpCrackUrl`
returns the query and fragment through `lpszExtraInfo`, separately from `lpszUrlPath`;
the worker now appends that extra component to the request target, while truncating at
`#` because fragments are client-only. A missing path becomes `/`. All path/query
concatenation is checked against the fixed wide request-target buffer before opening
the request.

## Compatibility and follow-up

Handles were already documented as opaque integers and polling return shapes are
unchanged. Cancellation still does not synchronously terminate WinHTTP. Status/header/
redirect metadata is intentionally deferred as additive API work; it must not weaken
the owner, concurrency, timeout, or body bounds.
