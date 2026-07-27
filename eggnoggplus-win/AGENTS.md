# Build and test safety

- Compiling a Windows test executable does not authorize launching it directly.
- Never run an executable from `build/` by hand. Use the repository's guarded test
  runners, which prepare the 32-bit MinGW/LuaJIT runtime `PATH` and preflight DLLs:
  - `tests/run_v2_map_test.ps1`
  - `tests/run_map_script_test.ps1`
  - `tests/run_updater_test.ps1`
  - `tests/run_core_native_tests.ps1`
  - `tests/prematch_net_test.py`
- A new runner that launches a MinGW executable must prepend the repository root,
  `C:\msys64\mingw32\bin`, and `C:\msys64\usr\bin` to its child environment and
  fail before launch when a required runtime DLL cannot be found.
- Do not launch `eggnoggplus.exe` as part of automated build verification.
