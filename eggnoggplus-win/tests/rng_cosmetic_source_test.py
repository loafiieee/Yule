from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "hooks.c").read_text(encoding="utf-8")
start = source.index("static int hooks_rng_caller_is_cosmetic(uintptr_t caller) {")
end = source.index("\n}\n", start) + 3
(ROOT / "build").mkdir(exist_ok=True)
classifier = source[start:end]
start = source.index("static long double __cdecl hooked_frnd(float lo, float hi) {")
end = source.index("\n}\n", start) + 3
# Supply a controlled caller address instead of the fixture's machine address.
frnd = source[start:end].replace("__builtin_return_address(0)", "((void*)test_caller)")
(ROOT / "build/rng_cosmetic_under_test.h").write_text(classifier + frnd, encoding="utf-8", newline="\n")
print("Production cosmetic RNG classifier extracted")
