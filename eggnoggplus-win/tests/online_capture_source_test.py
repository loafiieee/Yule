from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "hooks.c").read_text(encoding="utf-8")

def function(name):
    match = re.search(r"(?:static )?(?:int|void) " + name + r"\([^;]*?\)\s*\{", source)
    assert match, name
    start = source.index("{", match.start())
    depth, quote, escaped = 0, None, False
    for pos in range(start, len(source)):
        char = source[pos]
        if quote:
            if escaped: escaped = False
            elif char == "\\": escaped = True
            elif char == quote: quote = None
        elif char in ("\"", "'"): quote = char
        elif char == "{": depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0: return source[match.start():pos + 1] + "\n"
    raise AssertionError(name)

names = ["online_next_selectable", "online_move_selection", "online_clear_capture_state", "online_parse_long_range", "online_setting_is_text",
         "online_begin_setting_capture", "online_commit_capture", "online_cancel_capture",
         "online_commit_setting_capture_for_navigation", "online_capture_is_setting_row",
         "online_focus_selected_text_setting", "hooks_online_hub_mousebutton", "hooks_online_hub_keydown"]
text = "/* Extracted production handlers for the isolated, no-network fixture. */\n"
for name in names: text += function(name) + "\n"
keys = function("hooks_online_hub_keydown")
text += "static int test_capture_keydown(int sym, int mod) { return hooks_online_hub_keydown(sym, 0, mod); }\n"
for name in sorted(set(re.findall(r"\b(?:SDLK_[A-Z_]+|KMOD_SHIFT)\b", keys))):
    define = re.search(r"^#define " + name + r"\s+[^\n]+", source, re.M)
    assert define, name
    text = define.group(0) + "\n" + text
(ROOT / "build").mkdir(exist_ok=True)
(ROOT / "build/online_capture_under_test.h").write_text(text, encoding="utf-8", newline="\n")
mouse = function("hooks_online_hub_mousebutton")
assert mouse.index("online_commit_setting_capture_for_navigation()") < mouse.index("online_activate_selected_from_mouse")
assert "if (online_capture_is_setting_row(row)) return 1;" in mouse
keys = function("hooks_online_hub_keydown")
assert "sym == SDLK_TAB || sym == SDLK_UP || sym == SDLK_DOWN" in keys
assert "mod & KMOD_SHIFT" in keys
assert "online_focus_selected_text_setting();" in keys
assert "online_cancel_capture();" in keys
assert "g_online_capture_active || g_online_pending_match.active" in function("hooks_online_hub_mousemotion")
assert "online_commit_setting_capture_for_navigation()" in function("hooks_online_hub_mousewheel")
assert "online_commit_setting_capture_for_navigation()" in function("hooks_online_hub_control_action")
print("online capture production extraction and event wiring: OK")
