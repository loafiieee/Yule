from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INDEX = (ROOT / "greggnogg" / "index.html").read_text(encoding="utf-8")
EDITOR = (ROOT / "greggnogg" / "editor.js").read_text(encoding="utf-8")
DESIGNER = (ROOT / "greggnogg" / "ambiance-designer.js").read_text(encoding="utf-8")
ATLAS = (ROOT / "greggnogg" / "atlas-renderer.js").read_text(encoding="utf-8")
CORE = (ROOT / "greggnogg" / "editor-core.js").read_text(encoding="utf-8")
CSS = (ROOT / "greggnogg" / "editor.css").read_text(encoding="utf-8")
PREVIEW_CLIENT = (ROOT / "greggnogg" / "preview-client.js").read_text(encoding="utf-8")

assert '<script defer src="ambiance-designer.js"></script>' in INDEX
assert 'id="edit-ambiances-button"' in INDEX
assert "openGregAmbianceDesigner" in EDITOR
assert "activeRoom().id" in EDITOR
assert 'option.dataset.customAmbiance = "true"' in EDITOR
assert 'commit("Enable custom ambiance"' in EDITOR
room_canvas = EDITOR[EDITOR.index("function renderRoomCanvas()") : EDITOR.index("function renderGrid()")]
assert "particles: state.document && state.document.particles" in room_canvas
assert "ambiances: state.document && state.document.ambiances" in room_canvas
assert "mapId: state.document && state.document.id" in room_canvas
assert "externalImages: assetSources()" in room_canvas
assert 'setControlValue(ambientSelect, ambientValue === "fumes" ? "boil" : ambientValue, true)' in EDITOR

for label in (
    "Amount on screen",
    "Draw layer",
    "Spawn shape",
    "Motion timing",
    "Spawn shape bounds inside the room (pixels)",
    "Horizontal speed",
    "Vertical speed",
    "Horizontal acceleration",
    "Vertical acceleration",
    "Rotation (degrees)",
    "Additive glow",
    "Mirror movement and pictures with mirrored rooms",
):
    assert label in DESIGNER

assert "renderCustomAmbianceLayer" in ATLAS
assert ATLAS.count("renderCustomAmbianceLayer(context, assets, customAmbiance") == 5
assert 'context.globalCompositeOperation = "lighter"' in ATLAS
assert "particle.lifetime_ticks" in ATLAS and "visual.frame_ticks" in ATLAS
for token in ("visual.end_tint", "visual.end_scale_x", "visual.end_scale_y",
              "visual.start_rotation", "visual.end_rotation", "visual.interpolation",
              "particleEase", "particleLerp"):
    assert token in ATLAS, f"browser preview is missing particle transition support: {token}"
assert "options.mirrored && emitter.mirror_with_room" in ATLAS
assert 'data-ambiance-tab' in DESIGNER and 'data-particle-tab' in DESIGNER
assert "previewDrawing" in DESIGNER
assert "greg_particle_preview" in DESIGNER
for label in ("Ending color", "Ending opacity", "Ending width", "Ending height",
              "Starting rotation (degrees)", "Ending rotation (degrees)", "Transition curve"):
    assert label in DESIGNER, f"particle designer is missing {label}"
assert ".ambiance-designer-body>main" in CSS and "overflow-y:auto" in CSS
assert ".ambiance-picture-chooser" in CSS and "Choose a picture from the sheet or import a PNG" in DESIGNER
assert ".ambiance-sheet-picker" in CSS and "max-height:220px" in CSS
assert "grid-template-columns:minmax(0,1fr) minmax(280px,340px)" in CSS
assert "data.api<2" in PREVIEW_CLIENT and "too old for V2 map previews" in PREVIEW_CLIENT

assert "validateAmbianceCatalog(json, assets, errors)" in CORE
assert '"ambiance_lane_count"' in CORE
assert '"emitter_particle"' in CORE
assert "data.particles = deepClone" in CORE
assert "data.ambiances = deepClone" in CORE

print("custom ambiance Greggnogg integration checks: OK")
