(function (root, factory) {
  "use strict";

  var api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  if (root) {
    root.GregCore = api;
    /* Kept for the first Greggnogg UI draft and third-party prototypes. */
    root.YuleEditorCore = api;
  }
}(typeof globalThis !== "undefined" ? globalThis : (typeof self !== "undefined" ? self : this), function () {
  "use strict";

  var COLS = 33;
  var ROWS = 12;
  var FORMAT_V1 = "eggnogg-map/v1";
  var FORMAT_V2 = "eggnogg-map/v2";
  var FORMAT = FORMAT_V1;
  var MAX_ROOMS = 9;
  var LAYOUT_KIND = "mirrored_source_rooms";
  var ROOM_FORMAT = "vanilla_33x12";
  var GLYPH_WHITELIST = " !#()*+-.12:=?@ACEFGHIKLNOPQSTWXYZ^_`cefilmqstuvwx|~";
  var COLOR_KEYS = ["fg1", "fg2", "bg1", "bg2", "water", "water_hi", "special", "special2"];
  var AMBIENTS = ["none", "bugs", "clouds", "art", "flies", "drips", "dust", "bats", "bubbles", "boil"];
  var AMBIENT_ALIASES = { fumes: 9 };
  var MAX_TEXT_BYTES = 4 * 1024 * 1024;
  var MAX_SCRIPT_BYTES = 256 * 1024;
  var MAX_PARSED_ROOMS = 32;
  var PREVIEW_TARGET_CAP = 24576;
  var PREVIEW_FILE_MAX_BYTES = 12288;

  var COLOR_DEFAULTS = {
    fg1: "#808080",
    fg2: "#808080",
    bg1: "#808080",
    bg2: "#808080",
    water: "#0080bf",
    water_hi: "#e6e6e6",
    special: "#0080bf",
    special2: "#e6e6e6"
  };

  /* These are the engine's values. COLOR_DEFAULTS is only the nearest HTML
   * colour-input representation; serialization uses the exact float triples. */
  var COLOR_DEFAULT_FLOATS = {
    fg1: [0.5, 0.5, 0.5],
    fg2: [0.5, 0.5, 0.5],
    bg1: [0.5, 0.5, 0.5],
    bg2: [0.5, 0.5, 0.5],
    water: [0, 0.5, 0.75],
    water_hi: [0.9, 0.9, 0.9],
    special: [0, 0.5, 0.75],
    special2: [0.9, 0.9, 0.9]
  };

  function metadata(glyph, label, category, tileId, action, frame, arg, description, assets, footprint) {
    var item = {
      glyph: glyph,
      label: label,
      category: category,
      tileId: tileId,
      action: action,
      frame: frame,
      arg: arg,
      description: description,
      asset: null,
      assets: [],
      footprint: footprint || { width: 1, height: 1, anchorX: 0, anchorY: 0 }
    };
    /* Rendering comes from the original data/*.png atlases. Never point the
     * editor at the derived documentation preview tiles. */
    item.assetUrl = null;
    item.output = tileId === null ? action : "tile " + (Array.isArray(tileId) ? tileId.join(" or ") : "0x" + tileId.toString(16).toUpperCase().padStart(2, "0")) + (action ? ", " + action : "");
    return item;
  }

  var TILE_METADATA = [
    metadata(" ", "Empty", "terrain", 0x14, "colouring_action", 0x00, 0x00, "Empty/open filler. A period behaves the same way.", []),
    metadata(".", "Empty (period)", "terrain", 0x14, "colouring_action", 0x00, 0x00, "An alternate empty/open filler.", []),
    metadata("!", "Floor platform", "terrain", 0x03, "floor_action", 0x01, "random", "Floor/platform variant.", ["!.png"]),
    metadata("#", "Brick outline", "structure", 0x15, "colouring_action", 0x2D, "random", "Brick-outline decoration tile.", ["#.png"]),
    metadata("(", "Arch, left", "structure", 0x14, "colouring_action", 0x6A, "side", "Left half of an arch.", ["(.png"]),
    metadata(")", "Arch, right", "structure", 0x14, "colouring_action", 0x6B, "side", "Right half of an arch.", [").png"]),
    metadata("*", "Sword spawn", "gameplay", 0x1C, "spawn_thing_action", 0x00, 0x02, "Sword pickup/spawn. It remains active in karate mode.", ["asterisk.png"]),
    metadata("+", "Mushrooms", "decoration", 0x16, "colouring_action", "0x5D..0x5F", 0x00, "Mushroom decoration.", ["+.png"]),
    metadata("-", "Horizontal row", "structure", 0x15, "colouring_action", 0x3C, 0x00, "Horizontal row tile.", ["-.png"]),
    metadata("1", "Team hazard 1", "gameplay", 0x1E, "teamnogg_action", 0x05, 0x01, "Pass-through lethal team tile. Sword contact awards its fixed team.", ["1.gif"]),
    metadata("2", "Team hazard 2", "gameplay", 0x1E, "teamnogg_action", 0x05, 0x02, "Pass-through lethal team tile. Sword contact awards its fixed team.", ["1.gif"]),
    metadata(":", "Vertical column", "structure", 0x15, "colouring_action", 0x3D, 0x00, "Vertical column tile.", ["colon.png"]),
    metadata("=", "Hashtag decoration", "decoration", 0x15, "colouring_action", 0x1F, "random", "Decorative tile shaped like a hashtag.", ["=.png"]),
    metadata("?", "Reserved no-op", "utility", null, "no tile", null, null, "Recognized glyph that deliberately places no tile.", []),
    metadata("@", "Automatic terrain", "terrain", [0x01, 0x02], "wall_action or floor_action", "0x01 floor / 0x02 wall", "automatic/random", "Automatic terrain; its wall/floor result depends on support above.", ["@.png", "two @ stacked to show it adapts to neighboring tiles.png"]),
    metadata("A", "Spinning tile", "interactive", 0x0B, "spinny_action", 0x16, 0x04, "Spinning tile.", ["A.gif"]),
    metadata("C", "Chandelier", "decoration", 0x08, "chandelier_action", 0x18, 0x1E, "Wide-swinging chandelier.", ["C.gif"]),
    metadata("E", "Still Eggnogg goal", "gameplay", 0x0A, "tile_action_default", 0x65, 0x00, "Active pass-through round goal; entering starts the native 600-tick round-end countdown. It shares the waving goal's runtime enemy-player color.", ["E.png", "several 'E's with '^'s above them.gif"]),
    metadata("F", "Vertical column (F)", "structure", 0x17, "colouring_action", 0x59, 0x00, "Vertical column tile.", ["F.png"]),
    metadata("G", "Large mural", "large-art", 0x07, "decal_action", "0x28..0x3F", "multi-cell", "Three-by-three mural drawn strictly above its bottom-center source marker. It needs three rows of headroom and side clearance.", ["G.png"], { width: 3, height: 3, anchorX: 1, anchorY: 3, requiresHeadroom: 3, edgeClearance: 1 }),
    metadata("H", "Vertical column (H)", "structure", 0x15, "colouring_action", 0x06, 0x00, "Vertical column tile.", ["H.png"]),
    metadata("I", "Vertical column (I)", "structure", 0x15, "colouring_action", 0x1E, "random", "Vertical column tile.", ["I.png"]),
    metadata("K", "Spiked ball", "hazard", 0x1C, "spawn_thing_action", 0x00, 0x03, "Physics hazard that counts against the native reset-spawn budget.", ["K.png"]),
    metadata("L", "Large art (L)", "large-art", 0x0D, "arty_action", "multi-cell", "multi-cell", "Two-by-three art block drawn strictly above its source marker; it needs three rows of headroom and side clearance.", ["L.png"], { width: 2, height: 3, anchorX: 1, anchorY: 3, requiresHeadroom: 3, edgeClearance: 1 }),
    metadata("N", "Large art (N)", "large-art", 0x0D, "arty_action", "multi-cell", "multi-cell", "Two-by-three art block drawn strictly above its source marker; it needs three rows of headroom and side clearance.", ["N.png"], { width: 2, height: 3, anchorX: 1, anchorY: 3, requiresHeadroom: 3, edgeClearance: 1 }),
    metadata("O", "Sun", "decoration", 0x1B, "sky_glow_action", 0x11, "screen center", "Emits the animated native misc-atlas sun at the room's horizontal center and one-quarter height, independent of this marker's cell. It has no hitbox.", ["O.gif"]),
    metadata("P", "Changing art", "interactive", 0x0C, "puzzley_action", 0x08, 0x00, "Changing art tile.", ["P.png"]),
    metadata("Q", "Skull on a stick", "decoration", 0x1A, "colouring_action", "0x46 or 0x47", "random", "Skull-on-a-stick decoration.", ["Q.png"]),
    metadata("S", "Eye", "decoration", 0x14, "colouring_action", 0x07, 0x00, "Eye tile.", ["S.png"]),
    metadata("T", "Large tentacle", "interactive", 0x13, "tentacle_action", "column", "multi-cell", "Four-cell animated scenery column with no hitbox. It must be on row 4 or lower (one-based).", ["uppercase T.gif"], { width: 1, height: 4, anchorX: 0, anchorY: 3, requiresHeadroom: 3 }),
    metadata("W", "Deep water", "water", 0x0E, "high_water_action + Yule water hook", 0x65, 0x00, "Tall foreground water drawn from the native water sprite and tinted with the water-highlight color. It is lethal but pass-through, not solid.", ["W (alone).gif", "W (multiple side by side).gif"]),
    metadata("X", "Ground spikes", "hazard", 0x05, "spikes_action", 0x01, "random", "Solid floor hazard with native foreground colouring and white spikes.", ["X.png"]),
    metadata("Y", "Person art", "large-art", 0x0D, "arty_action", "multi-cell", "multi-cell", "Two-by-three person art block drawn strictly above its source marker; it needs three rows of headroom and side clearance.", ["Y.png"], { width: 2, height: 3, anchorX: 1, anchorY: 3, requiresHeadroom: 3, edgeClearance: 1 }),
    metadata("Z", "Metal plate", "structure", 0x17, "colouring_action", 0x05, "random", "Metal plate tile.", ["Z.png"]),
    metadata("^", "Waving Eggnogg goal", "gameplay", 0x09, "eggnogg_action", 0x65, 0x00, "Active bobbing pass-through round goal; entering starts the native 600-tick countdown. It shares E's color and staggers its wave by source column.", ["^.gif"]),
    metadata("_", "Ceiling", "terrain", 0x04, "ceiling_action", 0x02, "random", "Ceiling tile.", ["_.png"]),
    metadata("`", "Fence (random)", "structure", 0x15, "colouring_action", 0x56, "random", "Fence variant with a randomized argument.", ["`.png"]),
    metadata("c", "Small chandelier", "decoration", 0x08, "chandelier_action", 0x18, 0x05, "Chandelier with a shorter swing than C.", ["lowercase c.gif"]),
    metadata("e", "Head decoration", "decoration", 0x15, "colouring_action", 0x40, 0x00, "Head-like decorative tile.", ["lowercase e.png"]),
    metadata("f", "Scrolling decor", "interactive", 0x18, "scroll_action", 0x2E, 0x00, "Scrolling decoration.", ["f.gif"]),
    metadata("i", "Crowd", "decoration", 0x1D, "crowd_action", 0x00, "random", "Animated crowd/backdrop tile.", ["i.gif"]),
    metadata("l", "Score light", "gameplay", 0x1F, "score_light_action", 0x00, 0x00, "Score light.", ["lowercase l.png"]),
    metadata("m", "Mine", "hazard", 0x06, "mine_action", 0x01, 0x00, "Solid mine; counts against the native reset-spawn budget. Its normal preview is static; the alternate frame appears only when triggered.", ["m.png"]),
    metadata("q", "Hanging skeleton", "decoration", 0x1A, "colouring_action", 0x6F, "random", "Hanging skeleton.", ["lowercase q.png"]),
    metadata("s", "Small tentacle", "interactive", 0x13, "tentacle_action", "column", "multi-cell", "Two-cell animated scenery column with no hitbox. It needs one row of headroom.", ["s.gif"], { width: 1, height: 2, anchorX: 0, anchorY: 1, requiresHeadroom: 1 }),
    metadata("t", "Tentacle", "interactive", 0x13, "tentacle_action", "column", "multi-cell", "Three-cell animated scenery column with no hitbox. It needs two rows of headroom.", ["t.gif"], { width: 1, height: 3, anchorX: 0, anchorY: 2, requiresHeadroom: 2 }),
    metadata("u", "Arch", "structure", 0x14, "colouring_action", 0x6C, 0x00, "Arch tile.", ["u.png"]),
    metadata("v", "Hanging spikes", "hazard", 0x05, "spikes_action", 0x02, 0x00, "Solid ceiling hazard with native foreground colouring and white spikes.", ["v.png"]),
    metadata("w", "Shallow water", "water", 0x0F, "water_action", 0x05, 0x00, "Animated shallow water. It is lethal but pass-through, not solid.", ["w.gif"]),
    metadata("x", "Fence", "structure", 0x14, "colouring_action", 0x56, 0x00, "Fence tile.", ["lowercase x.png"]),
    metadata("|", "Vertical pipe", "structure", 0x19, "colouring_action", 0x26, 0x00, "Vertical pipe.", ["vertical line (U+007C).png"]),
    metadata("~", "Waterfall", "water", 0x10, "waterfall_action", 0x60, "row parity", "Animated non-solid, nonlethal water effect; it may add a decorative tile above itself.", ["~ (standalone).gif", "~ (two stacked).gif"])
  ];

  /* Native tiledef flags recovered from tiledef_init. Only these six glyphs
   * are solid; W/w/1/2 are the separate pass-through lethal set. Keeping this
   * on the catalog model prevents decorative structures from being presented
   * as collision terrain. */
  var SOLID_TERRAIN_GLYPHS = "!@_";
  var SOLID_HAZARD_GLYPHS = "Xvm";
  var LETHAL_PASS_GLYPHS = "Ww12";
  var ANIMATED_SCENERY_GLYPHS = "ACcPfiTtsO";
  var GOAL_ITEM_GLYPHS = "E^*l";
  var UTILITY_GLYPHS = " .?";
  TILE_METADATA.forEach(function (item) {
    var glyph = item.glyph;
    if (SOLID_TERRAIN_GLYPHS.indexOf(glyph) >= 0) {
      item.category = "solid-terrain";
      item.physics = "Solid";
      item.solid = true;
    } else if (SOLID_HAZARD_GLYPHS.indexOf(glyph) >= 0) {
      item.category = "hazard";
      item.physics = "Solid hazard";
      item.solid = true;
    } else if (LETHAL_PASS_GLYPHS.indexOf(glyph) >= 0) {
      item.category = "hazard";
      item.physics = "Lethal · no hitbox";
      item.solid = false;
      item.lethal = true;
    } else if (glyph === "K") {
      item.category = "hazard";
      item.physics = "Spawns a physics hazard";
      item.solid = false;
    } else if (glyph === "~") {
      item.category = "water-effect";
      item.physics = "Water effect · no hitbox";
      item.solid = false;
    } else if (GOAL_ITEM_GLYPHS.indexOf(glyph) >= 0) {
      item.category = "goal-item";
      item.physics = glyph === "*" ? "Spawns a sword pickup" :
        (glyph === "l" ? "Display · no hitbox" : "Round goal · no hitbox");
      item.solid = false;
    } else if (ANIMATED_SCENERY_GLYPHS.indexOf(glyph) >= 0) {
      item.category = "animated-scenery";
      item.physics = "Animated scenery · no hitbox";
      item.solid = false;
    } else if (UTILITY_GLYPHS.indexOf(glyph) >= 0) {
      item.category = "utility";
      item.physics = "Open · no hitbox";
      item.solid = false;
    } else {
      item.category = "background-art";
      item.physics = "Background art · no hitbox";
      item.solid = false;
    }
  });

  var TILE_BY_GLYPH = Object.create(null);
  TILE_METADATA.forEach(function (item) {
    TILE_BY_GLYPH[item.glyph] = item;
    /* String properties make TILE_METADATA useful as both a palette array and lookup. */
    TILE_METADATA[item.glyph] = item;
  });
  var GLYPHS = GLYPH_WHITELIST.split("");
  var GLYPH_SET = Object.create(null);
  GLYPHS.forEach(function (glyph) { GLYPH_SET[glyph] = true; });

  function deepClone(value, seen) {
    var out;
    var keys;
    var i;
    if (value === null || typeof value !== "object") return value;
    if (value instanceof Date) return new Date(value.getTime());
    /* The old JScript test host has no ArrayBuffer global, but its compatibility
     * layer still supplies Uint8Array. Preserve binary package members there as
     * bytes instead of recursively turning them into plain objects. */
    if (typeof Uint8Array !== "undefined" && value instanceof Uint8Array) {
      out = new Uint8Array(value.length);
      if (out.set) out.set(value);
      else for (i = 0; i < value.length; i += 1) out[i] = value[i];
      return out;
    }
    if (typeof ArrayBuffer !== "undefined" && value instanceof ArrayBuffer) return value.slice(0);
    if (typeof ArrayBuffer !== "undefined" && ArrayBuffer.isView && ArrayBuffer.isView(value)) {
      return new value.constructor(value);
    }
    seen = seen || (typeof WeakMap !== "undefined" ? new WeakMap() : null);
    if (seen && seen.has(value)) return seen.get(value);
    out = Array.isArray(value) ? [] : {};
    if (seen) seen.set(value, out);
    keys = Object.keys(value);
    for (i = 0; i < keys.length; i += 1) out[keys[i]] = deepClone(value[keys[i]], seen);
    return out;
  }

  function clamp(value, low, high) {
    return Math.max(low, Math.min(high, value));
  }

  function channelToByte(value) {
    var number = Number(value);
    if (!Number.isFinite(number)) number = 0;
    if (number > 1) return Math.round(clamp(number, 0, 255));
    return Math.round(clamp(number, 0, 1) * 255);
  }

  function colorToHex(color) {
    var values;
    if (typeof color === "string") {
      var match = color.trim().match(/^#?([0-9a-f]{3}|[0-9a-f]{6})$/i);
      if (!match) return null;
      if (match[1].length === 3) {
        return ("#" + match[1].split("").map(function (ch) { return ch + ch; }).join("")).toLowerCase();
      }
      return ("#" + match[1]).toLowerCase();
    }
    values = Array.isArray(color) || (color && typeof color.length === "number") ? color : null;
    if (!values || values.length !== 3) return null;
    return "#" + [channelToByte(values[0]), channelToByte(values[1]), channelToByte(values[2])]
      .map(function (part) { return part.toString(16).padStart(2, "0"); }).join("");
  }

  function hexToColor(hex) {
    var normalized = colorToHex(hex);
    if (!normalized) return null;
    return [1, 3, 5].map(function (offset) {
      return Math.round((parseInt(normalized.slice(offset, offset + 2), 16) / 255) * 1000000) / 1000000;
    });
  }

  function normalizeColorBank(bank, fallback) {
    var output = {};
    var source = bank && typeof bank === "object" ? bank : {};
    var base = fallback && typeof fallback === "object" ? fallback : {};
    COLOR_KEYS.forEach(function (key) {
      var converted = colorToHex(source[key]);
      if (converted) output[key] = converted;
      else if (base[key] !== undefined) output[key] = colorToHex(base[key]) || COLOR_DEFAULTS[key];
    });
    return output;
  }

  function createBlankGrid(fill) {
    var glyph = typeof fill === "string" && fill.length ? fill.charAt(0) : " ";
    var grid = [];
    var row;
    for (row = 0; row < ROWS; row += 1) grid.push(new Array(COLS).fill(glyph));
    return grid;
  }

  function createArenaGrid() {
    var grid = createBlankGrid(" ");
    var col;
    /* Two rows of automatic terrain produce a stable floor with plenty of
     * separated native respawn candidates. Avoid boxing the top of the room:
     * an @ below another solid @ resolves as a wall, not a spawn floor. */
    for (col = 0; col < COLS; col += 1) {
      grid[ROWS - 2][col] = "@";
      grid[ROWS - 1][col] = "@";
    }
    return grid;
  }

  function createRoom(id) {
    return {
      id: id,
      grid: createArenaGrid(),
      ambient: "none"
    };
  }

  function createDefaultDocument() {
    return {
      format: FORMAT,
      id: "untitled_map",
      name: "Untitled Map",
      author: "Mapmaker",
      description: "",
      sortOrder: 0,
      rules: {
        mode: "swords",
        roundEndRooms: "inner_only",
        scoreTarget: null,
        armedRespawnLimit: 4
      },
      layout: {
        kind: LAYOUT_KIND,
        roomFormat: ROOM_FORMAT,
        order: ["center", "outer_1"]
      },
      defaults: {
        ambient: "none"
      },
      rooms: [createRoom("center"), createRoom("outer_1")]
    };
  }

  function normalizeMapId(value) {
    var result = value === null || value === undefined ? "" : String(value).trim().toLowerCase();
    if (result.normalize) result = result.normalize("NFKD").replace(/[\u0300-\u036f]/g, "");
    result = result.replace(/[^a-z0-9._-]+/g, "_").replace(/^[.-]+/g, "").replace(/_+/g, "_");
    if (!result) result = "custom_map";
    return result.slice(0, 63).replace(/[._-]+$/g, "") || "custom_map";
  }

  function normalizeV2Id(value) {
    return normalizeMapId(value).slice(0, 43).replace(/[._-]+$/g, "") || "custom_map";
  }

  /* This intentionally starts with an empty declarative tileset. It is a
   * safe format upgrade for a native-only project, not a conversion of an
   * imported V2 package whose unknown fields/assets must stay byte-preserved. */
  function upgradeToV2(value) {
    var document = deepClone(unwrapDocument(value) || createDefaultDocument());
    if (document.format === FORMAT_V2 && document._preserved) throw new Error("Imported V2 packages stay preserve-only until their Tile Lab fields are implemented.");
    document.format = FORMAT_V2;
    document.id = normalizeV2Id(document.id || document.name);
    document.tileset = isPlainObject(document.tileset) ? document.tileset : { tiles: [] };
    if (!Array.isArray(document.tileset.tiles)) document.tileset.tiles = [];
    delete document._preserved;
    return document;
  }

  function makeIssue(severity, code, message, details) {
    details = details || {};
    return {
      severity: severity,
      code: code,
      message: message,
      path: details.path || null,
      roomId: details.roomId === undefined ? null : details.roomId,
      row: details.row === undefined ? null : details.row,
      col: details.col === undefined ? null : details.col,
      line: details.line === undefined ? null : details.line
    };
  }

  function parseMapText(text, options) {
    var errors = [];
    var warnings = [];
    var rooms = [];
    var room = null;
    var source = typeof text === "string" ? text : "";
    var lines;
    var seen = Object.create(null);
    var format = options && options.format === FORMAT_V2 ? FORMAT_V2 : FORMAT_V1;
    var customSymbols = Object.create(null);

    if (options && options.customSymbols) {
      if (Array.isArray(options.customSymbols)) {
        options.customSymbols.forEach(function (symbol) { customSymbols[symbol] = true; });
      } else {
        Object.keys(options.customSymbols).forEach(function (symbol) { customSymbols[symbol] = true; });
      }
    }
    if (typeof text !== "string") {
      errors.push(makeIssue("error", "map_text_type", "data.map must be text.", { path: "data.map" }));
    }
    if (utf8Bytes(source).length > MAX_TEXT_BYTES) {
      errors.push(makeIssue("error", "map_file_size", "data.map exceeds the loader's 4 MiB text-file limit.", { path: "data.map" }));
    }
    if (source.indexOf("\0") >= 0) {
      errors.push(makeIssue("error", "map_nul", "data.map contains an embedded NUL byte and cannot be parsed safely.", { path: "data.map" }));
      source = source.slice(0, source.indexOf("\0"));
    }
    if (source.charCodeAt(0) === 0xFEFF) {
      errors.push(makeIssue("error", "map_bom", "data.map starts with a UTF-8 BOM; the native parser expects the first map character directly.", { path: "data.map", line: 1 }));
      source = source.slice(1);
    }
    /* Native parse_map_file discards every CR byte while accumulating a line;
     * a lone or interior CR is not a line separator. */
    lines = source.replace(/\r/g, "").split("\n");

    function finishRoom(lineNumber) {
      if (!room) return;
      if (room.grid.length !== ROWS) {
        errors.push(makeIssue("error", "room_row_count", "Room \"" + room.id + "\" must contain exactly " + ROWS + " valid rows; found " + room.grid.length + ".", {
          path: "data.map", roomId: room.id, line: lineNumber
        }));
      }
    }

    lines.forEach(function (rawLine, index) {
      var lineNumber = index + 1;
      var trimmedLeft = rawLine.replace(/^[ \t\v\f]+/, "");
      var close;
      var id;
      var content;
      var rowIndex;
      var col;

      if (!trimmedLeft || trimmedLeft.charAt(0) === ";" || trimmedLeft.slice(0, 2) === "//") return;
      if (utf8Bytes(rawLine).length > 511) {
        errors.push(makeIssue("error", "map_line_size", "A data.map line exceeds the native parser's 511-byte line buffer.", { path: "data.map", line: lineNumber }));
      }
      if (trimmedLeft.charAt(0) === "[") {
        finishRoom(lineNumber - 1);
        room = null;
        close = trimmedLeft.indexOf("]");
        if (close < 0 || close !== trimmedLeft.length - 1) {
          errors.push(makeIssue("error", "invalid_room_header", "Room header must have the exact form [room_id], with nothing after it.", {
            path: "data.map", line: lineNumber
          }));
          return;
        }
        id = trimmedLeft.slice(1, close);
        if (!id) {
          errors.push(makeIssue("error", "empty_room_id", "Room id cannot be empty.", { path: "data.map", line: lineNumber }));
          return;
        }
        if (rooms.length >= MAX_PARSED_ROOMS) {
          errors.push(makeIssue("error", "parsed_room_cap", "data.map has more than the native parser cap of " + MAX_PARSED_ROOMS + " room sections.", { path: "data.map", line: lineNumber }));
          return;
        }
        if (utf8Bytes(id).length > 63) {
          errors.push(makeIssue("error", "room_id_bytes", "Room id \"" + id + "\" exceeds the 63-byte loader limit.", { path: "data.map", roomId: id, line: lineNumber }));
        }
        if (seen[id]) {
          errors.push(makeIssue("error", "duplicate_room_id", "Duplicate room id \"" + id + "\" in data.map.", {
            path: "data.map", roomId: id, line: lineNumber
          }));
          return;
        }
        room = { id: id, grid: [], rawRows: [], startLine: lineNumber };
        rooms.push(room);
        seen[id] = true;
        return;
      }
      if (trimmedLeft.charAt(0) === "\"") {
        if (!room) {
          errors.push(makeIssue("error", "row_before_room", "Found a quoted row before a valid room header.", {
            path: "data.map", line: lineNumber
          }));
          return;
        }
        close = trimmedLeft.lastIndexOf("\"");
        if (close <= 0 || close !== trimmedLeft.length - 1) {
          errors.push(makeIssue("error", "invalid_quoted_row", "Room rows must end at their last quote, with no trailing whitespace or comment.", {
            path: "data.map", roomId: room.id, row: room.grid.length, line: lineNumber
          }));
          return;
        }
        content = trimmedLeft.slice(1, close);
        rowIndex = room.grid.length;
        room.rawRows.push({ text: content, line: lineNumber });
        if (rowIndex >= ROWS) {
          errors.push(makeIssue("error", "too_many_rows", "Room \"" + room.id + "\" has more than " + ROWS + " valid rows.", {
            path: "data.map", roomId: room.id, row: rowIndex, line: lineNumber
          }));
          return;
        }
        if (utf8Bytes(content).length !== COLS) {
          errors.push(makeIssue("error", "row_width", "Room \"" + room.id + "\" row text must contain exactly " + COLS + " ASCII bytes; found " + utf8Bytes(content).length + ".", {
            path: "data.map", roomId: room.id, row: rowIndex, line: lineNumber
          }));
          /* The native parser does not advance row_count after a bad width. */
          return;
        }
        for (col = 0; col < content.length; col += 1) {
          var glyph = content.charAt(col);
          if (!GLYPH_SET[glyph] && !(format === FORMAT_V2 && customSymbols[glyph])) {
            errors.push(makeIssue("error", "invalid_glyph", "Glyph \"" + glyph + "\" is neither native nor declared by this V2 tileset.", {
              path: "data.map", roomId: room.id, row: rowIndex, col: col, line: lineNumber
            }));
          }
        }
        room.grid.push(content.split(""));
        return;
      }
      errors.push(makeIssue("error", "unexpected_map_line", "Expected a [room_id] header, quoted row, blank line, or comment.", {
        path: "data.map", roomId: room ? room.id : null, line: lineNumber
      }));
    });

    finishRoom(lines.length);
    if (!rooms.length) {
      errors.push(makeIssue("error", "no_rooms", "data.map does not contain any room sections.", { path: "data.map" }));
    }
    return {
      format: format,
      sourceText: typeof text === "string" ? text : "",
      rooms: rooms,
      order: rooms.map(function (entry) { return entry.id; }),
      errors: errors,
      warnings: warnings,
      issues: errors.concat(warnings),
      valid: errors.length === 0
    };
  }

  function unwrapDocument(value) {
    return value && value.document && value.document !== value ? value.document : value;
  }

  function roomList(document) {
    var rooms = document && document.rooms;
    if (Array.isArray(rooms)) return rooms;
    if (rooms && typeof rooms === "object") {
      return Object.keys(rooms).map(function (id) {
        var room = deepClone(rooms[id] || {});
        if (!room.id) room.id = id;
        return room;
      });
    }
    return [];
  }

  function getGrid(room) {
    return room && (room.grid || room.rows || room.tiles || room.data);
  }

  function rowString(row) {
    if (Array.isArray(row)) return row.join("");
    if (typeof row === "string") return row;
    return "";
  }

  function serializeMapText(value, options) {
    var document = unwrapDocument(value) || {};
    var rooms = Array.isArray(document) ? document : roomList(document);
    var format = document.format || FORMAT_V1;
    var chunks;
    if (format === FORMAT_V2 && document._preserved && typeof document._preserved.dataMapText === "string" && !(options && options.forceRebuild)) {
      return document._preserved.dataMapText;
    }
    if (format !== FORMAT_V1 && format !== FORMAT_V2) throw new Error("Unsupported map format.");
    chunks = ["; " + format, ""];
    if (!rooms.length) throw new Error("Cannot serialize data.map without at least one room.");
    rooms.forEach(function (room, roomIndex) {
      var id = room && typeof room.id === "string" ? room.id : "";
      var grid = getGrid(room);
      if (!id || /[\]\r\n\0]/.test(id)) throw new Error("Room " + (roomIndex + 1) + " has an invalid id.");
      if (utf8Bytes(id).length > 63) throw new Error("Room \"" + id + "\" exceeds the 63-byte id limit.");
      if (!Array.isArray(grid) || grid.length !== ROWS) throw new Error("Room \"" + id + "\" must have exactly " + ROWS + " rows.");
      chunks.push("[" + id + "]");
      grid.forEach(function (row, rowIndex) {
        var text = rowString(row);
        if (utf8Bytes(text).length !== COLS || text.length !== COLS) throw new Error("Room \"" + id + "\" row " + (rowIndex + 1) + " must have exactly " + COLS + " ASCII glyphs.");
        for (var col = 0; col < text.length; col += 1) {
          if (!GLYPH_SET[text.charAt(col)] && format !== FORMAT_V2) throw new Error("Room \"" + id + "\" row " + (rowIndex + 1) + " contains invalid glyph \"" + text.charAt(col) + "\".");
        }
        chunks.push("\"" + text + "\"");
      });
      if (roomIndex !== rooms.length - 1) chunks.push("");
    });
    var output = chunks.join("\n") + "\n";
    if (utf8Bytes(output).length > MAX_TEXT_BYTES) throw new Error("Serialized data.map exceeds 4 MiB.");
    return output;
  }

  function camelOrSnake(object, camel, snake, fallback) {
    if (object && object[camel] !== undefined) return object[camel];
    if (object && object[snake] !== undefined) return object[snake];
    return fallback;
  }

  function bankToJson(bank) {
    var output = {};
    if (!bank || typeof bank !== "object") return output;
    COLOR_KEYS.forEach(function (key) {
      var value = bank[key];
      var converted;
      if (value === undefined || value === null || value === "") return;
      if (typeof value === "string") converted = hexToColor(value);
      else if (Array.isArray(value) && value.length === 3) {
        converted = value.map(function (channel) { return Number(channel); });
        if (converted.some(function (channel) { return channel > 1; })) {
          converted = converted.map(function (channel) { return Math.round((channel / 255) * 1000000) / 1000000; });
        }
      }
      if (converted && converted.length === 3 && converted.every(function (channel) { return Number.isFinite(channel); })) output[key] = converted;
    });
    return output;
  }

  function hasKeys(object) {
    return object && Object.keys(object).length > 0;
  }

  function appearanceToJson(appearance, includeFallback) {
    var output = {};
    var source = appearance && typeof appearance === "object" ? appearance : {};
    var primary = bankToJson(source.primary || (includeFallback ? COLOR_DEFAULTS : null));
    var mirror = bankToJson(source.mirror);
    if (hasKeys(primary)) output.primary = primary;
    if (hasKeys(mirror)) output.mirror = mirror;
    return output;
  }

  function buildDataObject(value) {
    var document = unwrapDocument(value) || {};
    if (document.format === FORMAT_V2 && document._preserved && document._preserved.dataObject) return deepClone(document._preserved.dataObject);
    var rooms = roomList(document);
    var rules = document.rules || {};
    var defaults = document.defaults && document.defaults.room ? document.defaults.room : (document.defaults || {});
    var layout = document.layout || {};
    var data = {
      format: document.format === FORMAT_V2 ? FORMAT_V2 : FORMAT_V1,
      name: typeof document.name === "string" ? document.name : "",
      author: typeof document.author === "string" ? document.author : "",
      description: typeof document.description === "string" ? document.description : "",
      sort_order: camelOrSnake(document, "sortOrder", "sort_order", 0),
      rules: {
        mode: rules.mode === "karate" ? "karate" : "swords",
        round_end_rooms: camelOrSnake(rules, "roundEndRooms", "round_end_rooms", "inner_only"),
        score_target: camelOrSnake(rules, "scoreTarget", "score_target", null),
        armed_respawn_limit: camelOrSnake(rules, "armedRespawnLimit", "armed_respawn_limit", 4)
      },
      layout: {
        kind: LAYOUT_KIND,
        room_format: ROOM_FORMAT,
        order: rooms.map(function (room) { return room.id; })
      },
      defaults: {
        room: {
          ambient: defaults.ambient === undefined ? "none" : defaults.ambient
        }
      },
      rooms: {}
    };
    var defaultAppearance = appearanceToJson(defaults.appearance, false);
    /* An empty appearance object is semantically meaningful to the loader's
     * primary/mirror copy rules, so preserve authored presence. */
    if (hasOwn(defaults, "appearance")) data.defaults.room.appearance = defaultAppearance;
    /* V1 ids are opaque loader strings, not display labels. Trimming an
     * imported id changes selector/online identity, so retain it verbatim. */
    var id = typeof document.id === "string" ? document.id : "";
    if (id) data.id = id;
    if (data.format === FORMAT_V2) data.tileset = deepClone(document.tileset || { tiles: [] });
    /* Preserve an explicit, valid room order when it describes exactly these rooms. */
    var requestedOrder = layout.order;
    if (Array.isArray(requestedOrder) && requestedOrder.length === rooms.length) {
      var roomIds = rooms.map(function (room) { return room.id; }).sort();
      var orderIds = requestedOrder.slice().sort();
      if (roomIds.every(function (roomId, index) { return roomId === orderIds[index]; })) data.layout.order = requestedOrder.slice();
    }
    rooms.forEach(function (room) {
      var config = { ambient: room.ambient === undefined ? data.defaults.room.ambient : room.ambient };
      var appearance = appearanceToJson(room.appearance, false);
      if (hasOwn(room, "appearance")) config.appearance = appearance;
      if (room.hook === null) config.hook = null;
      data.rooms[room.id] = config;
    });
    return data;
  }

  function serializeDataJson(document, options) {
    document = unwrapDocument(document) || {};
    if (document.format === FORMAT_V2 && document._preserved && typeof document._preserved.dataJsonText === "string" && !(options && options.forceRebuild)) {
      return document._preserved.dataJsonText;
    }
    var output = JSON.stringify(buildDataObject(document), null, 2) + "\n";
    if (utf8Bytes(output).length > MAX_TEXT_BYTES) throw new Error("Serialized data.json exceeds 4 MiB.");
    return output;
  }

  function jsonBankToHex(bank) {
    var output = {};
    if (!bank || typeof bank !== "object" || Array.isArray(bank)) return output;
    COLOR_KEYS.forEach(function (key) {
      if (bank[key] !== undefined) output[key] = colorToHex(bank[key]) || bank[key];
    });
    return output;
  }

  function jsonAppearanceToHex(appearance) {
    if (appearance === undefined) return undefined;
    if (!appearance || typeof appearance !== "object" || Array.isArray(appearance)) return appearance;
    var output = {};
    if (appearance.primary !== undefined) output.primary = jsonBankToHex(appearance.primary);
    if (appearance.mirror !== undefined) output.mirror = jsonBankToHex(appearance.mirror);
    return output;
  }

  function isPlainObject(value) {
    return !!value && typeof value === "object" && !Array.isArray(value);
  }

  function hasOwn(object, key) {
    return Object.prototype.hasOwnProperty.call(object || {}, key);
  }

  function collectCustomSymbols(json) {
    var symbols = Object.create(null);
    var tiles = isPlainObject(json && json.tileset) && Array.isArray(json.tileset.tiles) ? json.tileset.tiles : [];
    tiles.forEach(function (tile) {
      if (isPlainObject(tile) && typeof tile.symbol === "string" && tile.symbol.length === 1 && tile.symbol.charCodeAt(0) >= 0x20 && tile.symbol.charCodeAt(0) <= 0x7E) {
        symbols[tile.symbol] = true;
      }
    });
    return symbols;
  }

  function normalizeAssets(assets) {
    var output = {};
    if (!assets) return output;
    if (Array.isArray(assets)) {
      assets.forEach(function (entry) {
        if (entry && typeof entry.name === "string") output[entry.name] = entry.data !== undefined ? entry.data : entry.content;
      });
    } else if (typeof assets === "object") {
      Object.keys(assets).forEach(function (name) { output[name] = assets[name]; });
    }
    return output;
  }

  /* JSON.parse keeps the last duplicate property, while the game's parser
   * rejects duplicates in each object scope. Walk the original token stream
   * after syntax parsing succeeds so escaped-equivalent keys are caught too. */
  function normalizeNativeJsonEscapes(source) {
    var output = [];
    var nulEscapes = [];
    var index = 0;
    var line = 1;
    var col = 1;
    var inString = false;

    function advancePosition(ch) {
      if (ch === "\n") { line += 1; col = 1; }
      else col += 1;
    }

    while (index < source.length) {
      var ch = source.charAt(index);
      if (!inString) {
        output.push(ch);
        if (ch === "\"") inString = true;
        advancePosition(ch);
        index += 1;
        continue;
      }
      if (ch === "\"") {
        output.push(ch);
        inString = false;
        advancePosition(ch);
        index += 1;
        continue;
      }
      if (ch === "\\" && source.charAt(index + 1) === "u" && /^[0-9a-fA-F]{4}$/.test(source.substr(index + 2, 4))) {
        var codepoint = parseInt(source.substr(index + 2, 4), 16);
        if (codepoint === 0) nulEscapes.push({ line: line, col: col });
        /* custom_maps.c intentionally stores only ASCII \u escapes; every
         * non-ASCII code unit becomes '?'. Keep the token six bytes long so
         * duplicate-key diagnostics retain useful source columns. */
        output.push(codepoint > 0x7F ? "\\u003f" : source.substr(index, 6));
        for (var unicodeOffset = 0; unicodeOffset < 6; unicodeOffset += 1) advancePosition(source.charAt(index + unicodeOffset));
        index += 6;
        continue;
      }
      if (ch === "\\" && index + 1 < source.length) {
        output.push(ch, source.charAt(index + 1));
        advancePosition(ch);
        advancePosition(source.charAt(index + 1));
        index += 2;
        continue;
      }
      output.push(ch);
      advancePosition(ch);
      index += 1;
    }
    return { text: output.join(""), nulEscapes: nulEscapes };
  }

  function findDuplicateJsonKeys(source) {
    var duplicates = [];
    var index = 0;
    var line = 1;
    var col = 1;

    function advance() {
      var ch = source.charAt(index++);
      if (ch === "\n") { line += 1; col = 1; }
      else col += 1;
      return ch;
    }
    function skipWhitespace() { while (index < source.length && /[ \t\r\n]/.test(source.charAt(index))) advance(); }
    function parseString() {
      var start = index;
      var startLine = line;
      var startCol = col;
      advance();
      while (index < source.length) {
        var ch = advance();
        if (ch === "\\") {
          if (index < source.length) advance();
        } else if (ch === "\"") {
          break;
        }
      }
      var token = source.slice(start, index);
      var decoded;
      try { decoded = JSON.parse(token); } catch (error) { decoded = token.slice(1, -1); }
      return { value: decoded, line: startLine, col: startCol };
    }
    function childPath(path, key) { return path ? path + "." + key : key; }
    function parseValue(path) {
      skipWhitespace();
      var ch = source.charAt(index);
      if (ch === "{") { parseObject(path); return; }
      if (ch === "[") { parseArray(path); return; }
      if (ch === "\"") { parseString(); return; }
      while (index < source.length && !/[\s,\]}]/.test(source.charAt(index))) advance();
    }
    function parseObject(path) {
      var seenKeys = [];
      advance();
      skipWhitespace();
      if (source.charAt(index) === "}") { advance(); return; }
      while (index < source.length) {
        skipWhitespace();
        var key = parseString();
        var keyPath = childPath(path, key.value);
        if (seenKeys.indexOf(key.value) >= 0) duplicates.push({ key: key.value, path: keyPath, line: key.line, col: key.col });
        else seenKeys.push(key.value);
        skipWhitespace();
        if (source.charAt(index) === ":") advance();
        parseValue(keyPath);
        skipWhitespace();
        if (source.charAt(index) === ",") { advance(); continue; }
        if (source.charAt(index) === "}") advance();
        return;
      }
    }
    function parseArray(path) {
      var item = 0;
      advance();
      skipWhitespace();
      if (source.charAt(index) === "]") { advance(); return; }
      while (index < source.length) {
        parseValue(path + "[" + item + "]");
        item += 1;
        skipWhitespace();
        if (source.charAt(index) === ",") { advance(); continue; }
        if (source.charAt(index) === "]") advance();
        return;
      }
    }
    skipWhitespace();
    if (source.charAt(index) === "{") parseObject("");
    else parseValue("");
    return duplicates;
  }

  function parsePackage(dataJsonText, dataMapText, options) {
    var errors = [];
    var warnings = [];
    var json = null;
    var jsonForParse = typeof dataJsonText === "string" ? dataJsonText : "";
    var format;
    var parsedMap;
    var document;
    var layoutOrder;
    var mapById = Object.create(null);
    var orderedRooms = [];
    var used = Object.create(null);
    var defaultsRoom;
    var jsonRoomConfigs;
    var assets;
    var mapLuaPresent;
    var mapLua;
    var nativeJsonEscapes;

    options = typeof options === "string" ? { mapLua: options } : (options || {});
    assets = normalizeAssets(options.assets || options.files);
    mapLuaPresent = hasOwn(options, "mapLua") || hasOwn(options, "mapLuaText");
    mapLua = hasOwn(options, "mapLua") ? options.mapLua : options.mapLuaText;

    if (typeof dataJsonText !== "string") {
      errors.push(makeIssue("error", "json_text_type", "data.json must be text.", { path: "data.json" }));
    } else {
      if (utf8Bytes(dataJsonText).length > MAX_TEXT_BYTES) {
        errors.push(makeIssue("error", "json_file_size", "data.json exceeds the loader's 4 MiB text-file limit.", { path: "data.json" }));
      }
      if (dataJsonText.indexOf("\0") >= 0) {
        errors.push(makeIssue("error", "json_nul", "data.json contains an embedded NUL byte.", { path: "data.json" }));
        jsonForParse = dataJsonText.slice(0, dataJsonText.indexOf("\0"));
      }
      if (jsonForParse.charCodeAt(0) === 0xFEFF) {
        errors.push(makeIssue("error", "json_bom", "data.json starts with a UTF-8 BOM; remove it for native-loader parity.", { path: "data.json" }));
        jsonForParse = jsonForParse.slice(1);
      }
      try {
        nativeJsonEscapes = normalizeNativeJsonEscapes(jsonForParse);
        json = JSON.parse(nativeJsonEscapes.text);
        nativeJsonEscapes.nulEscapes.forEach(function (location) {
          errors.push(makeIssue("error", "json_nul_escape", "data.json contains a \\u0000 escape; the native JSON parser rejects NUL in every string.", { path: "data.json", line: location.line, col: location.col }));
        });
        findDuplicateJsonKeys(nativeJsonEscapes.text).forEach(function (duplicate) {
          errors.push(makeIssue("error", "duplicate_json_key", "data.json repeats object key \"" + duplicate.key + "\" in the same object scope.", { path: duplicate.path, line: duplicate.line, col: duplicate.col }));
        });
      } catch (error) {
        errors.push(makeIssue("error", "invalid_json", "data.json is not valid JSON: " + error.message, { path: "data.json" }));
      }
    }
    if (!isPlainObject(json)) {
      if (json !== null) errors.push(makeIssue("error", "json_root", "The data.json root must be an object.", { path: "data.json" }));
      json = {};
    }
    format = json.format === FORMAT_V2 ? FORMAT_V2 : FORMAT_V1;
    if (json.format !== FORMAT_V1 && json.format !== FORMAT_V2) {
      errors.push(makeIssue("error", "format", "format must be exactly \"" + FORMAT_V1 + "\" or \"" + FORMAT_V2 + "\".", { path: "format" }));
    }
    parsedMap = parseMapText(dataMapText, { format: format, customSymbols: collectCustomSymbols(json) });
    errors = errors.concat(parsedMap.errors);
    warnings = warnings.concat(parsedMap.warnings);

    parsedMap.rooms.forEach(function (room) { mapById[room.id] = room; });
    layoutOrder = isPlainObject(json.layout) && Array.isArray(json.layout.order) ? json.layout.order.slice() : [];
    layoutOrder.forEach(function (id) {
      if (typeof id === "string" && mapById[id] && !used[id]) {
        orderedRooms.push(mapById[id]);
        used[id] = true;
      }
    });
    parsedMap.rooms.forEach(function (room) {
      if (!used[room.id]) {
        orderedRooms.push(room);
        used[room.id] = true;
      }
    });

    defaultsRoom = isPlainObject(json.defaults) && isPlainObject(json.defaults.room) ? json.defaults.room : {};
    jsonRoomConfigs = isPlainObject(json.rooms) ? json.rooms : {};
    document = {
      format: json.format,
      formatVersion: format === FORMAT_V2 ? 2 : 1,
      editable: format === FORMAT_V1,
      id: json.id === undefined ? "" : json.id,
      name: json.name === undefined ? "" : json.name,
      author: json.author === undefined ? "" : json.author,
      description: json.description === undefined ? "" : json.description,
      sortOrder: json.sort_order === undefined ? 0 : json.sort_order,
      rules: {
        mode: isPlainObject(json.rules) && json.rules.mode !== undefined ? json.rules.mode : "swords",
        roundEndRooms: isPlainObject(json.rules) && json.rules.round_end_rooms !== undefined ? json.rules.round_end_rooms : "inner_only",
        scoreTarget: isPlainObject(json.rules) && json.rules.score_target !== undefined ? json.rules.score_target : null,
        armedRespawnLimit: isPlainObject(json.rules) && json.rules.armed_respawn_limit !== undefined ? json.rules.armed_respawn_limit : 4
      },
      layout: {
        kind: isPlainObject(json.layout) ? json.layout.kind : undefined,
        roomFormat: isPlainObject(json.layout) ? json.layout.room_format : undefined,
        order: layoutOrder
      },
      defaults: {
        ambient: defaultsRoom.ambient === undefined ? "none" : defaultsRoom.ambient
      },
      rooms: orderedRooms.map(function (parsedRoom) {
        var config = isPlainObject(jsonRoomConfigs[parsedRoom.id]) ? jsonRoomConfigs[parsedRoom.id] : {};
        var entry = {
          id: parsedRoom.id,
          grid: parsedRoom.grid.map(function (row) { return row.slice(); }),
          ambient: config.ambient === undefined ? (defaultsRoom.ambient === undefined ? "none" : defaultsRoom.ambient) : config.ambient
        };
        if (hasOwn(config, "appearance")) entry.appearance = deepClone(config.appearance);
        if (hasOwn(config, "hook")) entry.hook = config.hook;
        return entry;
      })
    };
    if (hasOwn(defaultsRoom, "appearance")) document.defaults.appearance = deepClone(defaultsRoom.appearance);

    validateRawManifest(json, format, errors, warnings);

    if (format === FORMAT_V2) {
      document.tileset = deepClone(json.tileset);
      document.mapLua = mapLuaPresent ? mapLua : undefined;
      document.assets = deepClone(assets);
      document._preserved = {
        dataJsonText: typeof dataJsonText === "string" ? dataJsonText : "",
        dataMapText: typeof dataMapText === "string" ? dataMapText : "",
        dataJsonBytes: copyBytes(options.dataJsonBytes),
        dataMapBytes: copyBytes(options.dataMapBytes),
        dataObject: deepClone(json),
        mapLuaPresent: mapLuaPresent,
        mapLua: mapLuaPresent ? mapLua : undefined,
        mapLuaBytes: mapLuaPresent ? copyBytes(options.mapLuaBytes) : null,
        assets: deepClone(assets),
        folderId: options.folderId || null
      };
      warnings.push(makeIssue("warning", "v2_read_only", "This V2 package is preserved byte-for-byte. Greggnogg's native-tile canvas will not rewrite its manifest, map, PNG assets, or map.lua.", { path: "format" }));
      validateV2Manifest(json, assets, errors, warnings);
    } else if (json.tileset !== undefined) {
      errors.push(makeIssue("error", "v1_tileset", "tileset requires eggnogg-map/v2.", { path: "tileset" }));
    }

    if (mapLuaPresent) {
      if (format !== FORMAT_V2) errors.push(makeIssue("error", "script_requires_v2", "map.lua is only supported by eggnogg-map/v2.", { path: "map.lua" }));
      if (typeof mapLua !== "string") errors.push(makeIssue("error", "script_text_type", "map.lua must be decoded text.", { path: "map.lua" }));
      else {
        if (utf8Bytes(mapLua).length > MAX_SCRIPT_BYTES) errors.push(makeIssue("error", "script_size", "map.lua exceeds the 256 KiB limit.", { path: "map.lua" }));
        if (mapLua.indexOf("\0") >= 0) errors.push(makeIssue("error", "script_nul", "map.lua contains an embedded NUL byte.", { path: "map.lua" }));
      }
    }

    Object.keys(jsonRoomConfigs).forEach(function (id) {
      if (!mapById[id]) errors.push(makeIssue("error", "unknown_room_config", "Room configuration refers to unknown room \"" + id + "\".", { path: "rooms." + id, roomId: id }));
    });
    var validation = validateDocument(document);
    errors = errors.concat(validation.errors);
    warnings = warnings.concat(validation.warnings);
    var result = {
      document: document,
      format: format,
      formatVersion: format === FORMAT_V2 ? 2 : 1,
      editable: format === FORMAT_V1,
      preserved: format === FORMAT_V2,
      errors: errors,
      warnings: warnings,
      issues: errors.concat(warnings),
      valid: errors.length === 0
    };
    Object.keys(document).forEach(function (key) { if (result[key] === undefined) result[key] = document[key]; });
    return result;
  }

  function pushSchemaError(errors, code, message, path) {
    errors.push(makeIssue("error", code, message, { path: path }));
  }

  function warnUnknownKeys(object, allowed, path, warnings) {
    if (!isPlainObject(object)) return;
    Object.keys(object).forEach(function (key) {
      if (allowed.indexOf(key) < 0) warnings.push(makeIssue("warning", "unknown_key", (path || "data.json") + " contains unknown compatibility key \"" + key + "\".", { path: path ? path + "." + key : key }));
    });
  }

  function validateRawAppearance(value, path, errors, warnings) {
    if (value === undefined) return;
    if (!isPlainObject(value)) {
      pushSchemaError(errors, "appearance_type", path + " must be an object.", path);
      return;
    }
    warnUnknownKeys(value, ["primary", "mirror"], path, warnings);
    ["primary", "mirror"].forEach(function (bankName) {
      var bank = value[bankName];
      if (bank === undefined) return;
      if (!isPlainObject(bank)) {
        pushSchemaError(errors, "color_bank_type", path + "." + bankName + " must be an object.", path + "." + bankName);
        return;
      }
      warnUnknownKeys(bank, COLOR_KEYS, path + "." + bankName, warnings);
      COLOR_KEYS.forEach(function (key) {
        var triplet = bank[key];
        if (triplet !== undefined && (!Array.isArray(triplet) || triplet.length !== 3 || !triplet.every(function (channel) { return typeof channel === "number" && Number.isFinite(channel) && channel >= 0 && channel <= 1; }))) {
          pushSchemaError(errors, "invalid_color", path + "." + bankName + "." + key + " must contain three finite channels from 0 to 1.", path + "." + bankName + "." + key);
        }
      });
    });
    if (isPlainObject(value.primary) && isPlainObject(value.mirror)) {
      var duplicate = COLOR_KEYS.some(function (key) {
        return Array.isArray(value.primary[key]) && Array.isArray(value.mirror[key]) && value.primary[key].length === 3 && value.primary[key].every(function (channel, index) { return channel === value.mirror[key][index]; });
      });
      if (duplicate) warnings.push(makeIssue("warning", "duplicate_explicit_color", path + " defines at least one identical explicit primary/mirror colour.", { path: path }));
    }
  }

  function validateRawRoomConfig(config, path, errors, warnings) {
    if (!isPlainObject(config)) {
      pushSchemaError(errors, "room_config_type", path + " must be an object.", path);
      return;
    }
    warnUnknownKeys(config, ["ambient", "appearance", "hook"], path, warnings);
    if (config.ambient !== undefined && !validAmbient(config.ambient)) pushSchemaError(errors, "ambient", path + ".ambient must be a supported alias or integer 0..9.", path + ".ambient");
    validateRawAppearance(config.appearance, path + ".appearance", errors, warnings);
    if (config.hook !== undefined && config.hook !== null) pushSchemaError(errors, "unsupported_hook", path + ".hook must be omitted or null; map.lua is discovered at package level.", path + ".hook");
  }

  function validateRawManifest(json, format, errors, warnings) {
    var topKeys = ["format", "id", "name", "author", "description", "sort_order", "rules", "layout", "defaults", "rooms", "tileset"];
    warnUnknownKeys(json, topKeys, "", warnings);
    [["name", 127, true], ["author", 127, true], ["description", 255, false]].forEach(function (spec) {
      var key = spec[0];
      var value = json[key];
      if (value === undefined) {
        if (spec[2]) pushSchemaError(errors, key + "_required", key + " is required.", key);
        return;
      }
      if (typeof value !== "string" || (spec[2] && !value)) pushSchemaError(errors, key + "_type", key + (spec[2] ? " must be a non-empty string." : " must be a string."), key);
      else if (value.indexOf("\0") >= 0) pushSchemaError(errors, key + "_nul", key + " contains an embedded NUL character.", key);
      else if (utf8Bytes(value).length > spec[1]) pushSchemaError(errors, key + "_bytes", key + " exceeds the loader's " + spec[1] + "-byte storage limit.", key);
    });
    if (json.id !== undefined && typeof json.id !== "string") pushSchemaError(errors, "id_type", "id must be a string.", "id");
    else if (typeof json.id === "string" && json.id.indexOf("\0") >= 0) pushSchemaError(errors, "id_nul", "id contains an embedded NUL character.", "id");
    if (format === FORMAT_V2) {
      if (typeof json.id !== "string" || !json.id) pushSchemaError(errors, "v2_id_required", "V2 requires a non-empty stable id.", "id");
      else if (utf8Bytes(json.id).length > 43 || !/^[A-Za-z0-9_][A-Za-z0-9._-]*$/.test(json.id)) pushSchemaError(errors, "v2_id", "V2 id must fit 43 bytes and use ASCII letters, numbers, '.', '_' or '-' without starting '.' or '-'.", "id");
    } else if (typeof json.id === "string" && utf8Bytes(json.id).length > 63) {
      pushSchemaError(errors, "id_bytes", "V1 id exceeds the loader's 63-byte storage limit.", "id");
    }
    if (json.sort_order !== undefined && (!Number.isInteger(json.sort_order) || json.sort_order < -2147483648 || json.sort_order > 2147483647)) pushSchemaError(errors, "sort_order", "sort_order must be a signed 32-bit integer.", "sort_order");
    if (json.rules !== undefined) {
      if (!isPlainObject(json.rules)) pushSchemaError(errors, "rules_type", "rules must be an object.", "rules");
      else {
        warnUnknownKeys(json.rules, ["mode", "round_end_rooms", "score_target", "armed_respawn_limit"], "rules", warnings);
        if (json.rules.mode === undefined) pushSchemaError(errors, "rules_mode_required", "rules.mode is required when rules exists.", "rules.mode");
        else if (json.rules.mode !== "swords" && json.rules.mode !== "karate") pushSchemaError(errors, "rules_mode", "rules.mode must be swords or karate.", "rules.mode");
        if (json.rules.round_end_rooms !== undefined && json.rules.round_end_rooms !== "inner_only" && json.rules.round_end_rooms !== "any") pushSchemaError(errors, "round_end_rooms", "rules.round_end_rooms must be inner_only or any.", "rules.round_end_rooms");
        if (json.rules.score_target !== undefined && json.rules.score_target !== null && (!Number.isInteger(json.rules.score_target) || json.rules.score_target <= 0 || json.rules.score_target > 2147483647)) pushSchemaError(errors, "score_target", "rules.score_target must be null or a positive integer.", "rules.score_target");
        if (json.rules.armed_respawn_limit !== undefined && (!Number.isInteger(json.rules.armed_respawn_limit) || json.rules.armed_respawn_limit < 0 || json.rules.armed_respawn_limit > 2147483647)) pushSchemaError(errors, "armed_respawn_limit", "rules.armed_respawn_limit must be a non-negative integer.", "rules.armed_respawn_limit");
      }
    }
    if (!isPlainObject(json.layout)) pushSchemaError(errors, "layout_required", "layout is a required object.", "layout");
    else {
      warnUnknownKeys(json.layout, ["kind", "room_format", "order"], "layout", warnings);
      if (json.layout.kind !== LAYOUT_KIND) pushSchemaError(errors, "layout_kind", "layout.kind must be \"" + LAYOUT_KIND + "\".", "layout.kind");
      if (json.layout.room_format !== ROOM_FORMAT) pushSchemaError(errors, "room_format", "layout.room_format must be \"" + ROOM_FORMAT + "\".", "layout.room_format");
      if (!Array.isArray(json.layout.order) || !json.layout.order.length) pushSchemaError(errors, "layout_order", "layout.order must be a non-empty array.", "layout.order");
    }
    if (json.defaults !== undefined) {
      if (!isPlainObject(json.defaults)) pushSchemaError(errors, "defaults_type", "defaults must be an object.", "defaults");
      else {
        warnUnknownKeys(json.defaults, ["room"], "defaults", warnings);
        if (json.defaults.room !== undefined) validateRawRoomConfig(json.defaults.room, "defaults.room", errors, warnings);
      }
    }
    if (json.rooms !== undefined) {
      if (!isPlainObject(json.rooms)) pushSchemaError(errors, "rooms_type", "rooms must be an object.", "rooms");
      else Object.keys(json.rooms).forEach(function (id) { validateRawRoomConfig(json.rooms[id], "rooms." + id, errors, warnings); });
    }
  }

  function rejectUnknownKeys(object, allowed, path, errors) {
    if (!isPlainObject(object)) return;
    Object.keys(object).forEach(function (key) {
      if (allowed.indexOf(key) < 0) pushSchemaError(errors, "unknown_v2_key", path + " contains unknown key \"" + key + "\".", path + "." + key);
    });
  }

  function validateIntegerField(object, key, path, low, high, fallback, errors) {
    var value = object[key];
    if (value === undefined) return fallback;
    if (!Number.isInteger(value) || value < low || value > high) {
      pushSchemaError(errors, "v2_integer_range", path + "." + key + " must be an integer from " + low + " to " + high + ".", path + "." + key);
      return fallback;
    }
    return value;
  }

  function validateNumberField(object, key, path, low, high, fallback, errors, nonzero) {
    var value = object[key];
    if (value === undefined) return fallback;
    if (typeof value !== "number" || !Number.isFinite(value) || value < low || value > high || (nonzero && value === 0)) {
      pushSchemaError(errors, "v2_number_range", path + "." + key + " must be a finite" + (nonzero ? " non-zero" : "") + " number from " + low + " to " + high + ".", path + "." + key);
      return fallback;
    }
    return value;
  }

  function validateBooleanField(object, key, path, errors) {
    if (object[key] !== undefined && typeof object[key] !== "boolean") pushSchemaError(errors, "v2_boolean", path + "." + key + " must be boolean.", path + "." + key);
  }

  function assetBytes(value) {
    if (value && value.data !== undefined && !(value instanceof Uint8Array)) return assetBytes(value.data);
    if (value && value.bytes !== undefined && !(value instanceof Uint8Array)) return assetBytes(value.bytes);
    if (value instanceof Uint8Array) return value;
    if (Array.isArray(value)) return new Uint8Array(value);
    if (typeof value === "string" && /^data:image\/png;base64,/i.test(value) && typeof atob === "function") {
      var raw = atob(value.slice(value.indexOf(",") + 1));
      var decoded = new Uint8Array(raw.length);
      for (var dataIndex = 0; dataIndex < raw.length; dataIndex += 1) decoded[dataIndex] = raw.charCodeAt(dataIndex) & 0xff;
      return decoded;
    }
    if (typeof ArrayBuffer !== "undefined" && value instanceof ArrayBuffer) return new Uint8Array(value);
    if (typeof ArrayBuffer !== "undefined" && ArrayBuffer.isView && ArrayBuffer.isView(value)) return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    return null;
  }

  function copyBytes(value) {
    var bytes = assetBytes(value);
    var output;
    var index;
    if (!bytes) return null;
    output = new Uint8Array(bytes.length);
    if (output.set) output.set(bytes);
    else for (index = 0; index < bytes.length; index += 1) output[index] = bytes[index];
    return output;
  }

  function pngInfo(value) {
    var bytes = assetBytes(value);
    var signature = [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A];
    var i;
    if (!bytes || bytes.length < 24) return null;
    for (i = 0; i < signature.length; i += 1) if (bytes[i] !== signature[i]) return null;
    if (bytes[8] !== 0 || bytes[9] !== 0 || bytes[10] !== 0 || bytes[11] !== 13 || bytes[12] !== 0x49 || bytes[13] !== 0x48 || bytes[14] !== 0x44 || bytes[15] !== 0x52) return null;
    var width = (bytes[16] * 0x1000000 + bytes[17] * 0x10000 + bytes[18] * 0x100 + bytes[19]) >>> 0;
    var height = (bytes[20] * 0x1000000 + bytes[21] * 0x10000 + bytes[22] * 0x100 + bytes[23]) >>> 0;
    return width && height ? { width: width, height: height } : null;
  }

  function validateV2Manifest(json, assets, errors, warnings) {
    var tileset = json.tileset;
    var topKeys = ["sprite_sheet", "asset_sha256", "cell_w", "cell_h", "padding", "native_layout", "tiles"];
    var tileKeys = ["id", "symbol", "name", "native_glyph", "sprite_sheet", "asset_sha256", "sprite_index", "frame_count", "frame_ticks", "animation", "layer", "mirror_with_room", "random_phase", "native_visual", "offset_x", "offset_y", "scale_x", "scale_y", "angle_degrees", "tint", "cell_w", "cell_h", "padding", "collision", "force_mode", "force_x", "force_y", "max_speed_x", "max_speed_y"];
    var safeNative = "!#()*+-12:=@ACEFHIOPQSWXZ^_`cefilmquvwx|";
    var assetLookup = Object.create(null);
    var sheetConfigs = Object.create(null);
    var totalKnownCells = 0;
    var seenSymbols = Object.create(null);
    var seenIds = Object.create(null);

    Object.keys(assets || {}).forEach(function (name) { assetLookup[name.toLowerCase()] = assets[name]; });
    if (tileset === undefined) return;
    if (!isPlainObject(tileset)) {
      pushSchemaError(errors, "tileset_type", "tileset must be an object.", "tileset");
      return;
    }
    rejectUnknownKeys(tileset, topKeys, "tileset", errors);
    var topCellW = validateIntegerField(tileset, "cell_w", "tileset", 1, 512, 16, errors);
    var topCellH = validateIntegerField(tileset, "cell_h", "tileset", 1, 512, 16, errors);
    var topPadding = validateIntegerField(tileset, "padding", "tileset", 0, 64, 0, errors);
    validateBooleanField(tileset, "native_layout", "tileset", errors);

    function validateHash(value, path) {
      if (value === undefined) return;
      if (typeof value !== "string" || !/^[0-9a-fA-F]{64}$/.test(value)) pushSchemaError(errors, "asset_sha256", path + " must be a non-empty 64-digit hexadecimal SHA-256 string when present.", path);
    }

    function resolveSheet(sheet, sha, cellW, cellH, padding, geometryAuthored, path) {
      var info = { external: false, known: false, cells: 0, sheet: sheet };
      validateHash(sha, path + ".asset_sha256");
      if (typeof sheet !== "string" || !sheet) {
        pushSchemaError(errors, "sprite_sheet_required", path + ".sprite_sheet must resolve to a non-empty string.", path + ".sprite_sheet");
        return info;
      }
      if (sheet.slice(0, 8).toLowerCase() === "builtin:") {
        if (geometryAuthored) pushSchemaError(errors, "builtin_geometry", "cell_w, cell_h, and padding only apply to external PNG sheets.", path);
        if (sha !== undefined) pushSchemaError(errors, "builtin_hash", "asset_sha256 must be omitted for built-in sheets.", path + ".asset_sha256");
        if (["builtin:tiles", "builtin:sprites", "builtin:misc", "builtin:glyphs"].indexOf(sheet.toLowerCase()) < 0) {
          warnings.push(makeIssue("warning", "unknown_builtin_sheet", "The loader accepts \"" + sheet + "\" syntactically, but it may be unresolved at runtime and fall back visually.", { path: path + ".sprite_sheet" }));
        }
        return info;
      }
      info.external = true;
      if (typeof sha === "string" && /^[0-9a-fA-F]{64}$/.test(sha)) warnings.push(makeIssue("warning", "sha256_not_recomputed", "The declared SHA-256 for \"" + sheet + "\" is preserved but was not recomputed by this synchronous core check.", { path: path + ".asset_sha256" }));
      if (sheet === "." || sheet === ".." || /[\\\/:]/.test(sheet) || !/\.png$/i.test(sheet)) {
        pushSchemaError(errors, "direct_png_name", path + ".sprite_sheet must be a direct .png filename in the map folder.", path + ".sprite_sheet");
        return info;
      }
      var asset = assetLookup[sheet.toLowerCase()];
      if (asset === undefined) {
        pushSchemaError(errors, "missing_png", "Referenced PNG \"" + sheet + "\" was not supplied with the package.", path + ".sprite_sheet");
        return info;
      }
      var bytes = assetBytes(asset);
      var size = bytes ? bytes.length : (asset && typeof asset.size === "number" ? asset.size : null);
      if (size !== null && (size < 1 || size > 64 * 1024 * 1024)) pushSchemaError(errors, "png_size", "PNG \"" + sheet + "\" must be 1 byte through 64 MiB.", path + ".sprite_sheet");
      var dimensions = pngInfo(asset);
      if (!dimensions) {
        if (bytes) pushSchemaError(errors, "png_header", "PNG \"" + sheet + "\" has an invalid PNG/IHDR header.", path + ".sprite_sheet");
        else warnings.push(makeIssue("warning", "png_pending", "PNG \"" + sheet + "\" is preserved, but its header could not be inspected synchronously.", { path: path + ".sprite_sheet" }));
        return info;
      }
      if (dimensions.width > 4096 || dimensions.height > 4096) {
        pushSchemaError(errors, "png_dimensions", "PNG \"" + sheet + "\" exceeds 4096x4096.", path + ".sprite_sheet");
        return info;
      }
      if (dimensions.width < cellW || dimensions.height < cellH || (dimensions.width + padding) % (cellW + padding) !== 0 || (dimensions.height + padding) % (cellH + padding) !== 0) {
        pushSchemaError(errors, "png_grid", "PNG \"" + sheet + "\" does not form a whole " + cellW + "x" + cellH + " grid with padding " + padding + ".", path + ".sprite_sheet");
        return info;
      }
      info.cells = ((dimensions.width + padding) / (cellW + padding)) * ((dimensions.height + padding) / (cellH + padding));
      info.known = true;
      if (info.cells > 8192) pushSchemaError(errors, "sheet_cell_cap", "PNG \"" + sheet + "\" defines " + info.cells + " cells; one sheet is limited to 8192.", path + ".sprite_sheet");
      var configKey = sheet.toLowerCase() + "|" + String(sha || "").toLowerCase() + "|" + cellW + "|" + cellH + "|" + padding;
      if (!sheetConfigs[configKey]) {
        sheetConfigs[configKey] = true;
        totalKnownCells += info.cells;
      }
      return info;
    }

    if (hasOwn(tileset, "sprite_sheet") && (typeof tileset.sprite_sheet !== "string" || !tileset.sprite_sheet)) pushSchemaError(errors, "default_sheet", "tileset.sprite_sheet must be a non-empty string when present.", "tileset.sprite_sheet");
    if (hasOwn(tileset, "asset_sha256") && !hasOwn(tileset, "sprite_sheet")) pushSchemaError(errors, "orphan_default_hash", "tileset.asset_sha256 requires tileset.sprite_sheet.", "tileset.asset_sha256");
    var topGeometryAuthored = hasOwn(tileset, "cell_w") || hasOwn(tileset, "cell_h") || hasOwn(tileset, "padding");
    var defaultSheetInfo = null;
    if (typeof tileset.sprite_sheet === "string" && tileset.sprite_sheet) defaultSheetInfo = resolveSheet(tileset.sprite_sheet, tileset.asset_sha256, topCellW, topCellH, topPadding, topGeometryAuthored, "tileset");
    if (tileset.native_layout === true) {
      if (!defaultSheetInfo || !defaultSheetInfo.external) pushSchemaError(errors, "native_layout_sheet", "tileset.native_layout requires an external default PNG sheet.", "tileset.native_layout");
      else if (defaultSheetInfo.known && defaultSheetInfo.cells < 128) pushSchemaError(errors, "native_layout_cells", "tileset.native_layout requires at least 128 cells in the default sheet.", "tileset.native_layout");
    }
    if (tileset.tiles !== undefined && !Array.isArray(tileset.tiles)) {
      pushSchemaError(errors, "tiles_array", "tileset.tiles must be an array.", "tileset.tiles");
      return;
    }
    var tiles = Array.isArray(tileset.tiles) ? tileset.tiles : [];
    if (tiles.length > 64) pushSchemaError(errors, "tile_count", "tileset.tiles contains more than 64 definitions.", "tileset.tiles");
    tiles.slice(0, 64).forEach(function (tile, index) {
      var path = "tileset.tiles[" + index + "]";
      if (!isPlainObject(tile)) {
        pushSchemaError(errors, "tile_type", path + " must be an object.", path);
        return;
      }
      rejectUnknownKeys(tile, tileKeys, path, errors);
      if (typeof tile.id !== "string" || !tile.id || utf8Bytes(tile.id).length > 47 || !/^[A-Za-z0-9_][A-Za-z0-9._-]*$/.test(tile.id)) pushSchemaError(errors, "tile_id", path + ".id is required, must fit 47 bytes, and may use ASCII letters, numbers, '.', '_' or '-' without starting '.' or '-'.", path + ".id");
      else {
        var normalizedId = tile.id.toLowerCase();
        if (seenIds[normalizedId]) pushSchemaError(errors, "duplicate_tile_id", "Duplicate V2 tile id \"" + tile.id + "\" after lowercase normalization.", path + ".id");
        seenIds[normalizedId] = true;
      }
      if (typeof tile.symbol !== "string" || tile.symbol.length !== 1 || tile.symbol.charCodeAt(0) < 0x20 || tile.symbol.charCodeAt(0) > 0x7E) pushSchemaError(errors, "tile_symbol", path + ".symbol must be exactly one printable ASCII byte.", path + ".symbol");
      else {
        if (seenSymbols[tile.symbol]) pushSchemaError(errors, "duplicate_symbol", "Duplicate V2 tile symbol \"" + tile.symbol + "\".", path + ".symbol");
        seenSymbols[tile.symbol] = true;
      }
      if (tile.name !== undefined && (typeof tile.name !== "string" || utf8Bytes(tile.name).length > 95)) pushSchemaError(errors, "tile_name", path + ".name must be a string of at most 95 bytes.", path + ".name");
      if (tile.native_glyph !== undefined && (typeof tile.native_glyph !== "string" || tile.native_glyph.length !== 1 || safeNative.indexOf(tile.native_glyph) < 0)) pushSchemaError(errors, "native_glyph", path + ".native_glyph must be one of the loader's safe one-cell native fallback glyphs.", path + ".native_glyph");
      var cellW = validateIntegerField(tile, "cell_w", path, 1, 512, topCellW, errors);
      var cellH = validateIntegerField(tile, "cell_h", path, 1, 512, topCellH, errors);
      var padding = validateIntegerField(tile, "padding", path, 0, 64, topPadding, errors);
      var indexValue = validateIntegerField(tile, "sprite_index", path, 0, 1000000, 0, errors);
      var frameCount = validateIntegerField(tile, "frame_count", path, 1, 256, 1, errors);
      validateIntegerField(tile, "frame_ticks", path, 1, 3600, 1, errors);
      validateIntegerField(tile, "layer", path, 0, 1, 0, errors);
      validateBooleanField(tile, "mirror_with_room", path, errors);
      validateBooleanField(tile, "random_phase", path, errors);
      validateNumberField(tile, "offset_x", path, -4096, 4096, 0, errors, false);
      validateNumberField(tile, "offset_y", path, -4096, 4096, 0, errors, false);
      validateNumberField(tile, "scale_x", path, -64, 64, 1, errors, true);
      validateNumberField(tile, "scale_y", path, -64, 64, 1, errors, true);
      validateNumberField(tile, "angle_degrees", path, -360000, 360000, 0, errors, false);
      validateNumberField(tile, "force_x", path, -64, 64, 0, errors, false);
      validateNumberField(tile, "force_y", path, -64, 64, 0, errors, false);
      validateNumberField(tile, "max_speed_x", path, 0, 64, 0, errors, false);
      validateNumberField(tile, "max_speed_y", path, 0, 64, 0, errors, false);
      if (tile.max_speed_x !== undefined && tile.force_x === undefined) pushSchemaError(errors, "max_speed_axis", path + ".max_speed_x requires authored force_x.", path + ".max_speed_x");
      if (tile.max_speed_y !== undefined && tile.force_y === undefined) pushSchemaError(errors, "max_speed_axis", path + ".max_speed_y requires authored force_y.", path + ".max_speed_y");
      if (tile.force_mode !== undefined && tile.force_x === undefined && tile.force_y === undefined) pushSchemaError(errors, "force_mode_axes", path + ".force_mode requires force_x and/or force_y.", path + ".force_mode");
      if (tile.animation !== undefined && ["loop", "ping_pong", "pingpong", "once"].indexOf(tile.animation) < 0) pushSchemaError(errors, "animation", path + ".animation must be loop, ping_pong, or once.", path + ".animation");
      if (tile.collision !== undefined && ["native", "solid", "pass_through", "passthrough", "hazard"].indexOf(tile.collision) < 0) pushSchemaError(errors, "collision", path + ".collision must be native, solid, pass_through, or hazard.", path + ".collision");
      if (tile.force_mode !== undefined && ["add", "set"].indexOf(tile.force_mode) < 0) pushSchemaError(errors, "force_mode", path + ".force_mode must be add or set.", path + ".force_mode");
      if (tile.native_visual !== undefined && ["replace", "underlay"].indexOf(tile.native_visual) < 0) pushSchemaError(errors, "native_visual", path + ".native_visual must be replace or underlay.", path + ".native_visual");
      if (tile.tint !== undefined && (!Array.isArray(tile.tint) || tile.tint.length !== 4 || !tile.tint.every(function (channel) { return typeof channel === "number" && Number.isFinite(channel) && channel >= 0 && channel <= 1; }))) pushSchemaError(errors, "tint", path + ".tint must contain four finite channels from 0 to 1.", path + ".tint");
      var collision = tile.collision === undefined ? "native" : (tile.collision === "passthrough" ? "pass_through" : tile.collision);
      if (collision !== "native" && tile.native_glyph !== undefined) {
        var presetGlyph = collision === "solid" ? "@" : (collision === "pass_through" ? "x" : (collision === "hazard" ? "X" : null));
        if (presetGlyph && tile.native_glyph !== presetGlyph) pushSchemaError(errors, "native_glyph_conflict", path + ".native_glyph conflicts with the selected collision preset; omit it or use \"" + presetGlyph + "\".", path + ".native_glyph");
      }
      if (collision === "native" && tile.native_glyph === undefined && !(typeof tile.symbol === "string" && safeNative.indexOf(tile.symbol) >= 0)) pushSchemaError(errors, "native_collision_fallback", path + " uses native collision and needs a safe native_glyph (safe built-in symbols may default to themselves).", path + ".native_glyph");
      var sheet = hasOwn(tile, "sprite_sheet") ? tile.sprite_sheet : tileset.sprite_sheet;
      /* A default digest follows a tile only while that tile still uses the
       * default sheet. A per-tile sheet exception owns (or omits) its pin. */
      var sameAsDefaultSheet = typeof sheet === "string" && typeof tileset.sprite_sheet === "string" && sheet.toLowerCase() === tileset.sprite_sheet.toLowerCase();
      var sha = hasOwn(tile, "asset_sha256") ? tile.asset_sha256 : ((!hasOwn(tile, "sprite_sheet") || sameAsDefaultSheet) ? tileset.asset_sha256 : undefined);
      if (hasOwn(tile, "sprite_sheet") && (typeof tile.sprite_sheet !== "string" || !tile.sprite_sheet)) pushSchemaError(errors, "tile_sheet", path + ".sprite_sheet must be a non-empty string when present.", path + ".sprite_sheet");
      var geometryAuthored = hasOwn(tile, "cell_w") || hasOwn(tile, "cell_h") || hasOwn(tile, "padding") || (!hasOwn(tile, "sprite_sheet") && topGeometryAuthored);
      var sheetInfo = resolveSheet(sheet, sha, cellW, cellH, padding, geometryAuthored, path);
      if (sheetInfo.known && (indexValue >= sheetInfo.cells || frameCount > sheetInfo.cells - indexValue)) pushSchemaError(errors, "sprite_range", path + " sprite_index/frame_count exceeds its sheet's " + sheetInfo.cells + " cells.", path + ".sprite_index");
    });
    if (Object.keys(sheetConfigs).length > 16) pushSchemaError(errors, "sheet_config_count", "The tileset uses more than 16 unique external sheet configurations.", "tileset");
    if (totalKnownCells > 8192) warnings.push(makeIssue("warning", "aggregate_atlas_pressure", "External sheet configurations total " + totalKnownCells + " cells. Parsing can succeed, but the runtime atlas may be unable to append all of them.", { path: "tileset" }));
  }

  function validAmbient(value) {
    return (typeof value === "string" && (AMBIENTS.indexOf(value) >= 0 || value === "fumes")) ||
      (Number.isInteger(value) && value >= 0 && value <= 9);
  }

  function validateColorBank(bank, path, errors, roomId) {
    if (bank === undefined) return;
    if (!bank || typeof bank !== "object" || Array.isArray(bank)) {
      errors.push(makeIssue("error", "color_bank_type", path + " must be an object.", { path: path, roomId: roomId }));
      return;
    }
    Object.keys(bank).forEach(function (key) {
      var value = bank[key];
      var valid = false;
      if (COLOR_KEYS.indexOf(key) < 0) return;
      if (typeof value === "string") valid = colorToHex(value) !== null;
      else if (Array.isArray(value)) valid = value.length === 3 && value.every(function (channel) {
        return typeof channel === "number" && Number.isFinite(channel) && channel >= 0 && channel <= 1;
      });
      if (!valid) errors.push(makeIssue("error", "invalid_color", path + "." + key + " must be a #RRGGBB color or three finite channels from 0 to 1.", {
        path: path + "." + key, roomId: roomId
      }));
    });
  }

  function validateAppearance(appearance, path, errors, roomId) {
    if (appearance === undefined) return;
    if (!appearance || typeof appearance !== "object" || Array.isArray(appearance)) {
      errors.push(makeIssue("error", "appearance_type", path + " must be an object.", { path: path, roomId: roomId }));
      return;
    }
    validateColorBank(appearance.primary, path + ".primary", errors, roomId);
    validateColorBank(appearance.mirror, path + ".mirror", errors, roomId);
  }

  function validateDocument(value) {
    var document = unwrapDocument(value);
    var errors = [];
    var warnings = [];
    var rooms;
    var seen = Object.create(null);
    var ids;
    var layout;
    var rules;
    var sortOrder;
    var format;
    var customSymbols;
    var customNative = Object.create(null);
    var stats = { sourceRooms: 0, finalRooms: 0, goals: 0, maxResetSpawns: 0, spawnCandidates: {} };
    var roomMetrics = Object.create(null);

    if (!document || typeof document !== "object" || Array.isArray(document)) {
      errors.push(makeIssue("error", "document_type", "Map document must be an object.", { path: null }));
      return { valid: false, errors: errors, warnings: warnings, issues: errors.slice(), stats: stats };
    }
    format = document.format;
    if (format !== FORMAT_V1 && format !== FORMAT_V2) errors.push(makeIssue("error", "format", "format must be \"" + FORMAT_V1 + "\" or \"" + FORMAT_V2 + "\".", { path: "format" }));
    if (typeof document.name !== "string" || !document.name) errors.push(makeIssue("error", "name_required", "Map name is required.", { path: "name" }));
    else if (document.name.indexOf("\0") >= 0) errors.push(makeIssue("error", "name_nul", "Map name contains an embedded NUL character.", { path: "name" }));
    else if (utf8Bytes(document.name).length > 127) errors.push(makeIssue("error", "name_bytes", "Map name exceeds 127 UTF-8 bytes.", { path: "name" }));
    if (typeof document.author !== "string" || !document.author) errors.push(makeIssue("error", "author_required", "Map author is required.", { path: "author" }));
    else if (document.author.indexOf("\0") >= 0) errors.push(makeIssue("error", "author_nul", "Map author contains an embedded NUL character.", { path: "author" }));
    else if (utf8Bytes(document.author).length > 127) errors.push(makeIssue("error", "author_bytes", "Map author exceeds 127 UTF-8 bytes.", { path: "author" }));
    if (document.description !== undefined && typeof document.description !== "string") errors.push(makeIssue("error", "description_type", "description must be a string.", { path: "description" }));
    else if (typeof document.description === "string" && document.description.indexOf("\0") >= 0) errors.push(makeIssue("error", "description_nul", "description contains an embedded NUL character.", { path: "description" }));
    else if (typeof document.description === "string" && utf8Bytes(document.description).length > 255) errors.push(makeIssue("error", "description_bytes", "description exceeds 255 UTF-8 bytes.", { path: "description" }));
    if (format === FORMAT_V2) {
      if (typeof document.id !== "string" || !document.id || utf8Bytes(document.id).length > 43 || !/^[A-Za-z0-9_][A-Za-z0-9._-]*$/.test(document.id)) errors.push(makeIssue("error", "v2_id", "V2 id is required, limited to 43 bytes, and may use ASCII letters, numbers, '.', '_' or '-' without starting '.' or '-'.", { path: "id" }));
    } else if (document.id !== undefined && document.id !== null && document.id !== "") {
      if (typeof document.id !== "string") errors.push(makeIssue("error", "id_type", "Map id must be a string.", { path: "id" }));
      else if (document.id.indexOf("\0") >= 0) errors.push(makeIssue("error", "id_nul", "Map id contains an embedded NUL character.", { path: "id" }));
      else if (utf8Bytes(document.id).length > 63) errors.push(makeIssue("error", "id_bytes", "Map id exceeds 63 UTF-8 bytes.", { path: "id" }));
      else if (!/^[A-Za-z0-9_][A-Za-z0-9._-]*$/.test(document.id)) warnings.push(makeIssue("warning", "folder_id_recommended", "The loader accepts this V1 id, but a normalized direct-folder id such as \"" + normalizeMapId(document.id) + "\" is safer for export and online identity.", { path: "id" }));
    }
    sortOrder = camelOrSnake(document, "sortOrder", "sort_order", 0);
    if (!Number.isInteger(sortOrder) || sortOrder < -2147483648 || sortOrder > 2147483647) errors.push(makeIssue("error", "sort_order", "sort_order must be a signed 32-bit integer.", { path: "sort_order" }));
    rules = document.rules;
    if (rules !== undefined) {
      if (!isPlainObject(rules)) {
        errors.push(makeIssue("error", "rules_type", "rules must be an object.", { path: "rules" }));
        rules = {};
      }
      if (rules.mode !== "swords" && rules.mode !== "karate") errors.push(makeIssue("error", "rules_mode", "rules.mode must be \"swords\" or \"karate\".", { path: "rules.mode" }));
      var roundEnd = camelOrSnake(rules, "roundEndRooms", "round_end_rooms", "inner_only");
      if (roundEnd !== "inner_only" && roundEnd !== "any") errors.push(makeIssue("error", "round_end_rooms", "rules.round_end_rooms must be \"inner_only\" or \"any\".", { path: "rules.round_end_rooms" }));
      var score = camelOrSnake(rules, "scoreTarget", "score_target", null);
      if (score !== null && (!Number.isInteger(score) || score <= 0 || score > 2147483647)) errors.push(makeIssue("error", "score_target", "rules.score_target must be null or a positive integer.", { path: "rules.score_target" }));
      var armedLimit = camelOrSnake(rules, "armedRespawnLimit", "armed_respawn_limit", 4);
      if (!Number.isInteger(armedLimit) || armedLimit < 0 || armedLimit > 2147483647) errors.push(makeIssue("error", "armed_respawn_limit", "rules.armed_respawn_limit must be a non-negative integer.", { path: "rules.armed_respawn_limit" }));
    }

    customSymbols = format === FORMAT_V2 ? collectCustomSymbols({ tileset: document.tileset }) : Object.create(null);
    if (format === FORMAT_V2 && isPlainObject(document.tileset) && Array.isArray(document.tileset.tiles)) {
      document.tileset.tiles.forEach(function (tile) {
        if (!isPlainObject(tile) || typeof tile.symbol !== "string" || tile.symbol.length !== 1) return;
        var collision = tile.collision === "passthrough" ? "pass_through" : (tile.collision || "native");
        if (collision === "solid") customNative[tile.symbol] = "@";
        else if (collision === "pass_through") customNative[tile.symbol] = "x";
        else if (collision === "hazard") customNative[tile.symbol] = "X";
        else customNative[tile.symbol] = tile.native_glyph || (GLYPH_SET[tile.symbol] ? tile.symbol : null);
      });
    }
    function resolvedGlyph(glyph) { return customNative[glyph] || glyph; }

    rooms = roomList(document);
    stats.sourceRooms = rooms.length;
    stats.finalRooms = rooms.length ? rooms.length * 2 - 1 : 0;
    if (rooms.length < 1 || rooms.length > MAX_ROOMS) errors.push(makeIssue("error", "room_count", "A map must contain between 1 and " + MAX_ROOMS + " source rooms; found " + rooms.length + ".", { path: "rooms" }));
    rooms.forEach(function (room, roomIndex) {
      var roomId = room && room.id;
      var grid = getGrid(room);
      var spawns = { K: 0, sword: 0, mine: 0 };
      var covered = Object.create(null);
      var overlapWarned = false;
      var shallowWater = 0;
      var roomGoals = 0;
      var candidates = [];
      if (typeof roomId !== "string" || !roomId) errors.push(makeIssue("error", "room_id_required", "Room " + (roomIndex + 1) + " needs a non-empty id.", { path: "rooms." + roomIndex, roomId: roomId || null }));
      else {
        if (/[\]\r\n\0]/.test(roomId) || utf8Bytes(roomId).length > 63) errors.push(makeIssue("error", "invalid_room_id", "Room id \"" + roomId + "\" cannot contain ], NUL, a line break, or exceed 63 UTF-8 bytes.", { path: "rooms." + roomIndex + ".id", roomId: roomId }));
        if (seen[roomId]) errors.push(makeIssue("error", "duplicate_room_id", "Room id \"" + roomId + "\" is used more than once.", { path: "rooms." + roomIndex + ".id", roomId: roomId }));
        seen[roomId] = true;
      }
      if (!Array.isArray(grid) || grid.length !== ROWS) errors.push(makeIssue("error", "room_row_count", "Room \"" + (roomId || roomIndex + 1) + "\" must contain exactly " + ROWS + " rows.", { path: "rooms." + roomIndex + ".grid", roomId: roomId || null }));
      if (Array.isArray(grid)) grid.forEach(function (row, rowIndex) {
        var text = rowString(row);
        if (text.length !== COLS || utf8Bytes(text).length !== COLS) errors.push(makeIssue("error", "row_width", "Row " + (rowIndex + 1) + " must contain exactly " + COLS + " ASCII bytes; found " + utf8Bytes(text).length + ".", { path: "rooms." + roomIndex + ".grid." + rowIndex, roomId: roomId || null, row: rowIndex }));
        for (var col = 0; col < text.length; col += 1) {
          var glyph = text.charAt(col);
          var nativeGlyph = resolvedGlyph(glyph);
          var cellPath = "rooms." + roomIndex + ".grid." + rowIndex + "." + col;
          if (!GLYPH_SET[glyph] && !(format === FORMAT_V2 && customSymbols[glyph])) errors.push(makeIssue("error", "invalid_glyph", "Glyph \"" + glyph + "\" is neither native nor declared by the V2 tileset.", { path: cellPath, roomId: roomId || null, row: rowIndex, col: col }));
          if ((nativeGlyph === "G" || nativeGlyph === "L" || nativeGlyph === "N" || nativeGlyph === "Y") && (rowIndex < 3 || col === 0 || col === COLS - 1)) errors.push(makeIssue("error", "multicell_placement", "Glyph \"" + glyph + "\" resolves to a large native art action that needs three rows of headroom and side clearance.", { path: cellPath, roomId: roomId || null, row: rowIndex, col: col }));
          if (nativeGlyph === "T" && rowIndex < 3) errors.push(makeIssue("error", "tentacle_headroom", "T must be on row 4 or lower (one-based); the native four-cell expansion can otherwise write before the room buffer.", { path: cellPath, roomId: roomId || null, row: rowIndex, col: col }));
          if (nativeGlyph === "t" && rowIndex < 2) errors.push(makeIssue("error", "tentacle_headroom", "t needs two rows of headroom.", { path: cellPath, roomId: roomId || null, row: rowIndex, col: col }));
          if (nativeGlyph === "s" && rowIndex < 1) errors.push(makeIssue("error", "tentacle_headroom", "s needs one row of headroom.", { path: cellPath, roomId: roomId || null, row: rowIndex, col: col }));
          if (nativeGlyph === "K") spawns.K += 1;
          else if (nativeGlyph === "*") spawns.sword += 1;
          else if (nativeGlyph === "m") spawns.mine += 1;
          if (nativeGlyph === "w") shallowWater += 1;
          if (nativeGlyph === "E" || nativeGlyph === "^") { roomGoals += 1; stats.goals += 1; }

          var footprint = null;
          if (nativeGlyph === "G") footprint = { left: 1, right: 1, up: 3, endsAboveMarker: true };
          else if (nativeGlyph === "L" || nativeGlyph === "N" || nativeGlyph === "Y") footprint = { left: 1, right: 0, up: 3, endsAboveMarker: true };
          else if (nativeGlyph === "T") footprint = { left: 0, right: 0, up: 3 };
          else if (nativeGlyph === "t") footprint = { left: 0, right: 0, up: 2 };
          else if (nativeGlyph === "s") footprint = { left: 0, right: 0, up: 1 };
          if (footprint && rowIndex >= footprint.up && col >= footprint.left && col + footprint.right < COLS) {
            var footprintBottom = footprint.endsAboveMarker ? rowIndex - 1 : rowIndex;
            for (var yy = rowIndex - footprint.up; yy <= footprintBottom; yy += 1) for (var xx = col - footprint.left; xx <= col + footprint.right; xx += 1) {
              var key = yy + ":" + xx;
              var targetGlyph = Array.isArray(grid[yy]) ? grid[yy][xx] : rowString(grid[yy]).charAt(xx);
              if ((covered[key] || ((yy !== rowIndex || xx !== col) && targetGlyph && targetGlyph !== " " && targetGlyph !== "." && targetGlyph !== "?")) && !overlapWarned) {
                warnings.push(makeIssue("warning", "native_footprint_overlap", "Room \"" + (roomId || roomIndex + 1) + "\" has overlapping authored cells in a native multi-cell expansion; generation order can overwrite them.", { path: cellPath, roomId: roomId || null, row: rowIndex, col: col }));
                overlapWarned = true;
              }
              covered[key] = true;
            }
          }
        }
      });
      var spawnTotal = spawns.K + spawns.sword + spawns.mine;
      stats.maxResetSpawns = Math.max(stats.maxResetSpawns, spawnTotal);
      if (spawnTotal > 13) {
        var severity = spawns.K > 0 ? "error" : "warning";
        (severity === "error" ? errors : warnings).push(makeIssue(severity, "spawn_budget", "Room \"" + (roomId || roomIndex + 1) + "\" uses " + spawnTotal + "/13 reset spawns (K=" + spawns.K + ", swords=" + spawns.sword + ", mines=" + spawns.mine + ")." + (spawns.K ? " A failed K allocation is unsafe." : " This creates native pool pressure."), { path: "rooms." + roomIndex + ".grid", roomId: roomId || null }));
      }
      if (Array.isArray(grid) && grid.length === ROWS) {
        var solidAbove = "!@Xv";
        var lethalAbove = "12WwXvm";
        for (var y = 1; y < ROWS; y += 1) for (var x = 1; x < COLS - 1; x += 1) {
          var here = resolvedGlyph((Array.isArray(grid[y]) ? grid[y][x] : rowString(grid[y]).charAt(x)) || " ");
          var above = resolvedGlyph((Array.isArray(grid[y - 1]) ? grid[y - 1][x] : rowString(grid[y - 1]).charAt(x)) || " ");
          if (here === "@" && solidAbove.indexOf(above) < 0 && lethalAbove.indexOf(above) < 0) candidates.push({ row: y, col: x });
        }
      }
      stats.spawnCandidates[roomId || String(roomIndex)] = candidates.length;
      var ambient = room && room.ambient;
      if ((ambient === 9 || ambient === "boil" || ambient === "fumes") && shallowWater === 0) warnings.push(makeIssue("warning", "boil_without_shallow_water", "Room \"" + (roomId || roomIndex + 1) + "\" uses boil/fumes ambience without any shallow-water w cells.", { path: "rooms." + roomIndex + ".ambient", roomId: roomId || null }));
      if (room && room.hook !== undefined && room.hook !== null) errors.push(makeIssue("error", "unsupported_hook", "Room hooks must be omitted or null; V2 map.lua is discovered at package level.", { path: "rooms." + roomId + ".hook", roomId: roomId || null }));
      if (room && room.ambient !== undefined && !validAmbient(room.ambient)) errors.push(makeIssue("error", "ambient", "Room ambient must be a supported name or integer from 0 to 9.", { path: "rooms." + roomId + ".ambient", roomId: roomId || null }));
      validateAppearance(room && room.appearance, "rooms." + (roomId || roomIndex) + ".appearance", errors, roomId || null);
      roomMetrics[roomId || String(roomIndex)] = { goals: roomGoals, spawnCandidates: candidates.length };
    });
    if (rooms.length === 1) warnings.push(makeIssue("warning", "single_source_room", "This map has one source room, so its final mirrored arena also has one room.", { path: "rooms", roomId: rooms[0] && rooms[0].id }));

    layout = document.layout;
    ids = rooms.map(function (room) { return room && room.id; });
    if (!isPlainObject(layout)) errors.push(makeIssue("error", "layout_required", "A fixed mirrored layout is required.", { path: "layout" }));
    else {
      var roomFormat = camelOrSnake(layout, "roomFormat", "room_format", undefined);
      if (layout.kind !== LAYOUT_KIND) errors.push(makeIssue("error", "layout_kind", "layout.kind must be \"" + LAYOUT_KIND + "\".", { path: "layout.kind" }));
      if (roomFormat !== ROOM_FORMAT) errors.push(makeIssue("error", "room_format", "layout.room_format must be \"" + ROOM_FORMAT + "\".", { path: "layout.room_format" }));
      if (!Array.isArray(layout.order) || !layout.order.length) errors.push(makeIssue("error", "layout_order", "layout.order must be a non-empty array.", { path: "layout.order" }));
      else {
        var layoutSeen = Object.create(null);
        layout.order.forEach(function (id, index) {
          if (typeof id !== "string" || !id) errors.push(makeIssue("error", "layout_room_id", "Every layout.order item must be a non-empty room id.", { path: "layout.order." + index }));
          else if (layoutSeen[id]) errors.push(makeIssue("error", "layout_duplicate", "layout.order contains duplicate room \"" + id + "\".", { path: "layout.order." + index, roomId: id }));
          else if (ids.indexOf(id) < 0) errors.push(makeIssue("error", "layout_unknown_room", "layout.order references unknown room \"" + id + "\".", { path: "layout.order." + index, roomId: id }));
          layoutSeen[id] = true;
        });
        ids.forEach(function (id) { if (id && !layoutSeen[id]) errors.push(makeIssue("error", "layout_omits_room", "Room \"" + id + "\" is not referenced by layout.order.", { path: "layout.order", roomId: id })); });
      }
    }
    var centerId = layout && Array.isArray(layout.order) ? layout.order[0] : null;
    var centerRoom = rooms.filter(function (room) { return room && room.id === centerId; })[0];
    if (centerRoom && roomMetrics[centerId]) {
      var count = roomMetrics[centerId].spawnCandidates;
      if (count < 2) warnings.push(makeIssue("warning", "respawn_candidates", "The center source room has only " + count + " obvious native @ spawn-floor candidate" + (count === 1 ? "" : "s") + "; players need two separated candidates.", { path: "rooms." + centerId + ".grid", roomId: centerId }));
      else {
        var centerGrid = getGrid(centerRoom);
        var cols = [];
        for (var cy = 1; cy < ROWS; cy += 1) for (var cx = 1; cx < COLS - 1; cx += 1) {
          var cg = resolvedGlyph((Array.isArray(centerGrid[cy]) ? centerGrid[cy][cx] : rowString(centerGrid[cy]).charAt(cx)) || " ");
          var ca = resolvedGlyph((Array.isArray(centerGrid[cy - 1]) ? centerGrid[cy - 1][cx] : rowString(centerGrid[cy - 1]).charAt(cx)) || " ");
          if (cg === "@" && "!@Xv".indexOf(ca) < 0 && "12WwXvm".indexOf(ca) < 0) cols.push(cx);
        }
        var separated = cols.some(function (left) { return cols.some(function (right) { return Math.abs(left - right) > 1; }); });
        if (!separated) warnings.push(makeIssue("warning", "respawn_separation", "The center room's obvious spawn candidates are not separated by at least one column.", { path: "rooms." + centerId + ".grid", roomId: centerId }));
      }
    }
    var scoreTarget = rules ? camelOrSnake(rules, "scoreTarget", "score_target", null) : null;
    var roundEndMode = rules ? camelOrSnake(rules, "roundEndRooms", "round_end_rooms", "inner_only") : "inner_only";
    /* Native game_is_win_condition treats the two arena endpoints as automatic
     * wins in inner_only mode. It does not require the center (or any particular
     * inner room) to contain E/^, so warning about a missing center goal was a
     * false assumption. In any mode the endpoint shortcut is disabled; without
     * either an Eggnogg goal or score target, no normal round-end trigger exists. */
    if (roundEndMode === "any" && stats.goals === 0 && scoreTarget === null) {
      warnings.push(makeIssue("warning", "missing_round_end_trigger", "Goal-required mode has no E or ^ goal and no score target, so the round has no normal end trigger.", { path: "rules.round_end_rooms" }));
    }
    if (scoreTarget !== null) {
      var hasTeamTile = rooms.some(function (room) { return (getGrid(room) || []).some(function (row) { return /[12]/.test(rowString(row)); }); });
      if (!hasTeamTile) warnings.push(makeIssue("warning", "score_target_without_team_tiles", "A score target is set, but no 1 or 2 team hazard/score tiles are authored.", { path: "rules.score_target" }));
    }
    var defaults = document.defaults && document.defaults.room ? document.defaults.room : document.defaults;
    if (defaults) {
      if (defaults.ambient !== undefined && !validAmbient(defaults.ambient)) errors.push(makeIssue("error", "default_ambient", "Default room ambient must be a supported name or integer from 0 to 9.", { path: "defaults.room.ambient" }));
      validateAppearance(defaults.appearance, "defaults.room.appearance", errors, null);
    }
    if (format === FORMAT_V2) {
      validateV2Manifest(buildDataObject(document), document.assets || (document._preserved && document._preserved.assets) || {}, errors, warnings);
      if (document.mapLuaPresent || document.mapLua !== undefined) {
        if (typeof document.mapLua !== "string") errors.push(makeIssue("error", "script_text_type", "map.lua must be text.", { path: "map.lua" }));
        else {
          if (utf8Bytes(document.mapLua).length > MAX_SCRIPT_BYTES) errors.push(makeIssue("error", "script_size", "map.lua exceeds the 256 KiB limit.", { path: "map.lua" }));
          if (document.mapLua.indexOf("\0") >= 0) errors.push(makeIssue("error", "script_nul", "map.lua contains an embedded NUL byte.", { path: "map.lua" }));
        }
      }
    } else if (document.mapLuaPresent || document.mapLua !== undefined) {
      errors.push(makeIssue("error", "v1_script", "map.lua requires eggnogg-map/v2.", { path: "map.lua" }));
    }
    return { valid: errors.length === 0, errors: errors, warnings: warnings, issues: errors.concat(warnings), stats: stats };
  }

  function floatBank(source) {
    var output = {};
    COLOR_KEYS.forEach(function (key) { output[key] = COLOR_DEFAULT_FLOATS[key].slice(); });
    if (!isPlainObject(source)) return output;
    COLOR_KEYS.forEach(function (key) {
      if (source[key] === undefined) return;
      var value = typeof source[key] === "string" ? hexToColor(source[key]) : source[key];
      if (Array.isArray(value) && value.length === 3 && value.every(function (channel) { return typeof channel === "number" && Number.isFinite(channel); })) output[key] = value.slice();
    });
    return output;
  }

  function applyBank(target, source) {
    if (!isPlainObject(source)) return target;
    COLOR_KEYS.forEach(function (key) {
      if (source[key] === undefined) return;
      var value = typeof source[key] === "string" ? hexToColor(source[key]) : source[key];
      if (Array.isArray(value) && value.length === 3) target[key] = value.slice();
    });
    return target;
  }

  function resolveRoomAppearance(document, room) {
    document = unwrapDocument(document) || {};
    var defaults = document.defaults && document.defaults.room ? document.defaults.room : (document.defaults || {});
    var defaultsAppearance = isPlainObject(defaults.appearance) ? defaults.appearance : null;
    var roomAppearance = room && isPlainObject(room.appearance) ? room.appearance : null;
    var primary = floatBank();
    var mirror = floatBank();
    if (defaultsAppearance) {
      applyBank(primary, defaultsAppearance.primary);
      mirror = deepClone(primary);
      if (hasOwn(defaultsAppearance, "mirror")) applyBank(mirror, defaultsAppearance.mirror);
    }
    if (roomAppearance) {
      applyBank(primary, roomAppearance.primary);
      if (hasOwn(roomAppearance, "mirror")) {
        if (!defaultsAppearance || !hasOwn(defaultsAppearance, "mirror")) mirror = deepClone(primary);
        applyBank(mirror, roomAppearance.mirror);
      } else {
        /* This is deliberately presence-sensitive: an authored room
         * appearance without mirror discards a differing default mirror. */
        mirror = deepClone(primary);
      }
    }
    return { primary: primary, mirror: mirror };
  }

  function expandMirroredLayout(value) {
    var document = unwrapDocument(value) || {};
    var order = document.layout && Array.isArray(document.layout.order) ? document.layout.order.slice() : roomList(document).map(function (room) { return room.id; });
    var output = [];
    var index;
    for (index = order.length - 1; index >= 1; index -= 1) output.push({ sourceId: order[index], side: "left", geometryMirrored: false, appearanceBank: "mirror", sourceIndex: index });
    if (order.length) output.push({ sourceId: order[0], side: "center", geometryMirrored: false, appearanceBank: "primary", sourceIndex: 0 });
    for (index = 1; index < order.length; index += 1) output.push({ sourceId: order[index], side: "right", geometryMirrored: true, appearanceBank: "primary", sourceIndex: index });
    return output;
  }

  function packageFacts(value) {
    var document = unwrapDocument(value) || {};
    var sourceRooms = roomList(document).length;
    return {
      sourceRooms: sourceRooms,
      finalRooms: sourceRooms ? sourceRooms * 2 - 1 : 0,
      sourceWidthCells: sourceRooms * COLS,
      finalWidthCells: sourceRooms ? (sourceRooms * 2 - 1) * COLS : 0,
      finalWidthPixels: sourceRooms ? (sourceRooms * 2 - 1) * COLS * 16 : 0,
      heightCells: ROWS,
      heightPixels: ROWS * 16
    };
  }

  function utf8Bytes(value) {
    if (value instanceof Uint8Array) return value;
    if (typeof ArrayBuffer !== "undefined" && value instanceof ArrayBuffer) return new Uint8Array(value);
    if (typeof ArrayBuffer !== "undefined" && ArrayBuffer.isView && ArrayBuffer.isView(value)) return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    var string = String(value === undefined || value === null ? "" : value);
    if (typeof TextEncoder !== "undefined") return new TextEncoder().encode(string);
    var escaped = unescape(encodeURIComponent(string));
    var output = new Uint8Array(escaped.length);
    for (var index = 0; index < escaped.length; index += 1) output[index] = escaped.charCodeAt(index);
    return output;
  }

  function base64UrlEncode(value) {
    var alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    var bytes = utf8Bytes(value);
    var output = "";
    var index;
    for (index = 0; index + 2 < bytes.length; index += 3) {
      var value24 = (bytes[index] << 16) | (bytes[index + 1] << 8) | bytes[index + 2];
      output += alphabet.charAt(value24 >>> 18 & 63) +
        alphabet.charAt(value24 >>> 12 & 63) +
        alphabet.charAt(value24 >>> 6 & 63) +
        alphabet.charAt(value24 & 63);
    }
    if (bytes.length - index === 1) {
      var value8 = bytes[index];
      output += alphabet.charAt(value8 >>> 2) + alphabet.charAt((value8 & 3) << 4);
    } else if (bytes.length - index === 2) {
      var value16 = (bytes[index] << 8) | bytes[index + 1];
      output += alphabet.charAt(value16 >>> 10) +
        alphabet.charAt(value16 >>> 4 & 63) +
        alphabet.charAt((value16 & 15) << 2);
    }
    return output;
  }

  function writeU32Le(output, offset, value) {
    output[offset] = value & 0xFF;
    output[offset + 1] = value >>> 8 & 0xFF;
    output[offset + 2] = value >>> 16 & 0xFF;
    output[offset + 3] = value >>> 24 & 0xFF;
  }

  /*
   * Small deterministic LZSS stream used only by yule://preview/v1z. Each
   * flag byte describes up to eight following tokens (low bit first): zero is
   * one literal byte; one is a 12-bit backward distance and 4-bit length.
   * Keeping this codec here avoids a dependency and lets Yule reject malformed
   * links before allocating their declared output.
   */
  function packPreviewBytes(jsonBytes, mapBytes) {
    var input = new Uint8Array(jsonBytes.length + mapBytes.length);
    /* Plain arrays keep the dependency-free WSH regression harness usable. */
    var head = new Array(65536);
    var previous = new Array(input.length);
    var compressed = [];
    var position = 0;
    var i;
    input.set(jsonBytes, 0);
    input.set(mapBytes, jsonBytes.length);
    for (i = 0; i < head.length; i += 1) head[i] = -1;
    for (i = 0; i < previous.length; i += 1) previous[i] = -1;

    function hashAt(index) {
      if (index + 2 >= input.length) return -1;
      return ((input[index] * 251) ^ (input[index + 1] * 31) ^ input[index + 2]) & 65535;
    }

    function insert(index) {
      var hash = hashAt(index);
      if (hash < 0) return;
      previous[index] = head[hash];
      head[hash] = index;
    }

    while (position < input.length) {
      var flagIndex = compressed.length;
      var flags = 0;
      var bit;
      compressed.push(0);
      for (bit = 0; bit < 8 && position < input.length; bit += 1) {
        var hash = hashAt(position);
        var candidate = hash < 0 ? -1 : head[hash];
        var bestLength = 0;
        var bestDistance = 0;
        var checked = 0;
        while (candidate >= 0 && position - candidate <= 4096 && checked < 128) {
          var length = 0;
          while (length < 18 && position + length < input.length &&
                 input[candidate + length] === input[position + length]) length += 1;
          if (length > bestLength && length >= 3) {
            bestLength = length;
            bestDistance = position - candidate;
            if (length === 18) break;
          }
          candidate = previous[candidate];
          checked += 1;
        }
        if (bestLength >= 3) {
          var encodedDistance = bestDistance - 1;
          flags |= 1 << bit;
          compressed.push(encodedDistance & 0xFF);
          compressed.push((encodedDistance >>> 8 & 0x0F) | ((bestLength - 3) << 4));
          for (i = 0; i < bestLength; i += 1) insert(position + i);
          position += bestLength;
        } else {
          compressed.push(input[position]);
          insert(position);
          position += 1;
        }
      }
      compressed[flagIndex] = flags;
    }

    var packed = new Uint8Array(12 + compressed.length);
    packed[0] = 0x47; packed[1] = 0x47; packed[2] = 0x50; packed[3] = 0x31;
    writeU32Le(packed, 4, jsonBytes.length);
    writeU32Le(packed, 8, mapBytes.length);
    packed.set(compressed, 12);
    return packed;
  }

  function buildPreviewUri(value) {
    var document = unwrapDocument(value) || {};
    var validation;
    var json;
    var map;
    var jsonBytes;
    var mapBytes;
    var target;
    var packed;
    if (document.format !== FORMAT_V1) throw new Error("Preview links currently support V1 maps only.");
    validation = validateDocument(document);
    if (!validation.valid) throw new Error("Map has validation errors; fix them before previewing.");
    json = serializeDataJson(document);
    map = serializeMapText(document);
    jsonBytes = utf8Bytes(json);
    mapBytes = utf8Bytes(map);
    if (!jsonBytes.length || !mapBytes.length ||
        jsonBytes.length > PREVIEW_FILE_MAX_BYTES ||
        mapBytes.length > PREVIEW_FILE_MAX_BYTES) {
      throw new Error("The preview package exceeds Yule's link size limit.");
    }
    packed = packPreviewBytes(jsonBytes, mapBytes);
    target = base64UrlEncode(packed);
    if (packed.length > PREVIEW_FILE_MAX_BYTES || target.length >= PREVIEW_TARGET_CAP ||
        target.length + 19 > 8000) {
      throw new Error("This map is too large for a reliable preview link. Export it as a map package instead.");
    }
    return "yule://preview/v1z/" + target;
  }

  var CRC_TABLE = (function () {
    var table = new Uint32Array(256);
    for (var n = 0; n < 256; n += 1) {
      var value = n;
      for (var bit = 0; bit < 8; bit += 1) value = value & 1 ? 0xEDB88320 ^ (value >>> 1) : value >>> 1;
      table[n] = value >>> 0;
    }
    return table;
  }());

  function crc32(input) {
    var bytes = utf8Bytes(input);
    var crc = 0xFFFFFFFF;
    for (var index = 0; index < bytes.length; index += 1) crc = CRC_TABLE[(crc ^ bytes[index]) & 0xFF] ^ (crc >>> 8);
    return (crc ^ 0xFFFFFFFF) >>> 0;
  }

  function writeU16(output, offset, value) {
    output[offset] = value & 0xFF;
    output[offset + 1] = value >>> 8 & 0xFF;
  }

  function writeU32(output, offset, value) {
    output[offset] = value & 0xFF;
    output[offset + 1] = value >>> 8 & 0xFF;
    output[offset + 2] = value >>> 16 & 0xFF;
    output[offset + 3] = value >>> 24 & 0xFF;
  }

  function normalizeZipEntries(entries) {
    var list = [];
    if (Array.isArray(entries)) {
      entries.forEach(function (entry) {
        if (Array.isArray(entry)) list.push({ name: entry[0], data: entry[1] });
        else list.push({ name: entry && (entry.name || entry.filename || entry.path), data: entry && (entry.data !== undefined ? entry.data : entry.content) });
      });
    } else if (entries && typeof entries === "object") {
      Object.keys(entries).forEach(function (name) { list.push({ name: name, data: entries[name] }); });
    } else {
      throw new TypeError("ZIP entries must be an object or array.");
    }
    var seen = Object.create(null);
    return list.map(function (entry) {
      var name = typeof entry.name === "string" ? entry.name.replace(/\\/g, "/") : "";
      if (!name || name.charAt(0) === "/" || name.indexOf("\0") >= 0 || name.split("/").some(function (part) { return part === ".." || part === ""; })) {
        throw new Error("Unsafe or empty ZIP entry name: " + name);
      }
      if (seen[name]) throw new Error("Duplicate ZIP entry name: " + name);
      seen[name] = true;
      var nameBytes = utf8Bytes(name);
      var dataBytes = utf8Bytes(entry.data);
      if (nameBytes.length > 0xFFFF) throw new Error("ZIP entry name is too long: " + name);
      return { name: name, nameBytes: nameBytes, data: dataBytes, crc: crc32(dataBytes), offset: 0 };
    });
  }

  function buildStoredZip(entries) {
    var files = normalizeZipEntries(entries);
    var localSize = 0;
    var centralSize = 0;
    files.forEach(function (file) {
      if (file.data.length > 0xFFFFFFFF) throw new Error("ZIP entry exceeds the 4 GiB classic ZIP limit: " + file.name);
      localSize += 30 + file.nameBytes.length + file.data.length;
      centralSize += 46 + file.nameBytes.length;
    });
    if (files.length > 0xFFFF || localSize + centralSize + 22 > 0xFFFFFFFF) throw new Error("Archive exceeds classic ZIP limits.");
    var output = new Uint8Array(localSize + centralSize + 22);
    var offset = 0;
    var utf8Flag = 0x0800;
    var dosDate = 0x0021; /* 1980-01-01, making exports reproducible. */
    files.forEach(function (file) {
      file.offset = offset;
      writeU32(output, offset, 0x04034B50);
      writeU16(output, offset + 4, 20);
      writeU16(output, offset + 6, utf8Flag);
      writeU16(output, offset + 8, 0);
      writeU16(output, offset + 10, 0);
      writeU16(output, offset + 12, dosDate);
      writeU32(output, offset + 14, file.crc);
      writeU32(output, offset + 18, file.data.length);
      writeU32(output, offset + 22, file.data.length);
      writeU16(output, offset + 26, file.nameBytes.length);
      writeU16(output, offset + 28, 0);
      output.set(file.nameBytes, offset + 30);
      output.set(file.data, offset + 30 + file.nameBytes.length);
      offset += 30 + file.nameBytes.length + file.data.length;
    });
    var centralOffset = offset;
    files.forEach(function (file) {
      writeU32(output, offset, 0x02014B50);
      writeU16(output, offset + 4, 20);
      writeU16(output, offset + 6, 20);
      writeU16(output, offset + 8, utf8Flag);
      writeU16(output, offset + 10, 0);
      writeU16(output, offset + 12, 0);
      writeU16(output, offset + 14, dosDate);
      writeU32(output, offset + 16, file.crc);
      writeU32(output, offset + 20, file.data.length);
      writeU32(output, offset + 24, file.data.length);
      writeU16(output, offset + 28, file.nameBytes.length);
      writeU16(output, offset + 30, 0);
      writeU16(output, offset + 32, 0);
      writeU16(output, offset + 34, 0);
      writeU16(output, offset + 36, 0);
      writeU32(output, offset + 38, 0);
      writeU32(output, offset + 42, file.offset);
      output.set(file.nameBytes, offset + 46);
      offset += 46 + file.nameBytes.length;
    });
    writeU32(output, offset, 0x06054B50);
    writeU16(output, offset + 4, 0);
    writeU16(output, offset + 6, 0);
    writeU16(output, offset + 8, files.length);
    writeU16(output, offset + 10, files.length);
    writeU32(output, offset + 12, centralSize);
    writeU32(output, offset + 16, centralOffset);
    writeU16(output, offset + 20, 0);
    return output;
  }

  function readU16(input, offset) {
    return input[offset] | input[offset + 1] << 8;
  }

  function readU32(input, offset) {
    return (input[offset] | input[offset + 1] << 8 | input[offset + 2] << 16 | input[offset + 3] << 24) >>> 0;
  }

  function utf8Text(value) {
    var bytes = utf8Bytes(value);
    /* ignoreBOM=true means "do not consume it" in TextDecoder terminology.
     * Keeping U+FEFF lets the parser reject a native-incompatible BOM and lets
     * preserved V2 package bytes round-trip unchanged. */
    if (typeof TextDecoder !== "undefined") return new TextDecoder("utf-8", { fatal: false, ignoreBOM: true }).decode(bytes);
    var binary = "";
    for (var index = 0; index < bytes.length; index += 1) binary += String.fromCharCode(bytes[index]);
    try { return decodeURIComponent(escape(binary)); } catch (error) { return binary; }
  }

  function parseStoredZip(input) {
    var bytes = utf8Bytes(input);
    var eocd = -1;
    var min = Math.max(0, bytes.length - 65557);
    var offset;
    for (offset = bytes.length - 22; offset >= min; offset -= 1) {
      if (readU32(bytes, offset) === 0x06054B50) { eocd = offset; break; }
    }
    if (eocd < 0) throw new Error("ZIP end-of-central-directory record was not found.");
    if (readU16(bytes, eocd + 4) !== 0 || readU16(bytes, eocd + 6) !== 0) throw new Error("Multi-disk ZIP archives are not supported.");
    var count = readU16(bytes, eocd + 10);
    var centralSize = readU32(bytes, eocd + 12);
    var centralOffset = readU32(bytes, eocd + 16);
    if (centralOffset + centralSize > eocd || centralOffset > bytes.length) throw new Error("ZIP central directory is out of bounds.");
    var entries = {};
    offset = centralOffset;
    for (var fileIndex = 0; fileIndex < count; fileIndex += 1) {
      if (offset + 46 > bytes.length || readU32(bytes, offset) !== 0x02014B50) throw new Error("ZIP central directory entry is malformed.");
      var flags = readU16(bytes, offset + 8);
      var method = readU16(bytes, offset + 10);
      var expectedCrc = readU32(bytes, offset + 16);
      var compressedSize = readU32(bytes, offset + 20);
      var uncompressedSize = readU32(bytes, offset + 24);
      var nameLength = readU16(bytes, offset + 28);
      var extraLength = readU16(bytes, offset + 30);
      var commentLength = readU16(bytes, offset + 32);
      var localOffset = readU32(bytes, offset + 42);
      if (flags & 1) throw new Error("Encrypted ZIP entries are not supported.");
      if (method !== 0) throw new Error("This dependency-free importer accepts stored ZIP entries only; choose the data.json/data.map folder import for compressed archives.");
      if (compressedSize !== uncompressedSize) throw new Error("Stored ZIP entry has inconsistent sizes.");
      if (offset + 46 + nameLength + extraLength + commentLength > bytes.length) throw new Error("ZIP central entry exceeds the archive bounds.");
      var name = utf8Text(bytes.subarray(offset + 46, offset + 46 + nameLength));
      if (!name || name.charAt(0) === "/" || name.indexOf("\\") >= 0 || name.indexOf("\0") >= 0 || name.split("/").some(function (part) { return part === ".."; })) throw new Error("Unsafe ZIP entry path: " + name);
      if (entries[name] !== undefined) throw new Error("Duplicate ZIP entry: " + name);
      if (localOffset + 30 > bytes.length || readU32(bytes, localOffset) !== 0x04034B50) throw new Error("ZIP local entry is malformed: " + name);
      var localNameLength = readU16(bytes, localOffset + 26);
      var localExtraLength = readU16(bytes, localOffset + 28);
      var dataOffset = localOffset + 30 + localNameLength + localExtraLength;
      if (dataOffset + compressedSize > bytes.length) throw new Error("ZIP entry data exceeds archive bounds: " + name);
      var data = bytes.slice(dataOffset, dataOffset + compressedSize);
      if (crc32(data) !== expectedCrc) throw new Error("ZIP entry CRC-32 does not match: " + name);
      if (name.charAt(name.length - 1) !== "/") entries[name] = data;
      offset += 46 + nameLength + extraLength + commentLength;
    }
    return entries;
  }

  function normalizeFolderId(value) {
    var id = normalizeMapId(value);
    if (id.charAt(0) === "_") id = "map" + id;
    if (id.charAt(0) === "." || id.charAt(0) === "-") id = "map_" + id.slice(1);
    return id.slice(0, 63) || "custom_map";
  }

  function parsePackageFiles(files, options) {
    var entries = normalizeAssets(files);
    var names = Object.keys(entries).map(function (name) { return name.replace(/\\/g, "/").replace(/^\.\//, ""); });
    var jsonNames = names.filter(function (name) { return /(^|\/)data\.json$/i.test(name); });
    var pair = null;
    jsonNames.forEach(function (jsonName) {
      var prefix = jsonName.slice(0, jsonName.length - "data.json".length);
      var mapName = names.filter(function (name) { return name.toLowerCase() === (prefix + "data.map").toLowerCase(); })[0];
      if (mapName) {
        if (pair) throw new Error("Package contains more than one data.json/data.map pair.");
        pair = { json: jsonName, map: mapName, prefix: prefix };
      }
    });
    if (!pair) throw new Error("Package must contain data.json and data.map together.");
    if (pair.prefix && pair.prefix.slice(0, -1).indexOf("/") >= 0) throw new Error("Package files must be direct children of one top-level map folder.");
    var direct = {};
    names.forEach(function (normalizedName) {
      if (normalizedName.slice(0, pair.prefix.length).toLowerCase() !== pair.prefix.toLowerCase()) return;
      var local = normalizedName.slice(pair.prefix.length);
      if (!local || local.indexOf("/") >= 0) return;
      var originalName = Object.keys(entries).filter(function (candidate) { return candidate.replace(/\\/g, "/").replace(/^\.\//, "") === normalizedName; })[0];
      direct[local] = entries[originalName];
    });
    var assets = {};
    Object.keys(direct).forEach(function (name) { if (/\.png$/i.test(name)) assets[name] = direct[name]; });
    var callOptions = deepClone(options || {});
    callOptions.assets = assets;
    callOptions.folderId = pair.prefix ? pair.prefix.slice(0, -1) : (callOptions.folderId || null);
    var luaName = Object.keys(direct).filter(function (name) { return name.toLowerCase() === "map.lua"; })[0];
    if (luaName !== undefined) {
      callOptions.mapLua = utf8Text(direct[luaName]);
      callOptions.mapLuaBytes = direct[luaName];
    }
    var jsonNameDirect = Object.keys(direct).filter(function (name) { return name.toLowerCase() === "data.json"; })[0];
    var mapNameDirect = Object.keys(direct).filter(function (name) { return name.toLowerCase() === "data.map"; })[0];
    callOptions.dataJsonBytes = direct[jsonNameDirect];
    callOptions.dataMapBytes = direct[mapNameDirect];
    return parsePackage(utf8Text(direct[jsonNameDirect]), utf8Text(direct[mapNameDirect]), callOptions);
  }

  function exportProjectFiles(value, options) {
    var document = unwrapDocument(value) || {};
    var validation = validateDocument(document);
    options = options || {};
    if (!validation.valid && !(document.format === FORMAT_V2 && document._preserved) && !options.allowInvalid) throw new Error("Map has validation errors; fix them before export.");
    var preserved = document.format === FORMAT_V2 && document._preserved ? document._preserved : null;
    var files = {
      "data.json": preserved && preserved.dataJsonBytes ? copyBytes(preserved.dataJsonBytes) : serializeDataJson(document),
      "data.map": preserved && preserved.dataMapBytes ? copyBytes(preserved.dataMapBytes) : serializeMapText(document)
    };
    if (document.format === FORMAT_V2 && document._preserved) {
      Object.keys(document._preserved.assets || {}).forEach(function (name) {
        if (!name || /[\\\/:]/.test(name) || !/\.png$/i.test(name)) throw new Error("Unsafe preserved V2 asset name: " + name);
        files[name] = document._preserved.assets[name];
      });
      if (document._preserved.mapLuaPresent) files["map.lua"] = document._preserved.mapLuaBytes ? copyBytes(document._preserved.mapLuaBytes) : document._preserved.mapLua;
    } else if (document.format === FORMAT_V2) {
      Object.keys(document.assets || {}).forEach(function (name) {
        if (!name || /[\\\/:]/.test(name) || !/\.png$/i.test(name)) throw new Error("Unsafe V2 asset name: " + name);
        var bytes = copyBytes(document.assets[name]);
        if (!bytes) throw new Error("V2 asset could not be decoded: " + name);
        files[name] = bytes;
      });
      if (document.mapLuaPresent || document.mapLua !== undefined) files["map.lua"] = String(document.mapLua || "");
    }
    return files;
  }

  function buildPackageZip(value, options) {
    var document = unwrapDocument(value) || {};
    options = options || {};
    var folder = normalizeFolderId(options.folderId || (document._preserved && document._preserved.folderId) || document.id || document.name || "custom_map");
    var direct = exportProjectFiles(document, options);
    var entries = {};
    Object.keys(direct).forEach(function (name) { entries[folder + "/" + name] = direct[name]; });
    return { folderId: folder, files: direct, bytes: buildStoredZip(entries) };
  }

  function parseProject(input, dataMapText, options) {
    if (typeof input === "string") return parsePackage(input, dataMapText, options);
    input = input || {};
    var projectOptions = {
      assets: input.assets || input.files,
      folderId: input.folderId
    };
    if (hasOwn(input, "mapLua")) projectOptions.mapLua = input.mapLua;
    else if (hasOwn(input, "mapLuaText")) projectOptions.mapLua = input.mapLuaText;
    return parsePackage(input.dataJsonText !== undefined ? input.dataJsonText : input.dataJson, input.dataMapText !== undefined ? input.dataMapText : input.dataMap, projectOptions);
  }

  return {
    COLS: COLS,
    ROWS: ROWS,
    FORMAT: FORMAT,
    FORMAT_V1: FORMAT_V1,
    FORMAT_V2: FORMAT_V2,
    FORMATS: [FORMAT_V1, FORMAT_V2],
    MAX_ROOMS: MAX_ROOMS,
    MAX_PARSED_ROOMS: MAX_PARSED_ROOMS,
    MAX_TEXT_BYTES: MAX_TEXT_BYTES,
    MAX_SCRIPT_BYTES: MAX_SCRIPT_BYTES,
    PREVIEW_TARGET_CAP: PREVIEW_TARGET_CAP,
    PREVIEW_FILE_MAX_BYTES: PREVIEW_FILE_MAX_BYTES,
    LAYOUT_KIND: LAYOUT_KIND,
    ROOM_FORMAT: ROOM_FORMAT,
    GLYPHS: GLYPHS,
    GLYPH_WHITELIST: GLYPH_WHITELIST,
    ALLOWED_GLYPHS: GLYPH_WHITELIST,
    TILE_METADATA: TILE_METADATA,
    TILES: TILE_METADATA,
    NATIVE_TILES: TILE_METADATA,
    TILE_BY_GLYPH: TILE_BY_GLYPH,
    COLOR_KEYS: COLOR_KEYS,
    COLOR_DEFAULTS: COLOR_DEFAULTS,
    COLOR_DEFAULT_FLOATS: COLOR_DEFAULT_FLOATS,
    DEFAULT_COLORS: COLOR_DEFAULTS,
    AMBIENTS: AMBIENTS,
    AMBIENT_ALIASES: AMBIENT_ALIASES,
    colorToHex: colorToHex,
    rgbToHex: colorToHex,
    floatRgbToHex: colorToHex,
    hexToColor: hexToColor,
    hexToRgb: hexToColor,
    hexToFloatRgb: hexToColor,
    normalizeColorBank: normalizeColorBank,
    createBlankGrid: createBlankGrid,
    createArenaGrid: createArenaGrid,
    createRoom: createRoom,
    createDefaultDocument: createDefaultDocument,
    createProject: createDefaultDocument,
    upgradeToV2: upgradeToV2,
    normalizeMapId: normalizeMapId,
    normalizeV2Id: normalizeV2Id,
    normalizeFolderId: normalizeFolderId,
    deepClone: deepClone,
    makeIssue: makeIssue,
    utf8Bytes: utf8Bytes,
    utf8Text: utf8Text,
    base64UrlEncode: base64UrlEncode,
    packPreviewBytes: packPreviewBytes,
    buildPreviewUri: buildPreviewUri,
    parseMapText: parseMapText,
    serializeMapText: serializeMapText,
    buildDataObject: buildDataObject,
    serializeDataJson: serializeDataJson,
    parsePackage: parsePackage,
    parseProject: parseProject,
    parsePackageFiles: parsePackageFiles,
    validateDocument: validateDocument,
    resolveRoomAppearance: resolveRoomAppearance,
    expandMirroredLayout: expandMirroredLayout,
    packageFacts: packageFacts,
    exportProjectFiles: exportProjectFiles,
    buildPackageZip: buildPackageZip,
    crc32: crc32,
    CRC32: crc32,
    buildStoredZip: buildStoredZip,
    parseStoredZip: parseStoredZip
  };
}));
