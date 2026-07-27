# QuickJS-NG

This directory vendors the `quickjs-amalgam.c` and `quickjs.h` files from
QuickJS-NG v0.15.0, commit `433941b99fb3c5e7f98b7ebd78727972bcf467ee`.

Upstream: <https://github.com/quickjs-ng/quickjs>

The files are used as a core-only embedded ECMAScript runtime for Dollchan
bytebeat playlist tracks. Yule does not compile or expose `quickjs-libc.c`, the
`std` module, or the `os` module. See `LICENSE` for the upstream MIT license.
