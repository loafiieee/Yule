from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CSS = (ROOT / "greggnogg" / "editor.css").read_text(encoding="utf-8")
EDITOR = (ROOT / "greggnogg" / "editor.js").read_text(encoding="utf-8")
HTML = (ROOT / "greggnogg" / "index.html").read_text(encoding="utf-8")


# Greggnogg uses its own live RGB controls; the native browser/Windows color
# picker must never be reintroduced.
assert 'type="color"' not in HTML.lower()
assert 'buildColorBank(els["eggnogg-color"], {goal: doc.rules.eggnoggColor}' in EDITOR
assert "color-channel-editor" in EDITOR
assert 'slider.type = "range"' in EDITOR

# Inspector grid items must be allowed to shrink within the fixed right rail.
assert ".inspector-panel {\n  min-width: 0;" in CSS
assert "overflow-x: hidden;\n  scrollbar-gutter: stable;" in CSS
assert "grid-template-columns: 2.15rem minmax(0, 1fr);" in CSS
assert "grid-template-columns: 1rem minmax(0, 1fr) minmax(2.8rem, 3.5rem);" in CSS
assert '.color-channel input[type="range"] {\n  width: 100%;\n  min-width: 0;' in CSS
assert '@media (max-width: 420px)' in CSS

# Opening a picker is accordion-like instead of stacking several expanded
# editors into the rail and destabilizing its scroll layout.
assert 'host.querySelectorAll(".color-field.is-expanded")' in EDITOR
assert 'otherField.classList.remove("is-expanded")' in EDITOR
assert 'otherEditor.hidden = true' in EDITOR
assert 'otherSwatch.setAttribute("aria-expanded", "false")' in EDITOR

# Preview launch and sharing use one validated URI compiler, so copied links
# cannot drift from links opened locally.
assert "copy-preview-button" not in HTML
assert 'els["preview-button"].addEventListener("contextmenu"' in EDITOR
assert 'els["mobile-preview-button"].addEventListener("contextmenu"' in EDITOR
assert "event.preventDefault();\n      copyPreviewLink();" in EDITOR
assert "function compilePreviewUri()" in EDITOR
assert EDITOR.count("var uri = compilePreviewUri();") == 2
assert "await writeClipboardText(uri);" in EDITOR
assert "document.execCommand(\"copy\")" in EDITOR

print("Greggnogg responsive color-editor static checks: OK")
