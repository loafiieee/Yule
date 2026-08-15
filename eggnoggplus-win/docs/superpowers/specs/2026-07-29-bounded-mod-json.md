# Bounded `mod.json`

## Goal

Give ordinary mods one source-backed JSON parser/encoder suitable for HTTP and
interop data without code evaluation, unbounded recursion/allocation,
ambiguous null handling, or nondeterministic object output.

## Public surface

```lua
mod.json.null
mod.json.encode(value)
mod.json.decode(text)
mod.json.array([values])
mod.json.object([values])
mod.json.is_null(value)
```

JSON null maps to one private lightuserdata sentinel. It cannot collapse into
Lua nil, which would delete object fields and array slots. Decoded arrays and
objects have private kind metatables so empty containers round-trip.
`array`/`object` shallow-copy an optional source into a newly tagged table and
never overwrite the source metatable.

## Encoding

- Accepted leaves: boolean, finite Lua number, valid UTF-8 string, and the null
  sentinel.
- Dense positive-integer tables are arrays.
- String-keyed tables are objects.
- Untagged empty tables are objects; an explicit empty array uses `array()`.
- Mixed keys, sparse arrays, non-string object keys, nil, unsupported values,
  cycles, and non-finite numbers fail.
- Object keys are sorted by raw UTF-8 byte order before emission.
- Number parsing/formatting uses a private C numeric locale, so a mod calling
  `os.setlocale` cannot introduce decimal commas or alter accepted JSON.
- Strings escape JSON controls, quote, and backslash. Valid non-ASCII UTF-8 is
  retained.
- No conversion invokes `tostring`, `__pairs`, `__index`, or other user code.

## Decoding

The parser accepts one full strict JSON document. It validates raw UTF-8,
escape syntax, UTF-16 surrogate pairs, the JSON number grammar and finite
double range, delimiters, and end-of-input. Duplicate object keys fail. Errors
carry a byte offset and a bounded value path.

## Resource limits

- input: 1 MiB;
- encoded output: 1 MiB;
- individual string: 256 KiB;
- nesting depth: 32;
- total values/members: 65,536.

Buffers grow geometrically only up to their applicable limit. Object-key sort
storage is bounded by the node/member ceiling. Every allocation/failure path
returns `nil, diagnostic` and frees native temporary storage.

## Compatibility

This additive surface raises API revision to 2 and advertises `json.v1`.
Packages that cannot run without it should use:

```json
{
  "api_version": 1,
  "api_revision": 2,
  "api_requires": ["json.v1"]
}
```

Optional consumers should branch with `mod.api.has("json.v1")`.

## Verification

`tests/mod_json_test.c` runs the production implementation in a real guarded
LuaJIT state. It pins deterministic objects, Unicode surrogate pairs, null,
empty containers, shallow tag helpers, duplicate detection, cycle paths,
sparse/mixed table rejection, invalid UTF-8/JSON, nil/non-finite rejection,
input/string limits, and depth limits.

`tests/mod_json_integration_static_test.py` pins all native limits and guards,
ensures no Lua evaluation path exists, and checks capability, Lua registration,
docs, build, and guarded-runner integration. The general docs audit covers the
full exported namespace.
