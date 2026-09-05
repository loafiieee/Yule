(function () {
  "use strict";

  var Core = window.GregCore || window.YuleEditorCore;
  var Atlas = window.GregAtlas || null;
  if (!Core) {
    document.body.innerHTML = '<main class="fatal-error"><h1>Greggnogg could not start</h1><p>The map-format core did not load.</p></main>';
    return;
  }

  var COLS = Core.COLS || 33;
  var ROWS = Core.ROWS || 12;
  var MAX_ROOMS = Core.MAX_ROOMS || 9;
  var STORAGE_KEY = "greggnogg:draft:v1";
  var V2_BACKUP_KEY = "greggnogg:archived-v2-draft";
  var PALETTE_STORAGE_KEY = "greggnogg:saved-palettes:v1";
  var APPROXIMATE_GLYPHS = "12ACcOPTWw^~iltsm*K";
  var SPAWNER_GLYPHS = "K*m";
  var COLOR_KEYS = Core.COLOR_KEYS || ["fg1", "fg2", "bg1", "bg2", "water", "water_hi", "special", "special2"];
  var COLOR_LABELS = {
    fg1: "Foreground 1", fg2: "Foreground 2", bg1: "Background 1", bg2: "Background 2",
    water: "Water", water_hi: "Water highlight", special: "Special 1", special2: "Special 2"
  };
  var FALLBACK_COLORS = {
    fg1: "#808080", fg2: "#808080", bg1: "#808080", bg2: "#808080",
    water: "#0080bf", water_hi: "#e6e6e6", special: "#0080bf", special2: "#e6e6e6"
  };
  var PRESETS = [
    { name: "Native", colors: { fg1: [0.5, 0.5, 0.5], fg2: [0.5, 0.5, 0.5], bg1: [0.5, 0.5, 0.5], bg2: [0.5, 0.5, 0.5], water: [0, 0.5, 0.75], water_hi: [0.9, 0.9, 0.9], special: [0, 0.5, 0.75], special2: [0.9, 0.9, 0.9] } },
    { name: "Ember", colors: { fg1: "#cc5844", fg2: "#f0a35f", bg1: "#291319", bg2: "#4a2021", water: "#2f6178", water_hi: "#bde4e8", special: "#ff6b4f", special2: "#ffd379" } },
    { name: "Moss", colors: { fg1: "#6f855f", fg2: "#b4c77c", bg1: "#17211d", bg2: "#29372c", water: "#376f73", water_hi: "#c7e5c2", special: "#9e5f45", special2: "#e1c784" } },
    { name: "Moon", colors: { fg1: "#6874a0", fg2: "#c0bce8", bg1: "#11152b", bg2: "#25254b", water: "#31568f", water_hi: "#d9e4ff", special: "#a25f9e", special2: "#f0afd9" } },
    { name: "Ice", colors: { fg1: "#73a9c2", fg2: "#d4f3f4", bg1: "#0b1a2a", bg2: "#17354a", water: "#277caa", water_hi: "#e4ffff", special: "#7e6eb7", special2: "#f1d9ff" } },
    { name: "Dusk", colors: { fg1: "#a35f78", fg2: "#eab0a2", bg1: "#1c1025", bg2: "#41213b", water: "#34517e", water_hi: "#b8c9ed", special: "#d46a55", special2: "#f2d384" } },
    { name: "Cavern", colors: { fg1: "#726759", fg2: "#c4aa7d", bg1: "#100f12", bg2: "#292329", water: "#264f58", water_hi: "#9fc8bd", special: "#784a62", special2: "#d69b69" } }
  ];
  var CATEGORY_ORDER = ["Solid terrain", "Hazards & water", "Goals & items", "Animated scenery", "Background art", "Empty & utility"];
  var SCRIPT_COMPLETIONS = [
    { label: "map.on_enter", snippet: "map.on_enter(\"tile_id\", function(object, tile)\n  \nend)", detail: "Run when an object enters a tile sensor." },
    { label: "map.on_contact", snippet: "map.on_contact(\"tile_id\", function(object, tile)\n  \nend)", detail: "Run while an object contacts a tile." },
    { label: "map.on_leave", snippet: "map.on_leave(\"tile_id\", function(object, tile)\n  \nend)", detail: "Run when an object leaves a tile." },
    { label: "map.on_tick", snippet: "map.on_tick(function()\n  \nend)", detail: "Run once per deterministic map tick." },
    { label: "map.sensor", snippet: "map.sensor(\"tile_id\", {\n  tile_box = { left = 0, top = 0, right = 1, bottom = 1 },\n  object_box = \"body\",\n  objects = { \"alive_player\" },\n  contact_scope = \"cell\",\n  mirror_with_room = true,\n})", detail: "Configure contact geometry and object filters." },
    { label: "map.state", snippet: "map.state", detail: "Persistent table for this match." },
    { label: "map.tick", snippet: "map.tick()", detail: "Current deterministic map tick." },
    { label: "map.random", snippet: "map.random(1, 10)", detail: "Deterministic map RNG." },
    { label: "object:set_velocity", snippet: "object:set_velocity(0, 0)", detail: "Set an object's velocity." },
    { label: "object:add_velocity", snippet: "object:add_velocity(0, -1)", detail: "Add to an object's velocity." },
    { label: "object:set_velocity_limits", snippet: "object:set_velocity_limits({ max_x = 4, max_y = 4 }, 60)", detail: "Temporarily clamp velocity." },
    { label: "object:clear_velocity_limits", snippet: "object:clear_velocity_limits()", detail: "Clear scripted velocity limits." },
    { label: "tile:set_sprite", snippet: "tile:set_sprite(0, 30, { frame_count = 1, frame_ticks = 1 })", detail: "Temporarily change a tile sprite." },
    { label: "tile:reset_sprite", snippet: "tile:reset_sprite()", detail: "Restore a tile's declarative sprite." }
  ];

  var $ = function (id) { return document.getElementById(id); };
  var els = {};
  [
    "project-title", "save-label", "new-map-button", "import-button", "preview-button", "export-button", "help-button",
    "palette-panel", "tile-count", "tile-search", "tile-catalog", "selected-tile-preview", "selected-tile-name",
    "selected-tile-description", "selected-tile-glyph", "undo-button", "redo-button", "symmetry-toggle",
    "grid-toggle-button", "zoom-out-button", "zoom-label", "zoom-in-button", "arena-summary",
    "arena-track", "map-scroll", "map-stage", "room-atmosphere", "column-ruler", "row-ruler", "map-grid",
    "room-render-canvas", "coordinate-label", "tool-status", "validation-summary", "duplicate-room-button",
    "add-room-button", "room-tabs", "inspector-panel", "map-tab", "room-tab", "validation-tab", "validation-count",
    "map-inspector", "room-inspector", "validation-inspector", "map-name", "map-author", "map-id", "map-description",
    "description-count", "sort-order", "eggnogg-color-enabled", "eggnogg-color", "score-target", "respawn-limit", "format-badge", "format-title", "format-copy", "format-v2-toggle", "map-mode-button", "header-tile-lab-button", "header-script-button", "v2-tile-summary", "room-inspector-title", "room-position-copy",
    "room-id", "room-ambient", "reset-colors-button", "preview-mirror-colors-button", "palette-presets", "palette-name", "save-palette-button", "random-palette-button", "saved-palettes", "primary-color-fields",
    "custom-mirror-colors", "mirror-color-fields", "move-room-in-button", "move-room-out-button", "delete-room-button",
    "spawn-meter-fill", "spawn-budget-label", "spawn-budget-detail", "validation-heading", "validation-list",
    "fact-source-rooms", "fact-final-rooms", "mobile-rooms-button", "mobile-preview-button", "mobile-export-button", "new-map-dialog", "new-map-author",
    "confirm-new-map", "import-dialog", "drop-zone", "choose-files-button", "choose-folder-button", "package-file-input",
    "package-folder-input", "import-result", "confirm-import-button", "export-dialog", "export-dialog-copy", "package-tree",
    "export-ready-title", "export-ready-summary", "download-json-button", "download-map-button", "copy-map-button",
    "download-package-button", "help-dialog", "toast-region", "live-region",
    "tile-lab-dialog", "close-tile-lab-button", "tile-lab-done-button", "new-v2-tile-button", "duplicate-v2-tile-button", "move-v2-tile-up", "move-v2-tile-down", "delete-v2-tile-button", "v2-tile-list", "v2-asset-list", "tile-pixel-canvas", "pixel-frame-timeline", "tile-paint-color", "tile-paint-color-button", "tile-paint-color-swatch", "tile-paint-color-label", "tile-paint-color-popover", "tile-paint-color-plane", "tile-paint-color-hue", "pixel-undo-button", "pixel-redo-button", "pixel-flip-x", "pixel-flip-y", "pixel-rotate", "pixel-onion-skin", "tile-seed-art-button", "tile-clear-button", "tile-png-input", "tile-asset-status", "tile-lab-status", "tile-frame-prev", "tile-frame-next", "tile-frame-add", "tile-frame-duplicate", "tile-frame-delete", "tile-frame-move-left", "tile-frame-move-right", "tile-frame-label", "tile-animation-canvas", "tile-animation-tick", "tile-animation-playing", "tile-animation-readout", "tile-hitbox-canvas", "tile-hitbox-details",
    "v2-tile-id", "v2-tile-symbol", "v2-tile-name", "v2-sprite-sheet", "v2-sprite-index", "v2-cell-w", "v2-cell-h", "v2-padding", "v2-frame-count", "v2-frame-ticks", "v2-animation", "v2-collision", "v2-native-glyph", "v2-layer", "v2-native-visual", "v2-offset-x", "v2-offset-y", "v2-scale-x", "v2-scale-y", "v2-angle", "v2-tint", "v2-tint-button", "v2-tint-swatch", "v2-tint-label", "v2-tint-popover", "v2-tint-plane", "v2-tint-hue", "v2-tint-alpha", "v2-force-x", "v2-force-y", "v2-force-mode", "v2-max-speed-x", "v2-max-speed-y", "v2-mirror-with-room", "v2-random-phase",
    "script-dialog", "close-script-button", "script-done-button", "script-template-button", "script-format-button", "map-lua-editor", "script-autocomplete", "script-reference-list", "script-byte-count", "remove-script-button", "script-builder-tile", "script-builder-event", "script-builder-objects", "script-builder-action", "script-builder-insert", "script-api-search", "script-sensor-left", "script-sensor-top", "script-sensor-right", "script-sensor-bottom", "script-sensor-object-box", "script-sensor-scope"
  ].forEach(function (id) { els[id] = $(id); });

  var state = {
    document: null,
    roomIndex: 0,
    selectedGlyph: "@",
    tool: "pencil",
    symmetry: false,
    previewMirrored: false,
    showGrid: true,
    zoom: 1,
    autoFitZoom: true,
    focusCell: { row: 0, col: 0 },
    undo: [],
    redo: [],
    pendingImport: null,
    dirtySinceExport: false,
    savedTimer: null,
    pointer: null,
    atlasReady: false,
    roomRenderRequest: 0,
    importRequest: 0,
    paletteAppearanceKey: null,
    appearancePreviewBank: "primary",
    validation: { valid: false, errors: [], warnings: [], issues: [] },
    idWasEdited: false,
    tileLabIndex: -1,
    tileLabSnapshot: null,
    tilePixels: null,
    tileArtFrame: 0,
    tilePainting: false,
    tileEraser: false,
    pixelTool: "pencil",
    pixelStart: null,
    pixelPreview: null,
    pixelUndo: [],
    pixelRedo: [],
    hitboxRenderRequest: 0,
    hitboxLastTime: 0,
    scriptSnapshot: null,
    colorPickers: {},
    savedPalettes: []
  };

  function clone(value) {
    if (Core.deepClone) return Core.deepClone(value);
    return JSON.parse(JSON.stringify(value));
  }

  function normalizedPalette(colors) {
    var normalized = {};
    COLOR_KEYS.forEach(function (key) {
      normalized[key] = Core.colorToHex && Core.colorToHex(colors && colors[key]) || FALLBACK_COLORS[key];
    });
    return normalized;
  }

  function loadSavedPalettes() {
    try {
      var parsed = JSON.parse(localStorage.getItem(PALETTE_STORAGE_KEY) || "[]");
      if (!Array.isArray(parsed)) return [];
      return parsed.slice(0, 24).filter(function (entry) {
        return entry && typeof entry.name === "string" && entry.name.trim() && entry.colors && typeof entry.colors === "object";
      }).map(function (entry) {
        return { name: entry.name.trim().slice(0, 32), colors: normalizedPalette(entry.colors) };
      });
    } catch (ignore) {
      return [];
    }
  }

  function savePaletteLibrary() {
    try { localStorage.setItem(PALETTE_STORAGE_KEY, JSON.stringify(state.savedPalettes)); }
    catch (error) { toast("Palette was not saved", "Browser local storage is unavailable.", "warning"); }
  }

  function escapeHtml(value) {
    return String(value === undefined || value === null ? "" : value).replace(/[&<>"']/g, function (character) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;" }[character];
    });
  }

  function rooms() {
    if (!state.document) return [];
    if (Array.isArray(state.document.rooms)) return state.document.rooms;
    return Object.keys(state.document.rooms || {}).map(function (id) {
      var value = state.document.rooms[id];
      if (!value.id) value.id = id;
      return value;
    });
  }

  function activeRoom() {
    var list = rooms();
    if (!list.length) return null;
    state.roomIndex = Math.max(0, Math.min(state.roomIndex, list.length - 1));
    return list[state.roomIndex];
  }

  function rowArray(room, row) {
    if (!room || !Array.isArray(room.grid)) return null;
    if (typeof room.grid[row] === "string") room.grid[row] = room.grid[row].split("");
    return room.grid[row];
  }

  function utf8Length(value) {
    if (window.TextEncoder) return new TextEncoder().encode(String(value || "")).length;
    return unescape(encodeURIComponent(String(value || ""))).length;
  }

  function hsvToHex(hue, saturation, value) {
    var c = value * saturation; var part = ((hue % 360) + 360) % 360 / 60; var x = c * (1 - Math.abs(part % 2 - 1)); var rgb;
    if (part < 1) rgb = [c, x, 0]; else if (part < 2) rgb = [x, c, 0]; else if (part < 3) rgb = [0, c, x]; else if (part < 4) rgb = [0, x, c]; else if (part < 5) rgb = [x, 0, c]; else rgb = [c, 0, x];
    var m = value - c;
    return "#" + rgb.map(function (channel) { return Math.round((channel + m) * 255).toString(16).padStart(2, "0"); }).join("");
  }

  function hexToHsv(hex) {
    var match = /^#?([0-9a-f]{6})$/i.exec(String(hex || "")); if (!match) return null;
    var value = parseInt(match[1], 16); var r = ((value >> 16) & 255) / 255; var g = ((value >> 8) & 255) / 255; var b = (value & 255) / 255;
    var max = Math.max(r, g, b); var min = Math.min(r, g, b); var delta = max - min; var hue = 0;
    if (delta) { if (max === r) hue = 60 * (((g - b) / delta) % 6); else if (max === g) hue = 60 * ((b - r) / delta + 2); else hue = 60 * ((r - g) / delta + 4); }
    if (hue < 0) hue += 360;
    return { h: hue, s: max ? delta / max : 0, v: max };
  }

  function setupCustomColorPicker(name, onChange) {
    var prefix = name === "paint" ? "tile-paint-color" : "v2-tint";
    var input = els[prefix]; var button = els[prefix + "-button"]; var swatch = els[prefix + "-swatch"]; var label = els[prefix + "-label"];
    var popover = els[prefix + "-popover"]; var plane = els[prefix + "-plane"]; var hue = els[prefix + "-hue"]; var stateValue = hexToHsv(input.value) || { h: 0, s: 0, v: 1 };
    var picker = { input: input, button: button, swatch: swatch, label: label, popover: popover, plane: plane, hue: hue, hsv: stateValue, onChange: onChange };
    state.colorPickers[name] = picker;
    function drawPlane() {
      var context = plane.getContext("2d"); context.clearRect(0, 0, plane.width, plane.height); context.fillStyle = hsvToHex(picker.hsv.h, 1, 1); context.fillRect(0, 0, plane.width, plane.height);
      var white = context.createLinearGradient(0, 0, plane.width, 0); white.addColorStop(0, "#fff"); white.addColorStop(1, "rgba(255,255,255,0)"); context.fillStyle = white; context.fillRect(0, 0, plane.width, plane.height);
      var black = context.createLinearGradient(0, 0, 0, plane.height); black.addColorStop(0, "rgba(0,0,0,0)"); black.addColorStop(1, "#000"); context.fillStyle = black; context.fillRect(0, 0, plane.width, plane.height);
      context.strokeStyle = picker.hsv.v > 0.55 ? "#000" : "#fff"; context.lineWidth = 3; context.beginPath(); context.arc(picker.hsv.s * (plane.width - 1), (1 - picker.hsv.v) * (plane.height - 1), 6, 0, Math.PI * 2); context.stroke();
    }
    function apply(hex, speak) {
      var hsv = hexToHsv(hex); if (!hsv) { input.setAttribute("aria-invalid", "true"); return; }
      input.removeAttribute("aria-invalid"); picker.hsv = hsv; hue.value = String(Math.round(hsv.h)); input.value = hex.toLowerCase(); label.textContent = input.value; swatch.style.background = input.value; drawPlane();
      if (speak !== false && picker.onChange) picker.onChange(input.value);
    }
    function chooseFromPlane(event) {
      var rect = plane.getBoundingClientRect(); picker.hsv.s = Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width)); picker.hsv.v = 1 - Math.max(0, Math.min(1, (event.clientY - rect.top) / rect.height)); apply(hsvToHex(picker.hsv.h, picker.hsv.s, picker.hsv.v));
    }
    button.addEventListener("click", function () { var open = popover.hidden; document.querySelectorAll(".custom-color-popover").forEach(function (entry) { entry.hidden = true; }); document.querySelectorAll(".custom-color-button").forEach(function (entry) { entry.setAttribute("aria-expanded", "false"); }); popover.hidden = !open; button.setAttribute("aria-expanded", open ? "true" : "false"); if (open) drawPlane(); });
    plane.addEventListener("pointerdown", function (event) { plane.setPointerCapture(event.pointerId); chooseFromPlane(event); }); plane.addEventListener("pointermove", function (event) { if (event.buttons) chooseFromPlane(event); });
    plane.addEventListener("keydown", function (event) { var changed = true; if (event.key === "ArrowLeft") picker.hsv.s -= .01; else if (event.key === "ArrowRight") picker.hsv.s += .01; else if (event.key === "ArrowUp") picker.hsv.v += .01; else if (event.key === "ArrowDown") picker.hsv.v -= .01; else changed = false; if (changed) { event.preventDefault(); picker.hsv.s = Math.max(0, Math.min(1, picker.hsv.s)); picker.hsv.v = Math.max(0, Math.min(1, picker.hsv.v)); apply(hsvToHex(picker.hsv.h, picker.hsv.s, picker.hsv.v)); } });
    hue.addEventListener("input", function () { picker.hsv.h = Number(hue.value); apply(hsvToHex(picker.hsv.h, picker.hsv.s, picker.hsv.v)); });
    input.addEventListener("input", function () { if (/^#[0-9a-f]{6}$/i.test(input.value)) apply(input.value); else input.setAttribute("aria-invalid", "true"); });
    var presetHost = document.querySelector('[data-color-presets="' + name + '"]');
    ["#ffffff", "#808080", "#111111", "#ff5945", "#ffd166", "#77cf99", "#36a6d9", "#8267d8", "#d45aa5", "#8b5a3c"].forEach(function (color) { var preset = document.createElement("button"); preset.type = "button"; preset.style.background = color; preset.title = color; preset.setAttribute("aria-label", "Choose " + color); preset.addEventListener("click", function () { apply(color); }); presetHost.appendChild(preset); });
    picker.set = function (hex) { apply(hex, false); };
    apply(input.value, false);
  }

  function normalizeId(value) {
    var result = Core.normalizeMapId ? Core.normalizeMapId(value) : String(value || "").toLowerCase().replace(/[^a-z0-9._-]+/g, "_").replace(/^[-.]+/, "").slice(0, 63) || "custom_map";
    if (result.charAt(0) === "_") result = ("map" + result).slice(0, 63);
    if (/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(result)) result = ("map_" + result).slice(0, 63);
    return result;
  }

  function ensureDocumentShape(doc) {
    if (!doc || typeof doc !== "object" || Array.isArray(doc)) doc = Core.createDefaultDocument ? Core.createDefaultDocument() : {};
    doc.format = doc.format || "eggnogg-map/v1";
    doc.id = doc.id || "untitled_map";
    doc.name = doc.name || "Untitled Map";
    if (typeof doc.author !== "string") doc.author = "Mapmaker";
    if (typeof doc.description !== "string") doc.description = "";
    if (!Number.isInteger(doc.sortOrder)) doc.sortOrder = Number.isInteger(doc.sort_order) ? doc.sort_order : 0;
    if (!doc.rules || typeof doc.rules !== "object" || Array.isArray(doc.rules)) doc.rules = {};
    if (!doc.rules.mode) doc.rules.mode = "swords";
    if (!doc.rules.roundEndRooms) doc.rules.roundEndRooms = doc.rules.round_end_rooms || "inner_only";
    if (doc.rules.scoreTarget === undefined) doc.rules.scoreTarget = doc.rules.score_target === undefined ? null : doc.rules.score_target;
    if (doc.rules.eggnoggColor === undefined && doc.rules.eggnogg_color !== undefined) doc.rules.eggnoggColor = doc.rules.eggnogg_color;
    delete doc.rules.eggnogg_color;
    if (doc.rules.armedRespawnLimit === undefined) doc.rules.armedRespawnLimit = doc.rules.armed_respawn_limit === undefined ? 4 : doc.rules.armed_respawn_limit;
    if (!doc.layout || typeof doc.layout !== "object" || Array.isArray(doc.layout)) doc.layout = { kind: "mirrored_source_rooms", roomFormat: "vanilla_33x12", order: [] };
    doc.layout.kind = "mirrored_source_rooms";
    doc.layout.roomFormat = doc.layout.roomFormat || doc.layout.room_format || "vanilla_33x12";
    if (!Array.isArray(doc.rooms)) {
      doc.rooms = Object.keys(doc.rooms || {}).map(function (id) {
        var room = doc.rooms[id] || {};
        room.id = room.id || id;
        return room;
      });
    }
    doc.rooms = doc.rooms.map(function (room) { return room && typeof room === "object" && !Array.isArray(room) ? room : {}; });
    if (!doc.rooms.length) doc.rooms.push(newRoom("center", true));
    doc.rooms.forEach(function (room, index) {
      room.id = room.id || (index ? "outer_" + index : "center");
      if (!Array.isArray(room.grid) || room.grid.length !== ROWS) room.grid = blankGrid();
      room.grid = room.grid.map(function (row) {
        var chars = Array.isArray(row) ? row.slice(0, COLS) : String(row || "").split("").slice(0, COLS);
        while (chars.length < COLS) chars.push(" ");
        return chars;
      });
      if (room.ambient === undefined) room.ambient = "none";
      if (room.appearance !== undefined && (!room.appearance || typeof room.appearance !== "object" || Array.isArray(room.appearance))) delete room.appearance;
    });
    doc.layout.order = doc.rooms.map(function (room) { return room.id; });
    return doc;
  }

  function blankGrid() {
    if (Core.createBlankGrid) return Core.createBlankGrid(" ");
    return Array.from({ length: ROWS }, function () { return new Array(COLS).fill(" "); });
  }

  function playableGrid() {
    var grid = blankGrid();
    var col;
    for (col = 0; col < COLS; col += 1) {
      grid[ROWS - 2][col] = "@";
      grid[ROWS - 1][col] = "@";
    }
    for (col = 5; col <= 10; col += 1) grid[7][col] = "@";
    for (col = 22; col <= 27; col += 1) grid[7][col] = "@";
    return grid;
  }

  function newRoom(id, playable) {
    return {
      id: id,
      grid: playable ? playableGrid() : blankGrid(),
      ambient: "none"
    };
  }

  function makeNewDocument(author) {
    var doc = ensureDocumentShape(Core.createDefaultDocument ? Core.createDefaultDocument() : null);
    doc.format = "eggnogg-map/v1";
    doc.id = "untitled_map";
    doc.name = "Untitled Map";
    doc.author = author || "Mapmaker";
    doc.description = "";
    doc.sortOrder = 0;
    doc.rules = { mode: "swords", roundEndRooms: "inner_only", scoreTarget: null, armedRespawnLimit: 4 };
    doc.rooms = [newRoom("center", false), newRoom("outer_1", false)];
    doc.layout = { kind: "mirrored_source_rooms", roomFormat: "vanilla_33x12", order: doc.rooms.map(function (r) { return r.id; }) };
    return doc;
  }

  function announce(message) {
    els["live-region"].textContent = "";
    window.setTimeout(function () { els["live-region"].textContent = message; }, 20);
  }

  function toast(title, detail, level) {
    var node = document.createElement("div");
    node.className = "toast" + (level ? " is-" + level : "");
    var icon = document.createElement("span");
    icon.setAttribute("aria-hidden", "true");
    icon.textContent = level === "error" ? "!" : level === "warning" ? "△" : "✓";
    var copy = document.createElement("span");
    var strong = document.createElement("strong");
    var small = document.createElement("small");
    strong.textContent = title;
    small.textContent = detail || "";
    copy.append(strong, small);
    node.append(icon, copy);
    els["toast-region"].appendChild(node);
    window.setTimeout(function () { node.remove(); }, 4200);
    announce(title + (detail ? ". " + detail : ""));
  }

  function openDialog(dialog) {
    if (!dialog) return;
    if (dialog.showModal) dialog.showModal();
    else dialog.setAttribute("open", "");
  }

  function closeDialog(dialog) {
    if (!dialog) return;
    if (dialog.close) dialog.close();
    else dialog.removeAttribute("open");
  }

  function markChanged() {
    state.dirtySinceExport = true;
    els["save-label"].textContent = "Saving locally… · Not exported";
    window.clearTimeout(state.savedTimer);
    state.savedTimer = window.setTimeout(function () {
      try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(state.document));
        els["save-label"].textContent = "Saved locally · Not exported";
      } catch (error) {
        els["save-label"].textContent = "Local save unavailable";
      }
    }, 180);
  }

  function historySnapshot(label) {
    return { document: clone(state.document), roomIndex: state.roomIndex, idWasEdited: state.idWasEdited, label: label || "Edit" };
  }

  function pushHistory(snapshot, label) {
    snapshot.label = label || snapshot.label || "Edit";
    state.undo.push(snapshot);
    if (state.undo.length > 200) state.undo.shift();
    state.redo = [];
    markChanged();
    updateHistoryButtons();
  }

  function commit(label, mutate) {
    var before = historySnapshot(label);
    mutate();
    if (JSON.stringify(before.document) !== JSON.stringify(state.document) || before.roomIndex !== state.roomIndex) pushHistory(before, label);
    renderAll();
  }

  function undo() {
    if (!state.undo.length) return;
    var entry = state.undo.pop();
    state.redo.push(historySnapshot(entry.label));
    state.document = ensureDocumentShape(entry.document);
    state.roomIndex = Math.min(entry.roomIndex, rooms().length - 1);
    state.idWasEdited = entry.idWasEdited === undefined ? true : !!entry.idWasEdited;
    markChanged();
    renderAll();
    announce("Undid " + entry.label);
  }

  function redo() {
    if (!state.redo.length) return;
    var entry = state.redo.pop();
    state.undo.push(historySnapshot(entry.label));
    state.document = ensureDocumentShape(entry.document);
    state.roomIndex = Math.min(entry.roomIndex, rooms().length - 1);
    state.idWasEdited = entry.idWasEdited === undefined ? true : !!entry.idWasEdited;
    markChanged();
    renderAll();
    announce("Redid " + entry.label);
  }

  function updateHistoryButtons() {
    els["undo-button"].disabled = !state.undo.length;
    els["redo-button"].disabled = !state.redo.length;
    els["undo-button"].title = state.undo.length ? "Undo " + state.undo[state.undo.length - 1].label : "Nothing to undo";
    els["redo-button"].title = state.redo.length ? "Redo " + state.redo[state.redo.length - 1].label : "Nothing to redo";
  }

  function tileMeta(glyph) {
    var lookup = Core.TILE_BY_GLYPH || {};
    var custom = state.document && state.document.tileset && Array.isArray(state.document.tileset.tiles) &&
      state.document.tileset.tiles.find(function (tile) { return tile && tile.symbol === glyph; });
    if (custom) return {
      glyph: glyph,
      label: custom.name || custom.id || "Custom V2 tile",
      category: "animated-scenery",
      description: "V2 built-in atlas tile " + (custom.sprite_sheet || "builtin:tiles") + " #" + (custom.sprite_index || 0) + ".",
      physics: (custom.collision || "native").replace(/_/g, " ")
    };
    return lookup[glyph] || (Core.TILE_METADATA || Core.TILES || []).find(function (item) { return item.glyph === glyph; }) || {
      glyph: glyph, label: glyph === " " ? "Erase / open" : "Glyph " + glyph, category: "utility", description: "Native map glyph."
    };
  }

  function categoryFor(meta) {
    var category = String(meta.category || "").toLowerCase();
    if (category === "solid-terrain") return "Solid terrain";
    if (category === "hazard" || category === "water-effect" || category === "water") return "Hazards & water";
    if (category === "goal-item" || category === "gameplay" || category === "objective") return "Goals & items";
    if (category === "animated-scenery" || category === "interactive") return "Animated scenery";
    if (category === "utility" || category === "terrain") return "Empty & utility";
    return "Background art";
  }

  function cssCategory(meta) {
    var group = categoryFor(meta);
    if (group === "Hazards & water") return String(meta.category).toLowerCase() === "water" ? "water" : "hazard";
    if (group === "Goals & items") return "objective";
    return group === "Solid terrain" ? "terrain" : "prop";
  }

  function labelForGlyph(glyph) {
    if (glyph === " ") return "space, erase or open cell";
    var meta = tileMeta(glyph);
    return meta.label + ", glyph " + glyph;
  }

  function selectGlyph(glyph) {
    state.selectedGlyph = glyph;
    var meta = tileMeta(glyph);
    els["selected-tile-name"].textContent = meta.label || "Native tile";
    els["selected-tile-description"].textContent = (meta.description || "Native map glyph.") +
      (meta.physics ? " · " + meta.physics : "");
    els["selected-tile-glyph"].textContent = glyph === " " ? "SP" : glyph;
    document.querySelectorAll(".tile-button").forEach(function (button) {
      button.setAttribute("aria-selected", button.dataset.glyph === glyph ? "true" : "false");
    });
    renderGlyphPreview(els["selected-tile-preview"], glyph, 48);
    updateToolStatus();
    announce("Selected " + labelForGlyph(glyph));
  }

  function previewTicks() {
    var milliseconds = window.performance && typeof window.performance.now === "function" ? window.performance.now() : Date.now();
    return milliseconds * 0.06;
  }

  function paletteAppearanceKey() {
    var room = activeRoom();
    return room ? state.appearancePreviewBank + ":" +
      JSON.stringify(resolvedAppearance(room, state.appearancePreviewBank)) : "";
  }

  function renderGlyphPreview(host, glyph, size) {
    host.textContent = "";
    var canvas = document.createElement("canvas");
    canvas.width = size || 40;
    canvas.height = size || 40;
    canvas.setAttribute("aria-hidden", "true");
    host.appendChild(canvas);
    function fallback() {
      var ctx = canvas.getContext("2d");
      ctx.imageSmoothingEnabled = false;
      ctx.fillStyle = "#201619";
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = glyph === " " ? "#7f6d69" : "#f2e9e6";
      ctx.font = "bold " + Math.floor(canvas.width * 0.48) + "px monospace";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(glyph === " " ? "×" : glyph, canvas.width / 2, canvas.height / 2);
    }
    if (Atlas && Atlas.renderGlyph) {
      try {
        Promise.resolve(Atlas.renderGlyph(canvas, glyph, {
          size: size || 40,
          appearance: resolvedAppearance(activeRoom(), state.appearancePreviewBank),
          tileset: state.document && state.document.tileset,
          time: previewTicks()
        })).catch(fallback);
        return;
      } catch (ignore) {}
    }
    fallback();
  }

  function renderPalette() {
    var query = els["tile-search"].value.trim().toLowerCase();
    var tiles = (Core.TILE_METADATA || Core.TILES || []).filter(function (item, index, list) {
      return item && typeof item.glyph === "string" && list.findIndex(function (other) { return other.glyph === item.glyph; }) === index;
    });
    if (state.document && state.document.tileset && Array.isArray(state.document.tileset.tiles)) {
      state.document.tileset.tiles.forEach(function (tile) {
        if (tile && typeof tile.symbol === "string" && tile.symbol.length === 1) tiles.push(tileMeta(tile.symbol));
      });
    }
    if (!tiles.length && Core.GLYPHS) tiles = Core.GLYPHS.map(tileMeta);
    tiles = tiles.filter(function (item) {
      var haystack = [item.glyph, item.label, item.description, item.category, item.physics].join(" ").toLowerCase();
      return !query || haystack.indexOf(query) >= 0;
    });
    els["tile-catalog"].textContent = "";
    CATEGORY_ORDER.forEach(function (group) {
      var grouped = tiles.filter(function (tile) { return categoryFor(tile) === group; });
      if (!grouped.length) return;
      var heading = document.createElement("div");
      heading.className = "tile-group-label";
      heading.textContent = group;
      els["tile-catalog"].appendChild(heading);
      grouped.forEach(function (meta) {
        var button = document.createElement("button");
        button.type = "button";
        button.className = "tile-button" + (meta.glyph === " " || meta.glyph === "." ? " is-empty" : "");
        button.dataset.glyph = meta.glyph;
        button.setAttribute("role", "option");
        button.setAttribute("aria-label", labelForGlyph(meta.glyph) + ". " + (meta.description || "") +
          (meta.physics ? " " + meta.physics + "." : ""));
        button.setAttribute("aria-selected", meta.glyph === state.selectedGlyph ? "true" : "false");
        button.title = (meta.label || "Native tile") + " (" + (meta.glyph === " " ? "space" : meta.glyph) + ")\n" +
          (meta.physics ? meta.physics + "\n" : "") + (meta.description || "");
        var preview = document.createElement("span");
        preview.className = "tile-atlas-preview";
        var code = document.createElement("code");
        code.textContent = meta.glyph === " " ? "SP" : meta.glyph;
        button.append(preview, code);
        button.addEventListener("click", function () { selectGlyph(meta.glyph); });
        els["tile-catalog"].appendChild(button);
        renderGlyphPreview(preview, meta.glyph, 40);
      });
    });
    els["tile-count"].textContent = tiles.length + (tiles.length === 1 ? " tile" : " tiles");
    state.paletteAppearanceKey = paletteAppearanceKey();
  }

  function refreshPalettePreviews() {
    var key = paletteAppearanceKey();
    if (key === state.paletteAppearanceKey) return;
    state.paletteAppearanceKey = key;
    els["tile-catalog"].querySelectorAll(".tile-button").forEach(function (button) {
      var host = button.querySelector(".tile-atlas-preview");
      if (host) renderGlyphPreview(host, button.dataset.glyph, 40);
    });
    renderGlyphPreview(els["selected-tile-preview"], state.selectedGlyph, 48);
  }

  function createGridControls() {
    els["column-ruler"].textContent = "";
    els["row-ruler"].textContent = "";
    for (var col = 0; col < COLS; col += 1) {
      var colLabel = document.createElement("span");
      colLabel.textContent = String(col + 1);
      els["column-ruler"].appendChild(colLabel);
    }
    for (var row = 0; row < ROWS; row += 1) {
      var rowLabel = document.createElement("span");
      rowLabel.textContent = String(row + 1);
      els["row-ruler"].appendChild(rowLabel);
    }
    els["map-grid"].querySelectorAll(".map-row").forEach(function (node) { node.remove(); });
    for (var r = 0; r < ROWS; r += 1) {
      var rowNode = document.createElement("div");
      rowNode.className = "map-row";
      rowNode.setAttribute("role", "row");
      for (var c = 0; c < COLS; c += 1) {
        var cell = document.createElement("button");
        cell.type = "button";
        cell.className = "map-cell";
        cell.dataset.row = String(r);
        cell.dataset.col = String(c);
        cell.setAttribute("role", "gridcell");
        cell.tabIndex = r === 0 && c === 0 ? 0 : -1;
        var fallback = document.createElement("span");
        fallback.className = "cell-glyph";
        fallback.setAttribute("aria-hidden", "true");
        cell.appendChild(fallback);
        bindCellEvents(cell);
        rowNode.appendChild(cell);
      }
      els["map-grid"].appendChild(rowNode);
    }
  }

  function cellAt(row, col) {
    return els["map-grid"].querySelector('.map-cell[data-row="' + row + '"][data-col="' + col + '"]');
  }

  function setFocusCell(row, col, focus) {
    row = Math.max(0, Math.min(ROWS - 1, row));
    col = Math.max(0, Math.min(COLS - 1, col));
    var old = cellAt(state.focusCell.row, state.focusCell.col);
    var next = cellAt(row, col);
    if (old) old.tabIndex = -1;
    state.focusCell = { row: row, col: col };
    if (next) {
      next.tabIndex = 0;
      if (focus) next.focus({ preventScroll: true });
    }
    updateCoordinates(row, col);
  }

  function updateCoordinates(row, col) {
    var room = activeRoom();
    var glyph = room && rowArray(room, row) ? rowArray(room, row)[col] : " ";
    els["coordinate-label"].textContent = "ROW " + (row + 1) + " · COL " + (col + 1) + " · " + (glyph === " " ? "SPACE" : glyph);
  }

  function resolvedAppearance(room, bankName) {
    if (Core.resolveRoomAppearance) {
      var resolved = Core.resolveRoomAppearance(state.document, room);
      var resolvedBank = resolved[bankName === "mirror" ? "mirror" : "primary"] || {};
      var exact = {};
      COLOR_KEYS.forEach(function (key) { exact[key] = Core.colorToHex(resolvedBank[key]) || FALLBACK_COLORS[key]; });
      return exact;
    }
    var defaults = Core.COLOR_DEFAULTS || Core.DEFAULT_COLORS || FALLBACK_COLORS;
    var defaultAppearance = state.document && state.document.defaults && (state.document.defaults.room ? state.document.defaults.room.appearance : state.document.defaults.appearance);
    var defaultPrimary = defaultAppearance && defaultAppearance.primary || {};
    var defaultMirror = defaultAppearance && defaultAppearance.mirror || defaultPrimary;
    var appearance = room && room.appearance;
    var primary = appearance && appearance.primary || {};
    var mirror = appearance && appearance.mirror;
    var selected;
    if (bankName === "mirror") {
      if (mirror) selected = Object.assign({}, defaultMirror, mirror);
      else if (appearance) selected = Object.assign({}, defaultPrimary, primary);
      else selected = defaultMirror;
    } else selected = Object.assign({}, defaultPrimary, primary);
    var bank = Object.assign({}, FALLBACK_COLORS, defaults, selected);
    COLOR_KEYS.forEach(function (key) {
      if (Core.colorToHex) bank[key] = Core.colorToHex(bank[key]) || FALLBACK_COLORS[key];
      else if (!/^#[0-9a-f]{6}$/i.test(bank[key] || "")) bank[key] = FALLBACK_COLORS[key];
    });
    return bank;
  }

  function updateAppearancePreviewControl() {
    var mirror = state.appearancePreviewBank === "mirror";
    els["preview-mirror-colors-button"].textContent = mirror ? "Preview: Mirror" : "Preview: Primary";
    els["preview-mirror-colors-button"].setAttribute("aria-pressed", mirror ? "true" : "false");
    els["preview-mirror-colors-button"].title = mirror ?
      "The canvas and tile previews are using the resolved mirror color bank." :
      "The canvas and tile previews are using the resolved primary color bank.";
  }

  function setAppearancePreviewBank(bankName, speak) {
    var next = bankName === "mirror" ? "mirror" : "primary";
    if (state.appearancePreviewBank === next) return;
    state.appearancePreviewBank = next;
    updateAppearancePreviewControl();
    renderGrid();
    renderRoomTabs();
    refreshPalettePreviews();
    if (speak !== false) announce("Previewing the " + next + " room color bank");
  }

  function renderRoomCanvas() {
    var room = activeRoom();
    if (!room) return;
    var sourceRooms = rooms();
    var centreRoomIndex = sourceRooms.length - 1;
    /* The editor always paints the authored source room. */
    var worldRoomIndex = state.previewMirrored ? centreRoomIndex + state.roomIndex : centreRoomIndex - state.roomIndex;
    var canvas = els["room-render-canvas"];
    var appearance = resolvedAppearance(room, state.appearancePreviewBank);
    var request = ++state.roomRenderRequest;
    canvas.hidden = false;
    function fallback() {
      if (request !== state.roomRenderRequest) return;
      var context = canvas.getContext("2d");
      context.clearRect(0, 0, canvas.width, canvas.height);
      context.fillStyle = appearance.bg1;
      context.fillRect(0, 0, canvas.width, canvas.height);
      document.body.classList.remove("atlas-ready");
    }
    if (Atlas && Atlas.renderRoom) {
      try {
        Promise.resolve(Atlas.renderRoom(canvas, room, {
          grid: room.grid,
          appearance: appearance,
          ambient: room.ambient,
          eggnoggColor: state.document.rules.eggnoggColor,
          tileset: state.document && state.document.tileset,
          mirrored: state.previewMirrored,
          /* Native terrain variation is seeded from the destination room and
           * column, not from the glyph. Source rooms are stored centre-out;
           * their authored copies run left from centre, while their mirrored
           * runtime copies run the same distance to the right. */
          worldRoomIndex: Math.max(0, worldRoomIndex),
          time: previewTicks()
        }))
          .then(function () { if (request === state.roomRenderRequest) document.body.classList.add("atlas-ready"); })
          .catch(function (error) {
            if (request === state.roomRenderRequest) {
              fallback();
              console.warn("Greggnogg atlas preview failed; glyph overlay remains available.", error);
            }
          });
        return;
      } catch (error) {
        console.warn("Greggnogg atlas preview failed; glyph overlay remains available.", error);
      }
    }
    fallback();
  }

  function renderGrid() {
    var room = activeRoom();
    if (!room) return;
    var issues = state.validation.issues || [];
    var errorCells = Object.create(null);
    issues.forEach(function (issue) {
      if (issue.severity === "error" && issue.roomId === room.id && Number.isInteger(issue.row) && Number.isInteger(issue.col)) errorCells[issue.row + ":" + issue.col] = true;
    });
    els["map-grid"].classList.toggle("show-grid", state.showGrid);
    els["map-grid"].classList.toggle("is-mirrored", state.previewMirrored);
    els["map-grid"].querySelectorAll(".map-cell").forEach(function (cell) {
      var row = Number(cell.dataset.row);
      var col = Number(cell.dataset.col);
      var glyph = rowArray(room, row)[col] || " ";
      var meta = tileMeta(glyph);
      cell.dataset.glyph = glyph;
      cell.dataset.category = cssCategory(meta);
      cell.classList.toggle("is-empty", glyph === " " || glyph === ".");
      cell.classList.toggle("is-approximate", APPROXIMATE_GLYPHS.indexOf(glyph) >= 0);
      cell.classList.toggle("has-error", !!errorCells[row + ":" + col]);
      cell.setAttribute("aria-label", "Row " + (row + 1) + " column " + (col + 1) + ", " + labelForGlyph(glyph));
      cell.title = "Row " + (row + 1) + ", column " + (col + 1) + ": " + labelForGlyph(glyph);
      var token = cell.querySelector(".cell-glyph");
      if (token) token.textContent = glyph === " " ? "" : glyph;
    });
    var appearance = resolvedAppearance(room, state.appearancePreviewBank);
    COLOR_KEYS.forEach(function (key) { els["map-grid"].parentElement.style.setProperty("--room-" + key, appearance[key]); });
    els["room-atmosphere"].dataset.ambient = String(room.ambient || "none");
    renderRoomCanvas();
    updateCoordinates(state.focusCell.row, state.focusCell.col);
  }

  function updateToolStatus() {
    var meta = tileMeta(state.selectedGlyph);
    var toolName = state.tool.charAt(0).toUpperCase() + state.tool.slice(1);
    els["tool-status"].textContent = toolName + " · " + (meta.label || "Native tile") + " (" + (state.selectedGlyph === " " ? "space" : state.selectedGlyph) + ")";
  }

  function setTool(tool) {
    state.tool = tool;
    document.querySelectorAll("[data-tool]").forEach(function (button) {
      var active = button.dataset.tool === tool;
      button.classList.toggle("is-active", active);
      button.setAttribute("aria-pressed", active ? "true" : "false");
    });
    updateToolStatus();
    announce("Selected " + tool + " tool");
  }

  function footprintValid(glyph, row, col, notifyUser) {
    if ((glyph === "G" || glyph === "L" || glyph === "N" || glyph === "Y") && (row < 3 || col === 0 || col === COLS - 1)) {
      if (notifyUser !== false) toast("Placement blocked", glyph + " needs three rows of headroom and side clearance.", "error");
      return false;
    }
    var minRow = glyph === "T" ? 3 : glyph === "t" ? 2 : glyph === "s" ? 1 : 0;
    if (row < minRow) {
      if (notifyUser !== false) toast("Placement blocked", glyph + " expands upward and can crash native map generation here.", "error");
      return false;
    }
    return true;
  }

  function spawnerCounts(grid) {
    var counts = { K: 0, sword: 0, mine: 0, total: 0 };
    grid.forEach(function (row) {
      row.forEach(function (glyph) {
        if (glyph === "K") counts.K += 1;
        else if (glyph === "*") counts.sword += 1;
        else if (glyph === "m") counts.mine += 1;
      });
    });
    counts.total = counts.K + counts.sword + counts.mine;
    return counts;
  }

  function prospectiveSpawnerValid(room, changes) {
    var before = spawnerCounts(room.grid);
    var grid = room.grid.map(function (row) { return row.slice(); });
    changes.forEach(function (change) { grid[change.row][change.col] = change.glyph; });
    var count = spawnerCounts(grid);
    if (count.K > 0 && count.total > 13) {
      if (before.K > 0 && before.total > 13 && count.K <= before.K && count.total <= before.total) return true;
      toast("Reset-spawn limit reached", "A room containing K may use at most 13 combined K, sword, and mine markers.", "error");
      return false;
    }
    return true;
  }

  function mirroredChanges(row, col, glyph) {
    var list = [{ row: row, col: col, glyph: glyph }];
    if (state.symmetry && col !== COLS - 1 - col) list.push({ row: row, col: COLS - 1 - col, glyph: glyph });
    return list;
  }

  function applyChanges(changes, silent) {
    var room = activeRoom();
    if (!room) return false;
    var unique = Object.create(null);
    changes = changes.filter(function (change) {
      var key = change.row + ":" + change.col;
      if (unique[key] || change.row < 0 || change.row >= ROWS || change.col < 0 || change.col >= COLS) return false;
      unique[key] = true;
      return true;
    });
    var invalid = changes.find(function (change) { return !footprintValid(change.glyph, change.row, change.col, false); });
    if (invalid) { footprintValid(invalid.glyph, invalid.row, invalid.col, true); return false; }
    if (!changes.length || !prospectiveSpawnerValid(room, changes)) return false;
    var changed = false;
    changes.forEach(function (change) {
      var row = rowArray(room, change.row);
      if (row[change.col] !== change.glyph) {
        row[change.col] = change.glyph;
        changed = true;
      }
    });
    if (changed && !silent) renderAfterMapEdit();
    return changed;
  }

  function renderAfterMapEdit() {
    validate();
    renderGrid();
    renderRoomTabs();
    renderArena();
    renderSpawner();
  }

  function floodFill(startRow, startCol, glyph) {
    var room = activeRoom();
    var target = rowArray(room, startRow)[startCol];
    if (target === glyph) return [];
    var queue = [[startRow, startCol]];
    var seen = Object.create(null);
    var changes = [];
    while (queue.length) {
      var point = queue.shift();
      var row = point[0];
      var col = point[1];
      var key = row + ":" + col;
      if (seen[key] || row < 0 || row >= ROWS || col < 0 || col >= COLS) continue;
      seen[key] = true;
      if (rowArray(room, row)[col] !== target) continue;
      changes.push({ row: row, col: col, glyph: glyph });
      queue.push([row - 1, col], [row + 1, col], [row, col - 1], [row, col + 1]);
    }
    if (state.symmetry) {
      changes.slice().forEach(function (change) { changes.push({ row: change.row, col: COLS - 1 - change.col, glyph: glyph }); });
    }
    return changes;
  }

  function lineCells(start, end, glyph, constrain) {
    var x0 = start.col, y0 = start.row, x1 = end.col, y1 = end.row;
    if (constrain) {
      if (Math.abs(x1 - x0) >= Math.abs(y1 - y0)) y1 = y0;
      else x1 = x0;
    }
    var dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    var dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    var error = dx + dy;
    var output = [];
    while (true) {
      output.push({ row: y0, col: x0, glyph: glyph });
      if (x0 === x1 && y0 === y1) break;
      var twice = 2 * error;
      if (twice >= dy) { error += dy; x0 += sx; }
      if (twice <= dx) { error += dx; y0 += sy; }
    }
    return output;
  }

  function rectangleCells(start, end, glyph) {
    var minRow = Math.min(start.row, end.row), maxRow = Math.max(start.row, end.row);
    var minCol = Math.min(start.col, end.col), maxCol = Math.max(start.col, end.col);
    var output = [];
    for (var col = minCol; col <= maxCol; col += 1) {
      output.push({ row: minRow, col: col, glyph: glyph });
      if (maxRow !== minRow) output.push({ row: maxRow, col: col, glyph: glyph });
    }
    for (var row = minRow + 1; row < maxRow; row += 1) {
      output.push({ row: row, col: minCol, glyph: glyph });
      if (maxCol !== minCol) output.push({ row: row, col: maxCol, glyph: glyph });
    }
    return output;
  }

  function withSymmetry(changes) {
    if (!state.symmetry) return changes;
    var output = changes.slice();
    changes.forEach(function (change) { output.push({ row: change.row, col: COLS - 1 - change.col, glyph: change.glyph }); });
    return output;
  }

  function showShapePreview(changes) {
    els["map-grid"].querySelectorAll(".is-shape-preview").forEach(function (cell) { cell.classList.remove("is-shape-preview"); });
    withSymmetry(changes).forEach(function (change) {
      var cell = cellAt(change.row, change.col);
      if (cell) cell.classList.add("is-shape-preview");
    });
  }

  function footprintCells(row, col, glyph) {
    var output = [];
    var largeArt = glyph === "G" || glyph === "L" || glyph === "N" || glyph === "Y";
    var up = glyph === "T" ? 3 : glyph === "t" ? 2 : glyph === "s" ? 1 : 0;
    var left = largeArt ? 1 : 0;
    var right = glyph === "G" ? 1 : 0;
    var firstRow = largeArt ? row - 3 : row - up;
    var lastRow = largeArt ? row - 1 : row;
    for (var r = firstRow; r <= lastRow; r += 1) for (var c = col - left; c <= col + right; c += 1) {
      if (r >= 0 && r < ROWS && c >= 0 && c < COLS) output.push({ row: r, col: c, glyph: glyph });
    }
    return output;
  }

  function hoverFootprint(cell) {
    if (state.pointer || state.tool === "line" || state.tool === "rectangle" || state.tool === "fill" || state.tool === "eyedropper") return;
    var glyph = state.tool === "eraser" ? " " : state.selectedGlyph;
    showShapePreview(footprintCells(Number(cell.dataset.row), Number(cell.dataset.col), glyph));
  }

  function clearShapePreview() {
    els["map-grid"].querySelectorAll(".is-shape-preview").forEach(function (cell) { cell.classList.remove("is-shape-preview"); });
  }

  function beginDraw(cell, event) {
    if (state.pointer || event.isPrimary === false) return;
    var row = Number(cell.dataset.row), col = Number(cell.dataset.col);
    setFocusCell(row, col, false);
    var erase = event.button === 2 || state.tool === "eraser";
    var glyph = erase ? " " : state.selectedGlyph;
    if (state.tool === "eyedropper" && !erase) {
      selectGlyph(rowArray(activeRoom(), row)[col]);
      setTool("pencil");
      return;
    }
    state.pointer = {
      id: event.pointerId,
      start: { row: row, col: col },
      last: { row: row, col: col },
      glyph: glyph,
      tool: state.tool,
      snapshot: historySnapshot(state.tool === "eraser" || erase ? "Erase" : state.tool.charAt(0).toUpperCase() + state.tool.slice(1)),
      changed: false,
      constrain: event.shiftKey
    };
    if (state.tool === "fill") {
      state.pointer.changed = applyChanges(floodFill(row, col, glyph), false);
      finishDraw();
    } else if (state.tool === "line" || state.tool === "rectangle") {
      showShapePreview([{ row: row, col: col, glyph: glyph }]);
    } else {
      state.pointer.changed = applyChanges(mirroredChanges(row, col, glyph), false) || state.pointer.changed;
    }
  }

  function continueDraw(cell, event) {
    if (!state.pointer || (event.pointerId !== undefined && event.pointerId !== state.pointer.id) || (event.buttons === 0 && event.pointerType !== "touch")) return;
    var row = Number(cell.dataset.row), col = Number(cell.dataset.col);
    var previous = state.pointer.last;
    state.pointer.last = { row: row, col: col };
    state.pointer.constrain = event.shiftKey;
    updateCoordinates(row, col);
    if (state.pointer.tool === "line") showShapePreview(lineCells(state.pointer.start, state.pointer.last, state.pointer.glyph, event.shiftKey));
    else if (state.pointer.tool === "rectangle") showShapePreview(rectangleCells(state.pointer.start, state.pointer.last, state.pointer.glyph));
    else if (state.pointer.tool === "pencil" || state.pointer.tool === "eraser") {
      state.pointer.changed = applyChanges(withSymmetry(lineCells(previous, state.pointer.last, state.pointer.glyph, false)), false) || state.pointer.changed;
    }
  }

  function finishDraw(event) {
    if (!state.pointer || (event && event.pointerId !== undefined && event.pointerId !== state.pointer.id)) return;
    if (state.pointer.tool === "line") state.pointer.changed = applyChanges(withSymmetry(lineCells(state.pointer.start, state.pointer.last, state.pointer.glyph, state.pointer.constrain)), false) || state.pointer.changed;
    else if (state.pointer.tool === "rectangle") state.pointer.changed = applyChanges(withSymmetry(rectangleCells(state.pointer.start, state.pointer.last, state.pointer.glyph)), false) || state.pointer.changed;
    clearShapePreview();
    if (state.pointer.changed) pushHistory(state.pointer.snapshot, state.pointer.snapshot.label);
    state.pointer = null;
  }

  function bindCellEvents(cell) {
    cell.addEventListener("contextmenu", function (event) { event.preventDefault(); });
    cell.addEventListener("pointerdown", function (event) {
      if (event.button !== 0 && event.button !== 2) return;
      event.preventDefault();
      beginDraw(cell, event);
    });
    cell.addEventListener("pointerenter", function (event) { if (state.pointer) continueDraw(cell, event); else hoverFootprint(cell); });
    cell.addEventListener("pointerleave", function () { if (!state.pointer) clearShapePreview(); });
    cell.addEventListener("pointermove", function (event) { updateCoordinates(Number(cell.dataset.row), Number(cell.dataset.col)); });
    cell.addEventListener("pointerup", finishDraw);
    cell.addEventListener("pointercancel", finishDraw);
    cell.addEventListener("focus", function () { setFocusCell(Number(cell.dataset.row), Number(cell.dataset.col), false); hoverFootprint(cell); });
    cell.addEventListener("blur", function () { if (!state.pointer) clearShapePreview(); });
    cell.addEventListener("keydown", function (event) {
      var row = Number(cell.dataset.row), col = Number(cell.dataset.col);
      if (event.key === "ArrowUp" || event.key === "ArrowDown" || event.key === "ArrowLeft" || event.key === "ArrowRight") {
        event.preventDefault();
        if (event.key === "ArrowUp") row -= 1;
        if (event.key === "ArrowDown") row += 1;
        if (event.key === "ArrowLeft") col -= 1;
        if (event.key === "ArrowRight") col += 1;
        setFocusCell(row, col, true);
      } else if (event.key === " " || event.key === "Enter" || event.key === "Backspace" || event.key === "Delete") {
        event.preventDefault();
        var glyph = event.key === "Backspace" || event.key === "Delete" ? " " : state.selectedGlyph;
        commit(glyph === " " ? "Erase cell" : "Paint cell", function () { applyChanges(mirroredChanges(row, col, glyph), true); });
      }
    });
  }

  function issue(severity, code, message, details) {
    details = details || {};
    return {
      severity: severity,
      code: code,
      message: message,
      path: details.path || null,
      roomId: details.roomId === undefined ? null : details.roomId,
      row: details.row === undefined ? null : details.row,
      col: details.col === undefined ? null : details.col
    };
  }

  function playabilityIssues() {
    var output = [];
    var list = rooms();
    var scoreObjects = { one: 0, two: 0, lights: 0 };
    if (utf8Length(state.document.name) > 127) output.push(issue("error", "name_bytes", "Map name exceeds the loader’s 127-byte UTF-8 limit.", { path: "name" }));
    if (utf8Length(state.document.author) > 127) output.push(issue("error", "author_bytes", "Author exceeds the loader’s 127-byte UTF-8 limit.", { path: "author" }));
    if (utf8Length(state.document.description) > 255) output.push(issue("error", "description_bytes", "Description exceeds the loader’s 255-byte UTF-8 limit.", { path: "description" }));
    if (state.document.id && utf8Length(state.document.id) > 63) output.push(issue("error", "id_bytes", "V1 map ID exceeds 63 UTF-8 bytes.", { path: "id" }));
    if (typeof state.document.id === "string" && state.document.id.charAt(0) === "_") output.push(issue("error", "ignored_folder_id", "The exported folder ID cannot start with an underscore because Yule ignores those map directories.", { path: "id" }));
    if (/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(state.document.id || "")) output.push(issue("error", "reserved_folder_id", "The exported folder ID is a reserved Windows device name.", { path: "id" }));

    list.forEach(function (room) {
      var counts = { goals: 0, one: 0, two: 0, lights: 0 };
      room.grid.forEach(function (row, rowIndex) {
        row.forEach(function (glyph, colIndex) {
          var minimum = glyph === "T" ? 3 : glyph === "t" ? 2 : glyph === "s" ? 1 : 0;
          if (minimum && rowIndex < minimum) {
            output.push(issue("error", "tentacle_headroom", glyph + " expands upward in native map generation and is unsafe above row " + (minimum + 1) + ".", {
              roomId: room.id, row: rowIndex, col: colIndex
            }));
          }
          if (glyph === "~" && rowIndex > 0) {
            var above = room.grid[rowIndex - 1][colIndex];
            /* Most parser-zero frame requests retain a nonzero tile-definition
             * default. The invisible sword/spike-ball markers really do retain
             * byte1=0, so a waterfall below can replace those gameplay cells. */
            if (above === "*" || above === "K") output.push(issue("warning", "waterfall_backfill", "Waterfall replaces the frame-zero spawn marker directly above it during map generation.", {
              roomId: room.id, row: rowIndex, col: colIndex
            }));
          }
          if (glyph === "E" || glyph === "^") counts.goals += 1;
          if (glyph === "1") counts.one += 1;
          if (glyph === "2") counts.two += 1;
          if (glyph === "l") counts.lights += 1;
        });
      });
      scoreObjects.one += counts.one;
      scoreObjects.two += counts.two;
      scoreObjects.lights += counts.lights;
    });

    var score = state.document.rules && state.document.rules.scoreTarget;
    if (score && (!!scoreObjects.one !== !!scoreObjects.two)) output.push(issue("warning", "score_targets_missing", "Score-target mode is set, but the map contains only one of the fixed team targets 1 and 2.", { path: "rules.score_target" }));
    if (!score && (scoreObjects.one || scoreObjects.two || scoreObjects.lights)) output.push(issue("warning", "score_target_disabled", "The map has score objects, but score_target is off; sword hits still score, but no score-match ending/display is configured.", { path: "rules.score_target" }));

    return output;
  }

  function validate() {
    var base;
    try {
      base = Core.validateDocument ? Core.validateDocument(state.document) : { valid: true, errors: [], warnings: [], issues: [] };
    } catch (error) {
      base = { valid: false, errors: [issue("error", "validator_exception", error.message)], warnings: [] };
    }
    var combined = (base.issues || []).concat(playabilityIssues());
    var seen = Object.create(null);
    combined = combined.filter(function (item) {
      var key = [item.severity, item.code, item.roomId, item.row, item.col].join("|");
      if (seen[key]) return false;
      seen[key] = true;
      return true;
    });
    state.validation = {
      errors: combined.filter(function (item) { return item.severity === "error"; }),
      warnings: combined.filter(function (item) { return item.severity !== "error"; }),
      issues: combined
    };
    state.validation.valid = state.validation.errors.length === 0;
    renderValidation();
    return state.validation;
  }

  function locationCopy(item) {
    var parts = [];
    if (item.path) parts.push(item.path);
    if (item.roomId) parts.push("room " + item.roomId);
    if (Number.isInteger(item.row)) parts.push("row " + (item.row + 1));
    if (Number.isInteger(item.col)) parts.push("col " + (item.col + 1));
    return parts.join(" · ") || item.code || "package";
  }

  function jumpToIssue(item) {
    if (item.roomId) {
      var index = rooms().findIndex(function (room) { return room.id === item.roomId; });
      if (index >= 0) state.roomIndex = index;
    }
    renderAll();
    if (Number.isInteger(item.row) && Number.isInteger(item.col)) {
      setFocusCell(item.row, item.col, true);
      els["map-scroll"].scrollIntoView({ block: "nearest", inline: "nearest" });
    } else if (item.path) {
      var control = item.path.indexOf("author") >= 0 ? els["map-author"] : item.path.indexOf("name") >= 0 ? els["map-name"] : item.path.indexOf("id") >= 0 ? els["map-id"] : null;
      if (control) { switchInspector("map"); control.focus(); }
    }
  }

  function renderValidation() {
    var errors = state.validation.errors.length;
    var warnings = state.validation.warnings.length;
    var total = errors + warnings;
    els["validation-count"].textContent = String(total);
    els["validation-list"].textContent = "";
    els["validation-summary"].className = "validation-summary " + (errors ? "has-errors" : warnings ? "has-warnings" : "is-valid");
    els["validation-summary"].innerHTML = "";
    var icon = document.createElement("span");
    icon.setAttribute("aria-hidden", "true");
    icon.textContent = errors ? "!" : warnings ? "△" : "✓";
    var copy = document.createElement("span");
    copy.textContent = errors ? errors + (errors === 1 ? " error" : " errors") : warnings ? warnings + (warnings === 1 ? " warning" : " warnings") : "Ready to export";
    els["validation-summary"].append(icon, copy);
    els["validation-heading"].textContent = errors ? "Fix " + errors + (errors === 1 ? " error" : " errors") : warnings ? "Ready with warnings" : "Ready to export";
    els["export-button"].disabled = errors > 0;
    els["preview-button"].disabled = errors > 0 || state.document.format !== "eggnogg-map/v1";
    els["mobile-preview-button"].disabled = errors > 0 || state.document.format !== "eggnogg-map/v1";
    if (!total) {
      var empty = document.createElement("div");
      empty.className = "validation-empty";
      empty.innerHTML = "<strong>Package checks pass</strong><span>The map is ready to export.</span>";
      els["validation-list"].appendChild(empty);
    } else {
      state.validation.issues.forEach(function (item) {
        var actionable = item.roomId || Number.isInteger(item.row) || item.path;
        var node = document.createElement(actionable ? "button" : "div");
        if (actionable) node.type = "button";
        node.className = "validation-item is-" + (item.severity === "error" ? "error" : "warning");
        var marker = document.createElement("span");
        marker.className = "validation-icon";
        marker.setAttribute("aria-hidden", "true");
        marker.textContent = item.severity === "error" ? "!" : "△";
        var text = document.createElement("span");
        var strong = document.createElement("strong");
        var small = document.createElement("small");
        strong.textContent = item.message;
        small.textContent = locationCopy(item);
        text.append(strong, small);
        node.append(marker, text);
        if (actionable) node.addEventListener("click", function () { jumpToIssue(item); });
        els["validation-list"].appendChild(node);
      });
    }
    els["fact-source-rooms"].textContent = String(rooms().length);
    els["fact-final-rooms"].textContent = String(Math.max(1, rooms().length * 2 - 1));
  }

  function thumbnail(room) {
    var node = document.createElement("span");
    node.className = "room-thumbnail";
    node.setAttribute("aria-hidden", "true");
    var appearance = resolvedAppearance(room, state.appearancePreviewBank);
    node.style.setProperty("--thumb-bg", appearance.bg1);
    node.style.setProperty("--thumb-fg", appearance.fg1);
    for (var sr = 0; sr < 4; sr += 1) {
      for (var sc = 0; sc < 11; sc += 1) {
        var sample = document.createElement("i");
        var fromRow = Math.min(ROWS - 1, sr * 3 + 1);
        var fromCol = Math.min(COLS - 1, sc * 3 + 1);
        var occupied = false;
        for (var rr = Math.max(0, fromRow - 1); rr <= Math.min(ROWS - 1, fromRow + 1); rr += 1) {
          for (var cc = Math.max(0, fromCol - 1); cc <= Math.min(COLS - 1, fromCol + 1); cc += 1) {
            if (room.grid[rr][cc] !== " " && room.grid[rr][cc] !== ".") occupied = true;
          }
        }
        sample.style.opacity = occupied ? "0.9" : "0";
        node.appendChild(sample);
      }
    }
    return node;
  }

  function renderRoomTabs() {
    els["room-tabs"].textContent = "";
    rooms().forEach(function (room, index) {
      var button = document.createElement("button");
      button.type = "button";
      button.className = "room-tab-button";
      button.setAttribute("role", "tab");
      button.setAttribute("aria-selected", index === state.roomIndex ? "true" : "false");
      var number = document.createElement("span");
      number.className = "room-number";
      number.textContent = String(index + 1);
      var copy = document.createElement("span");
      copy.className = "room-tab-copy";
      var strong = document.createElement("strong");
      strong.textContent = room.id;
      var small = document.createElement("small");
      small.textContent = index === 0 ? "Center" : "Distance " + index + " · " + String(room.ambient || "none");
      copy.append(strong, small);
      button.append(number, thumbnail(room), copy);
      button.addEventListener("click", function () {
        state.roomIndex = index;
        state.previewMirrored = false;
        renderAll();
        announce("Editing source room " + room.id);
      });
      els["room-tabs"].appendChild(button);
    });
    els["add-room-button"].disabled = rooms().length >= MAX_ROOMS;
    els["duplicate-room-button"].disabled = rooms().length >= MAX_ROOMS;
  }

  function renderArena() {
    var list = rooms();
    var finalRooms = list.slice(1).reverse().map(function (room, index) { return { room: room, mirrored: false, bank: "mirror", sourceIndex: list.length - 1 - index }; })
      .concat(list.length ? [{ room: list[0], mirrored: false, bank: "primary", sourceIndex: 0 }] : [])
      .concat(list.slice(1).map(function (room, index) { return { room: room, mirrored: true, bank: "primary", sourceIndex: index + 1 }; }));
    els["arena-track"].textContent = "";
    finalRooms.forEach(function (entry) {
      var button = document.createElement("button");
      button.type = "button";
      button.className = "arena-segment" + (entry.sourceIndex === 0 ? " is-center" : "") + (entry.mirrored ? " is-mirrored" : "") + (entry.sourceIndex === state.roomIndex && entry.mirrored === state.previewMirrored ? " is-active" : "");
      button.title = entry.room.id + " · " + entry.bank + " appearance bank";
      var label = document.createElement("span");
      label.textContent = entry.room.id;
      button.appendChild(label);
      button.addEventListener("click", function () {
        state.roomIndex = entry.sourceIndex;
        state.previewMirrored = entry.mirrored;
        state.appearancePreviewBank = entry.bank;
        renderAll();
      });
      els["arena-track"].appendChild(button);
    });
    els["arena-summary"].textContent = list.length + " source " + (list.length === 1 ? "room" : "rooms") + " · " + Math.max(1, list.length * 2 - 1) + " final " + (list.length === 1 ? "room" : "rooms");
  }

  function renderSpawner() {
    var room = activeRoom();
    if (!room) return;
    var counts = spawnerCounts(room.grid);
    els["spawn-budget-label"].textContent = counts.total + " / 13 reset spawners";
    els["spawn-budget-detail"].textContent = counts.K + " hazards · " + counts.sword + " swords · " + counts.mine + " mines";
    els["spawn-meter-fill"].style.height = Math.min(100, counts.total / 13 * 100) + "%";
    els["spawn-meter-fill"].style.background = counts.K && counts.total > 13 ? "var(--danger)" : counts.total > 10 ? "var(--warning)" : "var(--good)";
  }

  function setControlValue(control, value) {
    if (!control || document.activeElement === control) return;
    control.value = value === null || value === undefined ? "" : String(value);
  }

  function renderMapFields() {
    var doc = state.document;
    var isV2 = doc.format === "eggnogg-map/v2";
    var v2Tiles = isV2 && doc.tileset && Array.isArray(doc.tileset.tiles) ? doc.tileset.tiles : [];
    els["project-title"].textContent = doc.name || "Untitled Map";
    setControlValue(els["map-name"], doc.name);
    setControlValue(els["map-author"], doc.author);
    setControlValue(els["map-id"], doc.id);
    setControlValue(els["map-description"], doc.description);
    els["description-count"].textContent = utf8Length(doc.description) + " / 255 bytes";
    setControlValue(els["sort-order"], doc.sortOrder);
    document.querySelectorAll('input[name="combat-mode"]').forEach(function (input) { input.checked = input.value === doc.rules.mode; });
    document.querySelectorAll('input[name="round-end"]').forEach(function (input) { input.checked = input.value === doc.rules.roundEndRooms; });
    els["eggnogg-color-enabled"].checked = doc.rules.eggnoggColor !== undefined;
    els["eggnogg-color"].disabled = doc.rules.eggnoggColor === undefined;
    setControlValue(els["eggnogg-color"], Core.colorToHex(doc.rules.eggnoggColor) || "#ffc966");
    setControlValue(els["score-target"], doc.rules.scoreTarget);
    setControlValue(els["respawn-limit"], doc.rules.armedRespawnLimit);
    els["format-badge"].textContent = isV2 ? "V2" : "V1";
    els["format-title"].textContent = isV2 ? "Custom-content package" : "Native map package";
    els["format-copy"].textContent = isV2 ?
      "V2 enables custom PNG tiles and deterministic map.lua behavior. Tile Lab edits the declarative tile definitions; scripting mode never executes code in the browser." :
      "Uses EGGNOGG+’s built-in tiles and produces data.json plus data.map.";
    els["format-v2-toggle"].checked = isV2;
    els["header-tile-lab-button"].disabled = !isV2;
    els["header-script-button"].disabled = !isV2;
    els["v2-tile-summary"].hidden = !isV2;
    if (isV2) {
      var assetCount = Object.keys(doc.assets || {}).length;
      els["v2-tile-summary"].textContent = v2Tiles.length + " / 64 tile" + (v2Tiles.length === 1 ? "" : "s") + " · " + assetCount + " PNG asset" + (assetCount === 1 ? "" : "s") + " · map.lua " + (doc.mapLuaPresent ? "enabled" : "off") + ".";
    }
  }

  function nextV2Symbol() {
    var used = Object.create(null);
    var candidates = "$%&/<>[]{}~";
    ((state.document.tileset && state.document.tileset.tiles) || []).forEach(function (tile) { if (tile && tile.symbol) used[tile.symbol] = true; });
    for (var index = 0; index < candidates.length; index += 1) if (!used[candidates.charAt(index)]) return candidates.charAt(index);
    return null;
  }

  function enableV2TileLab() {
    els["format-v2-toggle"].checked = false;
    toast("V1 maps only", "V2 object authoring is intentionally disabled until its editor workflow is redesigned.", "warning");
  }

  function disableV2() {
    var tiles = state.document.tileset && state.document.tileset.tiles || [];
    if (tiles.length || Object.keys(state.document.assets || {}).length || state.document.mapLuaPresent) {
      els["format-v2-toggle"].checked = true;
      toast("V2 content is in use", "Delete custom tiles and map.lua before switching this project back to V1.", "warning");
      return;
    }
    commit("Switch to V1", function () {
      state.document.format = "eggnogg-map/v1";
      delete state.document.tileset;
      delete state.document.assets;
      delete state.document._tileArt;
      delete state.document.mapLua;
      delete state.document.mapLuaPresent;
    });
  }

  function v2Tiles() {
    if (!state.document.tileset || !Array.isArray(state.document.tileset.tiles)) state.document.tileset = { tiles: [] };
    return state.document.tileset.tiles;
  }

  function selectedV2Tile() {
    var list = v2Tiles();
    if (!list.length) return null;
    state.tileLabIndex = Math.max(0, Math.min(state.tileLabIndex, list.length - 1));
    return list[state.tileLabIndex];
  }

  function uniqueTileId(base, exceptIndex) {
    var stem = String(base || "custom_tile").toLowerCase().replace(/[^a-z0-9._-]+/g, "_").replace(/^[.-]+/, "").slice(0, 47) || "custom_tile";
    var used = v2Tiles().map(function (tile, index) { return index === exceptIndex ? "" : String(tile.id || "").toLowerCase(); });
    var value = stem;
    var number = 2;
    while (used.indexOf(value) >= 0) { value = (stem.slice(0, 42) + "_" + number).slice(0, 47); number += 1; }
    return value;
  }

  function newV2Tile() {
    if (v2Tiles().length >= 64) { toast("Tile limit reached", "Yule accepts at most 64 declarative tiles.", "warning"); return; }
    var symbol = nextV2Symbol();
    if (!symbol) { toast("Choose a symbol", "No unused quick symbol remains. Change an existing tile symbol first.", "warning"); return; }
    var tile = {
      id: uniqueTileId("custom_tile"), symbol: symbol, name: "Custom tile", sprite_sheet: "builtin:tiles",
      sprite_index: 45, frame_count: 1, frame_ticks: 1, animation: "loop", layer: 0,
      native_visual: "replace", collision: "pass_through", mirror_with_room: false, random_phase: false
    };
    v2Tiles().push(tile);
    state.tileLabIndex = v2Tiles().length - 1;
    state.document._tileArt = state.document._tileArt || {};
    state.document._tileArt[tile.id] = { frames: [new Array(256).fill(null)], dirty: false };
    state.tileArtFrame = 0;
    renderTileLab();
    markChanged();
  }

  function deleteV2Tile() {
    var tile = selectedV2Tile();
    if (!tile) return;
    var used = rooms().some(function (room) { return room.grid.some(function (row) { return (Array.isArray(row) ? row.join("") : String(row || "")).indexOf(tile.symbol) >= 0; }); });
    if (used) {
      if (!window.confirm("Delete “" + (tile.name || tile.id) + "” and erase every “" + tile.symbol + "” occurrence from all rooms?")) return;
      rooms().forEach(function (room) { room.grid = room.grid.map(function (row) { var chars = Array.isArray(row) ? row : String(row || "").split(""); return chars.map(function (glyph) { return glyph === tile.symbol ? " " : glyph; }); }); });
    }
    var art = state.document._tileArt && state.document._tileArt[tile.id]; var assetName = art && art.assetName;
    v2Tiles().splice(state.tileLabIndex, 1);
    if (state.selectedGlyph === tile.symbol) state.selectedGlyph = "@";
    if (state.document._tileArt) delete state.document._tileArt[tile.id];
    if (assetName && state.document.assets && !v2Tiles().some(function (entry) { return entry.sprite_sheet === assetName; })) delete state.document.assets[assetName];
    state.tileLabIndex = Math.min(state.tileLabIndex, v2Tiles().length - 1);
    renderTileLab(); renderPalette(); markChanged();
  }

  function duplicateV2Tile() {
    var tile = selectedV2Tile(); var symbol = nextV2Symbol();
    if (!tile || !symbol || v2Tiles().length >= 64) { toast("Cannot duplicate tile", "A free symbol and tile slot are required.", "warning"); return; }
    var copy = clone(tile); copy.id = uniqueTileId(tile.id + "_copy"); copy.symbol = symbol; copy.name = (tile.name || tile.id) + " copy";
    v2Tiles().splice(state.tileLabIndex + 1, 0, copy); state.tileLabIndex += 1;
    if (state.document._tileArt && state.document._tileArt[tile.id]) { state.document._tileArt[copy.id] = clone(state.document._tileArt[tile.id]); delete state.document._tileArt[copy.id].assetName; state.document._tileArt[copy.id].dirty = true; }
    renderTileLab(); renderPalette(); markChanged();
  }

  function moveV2Tile(delta) {
    var list = v2Tiles(); var target = state.tileLabIndex + delta;
    if (state.tileLabIndex < 0 || target < 0 || target >= list.length) return;
    var tile = list.splice(state.tileLabIndex, 1)[0]; list.splice(target, 0, tile); state.tileLabIndex = target; renderTileLab(); markChanged();
  }

  function renderAssetList() {
    var host = els["v2-asset-list"]; host.textContent = "";
    var names = Object.keys(state.document.assets || {}).sort();
    if (!names.length) { var empty = document.createElement("small"); empty.textContent = "No package-owned PNGs"; host.appendChild(empty); return; }
    names.forEach(function (name) {
      var row = document.createElement("div"); var label = document.createElement("span"); label.textContent = name;
      var remove = document.createElement("button"); remove.type = "button"; remove.className = "text-button"; remove.textContent = "Remove";
      remove.addEventListener("click", function () {
        if (v2Tiles().some(function (tile) { return tile.sprite_sheet === name; })) { toast("PNG is still in use", "Choose another sheet for every tile before removing " + name + ".", "warning"); return; }
        delete state.document.assets[name]; renderAssetList(); refreshSheetOptions(); markChanged();
      });
      row.append(label, remove); host.appendChild(row);
    });
  }

  function assetSources() {
    var output = {};
    Object.keys(state.document.assets || {}).forEach(function (name) { output[name] = state.document.assets[name]; });
    return output;
  }

  function refreshExternalAssets() {
    if (!Atlas || !Atlas.loadAssets) return Promise.resolve();
    return Atlas.loadAssets({ external: assetSources() }).then(function () { state.atlasReady = true; renderRoomCanvas(); renderPalette(); });
  }

  function renderTilePixels() {
    var canvas = els["tile-pixel-canvas"];
    var context = canvas.getContext("2d");
    var tile = selectedV2Tile();
    state.document._tileArt = state.document._tileArt || {};
    var art = tile && state.document._tileArt[tile.id];
    if (Array.isArray(art)) art = { frames: [art] };
    if (!art || !Array.isArray(art.frames) || !art.frames.length) art = { frames: [new Array(256).fill(null)] };
    state.tileArtFrame = Math.max(0, Math.min(state.tileArtFrame, art.frames.length - 1));
    var pixels = art.frames[state.tileArtFrame];
    if (!Array.isArray(pixels) || pixels.length !== 256) art.frames[state.tileArtFrame] = pixels = new Array(256).fill(null);
    var drawPixels = state.pixelPreview || pixels;
    if (tile) state.document._tileArt[tile.id] = art;
    state.tilePixels = pixels;
    context.clearRect(0, 0, 16, 16);
    if (els["pixel-onion-skin"] && els["pixel-onion-skin"].checked && state.tileArtFrame > 0) {
      context.globalAlpha = 0.28;
      art.frames[state.tileArtFrame - 1].forEach(function (color, index) { if (!color) return; context.fillStyle = color; context.fillRect(index % 16, Math.floor(index / 16), 1, 1); });
      context.globalAlpha = 1;
    }
    drawPixels.forEach(function (color, index) {
      if (!color) return;
      context.fillStyle = color;
      context.fillRect(index % 16, Math.floor(index / 16), 1, 1);
    });
    els["tile-frame-label"].textContent = "Frame " + (state.tileArtFrame + 1) + " / " + art.frames.length;
    els["tile-frame-prev"].disabled = state.tileArtFrame <= 0; els["tile-frame-next"].disabled = state.tileArtFrame >= art.frames.length - 1;
    els["tile-frame-delete"].disabled = art.frames.length <= 1;
    els["tile-frame-move-left"].disabled = state.tileArtFrame <= 0; els["tile-frame-move-right"].disabled = state.tileArtFrame >= art.frames.length - 1;
    if (!state.tilePainting) renderPixelTimeline(art);
    els["pixel-undo-button"].disabled = !state.pixelUndo.length; els["pixel-redo-button"].disabled = !state.pixelRedo.length;
  }

  function renderPixelTimeline(art) {
    var host = els["pixel-frame-timeline"]; if (!host) return; host.textContent = "";
    art.frames.forEach(function (frame, frameIndex) {
      var button = document.createElement("button"); button.type = "button"; button.className = frameIndex === state.tileArtFrame ? "is-active" : ""; button.title = "Edit frame " + (frameIndex + 1);
      var canvas = document.createElement("canvas"); canvas.width = 16; canvas.height = 16; var context = canvas.getContext("2d");
      frame.forEach(function (color, pixelIndex) { if (!color) return; context.fillStyle = color; context.fillRect(pixelIndex % 16, Math.floor(pixelIndex / 16), 1, 1); });
      var label = document.createElement("span"); label.textContent = String(frameIndex + 1); button.append(canvas, label);
      button.addEventListener("click", function () { state.pixelPreview = null; state.tileArtFrame = frameIndex; renderTilePixels(); }); host.appendChild(button);
    });
  }

  function renderTileAnimation() {
    var tile = selectedV2Tile(); var canvas = els["tile-animation-canvas"];
    if (!tile || !canvas) return;
    var tick = els["tile-animation-playing"].checked ? previewTicks() : Number(els["tile-animation-tick"].value);
    var count = Math.max(1, Number(tile.frame_count) || 1); var speed = Math.max(1, Number(tile.frame_ticks) || 1);
    var rawFrame = Math.floor(tick / speed); var frame;
    if (tile.animation === "once") frame = Math.min(count - 1, rawFrame);
    else if (tile.animation === "ping_pong" && count > 1) { var cycle = count * 2 - 2; var phase = rawFrame % cycle; frame = phase < count ? phase : cycle - phase; }
    else frame = rawFrame % count;
    els["tile-animation-readout"].textContent = "Frame " + (frame + 1) + " of " + count + " · tick " + Math.floor(tick);
    if (els["tile-animation-playing"].checked) els["tile-animation-tick"].value = String(Math.floor(tick) % 3601);
    var art = state.document._tileArt && state.document._tileArt[tile.id];
    if (Array.isArray(art)) art = { frames: [art] };
    if (art && Array.isArray(art.frames) && art.frames.length) {
      var context = canvas.getContext("2d"); context.imageSmoothingEnabled = false; context.clearRect(0, 0, 192, 192); drawPreviewChecker(context, 192);
      if (!drawPixelArtPreview(context, art, frame, 192, tile) && Atlas && Atlas.renderGlyph) Atlas.renderGlyph(canvas, tile.symbol, { size: 192, tileset: state.document.tileset, appearance: resolvedAppearance(activeRoom(), state.appearancePreviewBank), time: tick });
    } else if (Atlas && Atlas.renderGlyph) Atlas.renderGlyph(canvas, tile.symbol, { size: 192, tileset: state.document.tileset, appearance: resolvedAppearance(activeRoom(), state.appearancePreviewBank), time: tick });
  }

  function tileArtRecord() {
    var tile = selectedV2Tile(); if (!tile) return null;
    state.document._tileArt = state.document._tileArt || {};
    var art = state.document._tileArt[tile.id];
    if (Array.isArray(art)) art = { frames: [art] };
    if (!art || !Array.isArray(art.frames) || !art.frames.length) art = { frames: [new Array(256).fill(null)] };
    state.document._tileArt[tile.id] = art; return art;
  }

  function changeTileArtFrame(delta) { var art = tileArtRecord(); if (!art) return; state.tileArtFrame = Math.max(0, Math.min(art.frames.length - 1, state.tileArtFrame + delta)); renderTilePixels(); }
  function addTileArtFrame(duplicate) {
    var art = tileArtRecord(); var tile = selectedV2Tile(); if (!art || !tile || art.frames.length >= 256) return;
    var frame = duplicate ? art.frames[state.tileArtFrame].slice() : new Array(256).fill(null);
    art.frames.splice(state.tileArtFrame + 1, 0, frame); art.dirty = true; state.tileArtFrame += 1; tile.frame_count = art.frames.length; renderTileLab(); markChanged();
  }
  function deleteTileArtFrame() {
    var art = tileArtRecord(); var tile = selectedV2Tile(); if (!art || !tile || art.frames.length <= 1) return;
    art.frames.splice(state.tileArtFrame, 1); art.dirty = true; state.tileArtFrame = Math.min(state.tileArtFrame, art.frames.length - 1); tile.frame_count = art.frames.length; renderTileLab(); markChanged();
  }
  function moveTileArtFrame(delta) {
    var art = tileArtRecord(); if (!art) return; var target = state.tileArtFrame + delta;
    if (target < 0 || target >= art.frames.length) return;
    var frame = art.frames.splice(state.tileArtFrame, 1)[0]; art.frames.splice(target, 0, frame); art.dirty = true; state.tileArtFrame = target; renderTilePixels(); markChanged();
  }

  function drawPreviewChecker(context, size) {
    var step = size / 12; context.fillStyle = "#100a0c"; context.fillRect(0, 0, size, size); context.fillStyle = "#1d1417";
    for (var y = 0; y < 12; y += 1) for (var x = 0; x < 12; x += 1) if ((x + y) % 2) context.fillRect(x * step, y * step, step, step);
  }

  function drawPixelArtPreview(context, art, frame, size, tile) {
    if (!art || !Array.isArray(art.frames) || !art.frames.length) return false;
    var pixels = art.frames[Math.max(0, frame % art.frames.length)];
    if (!Array.isArray(pixels)) return false;
    if (!pixels.some(function (color) { return !!color; }) && !art.assetName && !art.dirty) return false;
    var source = document.createElement("canvas"); source.width = 16; source.height = 16; var sourceContext = source.getContext("2d");
    pixels.forEach(function (color, index) { if (!color) return; sourceContext.fillStyle = color; sourceContext.fillRect(index % 16, Math.floor(index / 16), 1, 1); });
    tile = tile || {}; var tint = tile.tint || [1, 1, 1, 1];
    sourceContext.globalCompositeOperation = "multiply"; sourceContext.fillStyle = Core.colorToHex ? Core.colorToHex(tint.slice(0, 3)) : "#ffffff"; sourceContext.fillRect(0, 0, 16, 16); sourceContext.globalCompositeOperation = "source-over";
    context.save(); context.imageSmoothingEnabled = false; context.globalAlpha = tint[3] === undefined ? 1 : Number(tint[3]);
    context.translate(size / 2 + (Number(tile.offset_x) || 0) * size / 16, size / 2 + (Number(tile.offset_y) || 0) * size / 16); context.rotate((Number(tile.angle_degrees) || 0) * Math.PI / 180); context.scale(Number(tile.scale_x) || 1, Number(tile.scale_y) || 1);
    context.drawImage(source, -size / 2, -size / 2, size, size); context.restore();
    return true;
  }

  function drawHitboxOverlay(context, tile, collision) {
    var size = 192; var hazard = collision === "hazard"; var solid = collision === "solid";
    if (solid || hazard) {
      context.fillStyle = hazard ? "rgba(255,83,64,.25)" : "rgba(119,207,153,.20)"; context.fillRect(2, 2, size - 4, size - 4);
      context.strokeStyle = hazard ? "#ff7770" : "#77cf99"; context.lineWidth = 5; context.strokeRect(4, 4, size - 8, size - 8);
      if (hazard) {
        context.strokeStyle = "rgba(255,119,112,.58)"; context.lineWidth = 2;
        for (var slash = -size; slash < size * 2; slash += 24) { context.beginPath(); context.moveTo(slash, 0); context.lineTo(slash + size, size); context.stroke(); }
      }
    } else if (collision === "pass_through") {
      context.strokeStyle = "rgba(242,233,230,.45)"; context.lineWidth = 3; context.setLineDash([9, 7]); context.strokeRect(5, 5, size - 10, size - 10); context.setLineDash([]);
    } else {
      context.strokeStyle = "#ffd166"; context.lineWidth = 4; context.setLineDash([12, 7]); context.strokeRect(4, 4, size - 8, size - 8); context.setLineDash([]);
    }
    context.strokeStyle = "rgba(255,255,255,.10)"; context.lineWidth = 1;
    for (var line = 0; line <= 16; line += 1) { var pos = line * 12; context.beginPath(); context.moveTo(pos, 0); context.lineTo(pos, size); context.stroke(); context.beginPath(); context.moveTo(0, pos); context.lineTo(size, pos); context.stroke(); }
    var forceX = Number(tile && tile.force_x); var forceY = Number(tile && tile.force_y);
    if (Number.isFinite(forceX) || Number.isFinite(forceY)) {
      forceX = Number.isFinite(forceX) ? forceX : 0; forceY = Number.isFinite(forceY) ? forceY : 0;
      var magnitude = Math.max(1, Math.abs(forceX), Math.abs(forceY)); var endX = 96 + forceX / magnitude * 64; var endY = 96 + forceY / magnitude * 64;
      context.strokeStyle = "#36a6d9"; context.fillStyle = "#36a6d9"; context.lineWidth = 5; context.beginPath(); context.moveTo(96, 96); context.lineTo(endX, endY); context.stroke();
      var angle = Math.atan2(endY - 96, endX - 96); context.beginPath(); context.moveTo(endX, endY); context.lineTo(endX - 14 * Math.cos(angle - .55), endY - 14 * Math.sin(angle - .55)); context.lineTo(endX - 14 * Math.cos(angle + .55), endY - 14 * Math.sin(angle + .55)); context.closePath(); context.fill();
    }
  }

  function renderHitboxPreview() {
    var canvas = els["tile-hitbox-canvas"]; var context = canvas && canvas.getContext("2d"); var tile = selectedV2Tile();
    if (!context) return;
    context.clearRect(0, 0, canvas.width, canvas.height); context.fillStyle = "#120c0e"; context.fillRect(0, 0, canvas.width, canvas.height);
    if (!tile) { els["tile-hitbox-details"].textContent = "Create or select a tile to inspect its collision."; return; }
    var request = ++state.hitboxRenderRequest; var collision = tile && (tile.collision || "native");
    var art = state.document._tileArt && state.document._tileArt[tile.id]; if (Array.isArray(art)) art = { frames: [art] };
    var frame = Math.floor(previewTicks() / Math.max(1, Number(tile.frame_ticks) || 1));
    var finish = function () { if (request === state.hitboxRenderRequest && selectedV2Tile() === tile) drawHitboxOverlay(context, tile, collision); };
    if (drawPixelArtPreview(context, art, frame, 192, tile)) finish();
    else if (Atlas && Atlas.renderGlyph) {
      Atlas.renderGlyph(canvas, tile.symbol, { size: 192, tileset: state.document.tileset, appearance: resolvedAppearance(activeRoom(), state.appearancePreviewBank), time: previewTicks() }).then(finish, finish);
    } else finish();
    var labels = { solid: "Solid cell", pass_through: "No hitbox", hazard: "Lethal cell", native: "Native collision" };
    var fallback = collision === "native" ? (tile.native_glyph || tile.symbol || "?") : collision === "solid" ? "@" : collision === "hazard" ? "X" : "x";
    var force = (tile.force_x !== undefined || tile.force_y !== undefined) ? ((tile.force_mode || "add") + " (" + (Number(tile.force_x) || 0) + ", " + (Number(tile.force_y) || 0) + ")") : "None";
    els["tile-hitbox-details"].innerHTML = "<div><strong>" + labels[collision] + "</strong><span>Runtime fallback <code>" + escapeHtml(fallback) + "</code></span></div><div><strong>Force</strong><span>" + escapeHtml(force) + "</span></div><div><strong>Draw layer</strong><span>" + (Number(tile.layer) === 1 ? "Background" : "Normal") + (tile.mirror_with_room ? " Â· mirrors" : "") + "</span></div>";
    document.querySelectorAll("[data-hitbox]").forEach(function (button) { button.classList.toggle("is-active", button.dataset.hitbox === collision); });
  }

  function setTileControl(id, value) {
    var control = els[id];
    if (!control) return;
    if (control.type === "checkbox") control.checked = !!value;
    else control.value = value === undefined || value === null ? "" : String(value);
    if (id === "v2-tint" && state.colorPickers.tint && /^#[0-9a-f]{6}$/i.test(control.value)) state.colorPickers.tint.set(control.value);
  }

  function refreshSheetOptions(selected) {
    var select = els["v2-sprite-sheet"];
    while (select.options.length > 4) select.remove(4);
    Object.keys(state.document.assets || {}).sort().forEach(function (name) {
      var option = document.createElement("option"); option.value = name; option.textContent = name; select.appendChild(option);
    });
    select.value = selected || "builtin:tiles";
  }

  function renderTileLab() {
    var list = v2Tiles();
    els["v2-tile-list"].textContent = "";
    list.forEach(function (tile, index) {
      var button = document.createElement("button");
      button.type = "button"; button.className = "v2-tile-item" + (index === state.tileLabIndex ? " is-selected" : "");
      button.setAttribute("role", "option"); button.setAttribute("aria-selected", index === state.tileLabIndex ? "true" : "false");
      var glyph = document.createElement("strong"); glyph.textContent = tile.symbol || "?";
      var copy = document.createElement("span"); copy.textContent = tile.name || tile.id || "Unnamed tile";
      var small = document.createElement("small"); small.textContent = tile.id || "missing id";
      copy.appendChild(small); button.append(glyph, copy);
      button.addEventListener("click", function () { state.tileLabIndex = index; state.tileArtFrame = 0; state.pixelPreview = null; state.pixelUndo = []; state.pixelRedo = []; renderTileLab(); });
      els["v2-tile-list"].appendChild(button);
    });
    var tile = selectedV2Tile();
    els["delete-v2-tile-button"].disabled = !tile;
    els["duplicate-v2-tile-button"].disabled = !tile || list.length >= 64;
    els["move-v2-tile-up"].disabled = !tile || state.tileLabIndex <= 0;
    els["move-v2-tile-down"].disabled = !tile || state.tileLabIndex >= list.length - 1;
    renderAssetList();
    document.querySelectorAll(".tile-fields input, .tile-fields select").forEach(function (control) { control.disabled = !tile; });
    if (!tile) {
      els["tile-lab-status"].textContent = "Create a tile to start drawing or importing a PNG.";
      renderTilePixels();
      return;
    }
    refreshSheetOptions(tile.sprite_sheet);
    setTileControl("v2-tile-id", tile.id); setTileControl("v2-tile-symbol", tile.symbol); setTileControl("v2-tile-name", tile.name);
    setTileControl("v2-sprite-index", tile.sprite_index === undefined ? 0 : tile.sprite_index);
    setTileControl("v2-cell-w", tile.cell_w === undefined ? 16 : tile.cell_w); setTileControl("v2-cell-h", tile.cell_h === undefined ? 16 : tile.cell_h); setTileControl("v2-padding", tile.padding || 0);
    setTileControl("v2-frame-count", tile.frame_count === undefined ? 1 : tile.frame_count);
    setTileControl("v2-frame-ticks", tile.frame_ticks === undefined ? 1 : tile.frame_ticks);
    setTileControl("v2-animation", tile.animation || "loop"); setTileControl("v2-collision", tile.collision || "native");
    setTileControl("v2-native-glyph", tile.native_glyph || ""); setTileControl("v2-layer", tile.layer || 0);
    setTileControl("v2-native-visual", tile.native_visual || "replace"); setTileControl("v2-offset-x", tile.offset_x || 0);
    setTileControl("v2-offset-y", tile.offset_y || 0); setTileControl("v2-scale-x", tile.scale_x === undefined ? 1 : tile.scale_x);
    setTileControl("v2-scale-y", tile.scale_y === undefined ? 1 : tile.scale_y); setTileControl("v2-angle", tile.angle_degrees || 0);
    setTileControl("v2-tint", tile.tint ? (Core.colorToHex ? Core.colorToHex(tile.tint.slice(0, 3)) : "#ffffff") : "#ffffff");
    setTileControl("v2-tint-alpha", tile.tint ? tile.tint[3] : 1);
    setTileControl("v2-force-x", tile.force_x); setTileControl("v2-force-y", tile.force_y);
    setTileControl("v2-force-mode", tile.force_mode || "add"); setTileControl("v2-max-speed-x", tile.max_speed_x); setTileControl("v2-max-speed-y", tile.max_speed_y);
    setTileControl("v2-mirror-with-room", tile.mirror_with_room); setTileControl("v2-random-phase", tile.random_phase);
    els["tile-asset-status"].textContent = /^builtin:/.test(tile.sprite_sheet || "") ? "Using an original built-in game atlas" : "Using packaged PNG: " + tile.sprite_sheet;
    els["tile-lab-status"].textContent = "Editing “" + (tile.name || tile.id) + "” · paint symbol “" + tile.symbol + "”.";
    renderTilePixels();
    renderTileAnimation(); renderHitboxPreview();
  }

  function openTileLab() {
    state.tileLabSnapshot = historySnapshot("Edit V2 tiles");
    if (!v2Tiles().length) state.tileLabIndex = -1;
    else state.tileLabIndex = Math.max(0, state.tileLabIndex);
    renderTileLab();
    document.body.classList.add("tile-mode-open");
    els["map-mode-button"].classList.remove("is-active"); els["map-mode-button"].setAttribute("aria-pressed", "false");
    els["header-tile-lab-button"].classList.add("is-active");
    openDialog(els["tile-lab-dialog"]);
  }

  function closeTileLab() {
    var artChanged = commitAllTileArt();
    if (state.tileLabSnapshot && JSON.stringify(state.tileLabSnapshot.document) !== JSON.stringify(state.document)) pushHistory(state.tileLabSnapshot, "Edit V2 tiles");
    state.tileLabSnapshot = null; document.body.classList.remove("tile-mode-open");
    els["header-tile-lab-button"].classList.remove("is-active"); els["map-mode-button"].classList.add("is-active"); els["map-mode-button"].setAttribute("aria-pressed", "true");
    closeDialog(els["tile-lab-dialog"]); renderAll(); if (artChanged) refreshExternalAssets().then(renderAll);
  }

  function updateSelectedTileField(key, value, optional) {
    var tile = selectedV2Tile();
    if (!tile) return;
    if (optional && (value === "" || value === null || value === undefined || Number.isNaN(value))) delete tile[key];
    else tile[key] = value;
    markChanged(); validate(); renderPalette(); renderRoomCanvas();
  }

  function pixelPoint(event) {
    var rect = els["tile-pixel-canvas"].getBoundingClientRect();
    return { x: Math.max(0, Math.min(15, Math.floor((event.clientX - rect.left) / rect.width * 16))), y: Math.max(0, Math.min(15, Math.floor((event.clientY - rect.top) / rect.height * 16))) };
  }

  function pushPixelHistory(before) {
    if (!before || JSON.stringify(before) === JSON.stringify(state.tilePixels)) return;
    state.pixelUndo.push({ tileId: selectedV2Tile().id, frame: state.tileArtFrame, pixels: before }); if (state.pixelUndo.length > 100) state.pixelUndo.shift(); state.pixelRedo = [];
    var art = tileArtRecord(); if (art) art.dirty = true; markChanged();
  }

  function pixelUndo(direction) {
    var source = direction < 0 ? state.pixelUndo : state.pixelRedo; var target = direction < 0 ? state.pixelRedo : state.pixelUndo; var entry = source.pop(); var tile = selectedV2Tile(); if (!entry || !tile || entry.tileId !== tile.id) return;
    var art = tileArtRecord(); target.push({ tileId: tile.id, frame: entry.frame, pixels: art.frames[entry.frame].slice() }); art.frames[entry.frame] = entry.pixels.slice(); art.dirty = true; state.tileArtFrame = entry.frame; state.pixelPreview = null; renderTilePixels(); markChanged();
  }

  function floodPixel(pixels, x, y, replacement) {
    var original = pixels[y * 16 + x] || null; if (original === replacement) return; var queue = [[x, y]]; var seen = {};
    while (queue.length) { var point = queue.pop(); var key = point[0] + "," + point[1]; if (seen[key] || point[0] < 0 || point[0] > 15 || point[1] < 0 || point[1] > 15) continue; seen[key] = true; var index = point[1] * 16 + point[0]; if ((pixels[index] || null) !== original) continue; pixels[index] = replacement; queue.push([point[0] - 1, point[1]], [point[0] + 1, point[1]], [point[0], point[1] - 1], [point[0], point[1] + 1]); }
  }

  function linePixels(pixels, start, end, color) {
    var x0 = start.x; var y0 = start.y; var x1 = end.x; var y1 = end.y; var dx = Math.abs(x1 - x0); var sx = x0 < x1 ? 1 : -1; var dy = -Math.abs(y1 - y0); var sy = y0 < y1 ? 1 : -1; var error = dx + dy;
    while (true) { pixels[y0 * 16 + x0] = color; if (x0 === x1 && y0 === y1) break; var twice = 2 * error; if (twice >= dy) { error += dy; x0 += sx; } if (twice <= dx) { error += dx; y0 += sy; } }
  }

  function shapePixels(base, start, end, tool, color) {
    var pixels = base.slice();
    if (tool === "line") linePixels(pixels, start, end, color);
    else { var left = Math.min(start.x, end.x); var right = Math.max(start.x, end.x); var top = Math.min(start.y, end.y); var bottom = Math.max(start.y, end.y); for (var x = left; x <= right; x += 1) { pixels[top * 16 + x] = color; pixels[bottom * 16 + x] = color; } for (var y = top; y <= bottom; y += 1) { pixels[y * 16 + left] = color; pixels[y * 16 + right] = color; } }
    return pixels;
  }

  function beginPixelDraw(event) {
    if (!state.tilePixels || !selectedV2Tile()) return;
    var point = pixelPoint(event); var tool = event.button === 2 ? "eraser" : state.pixelTool; var before = state.tilePixels.slice(); state.pixelStart = { point: point, before: before, tool: tool };
    if (tool === "eyedropper") { var color = state.tilePixels[point.y * 16 + point.x]; if (color && state.colorPickers.paint) state.colorPickers.paint.set(/^#/.test(color) ? color : rgbStringToHex(color)); state.pixelStart = null; return; }
    if (tool === "fill") { floodPixel(state.tilePixels, point.x, point.y, els["tile-paint-color"].value); pushPixelHistory(before); state.pixelStart = null; renderTilePixels(); return; }
    if (tool === "line" || tool === "rectangle") { state.pixelPreview = shapePixels(before, point, point, tool, els["tile-paint-color"].value); renderTilePixels(); return; }
    state.tilePixels[point.y * 16 + point.x] = tool === "eraser" ? null : els["tile-paint-color"].value; renderTilePixels();
  }

  function continuePixelDraw(event) {
    if (!state.pixelStart) return; var point = pixelPoint(event); var tool = state.pixelStart.tool;
    if (tool === "line" || tool === "rectangle") state.pixelPreview = shapePixels(state.pixelStart.before, state.pixelStart.point, point, tool, els["tile-paint-color"].value);
    else state.tilePixels[point.y * 16 + point.x] = tool === "eraser" ? null : els["tile-paint-color"].value;
    renderTilePixels();
  }

  function endPixelDraw() {
    if (!state.pixelStart) return; var before = state.pixelStart.before;
    if (state.pixelPreview) { var art = tileArtRecord(); art.frames[state.tileArtFrame] = state.pixelPreview.slice(); state.tilePixels = art.frames[state.tileArtFrame]; state.pixelPreview = null; }
    state.pixelStart = null; pushPixelHistory(before); renderTilePixels();
  }

  function rgbStringToHex(value) {
    var match = /rgba?\((\d+),\s*(\d+),\s*(\d+)/i.exec(value); if (!match) return "#ffffff";
    return "#" + [match[1], match[2], match[3]].map(function (number) { return Number(number).toString(16).padStart(2, "0"); }).join("");
  }

  function transformPixelFrame(kind) {
    var art = tileArtRecord(); if (!art) return; var before = art.frames[state.tileArtFrame].slice(); var after = new Array(256).fill(null);
    for (var y = 0; y < 16; y += 1) for (var x = 0; x < 16; x += 1) { var nx = kind === "flipX" ? 15 - x : kind === "rotate" ? 15 - y : x; var ny = kind === "flipY" ? 15 - y : kind === "rotate" ? x : y; after[ny * 16 + nx] = before[y * 16 + x]; }
    art.frames[state.tileArtFrame] = after; state.tilePixels = after; pushPixelHistory(before); renderTilePixels();
  }

  function seedArtFromCurrentSprite() {
    var tile = selectedV2Tile(); var art = tileArtRecord(); if (!tile || !art || !Atlas || !Atlas.renderGlyph) return;
    var preview = document.createElement("canvas"); var before = art.frames[state.tileArtFrame].slice();
    Atlas.renderGlyph(preview, tile.symbol, { size: 16, tileset: state.document.tileset, appearance: resolvedAppearance(activeRoom(), state.appearancePreviewBank), time: Number(els["tile-animation-tick"].value) || 0 }).then(function () {
      var data = preview.getContext("2d").getImageData(0, 0, 16, 16).data; var pixels = []; var visible = 0;
      for (var index = 0; index < 256; index += 1) { var offset = index * 4; if (!data[offset + 3]) pixels.push(null); else { visible += 1; pixels.push("rgba(" + data[offset] + "," + data[offset + 1] + "," + data[offset + 2] + "," + (data[offset + 3] / 255) + ")"); } }
      if (!visible) { toast("Nothing to copy", "The current sprite rendered as a transparent frame.", "warning"); return; }
      art.frames[state.tileArtFrame] = pixels; art.dirty = true; state.tilePixels = pixels; pushPixelHistory(before); renderTilePixels(); renderTileAnimation(); markChanged(); toast("Sprite copied", "The rendered game sprite is now editable pixel art.");
    }, function () { toast("Sprite unavailable", "The selected atlas frame could not be copied.", "error"); });
  }

  function safeAssetName(value) {
    var name = String(value || "tile.png").replace(/[^A-Za-z0-9._-]+/g, "_").replace(/^[.-]+/, "");
    if (!/\.png$/i.test(name)) name += ".png";
    return name.slice(-120) || "tile.png";
  }

  function uniqueAssetName(value) {
    var base = safeAssetName(value); var name = base; var count = 2;
    while (state.document.assets && state.document.assets[name] !== undefined) { name = base.replace(/\.png$/i, "_" + count + ".png"); count += 1; }
    return name;
  }

  function writeTileArtAsset(tile, art, force) {
    if (!tile || !art || !Array.isArray(art.frames) || (!force && !art.dirty)) return false;
    var existing = art.assetName || (!/^builtin:/.test(tile.sprite_sheet || "") && state.document.assets && state.document.assets[tile.sprite_sheet] ? tile.sprite_sheet : "");
    var name = existing || uniqueAssetName((tile.id || "tile") + ".png");
    var sheet = document.createElement("canvas"); sheet.width = art.frames.length * 16; sheet.height = 16;
    var sheetContext = sheet.getContext("2d");
    art.frames.forEach(function (frame, frameIndex) { frame.forEach(function (color, pixelIndex) { if (!color) return; sheetContext.fillStyle = color; sheetContext.fillRect(frameIndex * 16 + pixelIndex % 16, Math.floor(pixelIndex / 16), 1, 1); }); });
    state.document.assets = state.document.assets || {};
    state.document.assets[name] = sheet.toDataURL("image/png");
    tile.sprite_sheet = name; tile.sprite_index = 0; tile.frame_count = art.frames.length; tile.cell_w = 16; tile.cell_h = 16; tile.padding = 0;
    art.assetName = name; art.dirty = false;
    return true;
  }

  function commitAllTileArt() {
    var changed = false; var artById = state.document._tileArt || {};
    v2Tiles().forEach(function (tile) { if (writeTileArtAsset(tile, artById[tile.id], false)) changed = true; });
    return changed;
  }

  function importTilePng(file) {
    if (!file) return;
    if (file.size > 64 * 1024 * 1024) { toast("PNG too large", "Yule limits each external sheet to 64 MiB.", "error"); return; }
    var reader = new FileReader();
    reader.onload = function () {
      var tile = selectedV2Tile(); if (!tile) return;
      var name = uniqueAssetName(file.name);
      state.document.assets = state.document.assets || {}; state.document.assets[name] = reader.result;
      tile.sprite_sheet = name; tile.sprite_index = 0; tile.cell_w = 16; tile.cell_h = 16; tile.padding = 0;
      var image = new Image();
      image.onload = function () {
        var columns = Math.max(1, Math.floor(image.naturalWidth / 16)); var rows = Math.max(1, Math.floor(image.naturalHeight / 16)); var count = Math.min(256, columns * rows);
        var source = document.createElement("canvas"); source.width = image.naturalWidth; source.height = image.naturalHeight; var context = source.getContext("2d"); context.drawImage(image, 0, 0);
        var frames = [];
        for (var frameIndex = 0; frameIndex < count; frameIndex += 1) {
          var data = context.getImageData((frameIndex % columns) * 16, Math.floor(frameIndex / columns) * 16, 16, 16).data; var pixels = [];
          for (var pixel = 0; pixel < 256; pixel += 1) { var offset = pixel * 4; pixels.push(data[offset + 3] ? "rgba(" + data[offset] + "," + data[offset + 1] + "," + data[offset + 2] + "," + (data[offset + 3] / 255) + ")" : null); }
          frames.push(pixels);
        }
        state.document._tileArt = state.document._tileArt || {}; state.document._tileArt[tile.id] = { frames: frames, assetName: name, dirty: false }; state.tileArtFrame = 0; tile.frame_count = frames.length;
        refreshExternalAssets().then(renderTileLab); markChanged();
      };
      image.onerror = function () { refreshExternalAssets().then(renderTileLab); markChanged(); };
      image.src = reader.result;
    };
    reader.readAsDataURL(file);
  }

  function bytesToPngDataUrl(bytes) {
    return new Promise(function (resolve, reject) {
      var reader = new FileReader();
      reader.onload = function () { resolve(reader.result); };
      reader.onerror = function () { reject(reader.error || new Error("PNG could not be read.")); };
      reader.readAsDataURL(new Blob([bytes], { type: "image/png" }));
    });
  }

  function scriptStarter() {
    var tile = v2Tiles()[0];
    var symbol = tile && tile.symbol || "?";
    return "-- Greggnogg map.lua\n-- The Yule runtime is deterministic; use map.random(), not math.random().\n\nmap.state.contacts = map.state.contacts or 0\n\nmap.on_enter(\"" + symbol + "\", function(object, tile)\n  map.state.contacts = map.state.contacts + 1\n  -- object:add_velocity(0, -1)\nend)\n";
  }

  function scriptDiagnostics() {
    var source = els["map-lua-editor"].value;
    var count = utf8Length(source);
    var notes = [];
    if (source.indexOf("\0") >= 0) notes.push("NUL characters are not allowed");
    if (count > 262144) notes.push("over the 256 KiB runtime limit");
    if (/\b(?:require|dofile|loadfile|loadstring)\s*\(/.test(source) || /\b(?:io|os|package|debug|ffi|jit)\s*\./.test(source)) notes.push("contains APIs unavailable in the sandbox");
    if (/\bmath\.random\s*\(/.test(source)) notes.push("use map.random() for deterministic behavior");
    if (/\^/.test(source.replace(/--[^\n]*/g, "").replace(/\"(?:\\.|[^\"])*\"/g, ""))) notes.push("the power operator is rejected for cross-CPU rollback stability");
    var registrations = Object.create(null);
    source.replace(/map\.(on_enter|on_contact|on_leave)\s*\(\s*(["'])(.)\2/g, function (_, eventName, quote, symbol) { var key = eventName + ":" + symbol; registrations[key] = (registrations[key] || 0) + 1; return _; });
    if (Object.keys(registrations).some(function (key) { return registrations[key] > 1; })) notes.push("contains a duplicate event/tile registration that Yule rejects");
    els["script-byte-count"].textContent = count + " / 262144 bytes" + (notes.length ? " · " + notes.join(" · ") : " · syntax is checked by Yule on load");
    els["script-byte-count"].classList.toggle("is-error", count > 262144 || source.indexOf("\0") >= 0);
  }

  function renderScriptReference() {
    els["script-reference-list"].textContent = "";
    var query = String(els["script-api-search"].value || "").toLowerCase();
    SCRIPT_COMPLETIONS.filter(function (entry) { return !query || (entry.label + " " + entry.detail).toLowerCase().indexOf(query) >= 0; }).forEach(function (entry) {
      var button = document.createElement("button"); button.type = "button"; button.className = "script-reference-item";
      var code = document.createElement("code"); code.textContent = entry.label;
      var small = document.createElement("small"); small.textContent = entry.detail;
      button.append(code, small); button.addEventListener("click", function () { insertScript(entry.snippet); });
      els["script-reference-list"].appendChild(button);
    });
  }

  function renderScriptBuilder() {
    var select = els["script-builder-tile"]; var previous = select.value; select.textContent = "";
    v2Tiles().forEach(function (tile) { var option = document.createElement("option"); option.value = tile.id; option.textContent = (tile.symbol || "?") + " · " + (tile.name || tile.id); select.appendChild(option); });
    if (previous) select.value = previous;
    els["script-builder-insert"].disabled = !v2Tiles().length && els["script-builder-event"].value !== "on_tick";
  }

  function buildScriptHandler() {
    var eventName = els["script-builder-event"].value;
    var tile = v2Tiles().filter(function (entry) { return entry.id === els["script-builder-tile"].value; })[0];
    if (eventName !== "on_tick" && !tile) { toast("Create a tile first", "Tile event handlers need a V2 tile binding.", "warning"); return; }
    var objects = els["script-builder-objects"].value;
    var objectList = objects === "all" ? "{ \"alive_player\", \"dead_body\", \"sword\", \"hazard\" }" : "{ \"" + objects + "\" }";
    var action = els["script-builder-action"].value; var body;
    if (action === "launch") body = "  object:add_velocity(0, -2)";
    else if (action === "set_velocity") body = "  object:set_velocity(0, -4)";
    else if (action === "sprite") body = "  tile:set_sprite(" + (Number(tile && tile.sprite_index) || 0) + ", 30, { offset_y = 0 })";
    else if (action === "counter") body = "  map.state.activations = (map.state.activations or 0) + 1";
    else body = "  -- Add deterministic behavior here";
    var snippet;
    if (eventName === "on_tick") snippet = "map.on_tick(function()\n" + (action === "counter" ? body : "  -- map.tick() and map.random() are safe here") + "\nend)";
    else {
      var edges = ["left", "top", "right", "bottom"].map(function (edge) { return Number(els["script-sensor-" + edge].value); });
      if (edges.some(function (value) { return !Number.isFinite(value); }) || edges[2] <= edges[0] || edges[3] <= edges[1]) { toast("Invalid sensor box", "Right/bottom must be greater than left/top.", "error"); return; }
      snippet = "map.sensor(\"" + tile.symbol + "\", {\n  tile_box = { left = " + edges[0] + ", top = " + edges[1] + ", right = " + edges[2] + ", bottom = " + edges[3] + " },\n  object_box = \"" + els["script-sensor-object-box"].value + "\",\n  objects = " + objectList + ",\n  contact_scope = \"" + els["script-sensor-scope"].value + "\",\n  mirror_with_room = true,\n})\n\nmap." + eventName + "(\"" + tile.symbol + "\", function(object, tile)\n" + body + "\nend)";
    }
    insertScript((els["map-lua-editor"].value ? "\n\n" : "") + snippet);
  }

  function insertScript(text) {
    var editor = els["map-lua-editor"];
    var start = editor.selectionStart; var end = editor.selectionEnd;
    editor.setRangeText(text, start, end, "end");
    editor.focus();
    state.document.mapLua = editor.value; state.document.mapLuaPresent = true;
    markChanged(); scriptDiagnostics();
  }

  function showScriptAutocomplete() {
    var editor = els["map-lua-editor"];
    var prefix = editor.value.slice(0, editor.selectionStart).match(/[A-Za-z0-9_.:]*$/);
    var query = prefix ? prefix[0].toLowerCase() : "";
    var tileCompletions = v2Tiles().map(function (tile) { return { label: "tile " + tile.id, snippet: "\"" + tile.symbol + "\"", detail: "Binding “" + tile.symbol + "” · map." + state.document.id.toLowerCase() + ":" + tile.id.toLowerCase() }; });
    var matches = SCRIPT_COMPLETIONS.concat(tileCompletions).filter(function (entry) { return !query || (entry.label + " " + entry.detail).toLowerCase().indexOf(query) >= 0; });
    els["script-autocomplete"].textContent = "";
    matches.forEach(function (entry) {
      var button = document.createElement("button"); button.type = "button"; button.setAttribute("role", "option");
      var code = document.createElement("code"); code.textContent = entry.label;
      var small = document.createElement("small"); small.textContent = entry.detail;
      button.append(code, small);
      button.addEventListener("click", function () {
        if (query) editor.setSelectionRange(editor.selectionStart - query.length, editor.selectionStart);
        insertScript(entry.snippet); els["script-autocomplete"].hidden = true;
      });
      els["script-autocomplete"].appendChild(button);
    });
    els["script-autocomplete"].hidden = !matches.length;
    if (matches.length) els["script-autocomplete"].querySelector("button").focus();
  }

  function openScriptingMode() {
    state.scriptSnapshot = historySnapshot("Edit map.lua");
    els["map-lua-editor"].value = state.document.mapLuaPresent ? String(state.document.mapLua || "") : "";
    renderScriptReference(); renderScriptBuilder(); scriptDiagnostics(); document.body.classList.add("script-mode-open");
    els["map-mode-button"].classList.remove("is-active"); els["map-mode-button"].setAttribute("aria-pressed", "false"); els["header-script-button"].classList.add("is-active");
    openDialog(els["script-dialog"]);
  }

  function closeScriptingMode() {
    if (state.scriptSnapshot && JSON.stringify(state.scriptSnapshot.document) !== JSON.stringify(state.document)) pushHistory(state.scriptSnapshot, "Edit map.lua");
    state.scriptSnapshot = null; els["script-autocomplete"].hidden = true; document.body.classList.remove("script-mode-open");
    els["header-script-button"].classList.remove("is-active"); els["map-mode-button"].classList.add("is-active"); els["map-mode-button"].setAttribute("aria-pressed", "true");
    closeDialog(els["script-dialog"]); renderAll();
  }

  function removeScript() {
    delete state.document.mapLua; delete state.document.mapLuaPresent;
    els["map-lua-editor"].value = ""; scriptDiagnostics(); markChanged();
  }

  function renderRoomFields() {
    var room = activeRoom();
    if (!room) return;
    els["room-inspector-title"].textContent = room.id;
    els["room-position-copy"].textContent = state.roomIndex === 0 ? "Center room" : "Distance " + state.roomIndex + " from center";
    setControlValue(els["room-id"], room.id);
    var ambientValue = Number.isInteger(room.ambient) ? (Core.AMBIENTS || ["none", "bugs", "clouds", "art", "flies", "drips", "dust", "bats", "bubbles", "boil"])[room.ambient] : room.ambient;
    setControlValue(els["room-ambient"], ambientValue === "fumes" ? "boil" : ambientValue);
    els["custom-mirror-colors"].checked = !!(room.appearance && room.appearance.mirror);
    els["mirror-color-fields"].hidden = !els["custom-mirror-colors"].checked;
    updateAppearancePreviewControl();
    els["move-room-in-button"].disabled = state.roomIndex === 0;
    els["move-room-out-button"].disabled = state.roomIndex >= rooms().length - 1;
    els["delete-room-button"].disabled = rooms().length <= 1;
    renderColorFields();
    renderSpawner();
  }

  function renderColorFields() {
    var room = activeRoom();
    if (!room) return;
    var appearance = room.appearance || {};
    var exact = Core.resolveRoomAppearance ? Core.resolveRoomAppearance(state.document, room) : null;
    buildColorBank(els["primary-color-fields"], exact ? exact.primary : resolvedAppearance(room, "primary"), "primary");
    if (appearance.mirror) buildColorBank(els["mirror-color-fields"], exact ? exact.mirror : resolvedAppearance(room, "mirror"), "mirror");
    else els["mirror-color-fields"].textContent = "";
  }

  function buildColorBank(host, bank, bankName) {
    var room = activeRoom();
    if (!room) return;
    if (host.contains(document.activeElement)) return;
    host.textContent = "";
    COLOR_KEYS.forEach(function (key) {
      var sourceValue = bank[key];
      var hex = Core.colorToHex ? Core.colorToHex(sourceValue) : sourceValue;
      if (!hex) hex = FALLBACK_COLORS[key];
      var field = document.createElement("div");
      field.className = "color-field";
      var title = document.createElement("span");
      title.id = "color-" + bankName + "-" + key + "-title";
      title.textContent = COLOR_LABELS[key];
      var swatch = document.createElement("button");
      swatch.type = "button";
      swatch.className = "color-swatch-button";
      swatch.setAttribute("aria-labelledby", title.id);
      swatch.setAttribute("aria-expanded", "false");
      var swatchFill = document.createElement("span");
      swatchFill.className = "color-swatch-fill";
      swatch.appendChild(swatchFill);
      var input = document.createElement("input");
      input.type = "text";
      input.value = hex;
      input.className = "color-hex";
      input.pattern = "#[0-9A-Fa-f]{6}";
      input.spellcheck = false;
      input.setAttribute("aria-label", COLOR_LABELS[key] + " hexadecimal color");
      var rgb = document.createElement("input");
      rgb.type = "text";
      rgb.className = "color-rgb";
      rgb.placeholder = "0.0, 0.0, 0.0";
      rgb.spellcheck = false;
      rgb.setAttribute("aria-label", COLOR_LABELS[key] + " RGB channels from zero to one");
      var editor = document.createElement("div");
      editor.className = "color-channel-editor";
      editor.hidden = true;
      editor.setAttribute("aria-label", COLOR_LABELS[key] + " live RGB editor");
      var sliders = [];
      var numbers = [];
      var channelNames = ["R", "G", "B"];
      var numericValue = Array.isArray(sourceValue) ? sourceValue.slice(0, 3) :
        (Core.hexToColor ? Core.hexToColor(hex) : [0, 0, 0]);
      var fieldSnapshot = null;
      if (!numericValue || numericValue.length !== 3) numericValue = [0, 0, 0];

      function begin() {
        if (!fieldSnapshot) fieldSnapshot = historySnapshot("Change room palette");
        setAppearancePreviewBank(bankName, false);
      }
      function updateSliderTracks(channels) {
        var bytes = channels.map(function (channel) { return Math.round(channel * 255); });
        sliders.forEach(function (slider, index) {
          var low = bytes.slice();
          var high = bytes.slice();
          low[index] = 0;
          high[index] = 255;
          slider.style.background = "linear-gradient(to right, rgb(" + low.join(",") + "), rgb(" + high.join(",") + "))";
        });
      }
      function sync(channels) {
        var normalized = Core.colorToHex ? Core.colorToHex(channels) : hex;
        numericValue = channels.slice(0, 3);
        swatchFill.style.backgroundColor = normalized;
        input.value = normalized;
        rgb.value = numericValue.map(function (channel) { return Number(channel.toFixed(4)); }).join(", ");
        sliders.forEach(function (slider, index) {
          slider.value = numericValue[index];
          slider.setAttribute("aria-valuetext", Number(numericValue[index].toFixed(3)) + " of 1");
          numbers[index].value = Number(numericValue[index].toFixed(3));
        });
        updateSliderTracks(numericValue);
      }
      function applyChannels(channels) {
        begin();
        if (!channels || channels.length !== 3 || channels.some(function (channel) {
          return !Number.isFinite(channel) || channel < 0 || channel > 1;
        })) return false;
        input.removeAttribute("aria-invalid");
        rgb.removeAttribute("aria-invalid");
        room.appearance = room.appearance || {};
        room.appearance[bankName] = room.appearance[bankName] || {};
        room.appearance[bankName][key] = channels.slice(0, 3);
        sync(channels);
        validate(); renderGrid(); renderRoomTabs(); markChanged();
        return true;
      }
      function applyHex() {
        var normalized = /^#[0-9a-f]{6}$/i.test(input.value.trim()) ? input.value.trim() : null;
        var channels = normalized && Core.hexToColor ? Core.hexToColor(normalized) : null;
        if (!channels) { input.setAttribute("aria-invalid", "true"); return; }
        applyChannels(channels);
      }
      function applyRgbText() {
        var channels = rgb.value.trim().split(/[\s,]+/).filter(Boolean).map(Number);
        if (channels.length !== 3 || channels.some(function (channel) { return !Number.isFinite(channel) || channel < 0 || channel > 1; })) {
          rgb.setAttribute("aria-invalid", "true");
          return;
        }
        applyChannels(channels);
      }
      function finish() {
        var snapshot = fieldSnapshot;
        fieldSnapshot = null;
        if (snapshot && JSON.stringify(snapshot.document) !== JSON.stringify(state.document)) pushHistory(snapshot, "Change room palette");
        refreshPalettePreviews();
      }

      channelNames.forEach(function (channelName, index) {
        var row = document.createElement("label");
        row.className = "color-channel";
        var channelLabel = document.createElement("span");
        channelLabel.textContent = channelName;
        var slider = document.createElement("input");
        slider.type = "range";
        slider.min = "0";
        slider.max = "1";
        slider.step = "0.001";
        slider.setAttribute("aria-label", COLOR_LABELS[key] + " " + channelName + " channel");
        var number = document.createElement("input");
        number.type = "number";
        number.min = "0";
        number.max = "1";
        number.step = "0.001";
        number.setAttribute("aria-label", COLOR_LABELS[key] + " " + channelName + " numeric channel");
        sliders.push(slider);
        numbers.push(number);
        slider.addEventListener("pointerdown", begin);
        slider.addEventListener("keydown", begin);
        slider.addEventListener("input", function () {
          var channels = numericValue.slice();
          channels[index] = Number(slider.value);
          applyChannels(channels);
        });
        slider.addEventListener("pointerup", finish);
        number.addEventListener("focus", begin);
        number.addEventListener("input", function () {
          var value = Number(number.value);
          var channels = numericValue.slice();
          if (!Number.isFinite(value) || value < 0 || value > 1) {
            number.setAttribute("aria-invalid", "true");
            return;
          }
          number.removeAttribute("aria-invalid");
          channels[index] = value;
          applyChannels(channels);
        });
        row.append(channelLabel, slider, number);
        editor.appendChild(row);
      });

      swatch.addEventListener("click", function () {
        var opening = editor.hidden;
        if (opening) {
          host.querySelectorAll(".color-field.is-expanded").forEach(function (otherField) {
            if (otherField === field) return;
            otherField.classList.remove("is-expanded");
            var otherEditor = otherField.querySelector(".color-channel-editor");
            var otherSwatch = otherField.querySelector(".color-swatch-button");
            if (otherEditor) otherEditor.hidden = true;
            if (otherSwatch) otherSwatch.setAttribute("aria-expanded", "false");
          });
        }
        editor.hidden = !opening;
        field.classList.toggle("is-expanded", opening);
        swatch.setAttribute("aria-expanded", opening ? "true" : "false");
        if (opening) sliders[0].focus();
      });
      input.addEventListener("focus", begin);
      rgb.addEventListener("focus", begin);
      input.addEventListener("input", applyHex);
      rgb.addEventListener("input", applyRgbText);
      field.addEventListener("focusout", function (event) {
        if (!event.relatedTarget || !field.contains(event.relatedTarget)) finish();
      });
      field.append(title, swatch, input, rgb, editor);
      host.appendChild(field);
      sync(numericValue);
    });
  }

  function renderAll() {
    validate();
    renderMapFields();
    renderRoomFields();
    renderRoomTabs();
    renderArena();
    renderGrid();
    refreshPalettePreviews();
    updateHistoryButtons();
    updateToolStatus();
  }

  function switchInspector(name) {
    ["map", "room", "validation"].forEach(function (key) {
      var tab = els[key + "-tab"];
      var panel = els[key + "-inspector"];
      var active = key === name;
      tab.setAttribute("aria-selected", active ? "true" : "false");
      panel.hidden = !active;
      panel.classList.toggle("is-active", active);
    });
    document.body.classList.add("inspector-open");
  }

  function bindTextField(control, label, reader, writer, options) {
    options = options || {};
    var snapshot = null;
    var original = null;
    control.addEventListener("focus", function () {
      snapshot = historySnapshot(label);
      original = JSON.stringify(reader());
    });
    control.addEventListener("input", function () {
      writer(control.value);
      if (options.onInput) options.onInput(control.value);
      markChanged();
      validate();
      if (options.renderGrid) renderGrid();
      if (options.renderRooms) { renderRoomTabs(); renderArena(); }
      if (options.renderMap) renderMapFields();
    });
    control.addEventListener("blur", function () {
      if (snapshot && JSON.stringify(reader()) !== original) pushHistory(snapshot, label);
      snapshot = null;
      original = null;
      renderAll();
    });
  }

  function bindNumberField(control, label, reader, writer, nullable) {
    bindTextField(control, label, reader, function (value) {
      var parsed = value.trim() === "" && nullable ? null : Number(value);
      writer(value.trim() === "" && !nullable ? value : parsed);
    });
  }

  function nextRoomId(base) {
    var used = rooms().map(function (room) { return room.id.toLowerCase(); });
    var stem = normalizeId(base || "outer").slice(0, 56);
    var candidate = stem;
    var number = 1;
    while (used.indexOf(candidate.toLowerCase()) >= 0) {
      candidate = (stem + "_" + number).slice(0, 63);
      number += 1;
    }
    return candidate;
  }

  function bindControls() {
    document.querySelectorAll("[data-tool]").forEach(function (button) {
      button.addEventListener("click", function () { setTool(button.dataset.tool); });
    });
    els["tile-search"].addEventListener("input", renderPalette);
    els["undo-button"].addEventListener("click", undo);
    els["redo-button"].addEventListener("click", redo);
    els["format-v2-toggle"].addEventListener("change", function () { if (this.checked) enableV2TileLab(); else disableV2(); });
    els["header-tile-lab-button"].addEventListener("click", openTileLab);
    els["header-script-button"].addEventListener("click", openScriptingMode);
    els["new-v2-tile-button"].addEventListener("click", newV2Tile);
    els["duplicate-v2-tile-button"].addEventListener("click", duplicateV2Tile);
    els["move-v2-tile-up"].addEventListener("click", function () { moveV2Tile(-1); });
    els["move-v2-tile-down"].addEventListener("click", function () { moveV2Tile(1); });
    els["delete-v2-tile-button"].addEventListener("click", deleteV2Tile);
    els["close-tile-lab-button"].addEventListener("click", closeTileLab);
    els["tile-lab-done-button"].addEventListener("click", closeTileLab);
    els["tile-lab-dialog"].addEventListener("cancel", function (event) { event.preventDefault(); closeTileLab(); });
    document.querySelectorAll("[data-pixel-tool]").forEach(function (button) { button.addEventListener("click", function () { state.pixelTool = button.dataset.pixelTool; document.querySelectorAll("[data-pixel-tool]").forEach(function (entry) { entry.classList.toggle("is-active", entry === button); }); }); });
    els["tile-clear-button"].addEventListener("click", function () { if (state.tilePixels) { var before = state.tilePixels.slice(); state.tilePixels.fill(null); pushPixelHistory(before); renderTilePixels(); } });
    els["pixel-undo-button"].addEventListener("click", function () { pixelUndo(-1); }); els["pixel-redo-button"].addEventListener("click", function () { pixelUndo(1); });
    els["pixel-flip-x"].addEventListener("click", function () { transformPixelFrame("flipX"); }); els["pixel-flip-y"].addEventListener("click", function () { transformPixelFrame("flipY"); }); els["pixel-rotate"].addEventListener("click", function () { transformPixelFrame("rotate"); });
    els["pixel-onion-skin"].addEventListener("change", renderTilePixels);
    els["tile-seed-art-button"].addEventListener("click", seedArtFromCurrentSprite);
    els["tile-frame-prev"].addEventListener("click", function () { changeTileArtFrame(-1); });
    els["tile-frame-next"].addEventListener("click", function () { changeTileArtFrame(1); });
    els["tile-frame-add"].addEventListener("click", function () { addTileArtFrame(false); });
    els["tile-frame-duplicate"].addEventListener("click", function () { addTileArtFrame(true); });
    els["tile-frame-delete"].addEventListener("click", deleteTileArtFrame);
    els["tile-frame-move-left"].addEventListener("click", function () { moveTileArtFrame(-1); });
    els["tile-frame-move-right"].addEventListener("click", function () { moveTileArtFrame(1); });
    els["tile-png-input"].addEventListener("change", function () { importTilePng(this.files && this.files[0]); this.value = ""; });
    setupCustomColorPicker("paint", function () {});
    setupCustomColorPicker("tint", function (hex) { var rgb = Core.hexToColor(hex); updateSelectedTileField("tint", [rgb[0], rgb[1], rgb[2], Number(els["v2-tint-alpha"].value)]); renderTileAnimation(); renderHitboxPreview(); });
    document.addEventListener("pointerdown", function (event) {
      if (event.target.closest && event.target.closest(".custom-color-control")) return;
      document.querySelectorAll(".custom-color-popover").forEach(function (popover) { popover.hidden = true; });
      document.querySelectorAll(".custom-color-button").forEach(function (button) { button.setAttribute("aria-expanded", "false"); });
    });
    els["tile-pixel-canvas"].addEventListener("contextmenu", function (event) { event.preventDefault(); });
    els["tile-pixel-canvas"].addEventListener("pointerdown", function (event) { state.tilePainting = true; this.setPointerCapture(event.pointerId); beginPixelDraw(event); });
    els["tile-pixel-canvas"].addEventListener("pointermove", function (event) { if (state.tilePainting) continuePixelDraw(event); });
    els["tile-pixel-canvas"].addEventListener("pointerup", function () { state.tilePainting = false; endPixelDraw(); });
    els["tile-pixel-canvas"].addEventListener("pointercancel", function () { state.tilePainting = false; endPixelDraw(); });
    els["tile-pixel-canvas"].addEventListener("keydown", function (event) { if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "z") { event.preventDefault(); pixelUndo(event.shiftKey ? 1 : -1); } else if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "y") { event.preventDefault(); pixelUndo(1); } else if (event.key === "[") changeTileArtFrame(-1); else if (event.key === "]") changeTileArtFrame(1); });
    document.querySelectorAll("[data-tile-workspace]").forEach(function (button) {
      button.addEventListener("click", function () {
        document.querySelectorAll("[data-tile-workspace]").forEach(function (entry) { entry.classList.toggle("is-active", entry === button); });
        document.querySelectorAll("[data-tile-view]").forEach(function (view) { view.hidden = view.dataset.tileView !== button.dataset.tileWorkspace; });
        var settingsGroup = button.dataset.tileWorkspace === "animation" ? "animation" : button.dataset.tileWorkspace === "hitbox" ? "physics" : "source";
        var details = document.querySelector('[data-tile-field-group="' + settingsGroup + '"]'); if (details) details.open = true;
        renderTileAnimation(); renderHitboxPreview();
      });
    });
    document.querySelectorAll("[data-hitbox]").forEach(function (button) {
      button.addEventListener("click", function () { updateSelectedTileField("collision", button.dataset.hitbox); setTileControl("v2-collision", button.dataset.hitbox); renderHitboxPreview(); });
    });
    els["tile-animation-tick"].addEventListener("input", function () { els["tile-animation-playing"].checked = false; renderTileAnimation(); });
    els["tile-animation-playing"].addEventListener("change", renderTileAnimation);

    [
      ["v2-tile-name", "name", "text"], ["v2-sprite-sheet", "sprite_sheet", "text"],
      ["v2-sprite-index", "sprite_index", "number"], ["v2-cell-w", "cell_w", "number"], ["v2-cell-h", "cell_h", "number"], ["v2-padding", "padding", "number"], ["v2-frame-count", "frame_count", "number"], ["v2-frame-ticks", "frame_ticks", "number"],
      ["v2-animation", "animation", "text"], ["v2-collision", "collision", "text"], ["v2-native-glyph", "native_glyph", "optionalText"],
      ["v2-layer", "layer", "number"], ["v2-native-visual", "native_visual", "text"], ["v2-offset-x", "offset_x", "number"], ["v2-offset-y", "offset_y", "number"],
      ["v2-scale-x", "scale_x", "number"], ["v2-scale-y", "scale_y", "number"], ["v2-angle", "angle_degrees", "number"],
      ["v2-force-x", "force_x", "optionalNumber"], ["v2-force-y", "force_y", "optionalNumber"], ["v2-force-mode", "force_mode", "text"],
      ["v2-max-speed-x", "max_speed_x", "optionalNumber"], ["v2-max-speed-y", "max_speed_y", "optionalNumber"]
    ].forEach(function (binding) {
      els[binding[0]].addEventListener("input", function () {
        var value = this.value;
        if (binding[2] === "number" || binding[2] === "optionalNumber") value = value === "" ? "" : Number(value);
        updateSelectedTileField(binding[1], value, binding[2].indexOf("optional") === 0);
        if (binding[0] === "v2-sprite-sheet") renderTileLab();
        if (["v2-collision", "v2-native-glyph", "v2-sprite-sheet", "v2-sprite-index", "v2-cell-w", "v2-cell-h", "v2-padding", "v2-layer", "v2-offset-x", "v2-offset-y", "v2-scale-x", "v2-scale-y", "v2-angle", "v2-force-x", "v2-force-y", "v2-force-mode", "v2-max-speed-x", "v2-max-speed-y"].indexOf(binding[0]) >= 0) renderHitboxPreview();
        if (["v2-sprite-sheet", "v2-sprite-index", "v2-cell-w", "v2-cell-h", "v2-padding", "v2-frame-count", "v2-frame-ticks", "v2-animation", "v2-offset-x", "v2-offset-y", "v2-scale-x", "v2-scale-y", "v2-angle"].indexOf(binding[0]) >= 0) renderTileAnimation();
      });
    });
    els["v2-tile-id"].addEventListener("change", function () {
      var tile = selectedV2Tile(); if (!tile) return; var old = tile.id; var next = uniqueTileId(this.value, state.tileLabIndex);
      tile.id = next; if (state.document._tileArt && state.document._tileArt[old]) { state.document._tileArt[next] = state.document._tileArt[old]; delete state.document._tileArt[old]; }
      if (state.document.mapLuaPresent && old !== next) {
        var oldKey = "map." + state.document.id.toLowerCase() + ":" + old.toLowerCase(); var newKey = "map." + state.document.id.toLowerCase() + ":" + next.toLowerCase();
        state.document.mapLua = String(state.document.mapLua || "").split(oldKey).join(newKey);
      }
      renderTileLab(); markChanged();
    });
    els["v2-tile-symbol"].addEventListener("change", function () {
      var tile = selectedV2Tile(); if (!tile) return; var symbol = this.value.charAt(0);
      if (!symbol || symbol.charCodeAt(0) < 32 || symbol.charCodeAt(0) > 126 || v2Tiles().some(function (other) { return other !== tile && other.symbol === symbol; })) {
        toast("Invalid tile symbol", "Use one unique printable ASCII character.", "error"); renderTileLab(); return;
      }
      var oldSymbol = tile.symbol;
      rooms().forEach(function (room) { room.grid = room.grid.map(function (row) { var chars = Array.isArray(row) ? row : String(row || "").split(""); return chars.map(function (glyph) { return glyph === oldSymbol ? symbol : glyph; }); }); });
      if (state.document.mapLuaPresent && oldSymbol) {
        var escaped = oldSymbol.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
        var bindingPattern = new RegExp("(map\\.(?:sensor|on_enter|on_contact|on_leave)\\s*\\(\\s*[\\\"'])" + escaped + "([\\\"'])", "g");
        state.document.mapLua = String(state.document.mapLua || "").replace(bindingPattern, "$1" + symbol + "$2");
      }
      tile.symbol = symbol; state.selectedGlyph = symbol; renderTileLab(); renderPalette(); markChanged();
    });
    els["v2-tint-alpha"].addEventListener("input", function () { var rgb = Core.hexToColor(els["v2-tint"].value); updateSelectedTileField("tint", [rgb[0], rgb[1], rgb[2], Number(this.value)]); renderTileAnimation(); renderHitboxPreview(); });
    els["v2-mirror-with-room"].addEventListener("change", function () { updateSelectedTileField("mirror_with_room", this.checked); renderHitboxPreview(); });
    els["v2-random-phase"].addEventListener("change", function () { updateSelectedTileField("random_phase", this.checked); });

    els["close-script-button"].addEventListener("click", closeScriptingMode);
    els["script-done-button"].addEventListener("click", closeScriptingMode);
    els["script-dialog"].addEventListener("cancel", function (event) { event.preventDefault(); closeScriptingMode(); });
    els["remove-script-button"].addEventListener("click", removeScript);
    els["script-template-button"].addEventListener("click", function () { insertScript((els["map-lua-editor"].value ? "\n\n" : "") + scriptStarter()); });
    els["script-builder-insert"].addEventListener("click", buildScriptHandler);
    els["script-builder-event"].addEventListener("change", renderScriptBuilder);
    els["script-api-search"].addEventListener("input", renderScriptReference);
    els["script-format-button"].addEventListener("click", function () {
      var editor = els["map-lua-editor"]; var start = editor.selectionStart; var end = editor.selectionEnd;
      var selected = editor.value.slice(start, end) || editor.value; var formatted = selected.split("\n").map(function (line) { return line.trim() ? "  " + line : line; }).join("\n");
      if (start === end) { editor.value = formatted; state.document.mapLua = editor.value; state.document.mapLuaPresent = true; markChanged(); scriptDiagnostics(); }
      else insertScript(formatted);
    });
    els["map-lua-editor"].addEventListener("input", function () { state.document.mapLua = this.value; state.document.mapLuaPresent = true; markChanged(); scriptDiagnostics(); });
    els["map-lua-editor"].addEventListener("keydown", function (event) {
      if ((event.ctrlKey || event.metaKey) && event.code === "Space") { event.preventDefault(); showScriptAutocomplete(); }
      else if (event.key === "Tab") { event.preventDefault(); insertScript("  "); }
      else if (event.key === "Escape") els["script-autocomplete"].hidden = true;
    });
    els["symmetry-toggle"].addEventListener("change", function () { state.symmetry = this.checked; announce("Symmetry brush " + (state.symmetry ? "on" : "off")); });
    els["grid-toggle-button"].addEventListener("click", function () {
      state.showGrid = !state.showGrid;
      this.setAttribute("aria-pressed", state.showGrid ? "true" : "false");
      renderGrid();
    });
    els["zoom-in-button"].addEventListener("click", function () { setZoom(state.zoom + 0.25, false); });
    els["zoom-out-button"].addEventListener("click", function () { setZoom(state.zoom - 0.25, false); });
    els["zoom-label"].addEventListener("click", fitZoom);
    els["validation-summary"].addEventListener("click", function () { switchInspector("validation"); });
    els["map-grid"].addEventListener("pointermove", function (event) {
      if (!state.pointer) return;
      var target = document.elementFromPoint(event.clientX, event.clientY);
      var cell = target && target.closest ? target.closest(".map-cell") : null;
      if (cell && els["map-grid"].contains(cell)) continueDraw(cell, event);
    });

    bindTextField(els["map-name"], "Edit map name", function () { return state.document.name; }, function (value) {
      state.document.name = value;
      els["project-title"].textContent = value || "Untitled Map";
      if (!state.idWasEdited) {
        state.document.id = normalizeId(value);
        setControlValue(els["map-id"], state.document.id);
      }
    });
    bindTextField(els["map-author"], "Edit author", function () { return state.document.author; }, function (value) { state.document.author = value; });
    bindTextField(els["map-id"], "Edit map ID", function () { return state.document.id; }, function (value) {
      var old = state.document.id;
      if (state.document.mapLuaPresent && old && old !== value) state.document.mapLua = String(state.document.mapLua || "").split("map." + old.toLowerCase() + ":").join("map." + String(value).toLowerCase() + ":");
      state.document.id = value; state.idWasEdited = true;
    });
    bindTextField(els["map-description"], "Edit description", function () { return state.document.description; }, function (value) {
      state.document.description = value;
      els["description-count"].textContent = utf8Length(value) + " / 255 bytes";
    });
    bindNumberField(els["sort-order"], "Edit sort order", function () { return state.document.sortOrder; }, function (value) { state.document.sortOrder = value; }, false);
    els["eggnogg-color-enabled"].addEventListener("change", function () {
      var enabled = this.checked;
      commit("Change Eggnogg color mode", function () {
        if (enabled) state.document.rules.eggnoggColor = Core.hexToColor(els["eggnogg-color"].value);
        else delete state.document.rules.eggnoggColor;
      });
    });
    els["eggnogg-color"].addEventListener("change", function () {
      var color = Core.hexToColor(this.value);
      if (color && state.document.rules.eggnoggColor !== undefined) commit("Change Eggnogg color", function () { state.document.rules.eggnoggColor = color; });
    });
    bindNumberField(els["score-target"], "Edit score target", function () { return state.document.rules.scoreTarget; }, function (value) { state.document.rules.scoreTarget = value; }, true);
    bindNumberField(els["respawn-limit"], "Edit armed respawn limit", function () { return state.document.rules.armedRespawnLimit; }, function (value) { state.document.rules.armedRespawnLimit = value; }, false);

    document.querySelectorAll('input[name="combat-mode"]').forEach(function (input) {
      input.addEventListener("change", function () { if (input.checked) commit("Change combat mode", function () { state.document.rules.mode = input.value; }); });
    });
    document.querySelectorAll('input[name="round-end"]').forEach(function (input) {
      input.addEventListener("change", function () { if (input.checked) commit("Change round ending", function () { state.document.rules.roundEndRooms = input.value; }); });
    });

    bindTextField(els["room-id"], "Rename room", function () { return activeRoom().id; }, function (value) {
      var room = activeRoom();
      var old = room.id;
      room.id = value;
      state.document.layout.order = rooms().map(function (entry) { return entry === room ? value : entry.id; });
      if (old !== value) renderArena();
    }, { renderRooms: true });
    els["room-ambient"].addEventListener("change", function () { var value = this.value; commit("Change ambience", function () { activeRoom().ambient = value; }); });
    els["custom-mirror-colors"].addEventListener("change", function () {
      var checked = this.checked;
      commit(checked ? "Unlink mirrored palette" : "Link mirrored palette", function () {
        var room = activeRoom();
        var currentMirror = checked ? (Core.resolveRoomAppearance ? Core.resolveRoomAppearance(state.document, room).mirror : resolvedAppearance(room, "mirror")) : null;
        room.appearance = room.appearance || {};
        if (checked) room.appearance.mirror = clone(currentMirror);
        else {
          delete room.appearance.mirror;
          if (!Object.keys(room.appearance).length) delete room.appearance;
        }
      });
    });
    els["reset-colors-button"].addEventListener("click", function () {
      commit("Reset room palette", function () { delete activeRoom().appearance; });
    });
    els["preview-mirror-colors-button"].addEventListener("click", function () {
      setAppearancePreviewBank(state.appearancePreviewBank === "mirror" ? "primary" : "mirror");
    });
    els["save-palette-button"].addEventListener("click", function () {
      var name = els["palette-name"].value.trim() || "Palette " + (state.savedPalettes.length + 1);
      var existing = state.savedPalettes.findIndex(function (preset) { return preset.name.toLowerCase() === name.toLowerCase(); });
      var preset = { name: name.slice(0, 32), colors: normalizedPalette(resolvedAppearance(activeRoom(), "primary")) };
      if (existing >= 0) state.savedPalettes[existing] = preset;
      else if (state.savedPalettes.length >= 24) {
        toast("Palette library is full", "Delete a saved palette before adding another.", "warning");
        return;
      } else state.savedPalettes.push(preset);
      els["palette-name"].value = preset.name;
      savePaletteLibrary();
      renderPresets();
      toast(existing >= 0 ? "Palette updated" : "Palette saved", preset.name + " is available in this browser.");
    });
    els["random-palette-button"].addEventListener("click", function () {
      applyRoomPalette({ name: "random", colors: randomPalette() });
      toast("Random palette applied", "Use Save current if you want to keep it.");
    });
    els["move-room-in-button"].addEventListener("click", function () {
      if (state.roomIndex <= 0) return;
      commit("Move room inward", function () {
        var list = rooms();
        var item = list.splice(state.roomIndex, 1)[0];
        state.roomIndex -= 1;
        list.splice(state.roomIndex, 0, item);
        state.document.layout.order = list.map(function (room) { return room.id; });
      });
    });
    els["move-room-out-button"].addEventListener("click", function () {
      if (state.roomIndex >= rooms().length - 1) return;
      commit("Move room outward", function () {
        var list = rooms();
        var item = list.splice(state.roomIndex, 1)[0];
        state.roomIndex += 1;
        list.splice(state.roomIndex, 0, item);
        state.document.layout.order = list.map(function (room) { return room.id; });
      });
    });
    els["add-room-button"].addEventListener("click", function () {
      if (rooms().length >= MAX_ROOMS) return;
      commit("Add room", function () {
        var id = nextRoomId("outer_" + rooms().length);
        rooms().push(newRoom(id, true));
        state.roomIndex = rooms().length - 1;
        state.document.layout.order = rooms().map(function (room) { return room.id; });
      });
    });
    els["duplicate-room-button"].addEventListener("click", function () {
      if (rooms().length >= MAX_ROOMS) return;
      commit("Duplicate room", function () {
        var copy = clone(activeRoom());
        copy.id = nextRoomId(copy.id + "_copy");
        rooms().splice(state.roomIndex + 1, 0, copy);
        state.roomIndex += 1;
        state.document.layout.order = rooms().map(function (room) { return room.id; });
      });
    });
    els["delete-room-button"].addEventListener("click", function () {
      if (rooms().length <= 1) return;
      var room = activeRoom();
      var nonempty = room.grid.some(function (row) { return row.some(function (glyph) { return glyph !== " " && glyph !== "."; }); });
      if (nonempty && !window.confirm('Delete non-empty room "' + room.id + '"? You can undo this action.')) return;
      commit("Delete room", function () {
        rooms().splice(state.roomIndex, 1);
        state.roomIndex = Math.min(state.roomIndex, rooms().length - 1);
        state.document.layout.order = rooms().map(function (entry) { return entry.id; });
      });
      toast("Room deleted", "Undo is available.", "warning");
    });

    document.querySelectorAll("[data-inspector-tab]").forEach(function (button) { button.addEventListener("click", function () { switchInspector(button.dataset.inspectorTab); }); });
    document.querySelectorAll("[data-open-panel]").forEach(function (button) {
      button.addEventListener("click", function () {
        document.body.classList.remove("palette-open", "inspector-open");
        document.body.classList.add(button.dataset.openPanel + "-open");
      });
    });
    document.querySelectorAll("[data-close-panel]").forEach(function (button) {
      button.addEventListener("click", function () { document.body.classList.remove(button.dataset.closePanel + "-open"); });
    });
    els["mobile-rooms-button"].addEventListener("click", function () { document.querySelector(".room-rail").scrollIntoView({ behavior: "smooth", block: "nearest" }); });
    els["mobile-preview-button"].addEventListener("click", launchPreview);
    els["mobile-preview-button"].addEventListener("contextmenu", function (event) {
      event.preventDefault();
      copyPreviewLink();
    });
    els["mobile-export-button"].addEventListener("click", openExport);

    els["new-map-button"].addEventListener("click", function () { els["new-map-author"].value = state.document.author || ""; openDialog(els["new-map-dialog"]); });
    els["confirm-new-map"].addEventListener("click", function (event) {
      event.preventDefault();
      var before = historySnapshot("Create new map");
      state.document = makeNewDocument(els["new-map-author"].value.trim());
      state.roomIndex = 0; state.idWasEdited = false;
      pushHistory(before, "Create new map");
      renderAll(); closeDialog(els["new-map-dialog"]);
      toast("New map created", "Two empty rooms are ready.");
    });

    els["import-button"].addEventListener("click", function () { resetImportResult(); openDialog(els["import-dialog"]); });
    els["choose-files-button"].addEventListener("click", function () { els["package-file-input"].click(); });
    els["choose-folder-button"].addEventListener("click", function () { els["package-folder-input"].click(); });
    els["package-file-input"].addEventListener("change", function () {
      var files = Array.from(this.files || []);
      this.value = "";
      stageImport(files);
    });
    els["package-folder-input"].addEventListener("change", function () {
      var files = Array.from(this.files || []);
      this.value = "";
      stageImport(files);
    });
    els["drop-zone"].addEventListener("click", function (event) { if (event.target === els["drop-zone"] || event.target.closest("button") === null) els["package-file-input"].click(); });
    els["drop-zone"].addEventListener("keydown", function (event) { if (event.key === "Enter" || event.key === " ") { event.preventDefault(); els["package-file-input"].click(); } });
    ["dragenter", "dragover"].forEach(function (name) { els["drop-zone"].addEventListener(name, function (event) { event.preventDefault(); this.classList.add("is-dragging"); }); });
    ["dragleave", "drop"].forEach(function (name) { els["drop-zone"].addEventListener(name, function (event) { event.preventDefault(); this.classList.remove("is-dragging"); }); });
    els["drop-zone"].addEventListener("drop", function (event) { stageImport(Array.from(event.dataTransfer.files || [])); });
    els["confirm-import-button"].addEventListener("click", confirmImport);
    els["import-dialog"].addEventListener("close", function () {
      state.importRequest += 1;
      state.pendingImport = null;
    });

    els["preview-button"].addEventListener("click", launchPreview);
    els["preview-button"].addEventListener("contextmenu", function (event) {
      event.preventDefault();
      copyPreviewLink();
    });
    els["export-button"].addEventListener("click", openExport);
    els["download-json-button"].addEventListener("click", function () { var data = compilePackage(); if (data) downloadBlob("data.json", data.json, "application/json"); });
    els["download-map-button"].addEventListener("click", function () { var data = compilePackage(); if (data) downloadBlob("data.map", data.map, "text/plain"); });
    els["copy-map-button"].addEventListener("click", copyMapText);
    els["download-package-button"].addEventListener("click", downloadPackage);
    els["help-button"].addEventListener("click", function () { openDialog(els["help-dialog"]); });

    document.addEventListener("pointerup", finishDraw);
    document.addEventListener("pointercancel", finishDraw);
    window.addEventListener("blur", finishDraw);
    window.addEventListener("resize", function () {
      if (state.autoFitZoom) fitZoom();
      else setZoom(state.zoom, false);
    });
    window.addEventListener("beforeunload", function (event) {
      if (!state.dirtySinceExport) return;
      event.preventDefault();
      event.returnValue = "";
    });
    document.addEventListener("keydown", globalShortcut);
  }

  function globalShortcut(event) {
    var target = event.target;
    var typing = target && (target.matches("input, textarea, select") || target.isContentEditable);
    if (document.querySelector("dialog[open]")) return;
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "z" && !typing) {
      event.preventDefault();
      if (event.shiftKey) redo(); else undo();
      return;
    }
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "y" && !typing) { event.preventDefault(); redo(); return; }
    if (typing) return;
    if (event.key === "/") { event.preventDefault(); els["tile-search"].focus(); return; }
    var tools = { "1": "pencil", "2": "eraser", "3": "fill", "4": "line", "5": "rectangle", i: "eyedropper", I: "eyedropper" };
    if (tools[event.key]) { event.preventDefault(); setTool(tools[event.key]); }
  }

  function setZoom(value, fromFit) {
    var minimum = window.innerWidth <= 650 ? 1 : 0.5;
    state.zoom = Math.max(minimum, Math.min(4, Math.round(value * 4) / 4));
    state.autoFitZoom = !!fromFit;
    document.documentElement.style.setProperty("--cell-size", Math.round(24 * state.zoom) + "px");
    els["zoom-label"].textContent = Math.round(state.zoom * 100) + "%";
  }

  function fitZoom() {
    var rect = els["map-scroll"].getBoundingClientRect();
    var availableWidth = Math.max(280, rect.width - 76);
    var availableHeight = Math.max(160, rect.height - 76);
    var ratio = Math.min(availableWidth / (COLS * 24), availableHeight / (ROWS * 24));
    setZoom(Math.max(window.innerWidth <= 650 ? 1 : 0.5, Math.min(2, Math.floor(ratio * 4) / 4)), true);
  }

  function applyRoomPalette(preset) {
    commit("Apply " + preset.name + " palette", function () {
      var room = activeRoom();
      room.appearance = room.appearance || {};
      room.appearance.primary = normalizedPalette(preset.colors);
    });
  }

  function paletteButton(preset) {
    var button = document.createElement("button");
    var strip = document.createElement("span");
    var label = document.createElement("small");
    button.type = "button";
    button.className = "palette-preset";
    button.title = "Use " + preset.name + " palette";
    button.setAttribute("aria-label", "Use " + preset.name + " room palette");
    strip.className = "palette-swatch-strip";
    [preset.colors.bg1, preset.colors.fg1, preset.colors.water, preset.colors.special2].forEach(function (color) {
      var swatch = document.createElement("i");
      swatch.style.background = Core.colorToHex ? Core.colorToHex(color) : color;
      strip.appendChild(swatch);
    });
    label.textContent = preset.name;
    button.append(strip, label);
    button.addEventListener("click", function () { applyRoomPalette(preset); });
    return button;
  }

  function renderPresets() {
    els["palette-presets"].textContent = "";
    PRESETS.forEach(function (preset) { els["palette-presets"].appendChild(paletteButton(preset)); });
    els["saved-palettes"].textContent = "";
    els["saved-palettes"].hidden = state.savedPalettes.length === 0;
    state.savedPalettes.forEach(function (preset, index) {
      var item = document.createElement("div");
      var remove = document.createElement("button");
      item.className = "saved-palette-item";
      remove.type = "button";
      remove.className = "saved-palette-delete";
      remove.textContent = "×";
      remove.title = "Delete " + preset.name;
      remove.setAttribute("aria-label", "Delete saved palette " + preset.name);
      remove.addEventListener("click", function () {
        state.savedPalettes.splice(index, 1);
        savePaletteLibrary();
        renderPresets();
        toast("Palette deleted", preset.name + " was removed from this browser.");
      });
      item.append(paletteButton(preset), remove);
      els["saved-palettes"].appendChild(item);
    });
  }

  function randomPalette() {
    var hue = Math.floor(Math.random() * 360);
    var complement = (hue + 145 + Math.random() * 40) % 360;
    return {
      bg1: hsvToHex(hue, 0.58, 0.12),
      bg2: hsvToHex((hue + 18) % 360, 0.52, 0.24),
      fg1: hsvToHex((hue + 350) % 360, 0.48, 0.58),
      fg2: hsvToHex((hue + 22) % 360, 0.42, 0.86),
      water: hsvToHex(complement, 0.68, 0.58),
      water_hi: hsvToHex((complement + 12) % 360, 0.24, 0.94),
      special: hsvToHex((hue + 65) % 360, 0.72, 0.82),
      special2: hsvToHex((hue + 105) % 360, 0.42, 0.98)
    };
  }

  function resetImportResult() {
    state.importRequest += 1;
    state.pendingImport = null;
    els["confirm-import-button"].disabled = true;
    els["import-result"].hidden = true;
    els["import-result"].className = "import-result";
    els["import-result"].textContent = "";
    els["package-file-input"].value = "";
    els["package-folder-input"].value = "";
  }

  function showImportResult(title, detail, isError, canImport) {
    els["import-result"].hidden = false;
    els["import-result"].className = "import-result" + (isError ? " is-error" : "");
    els["import-result"].textContent = "";
    var strong = document.createElement("strong");
    var span = document.createElement("span");
    strong.textContent = title;
    span.textContent = detail;
    els["import-result"].append(strong, span);
    els["confirm-import-button"].disabled = !canImport;
  }

  function u16(view, offset) { return view.getUint16(offset, true); }
  function u32(view, offset) { return view.getUint32(offset, true); }

  function safeArchivePath(path) {
    if (!path || path.indexOf("\0") >= 0 || path.indexOf("\\") >= 0 || /^([a-z]:|\/|\\\\)/i.test(path)) return false;
    var parts = path.split("/");
    var devices = /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i;
    return parts.every(function (part, index) {
      if (!part && index === parts.length - 1) return true;
      return part && part !== "." && part !== ".." && !/[. ]$/.test(part) && !devices.test(part);
    });
  }

  async function inflateRaw(bytes) {
    if (!window.DecompressionStream) throw new Error("This browser cannot open compressed ZIP entries. Choose the extracted map folder instead.");
    var stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }

  async function readZip(bytes) {
    var view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    var eocd = -1;
    var minimum = Math.max(0, bytes.length - 65557);
    for (var i = bytes.length - 22; i >= minimum; i -= 1) {
      if (u32(view, i) === 0x06054B50 && i + 22 + u16(view, i + 20) === bytes.length) { eocd = i; break; }
    }
    if (eocd < 0) throw new Error("ZIP end record was not found.");
    var zipCommentLength = u16(view, eocd + 20);
    if (eocd + 22 + zipCommentLength !== bytes.length) throw new Error("ZIP end record or comment is malformed.");
    if (u16(view, eocd + 4) !== 0 || u16(view, eocd + 6) !== 0 || u16(view, eocd + 8) !== u16(view, eocd + 10)) throw new Error("Multi-disk ZIP packages are not supported.");
    var count = u16(view, eocd + 10);
    var centralSize = u32(view, eocd + 12);
    var centralOffset = u32(view, eocd + 16);
    if (count === 0xFFFF || centralSize === 0xFFFFFFFF || centralOffset === 0xFFFFFFFF) throw new Error("ZIP64 packages are not supported.");
    if (count > 256 || centralOffset + centralSize > eocd) throw new Error("ZIP directory is too large or malformed.");
    var decoder = new TextDecoder("utf-8", { fatal: false });
    var entries = [];
    var seen = Object.create(null);
    var position = centralOffset;
    var expandedTotal = 0;
    for (var entryIndex = 0; entryIndex < count; entryIndex += 1) {
      if (position + 46 > bytes.length || u32(view, position) !== 0x02014B50) throw new Error("Malformed ZIP central directory.");
      var flags = u16(view, position + 8);
      var method = u16(view, position + 10);
      var expectedCrc = u32(view, position + 16);
      var compressedSize = u32(view, position + 20);
      var size = u32(view, position + 24);
      var nameLength = u16(view, position + 28);
      var extraLength = u16(view, position + 30);
      var commentLength = u16(view, position + 32);
      var externalAttributes = u32(view, position + 38);
      var localOffset = u32(view, position + 42);
      var entryEnd = position + 46 + nameLength + extraLength + commentLength;
      if (entryEnd > centralOffset + centralSize || entryEnd > bytes.length) throw new Error("Malformed ZIP central-directory entry bounds.");
      var name = decoder.decode(bytes.subarray(position + 46, position + 46 + nameLength));
      position = entryEnd;
      if (!safeArchivePath(name)) throw new Error("Unsafe ZIP path: " + name);
      var folded = name.toLowerCase();
      if (seen[folded]) throw new Error("Duplicate case-insensitive ZIP path: " + name);
      seen[folded] = true;
      if ((flags & 1) !== 0) throw new Error("Encrypted ZIP entries are not supported.");
      var unixType = (externalAttributes >>> 16) & 0xF000;
      if (unixType === 0xA000) throw new Error("Symbolic links are not accepted in map packages.");
      if (name.endsWith("/")) continue;
      if (method !== 0 && method !== 8) throw new Error("Unsupported ZIP compression method for " + name + ".");
      if (size > 64 * 1024 * 1024 || expandedTotal + size > 96 * 1024 * 1024) throw new Error("ZIP expands beyond Greggnogg’s safe import limit.");
      if (compressedSize && size / compressedSize > 500) throw new Error("ZIP entry has an unsafe expansion ratio: " + name);
      if (localOffset + 30 > bytes.length || u32(view, localOffset) !== 0x04034B50) throw new Error("Malformed ZIP local entry: " + name);
      var localNameLength = u16(view, localOffset + 26);
      var localExtraLength = u16(view, localOffset + 28);
      var dataOffset = localOffset + 30 + localNameLength + localExtraLength;
      if (dataOffset + compressedSize > bytes.length) throw new Error("Truncated ZIP entry: " + name);
      var compressed = bytes.subarray(dataOffset, dataOffset + compressedSize);
      var data = method === 0 ? compressed.slice() : await inflateRaw(compressed);
      if (data.length !== size) throw new Error("ZIP size mismatch for " + name);
      if (Core.crc32 && Core.crc32(data) !== expectedCrc) throw new Error("ZIP checksum mismatch for " + name);
      expandedTotal += size;
      entries.push({ name: name, bytes: data });
    }
    return entries;
  }

  async function filesToEntries(files) {
    if (!files.length) throw new Error("No files were selected.");
    if (files.length === 1 && /\.zip$/i.test(files[0].name)) {
      var zipBytes = new Uint8Array(await files[0].arrayBuffer());
      if (zipBytes.length > 96 * 1024 * 1024) throw new Error("ZIP is larger than the safe import limit.");
      return readZip(zipBytes);
    }
    if (files.some(function (file) { return /\.zip$/i.test(file.name); })) throw new Error("Choose either one ZIP or extracted package files, not both.");
    if (files.length > 256) throw new Error("Choose a map package with at most 256 direct files.");
    var totalSize = files.reduce(function (total, file) {
      if (file.size > 64 * 1024 * 1024) throw new Error(file.name + " exceeds Greggnogg’s safe per-file import limit.");
      return total + file.size;
    }, 0);
    if (totalSize > 96 * 1024 * 1024) throw new Error("Selected files exceed Greggnogg’s safe total import limit.");
    return Promise.all(files.map(async function (file) {
      var path = file.webkitRelativePath || file.name;
      if (!safeArchivePath(path)) throw new Error("Unsafe file path: " + path);
      return { name: path, bytes: new Uint8Array(await file.arrayBuffer()) };
    }));
  }

  function dirname(path) {
    var slash = path.lastIndexOf("/");
    return slash < 0 ? "" : path.slice(0, slash);
  }

  function basename(path) {
    var slash = path.lastIndexOf("/");
    return (slash < 0 ? path : path.slice(slash + 1)).toLowerCase();
  }

  function decodeText(entry, limit) {
    if (!entry) return null;
    if (entry.bytes.length > limit) throw new Error(basename(entry.name) + " exceeds the loader’s " + Math.floor(limit / 1048576) + " MiB limit.");
    if (entry.bytes.indexOf(0) >= 0) throw new Error(basename(entry.name) + " contains a NUL byte.");
    return new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(entry.bytes);
  }

  async function stageImport(files) {
    var request = ++state.importRequest;
    state.pendingImport = null;
    els["confirm-import-button"].disabled = true;
    showImportResult("Reading package…", "The current draft has not been changed.", false, false);
    try {
      var entries = await filesToEntries(files);
      if (request !== state.importRequest) return;
      var jsonEntries = entries.filter(function (entry) { return basename(entry.name) === "data.json"; });
      var mapEntries = entries.filter(function (entry) { return basename(entry.name) === "data.map"; });
      if (jsonEntries.length > 1 || mapEntries.length > 1) throw new Error("Package contains more than one data.json or data.map.");
      var jsonEntry = jsonEntries[0];
      var mapEntry = mapEntries[0];
      if (jsonEntry && mapEntry && dirname(jsonEntry.name).toLowerCase() !== dirname(mapEntry.name).toLowerCase()) throw new Error("data.json and data.map must be direct files in the same map folder.");
      if (!mapEntry) throw new Error("No data.map file was found.");
      var mapText = decodeText(mapEntry, 4 * 1024 * 1024);
      var jsonText = jsonEntry ? decodeText(jsonEntry, 4 * 1024 * 1024) : null;
      var packageDir = dirname(mapEntry.name).toLowerCase();
      if (packageDir && packageDir.split("/").length !== 1) throw new Error("Map files must be direct children of one top-level package folder.");
      var directExtras = entries.filter(function (entry) {
        return dirname(entry.name).toLowerCase() === packageDir && basename(entry.name) !== "data.json" && basename(entry.name) !== "data.map";
      });
      var json = null;
      if (jsonText !== null) {
        try { json = JSON.parse(jsonText.charCodeAt(0) === 0xFEFF ? jsonText.slice(1) : jsonText); }
        catch (error) { throw new Error("data.json is not valid JSON: " + error.message); }
        if (json && (json.format === "eggnogg-map/v2" || json.tileset !== undefined)) {
          throw new Error("This is a V2 package. Greggnogg’s native editor will not overwrite custom symbols, sprite sheets, or map.lua; the current draft is unchanged.");
        }
      }
      if (directExtras.length) {
        var extraNames = directExtras.map(function (entry) { return basename(entry.name); });
        throw new Error("Package has extra direct files (" + extraNames.join(", ") + "). Greggnogg refuses to discard V2 assets or map.lua; the current draft is unchanged.");
      }
      var parsed;
      if (jsonText !== null) {
        parsed = Core.parsePackage(jsonText, mapText);
      } else {
        var parsedMap = Core.parseMapText(mapText);
        if (!parsedMap.valid) parsed = parsedMap;
        else {
          var rawDoc = makeNewDocument(state.document.author);
          rawDoc.name = "Imported Map";
          rawDoc.id = normalizeId(dirname(mapEntry.name).split("/").pop() || "imported_map");
          rawDoc.rooms = parsedMap.rooms.map(function (room) { return { id: room.id, grid: room.grid, ambient: "none" }; });
          rawDoc.layout.order = rawDoc.rooms.map(function (room) { return room.id; });
          var rawValidation = Core.validateDocument(rawDoc);
          parsed = { document: rawDoc, valid: rawValidation.valid, errors: rawValidation.errors || [], warnings: [issue("warning", "raw_map_metadata", "data.map had no data.json, so Greggnogg supplied editable placeholder metadata.")].concat(rawValidation.warnings || []) };
        }
      }
      var documentValue = parsed.document || parsed.project || (parsed.rooms ? parsed : null);
      var errors = parsed.errors || [];
      var warnings = parsed.warnings || [];
      if (!documentValue || errors.length) {
        var first = errors[0] && errors[0].message || "The package could not be parsed.";
        throw new Error(first + (errors.length > 1 ? " (and " + (errors.length - 1) + " more)" : ""));
      }
      var unknownFields = warnings.filter(function (warning) { return warning.code === "unknown_key"; });
      if (unknownFields.length) throw new Error("This manifest contains compatibility fields Greggnogg cannot round-trip safely. Nothing was imported: " + unknownFields[0].message);
      if (documentValue.format === "eggnogg-map/v2" && documentValue._preserved) {
        var editableAssets = {};
        var preservedNames = Object.keys(documentValue._preserved.assets || {});
        for (var assetIndex = 0; assetIndex < preservedNames.length; assetIndex += 1) {
          var preservedName = preservedNames[assetIndex];
          editableAssets[preservedName] = await bytesToPngDataUrl(documentValue._preserved.assets[preservedName]);
        }
        documentValue.assets = editableAssets;
        documentValue.mapLuaPresent = !!documentValue._preserved.mapLuaPresent;
        if (documentValue.mapLuaPresent) documentValue.mapLua = documentValue._preserved.mapLua;
        delete documentValue._preserved;
      }
      if (typeof documentValue.id !== "string" || !documentValue.id.trim()) {
        documentValue.id = normalizeId(dirname(mapEntry.name).split("/").pop() || documentValue.name || "imported_map");
      }
      state.pendingImport = ensureDocumentShape(documentValue);
      showImportResult((state.pendingImport.format === "eggnogg-map/v2" ? "V2" : "V1") + " package ready", state.pendingImport.rooms.length + " source rooms found" + (warnings.length ? " · " + warnings.length + " warnings" : "") + ". Import is one undoable action; editing/export canonicalizes the package and changes its raw-byte online key.", false, true);
    } catch (error) {
      if (request === state.importRequest) showImportResult("Package not imported", error.message, true, false);
    }
  }

  function confirmImport() {
    if (!state.pendingImport) return;
    var before = historySnapshot("Import map");
    state.document = ensureDocumentShape(state.pendingImport);
    state.roomIndex = 0;
    state.idWasEdited = true;
    pushHistory(before, "Import map");
    resetImportResult(); closeDialog(els["import-dialog"]); renderAll();
    toast("Map imported", "The previous draft is available with Undo.");
  }

  function compilePackage() {
    if (state.document.format !== "eggnogg-map/v1") {
      toast("V1 maps only", "This Greggnogg build does not author or export V2 packages.", "error");
      return null;
    }
    commitAllTileArt();
    var validation = validate();
    if (validation.errors.length) {
      jumpToIssue(validation.errors[0]);
      toast("Export blocked", validation.errors[0].message, "error");
      return null;
    }
    try {
      var files = Core.exportProjectFiles ? Core.exportProjectFiles(state.document) : null;
      var json = files ? files["data.json"] : (Core.serializeDataJson ? Core.serializeDataJson(state.document) : JSON.stringify(Core.buildDataObject(state.document), null, 2) + "\n");
      var map = files ? files["data.map"] : Core.serializeMapText(state.document);
      var id = normalizeId(state.document.id || state.document.name);
      return { id: id, json: json, map: map, files: files || { "data.json": json, "data.map": map } };
    } catch (error) {
      toast("Export failed", error.message, "error");
      return null;
    }
  }

  function compilePreviewUri() {
    var validation;
    if (state.document.format !== "eggnogg-map/v1") {
      toast("V1 maps only", "Preview links currently support V1 packages.", "error");
      return null;
    }
    commitAllTileArt();
    validation = validate();
    if (validation.errors.length) {
      jumpToIssue(validation.errors[0]);
      toast("Preview blocked", validation.errors[0].message, "error");
      return null;
    }
    try {
      return Core.buildPreviewUri(state.document);
    } catch (error) {
      toast("Preview failed", error.message, "error");
      return null;
    }
  }

  function launchPreview() {
    var uri = compilePreviewUri();
    var link;
    if (!uri) return;
    try {
      link = document.createElement("a");
      link.href = uri;
      link.hidden = true;
      link.setAttribute("aria-hidden", "true");
      document.body.appendChild(link);
      link.click();
      link.remove();
      toast("Opening Yule", "Your browser may ask for permission to open EGGNOGG+.");
    } catch (error) {
      toast("Preview failed", error.message, "error");
    }
  }

  async function writeClipboardText(value) {
    try {
      if (!navigator.clipboard || !navigator.clipboard.writeText) throw new Error("Clipboard API unavailable");
      await navigator.clipboard.writeText(value);
    } catch (error) {
      var area = document.createElement("textarea");
      area.value = value;
      area.setAttribute("readonly", "");
      area.style.position = "fixed";
      area.style.opacity = "0";
      document.body.appendChild(area);
      area.select();
      if (!document.execCommand("copy")) {
        area.remove();
        throw error;
      }
      area.remove();
    }
  }

  async function copyPreviewLink() {
    var uri = compilePreviewUri();
    if (!uri) return;
    try {
      await writeClipboardText(uri);
      toast("Preview link copied", "Send it to anyone with this EGGNOGG+ build to open the map directly.");
    } catch (error) {
      toast("Could not copy link", "Your browser blocked clipboard access.", "error");
    }
  }

  function openExport() {
    var data = compilePackage();
    if (!data) return;
    els["package-tree"].textContent = "";
    [data.id + "/"].concat(Object.keys(data.files)).forEach(function (name) { var span = document.createElement("span"); span.textContent = name; els["package-tree"].appendChild(span); });
    els["export-ready-title"].textContent = state.document.format === "eggnogg-map/v2" ? "Valid V2 package" : "Valid V1 package";
    els["export-ready-summary"].textContent = rooms().length + " source rooms become " + (rooms().length * 2 - 1) + " mirrored rooms in game.";
    els["export-dialog-copy"].textContent = state.validation.warnings.length ? "The package passes loader checks with " + state.validation.warnings.length + " playability warning(s)." : "Download a ready-to-extract map package or either source file.";
    openDialog(els["export-dialog"]);
  }

  function downloadBlob(filename, value, type) {
    var blob = value instanceof Blob ? value : new Blob([value], { type: type || "application/octet-stream" });
    var url = URL.createObjectURL(blob);
    var link = document.createElement("a");
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    link.remove();
    window.setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
  }

  async function copyMapText() {
    var data = compilePackage();
    if (!data) return;
    try {
      await writeClipboardText(data.map);
      toast("data.map copied", "Spaces and fixed-width rows were preserved.");
    } catch (error) {
      toast("Could not copy data.map", "Your browser blocked clipboard access.", "error");
    }
  }

  function downloadPackage() {
    var data = compilePackage();
    if (!data) return;
    try {
      var entries = {};
      Object.keys(data.files).forEach(function (name) { entries[data.id + "/" + name] = data.files[name]; });
      var bytes = Core.buildStoredZip(entries);
      downloadBlob(data.id + ".zip", bytes, "application/zip");
      state.dirtySinceExport = false;
      window.clearTimeout(state.savedTimer);
      try { localStorage.setItem(STORAGE_KEY, JSON.stringify(state.document)); } catch (ignore) {}
      els["save-label"].textContent = "Saved locally · Exported";
      toast("Map ZIP downloaded", "Extract " + data.id + " directly into EGGNOGG+’s maps folder.");
    } catch (error) {
      toast("ZIP export failed", error.message, "error");
    }
  }

  function loadDraft() {
    try {
      var raw = localStorage.getItem(STORAGE_KEY);
      if (raw) {
        var value = JSON.parse(raw);
        if (value && value.format === "eggnogg-map/v2") {
          localStorage.setItem(V2_BACKUP_KEY, raw);
          localStorage.removeItem(STORAGE_KEY);
          els["save-label"].textContent = "V2 draft archived · New V1 map";
        } else if (value && value.format === "eggnogg-map/v1" && !value._preserved) {
          state.idWasEdited = true;
          els["save-label"].textContent = "Restored local draft · Not exported";
          return ensureDocumentShape(value);
        }
      }
    } catch (error) {
      console.warn("Greggnogg could not restore its local draft.", error);
    }
    state.idWasEdited = false;
    return makeNewDocument("Mapmaker");
  }

  async function startAtlas() {
    Atlas = window.GregAtlas || Atlas;
    if (!Atlas || !Atlas.loadAssets) { renderRoomCanvas(); return; }
    try {
      await Atlas.loadAssets({ external: assetSources() });
      state.atlasReady = true;
      renderPalette();
      renderRoomCanvas();
      if (els["tile-lab-dialog"].open && els["tile-animation-playing"].checked) renderTileAnimation();
      toast("Game atlases loaded", "Previews use the original PNGs shipped in data/.");
    } catch (error) {
      state.atlasReady = false;
      document.body.classList.remove("atlas-ready");
      toast("Atlas preview unavailable", "The editor still preserves and validates map glyphs. Serve this folder over HTTP to load local PNGs.", "warning");
    }
  }

  function startPreviewAnimation() {
    function frame() {
      window.requestAnimationFrame(frame);
      if (document.hidden || !state.atlasReady) return;
      /* requestAnimationFrame already follows the display refresh rate. A
       * second 60 Hz threshold aliases against 16.6 ms timestamps and can
       * accidentally reduce a 60 Hz browser to roughly 30 fps. */
      if (els["tile-lab-dialog"].open) {
        var animationView = document.querySelector('[data-tile-view="animation"]'); var hitboxView = document.querySelector('[data-tile-view="hitbox"]');
        if (animationView && !animationView.hidden && els["tile-animation-playing"].checked) renderTileAnimation();
        if (hitboxView && !hitboxView.hidden && performance.now() - state.hitboxLastTime >= 50) { state.hitboxLastTime = performance.now(); renderHitboxPreview(); }
        return;
      }
      renderRoomCanvas();
    }
    window.requestAnimationFrame(frame);
  }

  function init() {
    state.document = loadDraft();
    state.savedPalettes = loadSavedPalettes();
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(state.document)); }
    catch (error) { els["save-label"].textContent = "Local save unavailable"; }
    createGridControls();
    renderPresets();
    bindControls();
    renderPalette();
    selectGlyph("@");
    setTool("pencil");
    renderAll();
    window.requestAnimationFrame(fitZoom);
    startAtlas();
    if (!window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
      startPreviewAnimation();
    }
  }

  init();
}());
