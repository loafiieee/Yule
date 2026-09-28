from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CSS = (ROOT / "greggnogg" / "editor.css").read_text(encoding="utf-8")
EDITOR = (ROOT / "greggnogg" / "editor.js").read_text(encoding="utf-8")
HTML = (ROOT / "greggnogg" / "index.html").read_text(encoding="utf-8")
OBJECT_DESIGNER = (ROOT / "greggnogg" / "object-designer.js").read_text(encoding="utf-8")


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

# The large room-graph canvas has one-click fit and direct room controls;
# instance geometry and palette selection remain independent.
for token in ["room-layout-minimap", "room-layout-fit-button", "room-layout-auto-button", "room-layout-diagnostics",
              "room-layout-mirror-x", "room-layout-appearance",
              "room-layout-ambient", "room-layout-native-tileset",
              "room-layout-opponent-spawn", "room-layout-start-p1",
              "room-layout-start-p2", "room-layout-facing-p1",
              "room-layout-facing-p2"]:
    assert f'id="{token}"' in HTML
assert "function renderRoomLayoutMinimap" in EDITOR
assert "function centerRoomLayoutAt" in EDITOR
assert "Core.setRoomGraphNodePresentation" in EDITOR
assert "Core.setRoomGraphNodeOverrides" in EDITOR
assert "function renderPlacedRoomStarts" in EDITOR
assert "function savePlacedRoomStart" in EDITOR
assert 'id="spawn-marker-scope"' in HTML
assert 'id="spawn-scope-reset"' in HTML
assert "function visibleSpawnOverlay" in EDITOR
assert 'state.spawnMarkerScope === "copy"' in EDITOR
assert "Core.roomGraphDoorwayGeometry" in EDITOR
assert "Core.autoArrangeRoomGraph" in EDITOR
assert "room-layout-doorway-opening" in EDITOR
assert 'id="full-map-button"' in HTML
assert "function renderRoomLayoutPreview" in EDITOR
assert "Core.moveRoomGraphNode" in EDITOR
assert "Core.resizeRoomGraphRoom" in EDITOR
assert "function addMiniRoom" in EDITOR
assert 'function () { focusPlacedRoom(node.id); }' in EDITOR
assert "Rooms connect automatically wherever their edges touch" in HTML

# New rooms are independent and placed atomically. Copies are explicit and can
# transform their tiles, markers, and instance-owned objects before placement.
for token in ["room-layout-source-room", "room-layout-copy-flip-x", "room-layout-copy-flip-y", "room-edge-nav"]:
    assert f'id="{token}"' in HTML
assert "function createAndPlaceRoom" in EDITOR
assert "function transformCopiedRoom" in EDITOR
assert "GregObjects.transformAreaPlacements" in EDITOR
assert "Core.rebuildAutomaticRoomGraphConnections" in EDITOR
assert 'blankRoomOption.textContent = "New room (independent)"' in EDITOR
assert "function renderRoomEdges" in EDITOR
assert "function refreshAnimatedPalettePreviews" in EDITOR
assert "function refreshAnimatedRoomPreviews" in EDITOR
assert "room-focus-enter-from-" in EDITOR
assert HTML.count('data-layout-kind="symmetric"') == 1
assert HTML.count('data-layout-kind="free"') == 1
assert "calc(-100% - 2rem)" in CSS and "opacity: .35" not in CSS
assert 'var canvas = host.querySelector("canvas")' in EDITOR
assert 'els["full-map-button"].addEventListener("click", openRoomArrangement)' in EDITOR
assert 'function createSymmetricRoomPair(options)' in EDITOR
assert 'function moveSymmetricRoomPair(value, nodeId, y)' in EDITOR
assert 'left.y === right.y' in EDITOR
assert 'var cellX = symmetricAuthoring ? 0' in EDITOR
assert 'roomLayoutAuthoringMode() === "symmetric" ? createSymmetricRoomPair' in EDITOR
assert 'id="room-layout-inspector" hidden' in HTML
assert 'id="room-layout-add-button"' in HTML
assert 'id="room-layout-edit-button"' in HTML
assert "Core.convertRoomGraphToMirrored" in EDITOR
assert 'toast("Could not switch to Symmetrical", error.message, "warning")' in EDITOR
assert "var activeToasts = Object.create(null);" in EDITOR
assert 'existing.badge.textContent = "×" + existing.count;' in EDITOR
assert "MAX_VISIBLE_TOASTS = 4" in EDITOR
assert "state.layoutConversionError = error.message;" in EDITOR
assert 'state.layoutInspectorOpen = true;' in EDITOR
assert 'conversionError.split("\\n")' in EDITOR
assert 'function validationBlockDetail(validation)' in EDITOR
assert 'toast("Preview blocked", validationBlockDetail(validation), "error")' in EDITOR
assert "room-layout-inline-add" in EDITOR
assert "--edge-width-cells" in EDITOR
assert "width: calc(var(--edge-width-cells) * var(--cell-size));" in CSS
assert '[["Can cross", "players"' in EDITOR
assert '["Room focus", "focus"' in EDITOR
assert 'els["room-layout-remove"].disabled = nodes.length <= 1;' in EDITOR
assert 'state.layoutAuthoringMode = "free";' in EDITOR

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

# Imported graphics have visible removal controls in both ordinary map editing
# and the object designer. Object images also expose an arbitrary source crop.
assert 'id="remove-native-tileset-button"' in HTML
assert "button('Remove this PNG'" in OBJECT_DESIGNER
assert "function removePicture(name)" in OBJECT_DESIGNER
for label in ["Crop X", "Crop Y", "Crop width", "Crop height"]:
    assert label in OBJECT_DESIGNER

print("Greggnogg responsive color-editor static checks: OK")
