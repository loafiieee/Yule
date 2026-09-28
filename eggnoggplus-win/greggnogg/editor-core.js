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
  var ROOM_GRAPH_KIND = "room_graph";
  var ROOM_FORMAT = "vanilla_33x12";
  var VARIABLE_ROOM_FORMAT = "variable_cells";
  /* The supported executable owns 17 fixed native room-info slots. Keep the
   * authoring ceiling aligned with the loader so every valid editor graph is
   * playable instead of merely serializable. */
  var MAX_ROOM_GRAPH_NODES = 17;
  var MAX_ROOM_GRAPH_CONNECTIONS = 256;
  var ROOM_GRAPH_COORD_LIMIT = 4096;
  var MIN_ROOM_COLS = 8;
  var MAX_ROOM_COLS = 128;
  var MIN_ROOM_ROWS = 6;
  var MAX_ROOM_ROWS = 64;
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
    metadata("i", "Crowd", "decoration", 0x1D, "crowd_action", 0x3E, "random", "Crowd backdrop with four animated spectators.", ["i.gif"]),
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

  function createBlankGrid(fill, width, height) {
    var glyph = typeof fill === "string" && fill.length ? fill.charAt(0) : " ";
    var grid = [];
    var row;
    width = Number.isInteger(width) ? width : COLS;
    height = Number.isInteger(height) ? height : ROWS;
    for (row = 0; row < height; row += 1) grid.push(new Array(width).fill(glyph));
    return grid;
  }

  function createArenaGrid(width, height) {
    width = Number.isInteger(width) ? width : COLS;
    height = Number.isInteger(height) ? height : ROWS;
    var grid = createBlankGrid(" ", width, height);
    var col;
    /* Two rows of automatic terrain produce a stable floor with plenty of
     * separated native respawn candidates. Avoid boxing the top of the room:
     * an @ below another solid @ resolves as a wall, not a spawn floor. */
    for (col = 0; col < width; col += 1) {
      grid[height - 2][col] = "@";
      grid[height - 1][col] = "@";
    }
    return grid;
  }

  function createRoom(id, width, height) {
    return {
      id: id,
      grid: createArenaGrid(width, height),
      ambient: "none"
    };
  }

  function roomDimensions(room) {
    var grid = getGrid(room);
    var height = Array.isArray(grid) ? grid.length : 0;
    var width = height ? rowString(grid[0]).length : 0;
    return { width: width, height: height };
  }

  function spawnNativeGlyph(glyph, tileset) {
    var tile = isPlainObject(tileset) && Array.isArray(tileset.tiles) &&
      tileset.tiles.find(function (entry) { return entry && entry.symbol === glyph; });
    var collision;
    if (!tile) return glyph;
    collision = tile.collision === "passthrough" ? "pass_through" : (tile.collision || "native");
    if (collision === "solid") return "@";
    if (collision === "pass_through") return "x";
    if (collision === "hazard") return "X";
    return tile.native_glyph || glyph;
  }

  function isSafeSpawnFloor(room, x, y, tileset) {
    var grid = getGrid(room);
    var dimensions = roomDimensions(room);
    var here;
    var above;
    if (!Number.isInteger(x) || !Number.isInteger(y) || x <= 0 ||
        x >= dimensions.width - 1 || y <= 0 || y >= dimensions.height ||
        !Array.isArray(grid) || !grid[y] || !grid[y - 1]) return false;
    here = spawnNativeGlyph(rowString(grid[y]).charAt(x), tileset);
    above = spawnNativeGlyph(rowString(grid[y - 1]).charAt(x), tileset);
    return here === "@" && "!@_Xv12WwmK".indexOf(above) < 0;
  }

  /* Resize around the bottom-left corner. Platform maps are authored from the
   * floor upward, so changing height should not silently move the floor or any
   * spawn overlay painted on it. */
  function resizeRoom(room, width, height) {
    var oldGrid = getGrid(room);
    var old = roomDimensions(room);
    var next;
    var copyWidth;
    var copyHeight;
    var oldTop;
    var newTop;
    var row;
    var col;
    if (!room || !Number.isInteger(width) || !Number.isInteger(height) ||
        width < MIN_ROOM_COLS || width > MAX_ROOM_COLS ||
        height < MIN_ROOM_ROWS || height > MAX_ROOM_ROWS) {
      throw new Error("Room size must be " + MIN_ROOM_COLS + "-" + MAX_ROOM_COLS + " tiles wide and " + MIN_ROOM_ROWS + "-" + MAX_ROOM_ROWS + " tiles tall.");
    }
    if (!Array.isArray(oldGrid) || !old.width || !old.height ||
        !oldGrid.every(function (line) { return rowString(line).length === old.width; })) {
      throw new Error("Room grid has invalid dimensions.");
    }
    next = createBlankGrid(" ", width, height);
    copyWidth = Math.min(old.width, width);
    copyHeight = Math.min(old.height, height);
    oldTop = old.height - copyHeight;
    newTop = height - copyHeight;
    for (row = 0; row < copyHeight; row += 1) {
      var source = Array.isArray(oldGrid[oldTop + row]) ? oldGrid[oldTop + row] : rowString(oldGrid[oldTop + row]).split("");
      for (col = 0; col < copyWidth; col += 1) next[newTop + row][col] = source[col];
    }
    room.grid = next;
    if (room.spawn && room.spawn.players) {
      Object.keys(room.spawn.players).forEach(function (key) {
        var point = room.spawn.players[key];
        if (!point || !Number.isInteger(point.x) || !Number.isInteger(point.y)) return;
        point.y += height - old.height;
        if (point.x < 0 || point.y < 0 || point.x >= width || point.y >= height) delete room.spawn.players[key];
      });
    }
    if (room.spawn && Array.isArray(room.spawn.markers)) {
      room.spawn.markers = room.spawn.markers.map(function (marker) {
        return Object.assign({}, marker, { y: marker.y + height - old.height });
      }).filter(function (marker) {
        return Number.isInteger(marker.x) && Number.isInteger(marker.y) && marker.x >= 0 && marker.y >= 0 && marker.x < width && marker.y < height;
      });
    }
    return room;
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
      particles: [],
      ambiances: [],
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
    document.particles = Array.isArray(document.particles) ? document.particles : [];
    document.ambiances = Array.isArray(document.ambiances) ? document.ambiances : [];
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

  function graphField(object, camel, snake, fallback) {
    if (!object || typeof object !== "object") return fallback;
    if (object[camel] !== undefined) return object[camel];
    if (object[snake] !== undefined) return object[snake];
    return fallback;
  }

  function validateRoomGraph(value, explicitLayout) {
    var document = unwrapDocument(value) || {};
    var layout = explicitLayout || document.layout;
    var rooms = roomList(document);
    var roomById = Object.create(null);
    var referencedRooms = Object.create(null);
    var nodes = [];
    var nodeById = Object.create(null);
    var connections = [];
    var errors = [];
    var warnings = [];
    var sides = { left: true, right: true, top: true, bottom: true };
    var opposite = { left: "right", right: "left", top: "bottom", bottom: "top" };
    var endpointRanges = Object.create(null);
    var connectionIds = Object.create(null);
    var adjacency = Object.create(null);
    var bounds = { x: 0, y: 0, width: 0, height: 0 };
    var minX = Infinity;
    var minY = Infinity;
    var maxX = -Infinity;
    var maxY = -Infinity;

    rooms.forEach(function (room) {
      if (room && typeof room.id === "string" && !roomById[room.id]) roomById[room.id] = room;
    });

    if (!isPlainObject(layout)) {
      errors.push(makeIssue("error", "room_graph_required", "room_graph layout must be an object.", { path: "layout" }));
      return { valid: false, errors: errors, warnings: warnings, issues: errors.slice(), nodes: nodes, connections: connections, bounds: bounds };
    }
    if (layout.kind !== ROOM_GRAPH_KIND) errors.push(makeIssue("error", "room_graph_kind", "layout.kind must be \"" + ROOM_GRAPH_KIND + "\".", { path: "layout.kind" }));
    if (graphField(layout, "roomFormat", "room_format", undefined) !== VARIABLE_ROOM_FORMAT) {
      errors.push(makeIssue("error", "room_graph_format", "room_graph requires layout.room_format \"" + VARIABLE_ROOM_FORMAT + "\".", { path: "layout.room_format" }));
    }
    if (!Array.isArray(layout.nodes) || !layout.nodes.length || layout.nodes.length > MAX_ROOM_GRAPH_NODES) {
      errors.push(makeIssue("error", "room_graph_node_count", "layout.nodes must contain between 1 and " + MAX_ROOM_GRAPH_NODES + " room instances.", { path: "layout.nodes" }));
    } else {
      layout.nodes.forEach(function (source, index) {
        var path = "layout.nodes." + index;
        if (!isPlainObject(source)) {
          errors.push(makeIssue("error", "room_graph_node", "Every room graph node must be an object.", { path: path }));
          return;
        }
        var id = source.id;
        var roomId = source.room;
        var x = source.x;
        var y = source.y;
        var mirrorX = graphField(source, "mirrorX", "mirror_x", false);
        var appearance = source.appearance === undefined ? "primary" : source.appearance;
        var overrides = source.overrides === undefined ? {} : source.overrides;
        var room = typeof roomId === "string" ? roomById[roomId] : null;
        var dimensions = room ? roomDimensions(room) : { width: 0, height: 0 };
        var idValid = typeof id === "string" && /^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$/.test(id);
        var coordinateValid = Number.isInteger(x) && Number.isInteger(y) &&
          x >= -ROOM_GRAPH_COORD_LIMIT && y >= -ROOM_GRAPH_COORD_LIMIT &&
          x + dimensions.width <= ROOM_GRAPH_COORD_LIMIT && y + dimensions.height <= ROOM_GRAPH_COORD_LIMIT;

        if (!idValid) errors.push(makeIssue("error", "room_graph_node_id", "Room graph node ids must be 1-63 safe ASCII characters.", { path: path + ".id" }));
        else if (nodeById[id]) errors.push(makeIssue("error", "room_graph_duplicate_node", "Room graph node id \"" + id + "\" is used more than once.", { path: path + ".id" }));
        if (typeof roomId !== "string" || !roomId) errors.push(makeIssue("error", "room_graph_room_id", "Every room graph node must name a source room.", { path: path + ".room" }));
        else if (!room) errors.push(makeIssue("error", "room_graph_unknown_room", "Room graph node \"" + (id || index + 1) + "\" references unknown source room \"" + roomId + "\".", { path: path + ".room", roomId: roomId }));
        else referencedRooms[roomId] = true;
        if (!Number.isInteger(x) || !Number.isInteger(y)) errors.push(makeIssue("error", "room_graph_coordinate", "Room graph coordinates must be whole tile cells.", { path: path }));
        else if (!coordinateValid) errors.push(makeIssue("error", "room_graph_coordinate_bounds", "Room graph instances must stay within +/-" + ROOM_GRAPH_COORD_LIMIT + " cells.", { path: path }));
        if (typeof mirrorX !== "boolean") errors.push(makeIssue("error", "room_graph_mirror", "mirror_x must be true or false.", { path: path + ".mirror_x" }));
        if (appearance !== "primary" && appearance !== "mirror") errors.push(makeIssue("error", "room_graph_appearance", "appearance must be \"primary\" or \"mirror\".", { path: path + ".appearance" }));
        if (!isPlainObject(overrides)) {
          errors.push(makeIssue("error", "room_graph_overrides", "Placed-room overrides must be an object.", { path: path + ".overrides" }));
          overrides = {};
        } else {
          Object.keys(overrides).forEach(function (key) {
            if (["ambient", "opponent_spawn", "native_tileset", "spawn"].indexOf(key) < 0) {
              errors.push(makeIssue("error", "room_graph_override_key", "Placed-room overrides support ambience, opponent spawn, room graphics, and spawn positions.", { path: path + ".overrides." + key }));
            }
          });
          if (hasOwn(overrides, "ambient") && !validAmbient(overrides.ambient, customAmbientIds(document))) {
            errors.push(makeIssue("error", "room_graph_ambient", "Placed-room ambience must be a built-in name, declared custom ambiance, or integer from 0 to 9.", { path: path + ".overrides.ambient" }));
          }
          validateOpponentSpawn(overrides.opponent_spawn, path + ".overrides.opponent_spawn", errors);
          if (hasOwn(overrides, "native_tileset") && typeof overrides.native_tileset !== "string") {
            errors.push(makeIssue("error", "room_graph_native_tileset", "Placed-room graphics must name a packaged PNG sheet.", { path: path + ".overrides.native_tileset" }));
          }
          if (room && hasOwn(overrides, "spawn")) validateSpawnOverlay(overrides.spawn, path + ".overrides.spawn", dimensions.width, dimensions.height, errors, roomId, room, document.tileset);
        }

        var normalized = {
          id: id,
          room: roomId,
          x: x,
          y: y,
          width: dimensions.width,
          height: dimensions.height,
          mirrorX: mirrorX,
          appearance: appearance,
          overrides: deepClone(overrides)
        };
        nodes.push(normalized);
        if (idValid && !nodeById[id]) {
          nodeById[id] = normalized;
          adjacency[id] = [];
        }
        if (room && coordinateValid) {
          minX = Math.min(minX, x);
          minY = Math.min(minY, y);
          maxX = Math.max(maxX, x + dimensions.width);
          maxY = Math.max(maxY, y + dimensions.height);
        }
      });
    }

    rooms.forEach(function (room) {
      if (room && typeof room.id === "string" && room.id && !referencedRooms[room.id]) {
        warnings.push(makeIssue("warning", "room_graph_unused_room", "Room design \"" + room.id + "\" has not been placed on the room layout yet.", { path: "layout.nodes", roomId: room.id }));
      }
    });

    for (var firstIndex = 0; firstIndex < nodes.length; firstIndex += 1) {
      var first = nodes[firstIndex];
      if (!nodeById[first.id] || !first.width || !first.height || !Number.isInteger(first.x) || !Number.isInteger(first.y)) continue;
      for (var secondIndex = firstIndex + 1; secondIndex < nodes.length; secondIndex += 1) {
        var second = nodes[secondIndex];
        if (!nodeById[second.id] || !second.width || !second.height || !Number.isInteger(second.x) || !Number.isInteger(second.y)) continue;
        if (first.x < second.x + second.width && first.x + first.width > second.x &&
            first.y < second.y + second.height && first.y + first.height > second.y) {
          errors.push(makeIssue("error", "room_graph_overlap", "Room graph nodes \"" + first.id + "\" and \"" + second.id + "\" overlap.", { path: "layout.nodes." + secondIndex, roomId: second.room }));
        }
      }
    }

    function edgeLength(node, side) {
      return side === "left" || side === "right" ? node.height : node.width;
    }
    function openingsAlign(fromNode, fromSide, fromOffset, toNode, toSide, toOffset) {
      if (fromSide === "right" && toSide === "left") return fromNode.x + fromNode.width === toNode.x && fromNode.y + fromOffset === toNode.y + toOffset;
      if (fromSide === "left" && toSide === "right") return fromNode.x === toNode.x + toNode.width && fromNode.y + fromOffset === toNode.y + toOffset;
      if (fromSide === "bottom" && toSide === "top") return fromNode.y + fromNode.height === toNode.y && fromNode.x + fromOffset === toNode.x + toOffset;
      if (fromSide === "top" && toSide === "bottom") return fromNode.y === toNode.y + toNode.height && fromNode.x + fromOffset === toNode.x + toOffset;
      return false;
    }
    function reserveEndpoint(nodeId, side, offset, span, path) {
      var key = nodeId + "|" + side;
      var ranges = endpointRanges[key] || (endpointRanges[key] = []);
      var end = offset + span;
      for (var rangeIndex = 0; rangeIndex < ranges.length; rangeIndex += 1) {
        if (offset < ranges[rangeIndex].end && end > ranges[rangeIndex].start) {
          errors.push(makeIssue("error", "room_graph_ambiguous_exit", "Multiple connections overlap on " + nodeId + "'s " + side + " edge.", { path: path }));
          break;
        }
      }
      ranges.push({ start: offset, end: end });
    }

    if (!Array.isArray(layout.connections) || layout.connections.length > MAX_ROOM_GRAPH_CONNECTIONS) {
      errors.push(makeIssue("error", "room_graph_connection_count", "layout.connections must be an array with at most " + MAX_ROOM_GRAPH_CONNECTIONS + " entries.", { path: "layout.connections" }));
    } else {
      layout.connections.forEach(function (source, index) {
        var path = "layout.connections." + index;
        if (!isPlainObject(source)) {
          errors.push(makeIssue("error", "room_graph_connection", "Every room graph connection must be an object.", { path: path }));
          return;
        }
        var from = source.from;
        var to = source.to;
        var connectionId = source.id;
        var fromSide = graphField(source, "fromSide", "from_side", undefined);
        var toSide = graphField(source, "toSide", "to_side", undefined);
        var fromOffset = graphField(source, "fromOffset", "from_offset", undefined);
        var toOffset = graphField(source, "toOffset", "to_offset", undefined);
        var span = source.span;
        var oneWay = graphField(source, "oneWay", "one_way", false);
        var players = source.players === undefined ? "both" : source.players;
        var focus = source.focus === undefined ? "go" : source.focus;
        var fromNode = typeof from === "string" ? nodeById[from] : null;
        var toNode = typeof to === "string" ? nodeById[to] : null;
        var sidesValid = !!sides[fromSide] && !!sides[toSide];
        var valuesValid = Number.isInteger(fromOffset) && fromOffset >= 0 && Number.isInteger(toOffset) && toOffset >= 0 && Number.isInteger(span) && span > 0;
        var fromRangeValid = fromNode && valuesValid && fromOffset + span <= edgeLength(fromNode, fromSide);
        var toRangeValid = toNode && valuesValid && toOffset + span <= edgeLength(toNode, toSide);
        var geometryValid = fromNode && toNode && sidesValid && opposite[fromSide] === toSide && openingsAlign(fromNode, fromSide, fromOffset, toNode, toSide, toOffset);

        if (connectionId === undefined) connectionId = roomGraphUniqueConnectionId(source, connectionIds);
        else if (typeof connectionId !== "string" || !/^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$/.test(connectionId)) {
          errors.push(makeIssue("error", "room_graph_connection_id", "Doorway ids must be 1-63 safe ASCII characters.", { path: path + ".id" }));
        } else if (connectionIds[connectionId]) {
          errors.push(makeIssue("error", "room_graph_duplicate_connection_id", "Doorway id \"" + connectionId + "\" is used more than once.", { path: path + ".id" }));
        } else connectionIds[connectionId] = true;
        connections.push({ id: connectionId, from: from, fromSide: fromSide, fromOffset: fromOffset, to: to, toSide: toSide, toOffset: toOffset, span: span, oneWay: oneWay, players: players, focus: focus });
        if (!fromNode) errors.push(makeIssue("error", "room_graph_unknown_node", "Connection references unknown from node \"" + String(from) + "\".", { path: path + ".from" }));
        if (!toNode) errors.push(makeIssue("error", "room_graph_unknown_node", "Connection references unknown to node \"" + String(to) + "\".", { path: path + ".to" }));
        if (fromNode && toNode && from === to) errors.push(makeIssue("error", "room_graph_self_connection", "A room graph connection cannot connect a node to itself.", { path: path }));
        if (!sidesValid) errors.push(makeIssue("error", "room_graph_connection_side", "Connection sides must be left, right, top, or bottom.", { path: path }));
        else if (opposite[fromSide] !== toSide) errors.push(makeIssue("error", "room_graph_connection_opposites", "Connected doorways must use opposite room edges.", { path: path }));
        if (!valuesValid) errors.push(makeIssue("error", "room_graph_connection_range", "Doorway offsets must be non-negative whole cells and span must be positive.", { path: path }));
        else {
          if (fromNode && !fromRangeValid) errors.push(makeIssue("error", "room_graph_connection_range", "The connection opening is outside node \"" + from + "\".", { path: path + ".from_offset" }));
          if (toNode && !toRangeValid) errors.push(makeIssue("error", "room_graph_connection_range", "The connection opening is outside node \"" + to + "\".", { path: path + ".to_offset" }));
        }
        if (typeof oneWay !== "boolean") errors.push(makeIssue("error", "room_graph_one_way", "one_way must be true or false.", { path: path + ".one_way" }));
        if (["both", "player1", "player2", "go"].indexOf(players) < 0) {
          errors.push(makeIssue("error", "room_graph_players", "players must be both, player1, player2, or go.", { path: path + ".players" }));
        }
        if (focus !== "go" && focus !== "crossing") {
          errors.push(makeIssue("error", "room_graph_focus", "focus must be go or crossing.", { path: path + ".focus" }));
        }
        if (fromNode && toNode && sidesValid && opposite[fromSide] === toSide && valuesValid && !geometryValid) {
          errors.push(makeIssue("error", "room_graph_doorway_alignment", "Connected room edges and doorway cells must touch exactly.", { path: path }));
        }
        if (fromNode && sides[fromSide] && fromRangeValid) reserveEndpoint(from, fromSide, fromOffset, span, path + ".from_offset");
        if (toNode && sides[toSide] && toRangeValid) reserveEndpoint(to, toSide, toOffset, span, path + ".to_offset");
        if (fromNode && toNode && from !== to && sidesValid && opposite[fromSide] === toSide && fromRangeValid && toRangeValid && geometryValid && typeof oneWay === "boolean") {
          adjacency[from].push(to);
          if (!oneWay) adjacency[to].push(from);
        }
      });
    }

    var start = layout.start;
    if (typeof start !== "string" || !nodeById[start]) {
      errors.push(makeIssue("error", "room_graph_start", "layout.start must name a room graph node.", { path: "layout.start" }));
    } else {
      var visited = Object.create(null);
      var queue = [start];
      visited[start] = true;
      while (queue.length) {
        var current = queue.shift();
        (adjacency[current] || []).forEach(function (next) {
          if (!visited[next]) { visited[next] = true; queue.push(next); }
        });
      }
      Object.keys(nodeById).forEach(function (id) {
        if (!visited[id]) errors.push(makeIssue("error", "room_graph_unreachable", "Room graph node \"" + id + "\" cannot be reached from start node \"" + start + "\".", { path: "layout.nodes", roomId: nodeById[id].room }));
      });
    }

    if (!explicitLayout && document.entities && Array.isArray(document.entities.placements)) {
      document.entities.placements.forEach(function (placement, index) {
        if (!placement || placement.instance === undefined) return;
        if (typeof placement.instance !== "string" || !nodeById[placement.instance]) {
          errors.push(makeIssue("error", "room_graph_unknown_instance", "Object placement targets a removed or unknown placed room instance. Reassign or delete the object placement.", {
            path: "entities.placements." + index + ".instance"
          }));
        }
      });
    }

    if (minX !== Infinity) bounds = { x: minX, y: minY, width: maxX - minX, height: maxY - minY };
    return { valid: errors.length === 0, errors: errors, warnings: warnings, issues: errors.concat(warnings), nodes: nodes, connections: connections, bounds: bounds };
  }

  function createRoomGraphFromMirrored(value) {
    var document = unwrapDocument(value) || {};
    var layout = document.layout || {};
    var rooms = roomList(document);
    var roomById = Object.create(null);
    var order = Array.isArray(layout.order) ? layout.order.slice() : [];
    var finalNodes = [];
    var nodes = [];
    var connections = [];
    var seen = Object.create(null);
    var cursor = 0;
    var index;
    if (layout.kind !== LAYOUT_KIND) throw new Error("Only mirrored_source_rooms layouts can be converted to room_graph.");
    rooms.forEach(function (room) { if (room && typeof room.id === "string") roomById[room.id] = room; });
    if (!order.length || order.length !== rooms.length) throw new Error("The mirrored layout must reference every source room exactly once.");
    order.forEach(function (id) {
      if (typeof id !== "string" || !roomById[id] || seen[id]) throw new Error("The mirrored layout contains an unknown or duplicate source room.");
      seen[id] = true;
    });
    for (index = order.length - 1; index >= 1; index -= 1) finalNodes.push({ id: "left_" + index, room: order[index], mirrorX: false, appearance: "mirror" });
    finalNodes.push({ id: "center", room: order[0], mirrorX: false, appearance: "primary" });
    for (index = 1; index < order.length; index += 1) finalNodes.push({ id: "right_" + index, room: order[index], mirrorX: true, appearance: "primary" });
    finalNodes.forEach(function (source) {
      var dimensions = roomDimensions(roomById[source.room]);
      var node = { id: source.id, room: source.room, x: cursor, y: 0, mirrorX: source.mirrorX, appearance: source.appearance };
      if (!dimensions.width || !dimensions.height) throw new Error("Source room \"" + source.room + "\" has invalid dimensions.");
      nodes.push(node);
      cursor += dimensions.width;
    });
    for (index = 0; index + 1 < nodes.length; index += 1) {
      var left = nodes[index];
      var right = nodes[index + 1];
      connections.push({
        from: left.id,
        fromSide: "right",
        fromOffset: 0,
        to: right.id,
        toSide: "left",
        toOffset: 0,
        span: Math.min(roomDimensions(roomById[left.room]).height, roomDimensions(roomById[right.room]).height),
        oneWay: false,
        players: "both",
        focus: "go"
      });
    }
    var graph = { kind: ROOM_GRAPH_KIND, roomFormat: VARIABLE_ROOM_FORMAT, start: "center", nodes: nodes, connections: connections };
    var result = validateRoomGraph(document, graph);
    if (!result.valid) throw new Error("Converted room graph failed validation: " + result.errors[0].message);
    return graph;
  }

  function convertMirroredToRoomGraph(value) {
    var document = upgradeToV2(value);
    document.layout = createRoomGraphFromMirrored(document);
    roomGraphEnsureConnectionIdsInPlace(document.layout);
    if (document.entities && Array.isArray(document.entities.placements)) {
      var converted = [];
      document.entities.placements.forEach(function (placement) {
        if (placement.room === undefined) { converted.push(placement); return; }
        var side = placement.side || "both";
        var matches = document.layout.nodes.filter(function (node) {
          return node.room === placement.room &&
            (side === "both" || (side === "source" && node.mirrorX !== true) ||
             (side === "mirrored" && node.mirrorX === true));
        });
        if (!matches.length) throw new Error("Object \"" + placement.name + "\" has no matching placed room after conversion.");
        matches.forEach(function (node) {
          var copy = deepClone(placement);
          copy.instance = node.id;
          if (node.mirrorX === true) copy.name += ".mirror";
          delete copy.side;
          converted.push(copy);
        });
      });
      document.entities.placements = converted;
    }
    document.rules = document.rules || {};
    document.rules.roundEndRooms = "any";
    return document;
  }

  function convertRoomGraphToMirrored(value) {
    var document = deepClone(unwrapDocument(value) || {});
    var layout = document.layout || {};
    var order = Array.isArray(layout.order) && layout.order.length === roomList(document).length ? layout.order.slice() : roomList(document).map(function (room) { return room.id; });
    var expectedDocument;
    var expected;
    var actualById = Object.create(null);
    var nodeById = Object.create(null);
    var canonical = true;
    if (layout.kind !== ROOM_GRAPH_KIND) throw new Error("This map already uses the symmetrical layout.");
    expectedDocument = deepClone(document);
    expectedDocument.layout = { kind: LAYOUT_KIND, roomFormat: VARIABLE_ROOM_FORMAT, order: order };
    expected = createRoomGraphFromMirrored(expectedDocument);
    (layout.nodes || []).forEach(function (node) {
      actualById[node.id] = node;
      nodeById[node.id] = node;
    });
    if ((layout.nodes || []).length !== expected.nodes.length || layout.start !== expected.start) canonical = false;
    expected.nodes.forEach(function (node) {
      var actual = actualById[node.id];
      /* Choosing Symmetrical intentionally restores the generated positions.
       * Preserve a generated left/right pairing even if its rooms were dragged
       * or its old connection geometry is temporarily stale. */
      if (!actual || actual.room !== node.room ||
          actual.mirrorX !== node.mirrorX || (actual.appearance || "primary") !== node.appearance ||
          (actual.overrides && Object.keys(actual.overrides).length)) {
        canonical = false;
      }
    });
    if (!canonical) {
      var countByRoom = Object.create(null);
      var startNode = actualById[layout.start];
      var blockers = [];
      (layout.nodes || []).forEach(function (node) {
        countByRoom[node.room] = (countByRoom[node.room] || 0) + 1;
        if (node.mirrorX === true) {
          blockers.push("Placed room \"" + node.id + "\" (design \"" + node.room + "\", at " + node.x + ", " + node.y + ") is horizontally flipped. Symmetrical mode creates the opposite-side flip itself, so turn off this copy's horizontal flip first.");
        }
        if (node.overrides && Object.keys(node.overrides).length) {
          blockers.push("Placed room \"" + node.id + "\" has copy-only settings for " + Object.keys(node.overrides).join(", ") + ". Move those values onto room design \"" + node.room + "\" or clear them first; Symmetrical mode has one shared design for both sides.");
        }
      });
      Object.keys(countByRoom).forEach(function (roomId) {
        if (countByRoom[roomId] > 1) {
          var copies = (layout.nodes || []).filter(function (node) { return node.room === roomId; }).map(function (node) { return "\"" + node.id + "\" at " + node.x + ", " + node.y; });
          blockers.push("Room design \"" + roomId + "\" is used by " + countByRoom[roomId] + " placed rooms (" + copies.join("; ") + "). Symmetrical mode generates the second side automatically. Delete the extra copy, or duplicate the design if these rooms should stay independent.");
        }
      });
      if (!startNode) blockers.push("The start-room id \"" + String(layout.start || "") + "\" does not name a placed room. Select the room where the countdown should start and set it as the start room.");
      if (blockers.length) {
        throw new Error("This free layout cannot be represented without losing room-specific data:\n" +
          blockers.slice(0, 6).map(function (message) { return "• " + message; }).join("\n") +
          (blockers.length > 6 ? "\n• …and " + (blockers.length - 6) + " more conflicts." : ""));
      }
      order = [startNode.room].concat(roomList(document).map(function (room) { return room.id; }).filter(function (roomId) { return roomId !== startNode.room; }));
      if (document.entities && Array.isArray(document.entities.placements)) {
        document.entities.placements = document.entities.placements.map(function (placement) {
          var copy = deepClone(placement);
          if (copy.instance && nodeById[copy.instance]) {
            delete copy.instance;
            copy.side = "both";
          }
          return copy;
        });
      }
      document.layout = { kind: LAYOUT_KIND, roomFormat: VARIABLE_ROOM_FORMAT, order: order };
      return document;
    }
    if (document.entities && Array.isArray(document.entities.placements)) {
      var passthrough = [];
      var groups = Object.create(null);
      document.entities.placements.forEach(function (placement) {
        var node = placement.instance && nodeById[placement.instance];
        var baseName;
        var group;
        if (!node || placement.room === undefined) { passthrough.push(placement); return; }
        baseName = String(placement.name || "").replace(/\.mirror$/, "");
        group = groups[node.room + "\u0000" + baseName] || { name: baseName, room: node.room, source: null, mirrored: null };
        if (node.mirrorX === true) group.mirrored = placement;
        else group.source = placement;
        groups[node.room + "\u0000" + baseName] = group;
      });
      Object.keys(groups).forEach(function (key) {
        var group = groups[key];
        var source = group.source && deepClone(group.source);
        var mirrored = group.mirrored && deepClone(group.mirrored);
        var leftComparable;
        var rightComparable;
        if (source) { delete source.instance; source.name = group.name; }
        if (mirrored) { delete mirrored.instance; mirrored.name = group.name; }
        if (source && mirrored) {
          leftComparable = deepClone(source); rightComparable = deepClone(mirrored);
          delete leftComparable.side; delete rightComparable.side;
          if (JSON.stringify(leftComparable) !== JSON.stringify(rightComparable)) {
            var differing = Object.keys(Object.assign({}, leftComparable, rightComparable)).filter(function (field) {
              return JSON.stringify(leftComparable[field]) !== JSON.stringify(rightComparable[field]);
            });
            throw new Error("Custom object \"" + group.name + "\" differs between the two copies of room design \"" + group.room + "\" (fields: " + differing.join(", ") + "). Make those fields match, or delete one copy, before switching to Symmetrical.");
          }
          source.side = "both";
          passthrough.push(source);
        } else if (source) {
          source.side = expected.nodes.filter(function (node) { return node.room === group.room; }).length === 1 ? "both" : "source";
          passthrough.push(source);
        } else if (mirrored) {
          mirrored.side = "mirrored";
          passthrough.push(mirrored);
        }
      });
      document.entities.placements = passthrough;
    }
    document.layout = { kind: LAYOUT_KIND, roomFormat: VARIABLE_ROOM_FORMAT, order: order };
    return document;
  }

  function roomGraphOppositeSide(side) {
    return { left: "right", right: "left", top: "bottom", bottom: "top" }[side] || null;
  }

  function roomGraphUniqueNodeId(layout, preferred) {
    var used = Object.create(null);
    var base = String(preferred || "room").replace(/[^A-Za-z0-9._-]+/g, "_").replace(/^[_\.-]+/, "").slice(0, 54) || "room";
    var candidate = base;
    var suffix = 2;
    (layout.nodes || []).forEach(function (node) { if (node && typeof node.id === "string") used[node.id] = true; });
    while (used[candidate]) candidate = base + "_" + suffix++;
    return candidate;
  }

  function roomGraphUniqueConnectionId(connection, used) {
    var raw = "door_" + String(connection && connection.from || "room") + "_" +
      String(graphField(connection, "fromSide", "from_side", "edge")) + "_" +
      String(connection && connection.to || "room") + "_" +
      String(graphField(connection, "toSide", "to_side", "edge"));
    var base = raw.replace(/[^A-Za-z0-9._-]+/g, "_").replace(/^[_\.-]+/, "").slice(0, 54) || "door";
    var candidate = base;
    var suffix = 2;
    used = used || Object.create(null);
    while (used[candidate]) candidate = base.slice(0, 54 - String(suffix).length) + "_" + suffix++;
    used[candidate] = true;
    return candidate;
  }

  function roomGraphEnsureConnectionIdsInPlace(layout) {
    var used = Object.create(null);
    if (!layout || !Array.isArray(layout.connections)) return;
    layout.connections.forEach(function (connection) {
      if (connection && typeof connection.id === "string" && /^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$/.test(connection.id)) used[connection.id] = true;
    });
    layout.connections.forEach(function (connection) {
      if (connection && connection.id === undefined) connection.id = roomGraphUniqueConnectionId(connection, used);
    });
  }

  function ensureRoomGraphConnectionIds(value) {
    var document = deepClone(unwrapDocument(value) || {});
    if (document.layout && document.layout.kind === ROOM_GRAPH_KIND) roomGraphEnsureConnectionIdsInPlace(document.layout);
    return document;
  }

  function connectRoomGraphNodes(value, options) {
    var document = deepClone(unwrapDocument(value) || {});
    var layout = document.layout || {};
    var connection;
    var result;
    options = options || {};
    if (layout.kind !== ROOM_GRAPH_KIND) throw new Error("Room connections require a room_graph layout.");
    roomGraphEnsureConnectionIdsInPlace(layout);
    connection = {
      from: options.from,
      fromSide: options.fromSide,
      fromOffset: options.fromOffset === undefined ? 0 : options.fromOffset,
      to: options.to,
      toSide: options.toSide,
      toOffset: options.toOffset === undefined ? 0 : options.toOffset,
      span: options.span,
      oneWay: options.oneWay === true,
      players: options.players === undefined ? "both" : options.players,
      focus: options.focus === undefined ? "go" : options.focus
    };
    layout.connections = Array.isArray(layout.connections) ? layout.connections.slice() : [];
    layout.connections.push(connection);
    document.layout = layout;
    result = validateRoomGraph(document);
    if (!result.valid) throw new Error(result.errors[0].message);
    return document;
  }

  function roomGraphConnectionOptions(value, nodeId) {
    var document = unwrapDocument(value) || {};
    var result = validateRoomGraph(document);
    var selected;
    var connected = Object.create(null);
    var output = [];
    if (!result.valid) return output;
    result.nodes.forEach(function (node) { if (node.id === nodeId) selected = node; });
    if (!selected) return output;
    result.connections.forEach(function (connection) {
      if (connection.from === nodeId) connected[connection.to] = true;
      if (connection.to === nodeId) connected[connection.from] = true;
    });
    result.nodes.forEach(function (other) {
      var start;
      var end;
      var option;
      if (other.id === nodeId || connected[other.id]) return;
      if (selected.x + selected.width === other.x || other.x + other.width === selected.x) {
        start = Math.max(selected.y, other.y);
        end = Math.min(selected.y + selected.height, other.y + other.height);
        if (end <= start) return;
        option = selected.x + selected.width === other.x ?
          { fromSide: "right", toSide: "left" } : { fromSide: "left", toSide: "right" };
        option.fromOffset = start - selected.y;
        option.toOffset = start - other.y;
      } else if (selected.y + selected.height === other.y || other.y + other.height === selected.y) {
        start = Math.max(selected.x, other.x);
        end = Math.min(selected.x + selected.width, other.x + other.width);
        if (end <= start) return;
        option = selected.y + selected.height === other.y ?
          { fromSide: "bottom", toSide: "top" } : { fromSide: "top", toSide: "bottom" };
        option.fromOffset = start - selected.x;
        option.toOffset = start - other.x;
      } else return;
      option.from = selected.id;
      option.to = other.id;
      option.span = end - start;
      output.push(option);
    });
    return output;
  }

  function roomGraphDoorwayGeometry(value, connectionIndex) {
    var result = validateRoomGraph(value);
    var connection;
    var node;
    var x1;
    var y1;
    var x2;
    var y2;
    if (!result.valid || !Number.isInteger(connectionIndex) || connectionIndex < 0 || connectionIndex >= result.connections.length) return null;
    connection = result.connections[connectionIndex];
    node = result.nodes.find(function (entry) { return entry.id === connection.from; });
    if (!node) return null;
    if (connection.fromSide === "left" || connection.fromSide === "right") {
      x1 = x2 = node.x + (connection.fromSide === "right" ? node.width : 0);
      y1 = node.y + connection.fromOffset;
      y2 = y1 + connection.span;
    } else {
      y1 = y2 = node.y + (connection.fromSide === "bottom" ? node.height : 0);
      x1 = node.x + connection.fromOffset;
      x2 = x1 + connection.span;
    }
    return {
      connectionIndex: connectionIndex,
      id: connection.id,
      x1: x1,
      y1: y1,
      x2: x2,
      y2: y2,
      centerX: (x1 + x2) * 0.5,
      centerY: (y1 + y2) * 0.5,
      span: connection.span,
      oneWay: connection.oneWay,
      from: connection.from,
      to: connection.to
    };
  }

  function autoArrangeRoomGraph(value) {
    var document = deepClone(unwrapDocument(value) || {});
    var layout = document.layout || {};
    var roomsById = Object.create(null);
    var nodesById = Object.create(null);
    var positions = Object.create(null);
    var adjacency = Object.create(null);
    var sides = { left: true, right: true, top: true, bottom: true };
    var connections = [];
    var queue;
    var minX = Infinity;
    var minY = Infinity;
    var result;

    if (layout.kind !== ROOM_GRAPH_KIND) throw new Error("Arrangement repair requires a room_graph layout.");
    if (!Array.isArray(layout.nodes) || !layout.nodes.length) throw new Error("The room layout has no placed rooms.");
    if (!Array.isArray(layout.connections)) throw new Error("The room layout connections are missing.");
    roomList(document).forEach(function (room) {
      if (room && typeof room.id === "string" && !roomsById[room.id]) roomsById[room.id] = room;
    });
    layout.nodes.forEach(function (node) {
      var dimensions;
      if (!isPlainObject(node) || typeof node.id !== "string" || !/^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$/.test(node.id)) {
        throw new Error("Every placed room needs a valid unique id before its arrangement can be repaired.");
      }
      if (nodesById[node.id]) throw new Error("Placed room id \"" + node.id + "\" is used more than once.");
      if (!roomsById[node.room]) throw new Error("Placed room \"" + node.id + "\" references a missing room design.");
      dimensions = roomDimensions(roomsById[node.room]);
      if (!dimensions.width || !dimensions.height) throw new Error("Room design \"" + node.room + "\" has invalid dimensions.");
      nodesById[node.id] = { source: node, width: dimensions.width, height: dimensions.height };
      adjacency[node.id] = [];
    });
    if (typeof layout.start !== "string" || !nodesById[layout.start]) throw new Error("Choose a valid player start room before repairing the arrangement.");

    layout.connections.forEach(function (source, index) {
      var connection;
      var fromNode;
      var toNode;
      var fromLength;
      var toLength;
      if (!isPlainObject(source)) throw new Error("Door " + (index + 1) + " is not a valid connection.");
      connection = {
        from: source.from,
        fromSide: graphField(source, "fromSide", "from_side", undefined),
        fromOffset: graphField(source, "fromOffset", "from_offset", undefined),
        to: source.to,
        toSide: graphField(source, "toSide", "to_side", undefined),
        toOffset: graphField(source, "toOffset", "to_offset", undefined),
        span: source.span
      };
      fromNode = nodesById[connection.from];
      toNode = nodesById[connection.to];
      if (!fromNode || !toNode || connection.from === connection.to) throw new Error("Door " + (index + 1) + " must connect two different placed rooms.");
      if (!sides[connection.fromSide] || !sides[connection.toSide] || roomGraphOppositeSide(connection.fromSide) !== connection.toSide) {
        throw new Error("Door " + (index + 1) + " must connect opposite room edges.");
      }
      if (!Number.isInteger(connection.fromOffset) || connection.fromOffset < 0 ||
          !Number.isInteger(connection.toOffset) || connection.toOffset < 0 ||
          !Number.isInteger(connection.span) || connection.span < 1) {
        throw new Error("Door " + (index + 1) + " needs whole-cell starts and a positive width.");
      }
      fromLength = connection.fromSide === "left" || connection.fromSide === "right" ? fromNode.height : fromNode.width;
      toLength = connection.toSide === "left" || connection.toSide === "right" ? toNode.height : toNode.width;
      if (connection.fromOffset + connection.span > fromLength || connection.toOffset + connection.span > toLength) {
        throw new Error("Door " + (index + 1) + " extends beyond a room edge.");
      }
      connection.index = index;
      connections.push(connection);
      adjacency[connection.from].push({ connection: connection, knownIsFrom: true, other: connection.to });
      adjacency[connection.to].push({ connection: connection, knownIsFrom: false, other: connection.from });
    });

    function positionNeighbor(knownId, edge) {
      var known = nodesById[knownId];
      var other = nodesById[edge.other];
      var knownPosition = positions[knownId];
      var connection = edge.connection;
      var knownSide = edge.knownIsFrom ? connection.fromSide : connection.toSide;
      var knownOffset = edge.knownIsFrom ? connection.fromOffset : connection.toOffset;
      var otherSide = edge.knownIsFrom ? connection.toSide : connection.fromSide;
      var otherOffset = edge.knownIsFrom ? connection.toOffset : connection.fromOffset;
      var x;
      var y;
      if (knownSide === "left" || knownSide === "right") {
        x = knownPosition.x + (knownSide === "right" ? known.width : 0) - (otherSide === "right" ? other.width : 0);
        y = knownPosition.y + knownOffset - otherOffset;
      } else {
        x = knownPosition.x + knownOffset - otherOffset;
        y = knownPosition.y + (knownSide === "bottom" ? known.height : 0) - (otherSide === "bottom" ? other.height : 0);
      }
      return { x: x, y: y };
    }

    positions[layout.start] = { x: 0, y: 0 };
    queue = [layout.start];
    while (queue.length) {
      var current = queue.shift();
      adjacency[current].forEach(function (edge) {
        var proposed = positionNeighbor(current, edge);
        var existing = positions[edge.other];
        if (existing) {
          if (existing.x !== proposed.x || existing.y !== proposed.y) {
            throw new Error("Door " + (edge.connection.index + 1) + " creates an inconsistent room loop. Check its sides and doorway starts.");
          }
          return;
        }
        positions[edge.other] = proposed;
        queue.push(edge.other);
      });
    }
    Object.keys(nodesById).forEach(function (id) {
      if (!positions[id]) throw new Error("Placed room \"" + id + "\" is disconnected from the start room.");
      minX = Math.min(minX, positions[id].x);
      minY = Math.min(minY, positions[id].y);
    });
    layout.nodes.forEach(function (node) {
      node.x = positions[node.id].x - minX;
      node.y = positions[node.id].y - minY;
    });
    roomGraphEnsureConnectionIdsInPlace(layout);
    document.layout = layout;
    result = validateRoomGraph(document);
    if (!result.valid) throw new Error("The doorways imply an invalid arrangement: " + result.errors[0].message);
    return document;
  }

  function disconnectRoomGraphNodes(value, fromId, toId) {
    var document = deepClone(unwrapDocument(value) || {});
    var layout = document.layout || {};
    var removed = false;
    var result;
    if (layout.kind !== ROOM_GRAPH_KIND) throw new Error("Removing a doorway requires a room_graph layout.");
    roomGraphEnsureConnectionIdsInPlace(layout);
    result = validateRoomGraph(document);
    if (!result.valid) throw new Error("Fix the current room layout first: " + result.errors[0].message);
    layout.connections = layout.connections.filter(function (connection) {
      var match = !removed && ((connection.from === fromId && connection.to === toId) ||
        (connection.from === toId && connection.to === fromId));
      if (match) removed = true;
      return !match;
    });
    if (!removed) throw new Error("That doorway connection no longer exists.");
    document.layout = layout;
    result = validateRoomGraph(document);
    if (!result.valid) throw new Error("Removing that doorway would break the room layout: " + result.errors[0].message);
    return document;
  }

  function updateRoomGraphConnection(value, connectionIndex, options) {
    var document = deepClone(unwrapDocument(value) || {});
    var layout = document.layout || {};
    var connection;
    var result;
    options = options || {};
    if (layout.kind !== ROOM_GRAPH_KIND) throw new Error("Editing a doorway requires a room_graph layout.");
    roomGraphEnsureConnectionIdsInPlace(layout);
    if (!Number.isInteger(connectionIndex) || connectionIndex < 0 ||
        connectionIndex >= (layout.connections || []).length) throw new Error("Choose an existing doorway connection.");
    result = validateRoomGraph(document);
    if (!result.valid) throw new Error("Fix the current room layout first: " + result.errors[0].message);
    connection = layout.connections[connectionIndex];
    ["fromOffset", "toOffset", "span"].forEach(function (key) {
      if (options[key] === undefined) return;
      if (!Number.isInteger(options[key]) || options[key] < (key === "span" ? 1 : 0)) {
        throw new Error(key === "span" ? "Doorway width must be a positive whole cell count." : "Doorway starts must be non-negative whole cell counts.");
      }
      connection[key] = options[key];
    });
    if (options.oneWay !== undefined) connection.oneWay = options.oneWay === true;
    if (options.players !== undefined) {
      if (["both", "player1", "player2", "go"].indexOf(options.players) < 0) throw new Error("Can cross must be both, player1, player2, or go.");
      connection.players = options.players;
    }
    if (options.focus !== undefined) {
      if (options.focus !== "go" && options.focus !== "crossing") throw new Error("Room focus must be go or crossing.");
      connection.focus = options.focus;
    }
    document.layout = layout;
    result = validateRoomGraph(document);
    if (!result.valid) throw new Error(result.errors[0].message);
    return document;
  }

  function disconnectRoomGraphConnection(value, connectionIndex) {
    var document = deepClone(unwrapDocument(value) || {});
    var layout = document.layout || {};
    var result;
    if (layout.kind !== ROOM_GRAPH_KIND) throw new Error("Removing a doorway requires a room_graph layout.");
    roomGraphEnsureConnectionIdsInPlace(layout);
    if (!Number.isInteger(connectionIndex) || connectionIndex < 0 ||
        connectionIndex >= (layout.connections || []).length) throw new Error("That doorway connection no longer exists.");
    result = validateRoomGraph(document);
    if (!result.valid) throw new Error("Fix the current room layout first: " + result.errors[0].message);
    layout.connections.splice(connectionIndex, 1);
    document.layout = layout;
    result = validateRoomGraph(document);
    if (!result.valid) throw new Error("Removing that doorway would break the room layout: " + result.errors[0].message);
    return document;
  }

  function addConnectedRoomGraphNode(value, options) {
    var document = deepClone(unwrapDocument(value) || {});
    var layout = document.layout || {};
    var rooms = roomList(document);
    var sourceById = Object.create(null);
    var graphResult;
    var fromNode;
    var source;
    var fromSide;
    var toSide;
    var fromOffset;
    var toOffset;
    var span;
    var dimensions;
    var id;
    var node;
    options = options || {};
    if (layout.kind !== ROOM_GRAPH_KIND) throw new Error("Adding a placed room requires a room_graph layout.");
    roomGraphEnsureConnectionIdsInPlace(layout);
    graphResult = validateRoomGraph(document);
    if (!graphResult.valid) throw new Error("Fix the current room layout first: " + graphResult.errors[0].message);
    graphResult.nodes.forEach(function (entry) { if (entry.id === options.from) fromNode = entry; });
    if (!fromNode) throw new Error("Choose an existing room instance to connect from.");
    rooms.forEach(function (room) { if (room && typeof room.id === "string") sourceById[room.id] = room; });
    source = sourceById[options.room];
    if (!source) throw new Error("Choose an existing source room.");
    fromSide = options.fromSide;
    toSide = options.toSide || roomGraphOppositeSide(fromSide);
    if (!roomGraphOppositeSide(fromSide) || toSide !== roomGraphOppositeSide(fromSide)) throw new Error("Connected rooms must use opposite edges.");
    fromOffset = options.fromOffset === undefined ? 0 : options.fromOffset;
    toOffset = options.toOffset === undefined ? 0 : options.toOffset;
    dimensions = roomDimensions(source);
    if (!Number.isInteger(fromOffset) || fromOffset < 0 || !Number.isInteger(toOffset) || toOffset < 0) throw new Error("Doorway offsets must be non-negative whole cells.");
    span = options.span;
    if (span === undefined) {
      span = Math.min(
        (fromSide === "left" || fromSide === "right" ? fromNode.height : fromNode.width) - fromOffset,
        (toSide === "left" || toSide === "right" ? dimensions.height : dimensions.width) - toOffset
      );
    }
    id = roomGraphUniqueNodeId(layout, options.id || options.room);
    node = { id: id, room: options.room, x: 0, y: 0, mirrorX: options.mirrorX === true, appearance: options.appearance === "mirror" ? "mirror" : "primary" };
    if (fromSide === "right") {
      node.x = fromNode.x + fromNode.width;
      node.y = fromNode.y + fromOffset - toOffset;
    } else if (fromSide === "left") {
      node.x = fromNode.x - dimensions.width;
      node.y = fromNode.y + fromOffset - toOffset;
    } else if (fromSide === "bottom") {
      node.x = fromNode.x + fromOffset - toOffset;
      node.y = fromNode.y + fromNode.height;
    } else {
      node.x = fromNode.x + fromOffset - toOffset;
      node.y = fromNode.y - dimensions.height;
    }
    layout.nodes = Array.isArray(layout.nodes) ? layout.nodes.slice() : [];
    layout.connections = Array.isArray(layout.connections) ? layout.connections.slice() : [];
    layout.nodes.push(node);
    layout.connections.push({
      from: fromNode.id,
      fromSide: fromSide,
      fromOffset: fromOffset,
      to: id,
      toSide: toSide,
      toOffset: toOffset,
      span: span,
      oneWay: options.oneWay === true,
      players: options.players === undefined ? "both" : options.players,
      focus: options.focus === undefined ? "go" : options.focus
    });
    document.layout = layout;
    graphResult = validateRoomGraph(document);
    if (!graphResult.valid) throw new Error(graphResult.errors[0].message);
    return document;
  }

  function rebuildAutomaticRoomGraphConnections(document) {
    var layout = document.layout || {};
    var roomById = Object.create(null);
    var previousByPair = Object.create(null);
    var usedIds = Object.create(null);
    var nodes = Array.isArray(layout.nodes) ? layout.nodes : [];
    var connections = [];
    if (layout.kind !== ROOM_GRAPH_KIND) return document;
    roomList(document).forEach(function (room) { if (room && room.id) roomById[room.id] = room; });
    (layout.connections || []).forEach(function (connection) {
      var pair;
      if (!connection || !connection.from || !connection.to) return;
      pair = [connection.from, connection.to].sort().join("\u0000");
      if (!previousByPair[pair]) previousByPair[pair] = connection;
      if (typeof connection.id === "string") usedIds[connection.id] = true;
    });
    nodes.forEach(function (node) {
      var dimensions = node && roomById[node.room] && roomDimensions(roomById[node.room]);
      if (!node || !node.id || !dimensions || !dimensions.width || !dimensions.height) throw new Error("Every placed room must reference an existing room design.");
      if (!Number.isInteger(node.x) || !Number.isInteger(node.y) ||
          node.x < -ROOM_GRAPH_COORD_LIMIT || node.y < -ROOM_GRAPH_COORD_LIMIT ||
          node.x + dimensions.width > ROOM_GRAPH_COORD_LIMIT || node.y + dimensions.height > ROOM_GRAPH_COORD_LIMIT) {
        throw new Error("Room positions must use whole tile cells inside the supported map area.");
      }
      node._autoWidth = dimensions.width;
      node._autoHeight = dimensions.height;
    });
    for (var leftIndex = 0; leftIndex < nodes.length; leftIndex += 1) {
      var left = nodes[leftIndex];
      for (var rightIndex = leftIndex + 1; rightIndex < nodes.length; rightIndex += 1) {
        var right = nodes[rightIndex];
        var connection = null;
        var start;
        var end;
        var previous;
        if (left.x < right.x + right._autoWidth && left.x + left._autoWidth > right.x &&
            left.y < right.y + right._autoHeight && left.y + left._autoHeight > right.y) {
          throw new Error("Placed rooms cannot overlap.");
        }
        if (left.x + left._autoWidth === right.x || right.x + right._autoWidth === left.x) {
          start = Math.max(left.y, right.y);
          end = Math.min(left.y + left._autoHeight, right.y + right._autoHeight);
          if (end > start) connection = left.x + left._autoWidth === right.x ?
            { from: left.id, fromSide: "right", fromOffset: start - left.y, to: right.id, toSide: "left", toOffset: start - right.y, span: end - start, oneWay: false } :
            { from: left.id, fromSide: "left", fromOffset: start - left.y, to: right.id, toSide: "right", toOffset: start - right.y, span: end - start, oneWay: false };
        } else if (left.y + left._autoHeight === right.y || right.y + right._autoHeight === left.y) {
          start = Math.max(left.x, right.x);
          end = Math.min(left.x + left._autoWidth, right.x + right._autoWidth);
          if (end > start) connection = left.y + left._autoHeight === right.y ?
            { from: left.id, fromSide: "bottom", fromOffset: start - left.x, to: right.id, toSide: "top", toOffset: start - right.x, span: end - start, oneWay: false } :
            { from: left.id, fromSide: "top", fromOffset: start - left.x, to: right.id, toSide: "bottom", toOffset: start - right.x, span: end - start, oneWay: false };
        }
        if (!connection) continue;
        previous = previousByPair[[left.id, right.id].sort().join("\u0000")];
        if (previous) {
          if (typeof previous.id === "string") connection.id = previous.id;
          connection.players = previous.players === undefined ? "both" : previous.players;
          connection.focus = previous.focus === undefined ? "go" : previous.focus;
        } else {
          connection.players = "both";
          connection.focus = "go";
        }
        connections.push(connection);
      }
    }
    nodes.forEach(function (node) { delete node._autoWidth; delete node._autoHeight; });
    layout.connections = connections;
    roomGraphEnsureConnectionIdsInPlace(layout);
    document.layout = layout;
    return document;
  }

  function nearestRoomGraphNode(nodes, current, arrow) {
    var horizontal = arrow === "ArrowLeft" || arrow === "ArrowRight";
    var positive = arrow === "ArrowRight" || arrow === "ArrowDown";
    var x, y, best = null, bestScore = Infinity;
    if (!Array.isArray(nodes) || !current ||
        ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].indexOf(arrow) < 0)
      return null;
    x = current.x + current.width / 2;
    y = current.y + current.height / 2;
    nodes.forEach(function (candidate) {
      if (!candidate || candidate.id === current.id) return;
      var dx = candidate.x + candidate.width / 2 - x;
      var dy = candidate.y + candidate.height / 2 - y;
      var forward = horizontal ? dx : dy;
      var cross = horizontal ? dy : dx;
      var score;
      if (positive ? forward <= 0 : forward >= 0) return;
      score = forward * forward + cross * cross * 2;
      if (score < bestScore || (score === bestScore && best && candidate.id < best.id)) {
        best = candidate;
        bestScore = score;
      }
    });
    return best;
  }

  function moveRoomGraphNode(value, nodeId, x, y) {
    var document = deepClone(unwrapDocument(value) || {});
    var layout = document.layout || {};
    var moved;
    if (layout.kind !== ROOM_GRAPH_KIND) throw new Error("Moving placed rooms requires a room_graph layout.");
    if (!Number.isInteger(x) || !Number.isInteger(y)) throw new Error("Room positions must use whole tile cells.");
    (layout.nodes || []).forEach(function (node) { if (node && node.id === nodeId) moved = node; });
    if (!moved) throw new Error("Choose a placed room to move.");
    moved.x = x;
    moved.y = y;
    document.layout = layout;
    return rebuildAutomaticRoomGraphConnections(document);
  }

  function resizeRoomGraphRoom(value, roomId, width, height) {
    var document = deepClone(unwrapDocument(value) || {});
    var room = roomList(document).find(function (entry) { return entry && entry.id === roomId; });
    var layout = document.layout || {};
    var oldSize;
    var deltaWidth;
    var deltaHeight;
    var instances;
    if (!room) throw new Error("Choose an existing room design to resize.");
    oldSize = roomDimensions(room);
    resizeRoom(room, width, height);
    if (layout.kind !== ROOM_GRAPH_KIND) return document;
    deltaWidth = width - oldSize.width;
    deltaHeight = height - oldSize.height;
    instances = (layout.nodes || []).filter(function (node) { return node && node.room === roomId; });
    /* Rooms grow from their top-left graph anchor. Shift everything beyond the
     * old right/bottom edge by the same delta so an already arranged map stays
     * arranged instead of forbidding edits after placement. */
    instances.slice().sort(function (left, right) { return left.x - right.x; }).forEach(function (instance) {
      var oldRight = instance.x + oldSize.width;
      if (!deltaWidth) return;
      (layout.nodes || []).forEach(function (node) {
        if (node !== instance && node.x >= oldRight) node.x += deltaWidth;
      });
    });
    instances.slice().sort(function (top, bottom) { return top.y - bottom.y; }).forEach(function (instance) {
      var oldBottom = instance.y + oldSize.height;
      if (!deltaHeight) return;
      (layout.nodes || []).forEach(function (node) {
        if (node !== instance && node.y >= oldBottom) node.y += deltaHeight;
      });
    });
    return rebuildAutomaticRoomGraphConnections(document);
  }

  function setRoomGraphNodePresentation(value, nodeId, options) {
    var document = deepClone(unwrapDocument(value) || {});
    var layout = document.layout || {};
    var node;
    var result;
    options = options || {};
    if (layout.kind !== ROOM_GRAPH_KIND) throw new Error("Placed-room appearance requires a room_graph layout.");
    (layout.nodes || []).forEach(function (entry) { if (entry && entry.id === nodeId) node = entry; });
    if (!node) throw new Error("Choose a placed room to edit.");
    if (options.mirrorX !== undefined) {
      if (typeof options.mirrorX !== "boolean") throw new Error("Geometry mirroring must be true or false.");
      node.mirrorX = options.mirrorX;
      delete node.mirror_x;
    }
    if (options.appearance !== undefined) {
      if (options.appearance !== "primary" && options.appearance !== "mirror") throw new Error("Room colors must use the primary or mirror bank.");
      node.appearance = options.appearance;
    }
    document.layout = layout;
    result = validateRoomGraph(document);
    if (!result.valid) throw new Error(result.errors[0].message);
    return document;
  }

  function setRoomGraphNodeOverrides(value, nodeId, options) {
    var document = deepClone(unwrapDocument(value) || {});
    var layout = document.layout || {};
    var node;
    var result;
    var keys = ["ambient", "opponent_spawn", "native_tileset", "spawn"];
    options = options || {};
    if (layout.kind !== ROOM_GRAPH_KIND) throw new Error("Placed-room overrides require a room_graph layout.");
    (layout.nodes || []).forEach(function (entry) { if (entry && entry.id === nodeId) node = entry; });
    if (!node) throw new Error("Choose a placed room to edit.");
    if (!isPlainObject(node.overrides)) node.overrides = {};
    keys.forEach(function (key) {
      if (!hasOwn(options, key)) return;
      if (options[key] === undefined || options[key] === null || options[key] === "") delete node.overrides[key];
      else node.overrides[key] = options[key];
    });
    if (!Object.keys(node.overrides).length) delete node.overrides;
    document.layout = layout;
    result = validateRoomGraph(document);
    if (!result.valid) throw new Error(result.errors[0].message);
    return document;
  }

  function removeRoomGraphNode(value, nodeId) {
    var document = deepClone(unwrapDocument(value) || {});
    var layout = document.layout || {};
    var before;
    if (layout.kind !== ROOM_GRAPH_KIND) throw new Error("Removing placed rooms requires a room_graph layout.");
    roomGraphEnsureConnectionIdsInPlace(layout);
    if (!Array.isArray(layout.nodes)) throw new Error("The room layout has no placed rooms to remove.");
    before = layout.nodes.length;
    if (before <= 1 && layout.nodes.some(function (node) { return node && node.id === nodeId; })) {
      throw new Error("A room layout must keep at least one placed room.");
    }
    layout.nodes = layout.nodes.filter(function (node) { return node.id !== nodeId; });
    if (layout.nodes.length === before) throw new Error("Choose a placed room to remove.");
    layout.connections = (Array.isArray(layout.connections) ? layout.connections : []).filter(function (connection) {
      return connection && connection.from !== nodeId && connection.to !== nodeId;
    });
    if (layout.start === nodeId) layout.start = layout.nodes[0].id;
    document.layout = layout;
    return document;
  }

  function serializeRoomGraphLayout(value) {
    var document = unwrapDocument(value) || {};
    var result = validateRoomGraph(document);
    if (!result.valid) throw new Error("Cannot serialize invalid room_graph: " + result.errors[0].message);
    return {
      kind: ROOM_GRAPH_KIND,
      room_format: VARIABLE_ROOM_FORMAT,
      start: document.layout.start,
      nodes: result.nodes.map(function (node) {
        var output = { id: node.id, room: node.room, x: node.x, y: node.y, mirror_x: node.mirrorX, appearance: node.appearance };
        if (node.overrides && Object.keys(node.overrides).length) output.overrides = deepClone(node.overrides);
        return output;
      }),
      connections: result.connections.map(function (connection) {
        var output = {
          id: connection.id,
          from: connection.from,
          from_side: connection.fromSide,
          from_offset: connection.fromOffset,
          to: connection.to,
          to_side: connection.toSide,
          to_offset: connection.toOffset,
          span: connection.span,
          one_way: connection.oneWay
        };
        if (connection.players !== "both") output.players = connection.players;
        if (connection.focus !== "go") output.focus = connection.focus;
        return output;
      })
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
    var roomFormat = options && options.roomFormat === VARIABLE_ROOM_FORMAT ? VARIABLE_ROOM_FORMAT : ROOM_FORMAT;
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
      var expectedRows = roomFormat === VARIABLE_ROOM_FORMAT ? null : ROWS;
      if ((expectedRows !== null && room.grid.length !== expectedRows) ||
          (roomFormat === VARIABLE_ROOM_FORMAT && (room.grid.length < MIN_ROOM_ROWS || room.grid.length > MAX_ROOM_ROWS))) {
        errors.push(makeIssue("error", "room_row_count", roomFormat === VARIABLE_ROOM_FORMAT
          ? "Room \"" + room.id + "\" must contain " + MIN_ROOM_ROWS + "-" + MAX_ROOM_ROWS + " rows; found " + room.grid.length + "."
          : "Room \"" + room.id + "\" must contain exactly " + ROWS + " valid rows; found " + room.grid.length + ".", {
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
        var rowLimit = roomFormat === VARIABLE_ROOM_FORMAT ? MAX_ROOM_ROWS : ROWS;
        if (rowIndex >= rowLimit) {
          errors.push(makeIssue("error", "too_many_rows", "Room \"" + room.id + "\" has more than " + rowLimit + " valid rows.", {
            path: "data.map", roomId: room.id, row: rowIndex, line: lineNumber
          }));
          return;
        }
        var expectedCols = roomFormat === VARIABLE_ROOM_FORMAT && room.grid.length ? room.grid[0].length : COLS;
        var contentBytes = utf8Bytes(content).length;
        var variableWidthInvalid = roomFormat === VARIABLE_ROOM_FORMAT &&
          (contentBytes < MIN_ROOM_COLS || contentBytes > MAX_ROOM_COLS ||
           (room.grid.length && contentBytes !== expectedCols));
        if ((roomFormat === ROOM_FORMAT && contentBytes !== COLS) || variableWidthInvalid) {
          errors.push(makeIssue("error", "row_width", roomFormat === VARIABLE_ROOM_FORMAT
            ? "Room \"" + room.id + "\" rows must have one consistent width between " + MIN_ROOM_COLS + " and " + MAX_ROOM_COLS + " ASCII bytes; found " + contentBytes + "."
            : "Room \"" + room.id + "\" row text must contain exactly " + COLS + " ASCII bytes; found " + contentBytes + ".", {
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
    var roomFormat = document.layout && camelOrSnake(document.layout, "roomFormat", "room_format", ROOM_FORMAT);
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
      var dimensions = roomDimensions(room);
      if (!Array.isArray(grid) || (roomFormat === ROOM_FORMAT && grid.length !== ROWS) ||
          (roomFormat === VARIABLE_ROOM_FORMAT && (grid.length < MIN_ROOM_ROWS || grid.length > MAX_ROOM_ROWS))) {
        throw new Error("Room \"" + id + "\" has an invalid height.");
      }
      chunks.push("[" + id + "]");
      grid.forEach(function (row, rowIndex) {
        var text = rowString(row);
        var widthValid = roomFormat === VARIABLE_ROOM_FORMAT
          ? dimensions.width >= MIN_ROOM_COLS && dimensions.width <= MAX_ROOM_COLS && text.length === dimensions.width
          : text.length === COLS;
        if (!widthValid || utf8Bytes(text).length !== text.length) throw new Error("Room \"" + id + "\" row " + (rowIndex + 1) + " has an invalid width or non-ASCII glyph.");
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
    var graphLayout = layout.kind === ROOM_GRAPH_KIND;
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
      layout: graphLayout ? serializeRoomGraphLayout(document) : {
          kind: LAYOUT_KIND,
          room_format: camelOrSnake(layout, "roomFormat", "room_format", ROOM_FORMAT),
          order: rooms.map(function (room) { return room.id; })
        },
      defaults: {
        room: {
          ambient: defaults.ambient === undefined ? "none" : defaults.ambient
        }
      },
      rooms: {}
    };
    var eggnoggColor = camelOrSnake(rules, "eggnoggColor", "eggnogg_color", undefined);
    if (eggnoggColor !== undefined) data.rules.eggnogg_color = deepClone(eggnoggColor);
    var defaultAppearance = appearanceToJson(defaults.appearance, false);
    /* An empty appearance object is semantically meaningful to the loader's
     * primary/mirror copy rules, so preserve authored presence. */
    if (hasOwn(defaults, "appearance")) data.defaults.room.appearance = defaultAppearance;
    if (hasOwn(defaults, "opponent_spawn")) data.defaults.room.opponent_spawn = defaults.opponent_spawn;
    if (hasOwn(defaults, "native_tileset")) data.defaults.room.native_tileset = defaults.native_tileset;
    /* V1 ids are opaque loader strings, not display labels. Trimming an
     * imported id changes selector/online identity, so retain it verbatim. */
    var id = typeof document.id === "string" ? document.id : "";
    if (id) data.id = id;
    if (data.format === FORMAT_V2) {
      data.tileset = deepClone(document.tileset || { tiles: [] });
      data.particles = deepClone(Array.isArray(document.particles) ? document.particles : []);
      data.ambiances = deepClone(Array.isArray(document.ambiances) ? document.ambiances : []);
    }
    /* Preserve an explicit, valid room order when it describes exactly these rooms. */
    var requestedOrder = layout.order;
    if (!graphLayout && Array.isArray(requestedOrder) && requestedOrder.length === rooms.length) {
      var roomIds = rooms.map(function (room) { return room.id; }).sort();
      var orderIds = requestedOrder.slice().sort();
      if (roomIds.every(function (roomId, index) { return roomId === orderIds[index]; })) data.layout.order = requestedOrder.slice();
    }
    rooms.forEach(function (room) {
      var config = { ambient: room.ambient === undefined ? data.defaults.room.ambient : room.ambient };
      var appearance = appearanceToJson(room.appearance, false);
      if (hasOwn(room, "appearance")) config.appearance = appearance;
      if (room.hook === null) config.hook = null;
      if (hasOwn(room, "opponent_spawn")) config.opponent_spawn = room.opponent_spawn;
      if (hasOwn(room, "native_tileset")) config.native_tileset = room.native_tileset;
      if (hasOwn(room, "spawn")) config.spawn = deepClone(room.spawn);
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
    parsedMap = parseMapText(dataMapText, {
      format: format,
      customSymbols: collectCustomSymbols(json),
      roomFormat: isPlainObject(json.layout) ? json.layout.room_format : ROOM_FORMAT
    });
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
        eggnoggColor: isPlainObject(json.rules) ? json.rules.eggnogg_color : undefined,
        armedRespawnLimit: isPlainObject(json.rules) && json.rules.armed_respawn_limit !== undefined ? json.rules.armed_respawn_limit : 4
      },
      layout: isPlainObject(json.layout) && json.layout.kind === ROOM_GRAPH_KIND ? {
        kind: ROOM_GRAPH_KIND,
        roomFormat: json.layout.room_format,
        start: json.layout.start,
        nodes: Array.isArray(json.layout.nodes) ? json.layout.nodes.map(function (node) {
          return isPlainObject(node) ? {
            id: node.id, room: node.room, x: node.x, y: node.y,
            mirrorX: node.mirror_x === undefined ? false : node.mirror_x,
            appearance: node.appearance === undefined ? "primary" : node.appearance,
            overrides: isPlainObject(node.overrides) ? deepClone(node.overrides) : node.overrides
          } : node;
        }) : json.layout.nodes,
        connections: Array.isArray(json.layout.connections) ? json.layout.connections.map(function (connection) {
          return isPlainObject(connection) ? {
            id: connection.id, from: connection.from, fromSide: connection.from_side,
            fromOffset: connection.from_offset, to: connection.to,
            toSide: connection.to_side, toOffset: connection.to_offset,
            span: connection.span,
            oneWay: connection.one_way === undefined ? false : connection.one_way,
            players: connection.players === undefined ? "both" : connection.players,
            focus: connection.focus === undefined ? "go" : connection.focus
          } : connection;
        }) : json.layout.connections
      } : {
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
        if (hasOwn(config, "opponent_spawn")) entry.opponent_spawn = config.opponent_spawn;
        if (hasOwn(config, "native_tileset")) entry.native_tileset = config.native_tileset;
        if (hasOwn(config, "spawn")) entry.spawn = deepClone(config.spawn);
        return entry;
      })
    };
    if (hasOwn(defaultsRoom, "appearance")) document.defaults.appearance = deepClone(defaultsRoom.appearance);
    if (hasOwn(defaultsRoom, "opponent_spawn")) document.defaults.opponent_spawn = defaultsRoom.opponent_spawn;
    if (hasOwn(defaultsRoom, "native_tileset")) document.defaults.native_tileset = defaultsRoom.native_tileset;

    if (document.layout && document.layout.kind === ROOM_GRAPH_KIND) roomGraphEnsureConnectionIdsInPlace(document.layout);
    validateRawManifest(json, format, errors, warnings);

    if (format === FORMAT_V2) {
      document.tileset = deepClone(isPlainObject(json.tileset) ? json.tileset : { tiles: [] });
      document.particles = deepClone(Array.isArray(json.particles) ? json.particles : []);
      document.ambiances = deepClone(Array.isArray(json.ambiances) ? json.ambiances : []);
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

  function customAmbientIds(value) {
    var ids = Object.create(null);
    var list = value && Array.isArray(value.ambiances) ? value.ambiances : [];
    list.forEach(function (ambiance) {
      if (isPlainObject(ambiance) && typeof ambiance.id === "string") ids[ambiance.id] = true;
    });
    return ids;
  }

  function validateRawRoomConfig(config, path, errors, warnings, customAmbients) {
    if (!isPlainObject(config)) {
      pushSchemaError(errors, "room_config_type", path + " must be an object.", path);
      return;
    }
    warnUnknownKeys(config, ["ambient", "appearance", "hook", "opponent_spawn", "native_tileset", "spawn"], path, warnings);
    if (config.ambient !== undefined && !validAmbient(config.ambient, customAmbients)) pushSchemaError(errors, "ambient", path + ".ambient must be a built-in alias, declared custom ambiance id, or integer 0..9.", path + ".ambient");
    validateOpponentSpawn(config.opponent_spawn, path + ".opponent_spawn", errors);
    if (config.native_tileset !== undefined && config.native_tileset !== null && typeof config.native_tileset !== "string") {
      pushSchemaError(errors, "native_tileset_type", path + ".native_tileset must be a PNG sheet name, null, or an empty string for inheritance.", path + ".native_tileset");
    }
    validateRawAppearance(config.appearance, path + ".appearance", errors, warnings);
    if (config.hook !== undefined && config.hook !== null) pushSchemaError(errors, "unsupported_hook", path + ".hook must be omitted or null; map.lua is discovered at package level.", path + ".hook");
  }

  function validateRawManifest(json, format, errors, warnings) {
    var topKeys = ["format", "id", "name", "author", "description", "sort_order", "rules", "layout", "defaults", "rooms", "tileset", "particles", "ambiances"];
    var ambientIds = customAmbientIds(json);
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
        warnUnknownKeys(json.rules, ["mode", "round_end_rooms", "score_target", "armed_respawn_limit", "eggnogg_color"], "rules", warnings);
        validateEggnoggColor(json.rules.eggnogg_color, errors);
        if (json.rules.mode === undefined) pushSchemaError(errors, "rules_mode_required", "rules.mode is required when rules exists.", "rules.mode");
        else if (json.rules.mode !== "swords" && json.rules.mode !== "karate") pushSchemaError(errors, "rules_mode", "rules.mode must be swords or karate.", "rules.mode");
        if (json.rules.round_end_rooms !== undefined && json.rules.round_end_rooms !== "inner_only" && json.rules.round_end_rooms !== "any") pushSchemaError(errors, "round_end_rooms", "rules.round_end_rooms must be inner_only or any.", "rules.round_end_rooms");
        if (json.rules.score_target !== undefined && json.rules.score_target !== null && (!Number.isInteger(json.rules.score_target) || json.rules.score_target <= 0 || json.rules.score_target > 2147483647)) pushSchemaError(errors, "score_target", "rules.score_target must be null or a positive integer.", "rules.score_target");
        if (json.rules.armed_respawn_limit !== undefined && (!Number.isInteger(json.rules.armed_respawn_limit) || json.rules.armed_respawn_limit < 0 || json.rules.armed_respawn_limit > 2147483647)) pushSchemaError(errors, "armed_respawn_limit", "rules.armed_respawn_limit must be a non-negative integer.", "rules.armed_respawn_limit");
      }
    }
    if (!isPlainObject(json.layout)) pushSchemaError(errors, "layout_required", "layout is a required object.", "layout");
    else {
      if (json.layout.kind === ROOM_GRAPH_KIND) {
        warnUnknownKeys(json.layout, ["kind", "room_format", "start", "nodes", "connections"], "layout", warnings);
        if (format !== FORMAT_V2) pushSchemaError(errors, "room_graph_requires_v2", "room_graph requires eggnogg-map/v2.", "layout.kind");
        if (json.layout.room_format !== VARIABLE_ROOM_FORMAT) pushSchemaError(errors, "room_graph_format", "room_graph requires layout.room_format \"" + VARIABLE_ROOM_FORMAT + "\".", "layout.room_format");
        if (typeof json.layout.start !== "string" || !json.layout.start) pushSchemaError(errors, "room_graph_start", "room_graph requires a non-empty start node id.", "layout.start");
        if (!Array.isArray(json.layout.nodes) || !json.layout.nodes.length) pushSchemaError(errors, "room_graph_node_count", "room_graph requires at least one placed room.", "layout.nodes");
        else json.layout.nodes.forEach(function (node, index) {
          var path = "layout.nodes[" + index + "]";
          if (!isPlainObject(node)) return;
          warnUnknownKeys(node, ["id", "room", "x", "y", "mirror_x", "appearance", "overrides"], path, warnings);
          if (node.overrides !== undefined) {
            if (!isPlainObject(node.overrides)) pushSchemaError(errors, "room_graph_overrides", path + ".overrides must be an object.", path + ".overrides");
            else {
              warnUnknownKeys(node.overrides, ["ambient", "opponent_spawn", "native_tileset", "spawn"], path + ".overrides", warnings);
              if (node.overrides.ambient !== undefined && !validAmbient(node.overrides.ambient, ambientIds)) pushSchemaError(errors, "room_graph_ambient", path + ".overrides.ambient must be a built-in alias, declared custom ambiance id, or integer 0..9.", path + ".overrides.ambient");
              validateOpponentSpawn(node.overrides.opponent_spawn, path + ".overrides.opponent_spawn", errors);
              if (node.overrides.native_tileset !== undefined && typeof node.overrides.native_tileset !== "string") pushSchemaError(errors, "room_graph_native_tileset", path + ".overrides.native_tileset must name a PNG sheet.", path + ".overrides.native_tileset");
            }
          }
        });
        if (!Array.isArray(json.layout.connections)) pushSchemaError(errors, "room_graph_connection_count", "room_graph connections must be an array.", "layout.connections");
        else json.layout.connections.forEach(function (connection, index) {
          var path = "layout.connections[" + index + "]";
          if (!isPlainObject(connection)) return;
          warnUnknownKeys(connection, ["id", "from", "from_side", "from_offset", "to", "to_side", "to_offset", "span", "one_way", "players", "focus"], path, warnings);
          if (connection.players !== undefined && ["both", "player1", "player2", "go"].indexOf(connection.players) < 0) {
            pushSchemaError(errors, "room_graph_players", path + ".players must be both, player1, player2, or go.", path + ".players");
          }
          if (connection.focus !== undefined && connection.focus !== "go" && connection.focus !== "crossing") {
            pushSchemaError(errors, "room_graph_focus", path + ".focus must be go or crossing.", path + ".focus");
          }
        });
      } else {
        warnUnknownKeys(json.layout, ["kind", "room_format", "order"], "layout", warnings);
        if (json.layout.kind !== LAYOUT_KIND) pushSchemaError(errors, "layout_kind", "layout.kind must be \"" + LAYOUT_KIND + "\" or \"" + ROOM_GRAPH_KIND + "\".", "layout.kind");
        if (json.layout.room_format !== ROOM_FORMAT && json.layout.room_format !== VARIABLE_ROOM_FORMAT) pushSchemaError(errors, "room_format", "layout.room_format must be \"" + ROOM_FORMAT + "\" or \"" + VARIABLE_ROOM_FORMAT + "\".", "layout.room_format");
        if (!Array.isArray(json.layout.order) || !json.layout.order.length) pushSchemaError(errors, "layout_order", "layout.order must be a non-empty array.", "layout.order");
      }
    }
    if (json.defaults !== undefined) {
      if (!isPlainObject(json.defaults)) pushSchemaError(errors, "defaults_type", "defaults must be an object.", "defaults");
      else {
        warnUnknownKeys(json.defaults, ["room"], "defaults", warnings);
        if (json.defaults.room !== undefined) validateRawRoomConfig(json.defaults.room, "defaults.room", errors, warnings, ambientIds);
      }
    }
    if (json.rooms !== undefined) {
      if (!isPlainObject(json.rooms)) pushSchemaError(errors, "rooms_type", "rooms must be an object.", "rooms");
      else Object.keys(json.rooms).forEach(function (id) { validateRawRoomConfig(json.rooms[id], "rooms." + id, errors, warnings, ambientIds); });
    }
    if (format !== FORMAT_V2 && (json.particles !== undefined || json.ambiances !== undefined)) {
      pushSchemaError(errors, "ambiance_requires_v2", "particles and ambiances require eggnogg-map/v2.", "particles");
    }
    if (format !== FORMAT_V2) {
      var defaultRoom = isPlainObject(json.defaults) && isPlainObject(json.defaults.room) ? json.defaults.room : null;
      var roomConfigs = isPlainObject(json.rooms) ? json.rooms : {};
      if (defaultRoom && hasOwn(defaultRoom, "native_tileset")) pushSchemaError(errors, "native_tileset_requires_v2", "Per-room native tilesets require eggnogg-map/v2.", "defaults.room.native_tileset");
      Object.keys(roomConfigs).forEach(function (id) {
        if (isPlainObject(roomConfigs[id]) && hasOwn(roomConfigs[id], "native_tileset")) pushSchemaError(errors, "native_tileset_requires_v2", "Per-room native tilesets require eggnogg-map/v2.", "rooms." + id + ".native_tileset");
      });
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

  function ambianceSheetCatalog(json, assets) {
    var tileset = isPlainObject(json.tileset) ? json.tileset : {};
    var lookup = Object.create(null);
    var entries = [];
    Object.keys(assets || {}).forEach(function (name) { lookup[name.toLowerCase()] = assets[name]; });

    function integerOr(value, fallback, low, high) {
      return Number.isInteger(value) && value >= low && value <= high ? value : fallback;
    }
    function add(sheet, cellW, cellH, padding, sha) {
      if (typeof sheet !== "string" || !sheet || sheet.slice(0, 8) === "builtin:" ||
          sheet === "." || sheet === ".." || /[\\\/:]/.test(sheet) || !/\.png$/i.test(sheet)) return;
      var asset = lookup[sheet.toLowerCase()];
      var dimensions = pngInfo(asset);
      if (!dimensions || dimensions.width > 4096 || dimensions.height > 4096 ||
          dimensions.width < cellW || dimensions.height < cellH ||
          (dimensions.width + padding) % (cellW + padding) !== 0 ||
          (dimensions.height + padding) % (cellH + padding) !== 0) return;
      var key = sheet.toLowerCase() + "|" + String(sha || "").toLowerCase() + "|" + cellW + "|" + cellH + "|" + padding;
      if (entries.some(function (entry) { return entry.key === key; })) return;
      entries.push({
        key: key,
        name: sheet,
        cells: ((dimensions.width + padding) / (cellW + padding)) * ((dimensions.height + padding) / (cellH + padding))
      });
    }

    var defaultW = integerOr(tileset.cell_w, 16, 1, 512);
    var defaultH = integerOr(tileset.cell_h, 16, 1, 512);
    var defaultPadding = integerOr(tileset.padding, 0, 0, 64);
    add(tileset.sprite_sheet, defaultW, defaultH, defaultPadding, tileset.asset_sha256);
    if (Array.isArray(tileset.sheets)) tileset.sheets.slice(0, 16).forEach(function (sheet) {
      if (!isPlainObject(sheet)) return;
      add(sheet.sprite_sheet,
        integerOr(sheet.cell_w, 16, 1, 512),
        integerOr(sheet.cell_h, 16, 1, 512),
        integerOr(sheet.padding, 0, 0, 64),
        sheet.asset_sha256);
    });
    if (Array.isArray(tileset.tiles)) tileset.tiles.slice(0, 64).forEach(function (tile) {
      if (!isPlainObject(tile)) return;
      var sheet = hasOwn(tile, "sprite_sheet") ? tile.sprite_sheet : tileset.sprite_sheet;
      var sameDefault = typeof sheet === "string" && typeof tileset.sprite_sheet === "string" && sheet.toLowerCase() === tileset.sprite_sheet.toLowerCase();
      var sha = hasOwn(tile, "asset_sha256") ? tile.asset_sha256 : ((!hasOwn(tile, "sprite_sheet") || sameDefault) ? tileset.asset_sha256 : undefined);
      add(sheet,
        integerOr(tile.cell_w, defaultW, 1, 512),
        integerOr(tile.cell_h, defaultH, 1, 512),
        integerOr(tile.padding, defaultPadding, 0, 64),
        sha);
    });
    return entries;
  }

  function validateAmbianceCatalog(json, assets, errors) {
    var particleList = json.particles;
    var ambianceList = json.ambiances;
    if (particleList === undefined && ambianceList === undefined) return;
    if (!Array.isArray(particleList) || particleList.length > 64) {
      pushSchemaError(errors, "particles_array", "particles must be an array with at most 64 definitions.", "particles");
      particleList = [];
    }
    if (!Array.isArray(ambianceList) || ambianceList.length > 32) {
      pushSchemaError(errors, "ambiances_array", "ambiances must be an array with at most 32 definitions.", "ambiances");
      ambianceList = [];
    }
    var sheets = ambianceSheetCatalog(json, assets);
    var particleIds = Object.create(null);
    var ambianceIds = Object.create(null);
    var laneCount = 0;
    var idPattern = /^[a-z][a-z0-9_.-]{0,31}$/;
    var builtins = AMBIENTS.concat(["fumes"]);
    var builtinSheetCounts = {
      "builtin:tiles": 128,
      "builtin:sprites": 128,
      "builtin:misc": 64,
      "builtin:glyphs": 256
    };

    function requiredString(object, key, path, maxBytes, code) {
      var value = object[key];
      if (typeof value !== "string" || !value || value.indexOf("\0") >= 0 || utf8Bytes(value).length > maxBytes) {
        pushSchemaError(errors, code, path + "." + key + " must be a non-empty string of at most " + maxBytes + " UTF-8 bytes.", path + "." + key);
        return null;
      }
      return value;
    }
    function range(value, path) {
      if (value === undefined) return;
      if (!isPlainObject(value)) {
        pushSchemaError(errors, "ambiance_range", path + " must be an object with min and max numbers.", path);
        return;
      }
      rejectUnknownKeys(value, ["min", "max"], path, errors);
      var minimum = validateNumberField(value, "min", path, -256, 256, 0, errors, false);
      var maximum = validateNumberField(value, "max", path, -256, 256, 0, errors, false);
      if (minimum > maximum) pushSchemaError(errors, "ambiance_range_order", path + ".min must not exceed .max.", path);
    }

    particleList.slice(0, 64).forEach(function (particle, index) {
      var path = "particles[" + index + "]";
      if (!isPlainObject(particle)) {
        pushSchemaError(errors, "particle_object", path + " must be an object.", path);
        return;
      }
      rejectUnknownKeys(particle, ["id", "name", "visual", "lifetime_ticks", "fade_in_ticks", "fade_out_ticks"], path, errors);
      var id = requiredString(particle, "id", path, 32, "particle_id");
      if (id && !idPattern.test(id)) pushSchemaError(errors, "particle_id", path + ".id must start with a lowercase letter and use only lowercase letters, numbers, '.', '_' or '-'.", path + ".id");
      if (id) {
        if (particleIds[id]) pushSchemaError(errors, "duplicate_particle_id", "Duplicate particle id \"" + id + "\".", path + ".id");
        particleIds[id] = true;
      }
      requiredString(particle, "name", path, 64, "particle_name");
      var lifetime = validateIntegerField(particle, "lifetime_ticks", path, 1, 360000, 120, errors);
      var fadeIn = validateIntegerField(particle, "fade_in_ticks", path, 0, 360000, 0, errors);
      var fadeOut = validateIntegerField(particle, "fade_out_ticks", path, 0, 360000, 0, errors);
      if (fadeIn > lifetime) pushSchemaError(errors, "particle_fade", path + ".fade_in_ticks must not exceed lifetime_ticks.", path + ".fade_in_ticks");
      if (fadeOut > lifetime) pushSchemaError(errors, "particle_fade", path + ".fade_out_ticks must not exceed lifetime_ticks.", path + ".fade_out_ticks");
      var visual = particle.visual;
      if (!isPlainObject(visual)) {
        pushSchemaError(errors, "particle_visual", path + ".visual must be an object.", path + ".visual");
        return;
      }
      rejectUnknownKeys(visual, ["sprite_sheet", "sprite_index", "frame_count", "frame_ticks", "tint", "end_tint", "scale_x", "scale_y", "end_scale_x", "end_scale_y", "start_rotation", "end_rotation", "interpolation"], path + ".visual", errors);
      var sheet = requiredString(visual, "sprite_sheet", path + ".visual", 127, "particle_sheet");
      var spriteIndex = validateIntegerField(visual, "sprite_index", path + ".visual", 0, 1000000, 0, errors);
      var frameCount = validateIntegerField(visual, "frame_count", path + ".visual", 1, 256, 1, errors);
      validateIntegerField(visual, "frame_ticks", path + ".visual", 1, 3600, 1, errors);
      validateNumberField(visual, "scale_x", path + ".visual", -64, 64, 1, errors, true);
      validateNumberField(visual, "scale_y", path + ".visual", -64, 64, 1, errors, true);
      validateNumberField(visual, "end_scale_x", path + ".visual", -64, 64, visual.scale_x === undefined ? 1 : visual.scale_x, errors, false);
      validateNumberField(visual, "end_scale_y", path + ".visual", -64, 64, visual.scale_y === undefined ? 1 : visual.scale_y, errors, false);
      validateNumberField(visual, "start_rotation", path + ".visual", -3600, 3600, 0, errors, false);
      validateNumberField(visual, "end_rotation", path + ".visual", -3600, 3600, visual.start_rotation === undefined ? 0 : visual.start_rotation, errors, false);
      if (visual.interpolation !== undefined && ["linear", "ease_in", "ease_out", "ease_in_out"].indexOf(visual.interpolation) < 0) pushSchemaError(errors, "particle_interpolation", path + ".visual.interpolation must be linear, ease_in, ease_out, or ease_in_out.", path + ".visual.interpolation");
      if (visual.tint !== undefined && (!Array.isArray(visual.tint) || visual.tint.length !== 4 || !visual.tint.every(function (channel) {
        return typeof channel === "number" && Number.isFinite(channel) && channel >= 0 && channel <= 1;
      }))) pushSchemaError(errors, "particle_tint", path + ".visual.tint must contain four finite channels from 0 to 1.", path + ".visual.tint");
      if (visual.end_tint !== undefined && (!Array.isArray(visual.end_tint) || visual.end_tint.length !== 4 || !visual.end_tint.every(function (channel) {
        return typeof channel === "number" && Number.isFinite(channel) && channel >= 0 && channel <= 1;
      }))) pushSchemaError(errors, "particle_end_tint", path + ".visual.end_tint must contain four finite channels from 0 to 1.", path + ".visual.end_tint");
      if (sheet) {
        if (sheet.slice(0, 8) === "builtin:") {
          if (!hasOwn(builtinSheetCounts, sheet)) pushSchemaError(errors, "particle_builtin_sheet", "Unknown built-in particle sheet \"" + sheet + "\".", path + ".visual.sprite_sheet");
          else if (spriteIndex + frameCount > builtinSheetCounts[sheet]) pushSchemaError(errors, "particle_sprite_range", path + ".visual animation exceeds the built-in sheet's " + builtinSheetCounts[sheet] + " sprites.", path + ".visual.sprite_index");
        } else {
          var matches = sheets.filter(function (entry) { return entry.name === sheet; });
          if (matches.length !== 1) pushSchemaError(errors, "particle_sheet_declaration", path + ".visual.sprite_sheet must identify exactly one resolved external tileset sheet.", path + ".visual.sprite_sheet");
          else if (spriteIndex + frameCount > matches[0].cells) pushSchemaError(errors, "particle_sprite_range", path + ".visual animation exceeds the declared sheet's " + matches[0].cells + " cells.", path + ".visual.sprite_index");
        }
      }
    });

    ambianceList.slice(0, 32).forEach(function (ambiance, index) {
      var path = "ambiances[" + index + "]";
      if (!isPlainObject(ambiance)) {
        pushSchemaError(errors, "ambiance_object", path + " must be an object.", path);
        return;
      }
      rejectUnknownKeys(ambiance, ["id", "name", "native_ambient", "emitters"], path, errors);
      var id = requiredString(ambiance, "id", path, 32, "ambiance_id");
      if (id && !idPattern.test(id)) pushSchemaError(errors, "ambiance_id", path + ".id must start with a lowercase letter and use only lowercase letters, numbers, '.', '_' or '-'.", path + ".id");
      if (id && builtins.indexOf(id) >= 0) pushSchemaError(errors, "ambiance_builtin_collision", path + ".id collides with a built-in ambiance name.", path + ".id");
      if (id) {
        if (ambianceIds[id]) pushSchemaError(errors, "duplicate_ambiance_id", "Duplicate ambiance id \"" + id + "\".", path + ".id");
        ambianceIds[id] = true;
      }
      requiredString(ambiance, "name", path, 64, "ambiance_name");
      if (ambiance.native_ambient !== undefined && !validAmbient(ambiance.native_ambient)) pushSchemaError(errors, "native_ambient", path + ".native_ambient must be a built-in alias or integer 0..9.", path + ".native_ambient");
      if (!Array.isArray(ambiance.emitters) || ambiance.emitters.length > 16) {
        pushSchemaError(errors, "emitters_array", path + ".emitters must be an array with at most 16 entries.", path + ".emitters");
        return;
      }
      ambiance.emitters.slice(0, 16).forEach(function (emitter, emitterIndex) {
        var emitterPath = path + ".emitters[" + emitterIndex + "]";
        if (!isPlainObject(emitter)) {
          pushSchemaError(errors, "emitter_object", emitterPath + " must be an object.", emitterPath);
          return;
        }
        rejectUnknownKeys(emitter, ["particle", "count", "area", "velocity_x", "velocity_y", "acceleration_x", "acceleration_y", "rotation_speed", "particle_layer", "blend", "mirror_with_room", "shape", "motion_interpolation"], emitterPath, errors);
        if (typeof emitter.particle !== "string" || !particleIds[emitter.particle]) pushSchemaError(errors, "emitter_particle", emitterPath + ".particle must reference a declared particle id.", emitterPath + ".particle");
        var count = validateIntegerField(emitter, "count", emitterPath, 1, 512, 1, errors);
        laneCount += count;
        validateIntegerField(emitter, "particle_layer", emitterPath, 0, 4, 2, errors);
        validateBooleanField(emitter, "mirror_with_room", emitterPath, errors);
        validateNumberField(emitter, "acceleration_x", emitterPath, -64, 64, 0, errors, false);
        validateNumberField(emitter, "acceleration_y", emitterPath, -64, 64, 0, errors, false);
        if (emitter.blend !== undefined && emitter.blend !== "alpha" && emitter.blend !== "additive") pushSchemaError(errors, "emitter_blend", emitterPath + ".blend must be alpha or additive.", emitterPath + ".blend");
        if (emitter.shape !== undefined && ["rectangle", "ellipse", "line"].indexOf(emitter.shape) < 0) pushSchemaError(errors, "emitter_shape", emitterPath + ".shape must be rectangle, ellipse, or line.", emitterPath + ".shape");
        if (emitter.motion_interpolation !== undefined && ["linear", "ease_in", "ease_out", "ease_in_out"].indexOf(emitter.motion_interpolation) < 0) pushSchemaError(errors, "emitter_motion_interpolation", emitterPath + ".motion_interpolation must be linear, ease_in, ease_out, or ease_in_out.", emitterPath + ".motion_interpolation");
        if (!isPlainObject(emitter.area)) pushSchemaError(errors, "emitter_area", emitterPath + ".area must be an object.", emitterPath + ".area");
        else {
          rejectUnknownKeys(emitter.area, ["x", "y", "width", "height"], emitterPath + ".area", errors);
          validateNumberField(emitter.area, "x", emitterPath + ".area", -4096, 4096, 0, errors, false);
          validateNumberField(emitter.area, "y", emitterPath + ".area", -4096, 4096, 0, errors, false);
          validateNumberField(emitter.area, "width", emitterPath + ".area", 0, 8192, 528, errors, false);
          validateNumberField(emitter.area, "height", emitterPath + ".area", 0, 8192, 192, errors, false);
        }
        range(emitter.velocity_x, emitterPath + ".velocity_x");
        range(emitter.velocity_y, emitterPath + ".velocity_y");
        range(emitter.rotation_speed, emitterPath + ".rotation_speed");
      });
    });
    if (laneCount > 512) pushSchemaError(errors, "ambiance_lane_count", "Custom ambiances declare " + laneCount + " particle lanes; the package limit is 512.", "ambiances");
  }

  function validateV2Manifest(json, assets, errors, warnings) {
    validateAmbianceCatalog(json, assets, errors);
    var tileset = json.tileset;
    var topKeys = ["sprite_sheet", "asset_sha256", "cell_w", "cell_h", "padding", "source_x", "source_y", "source_w", "source_h", "native_layout", "tiles", "sheets"];
    var tileKeys = ["id", "symbol", "name", "native_glyph", "sprite_sheet", "asset_sha256", "sprite_index", "frame_count", "frame_ticks", "animation", "layer", "mirror_with_room", "random_phase", "native_visual", "offset_x", "offset_y", "scale_x", "scale_y", "angle_degrees", "tint", "cell_w", "cell_h", "padding", "source_x", "source_y", "source_w", "source_h", "collision", "force_mode", "force_x", "force_y", "max_speed_x", "max_speed_y"];
    var safeNative = "!#()*+-12:=@ACEFHIOPQSWXZ^_`cefilmquvwx|";
    var assetLookup = Object.create(null);
    var sheetConfigs = Object.create(null);
    var totalKnownCells = 0;
    var seenSymbols = Object.create(null);
    var seenIds = Object.create(null);
    var nativeSheetDeclarations = Object.create(null);

    Object.keys(assets || {}).forEach(function (name) { assetLookup[name.toLowerCase()] = assets[name]; });
    if (tileset === undefined) {
      var missingDefaults = isPlainObject(json.defaults) && isPlainObject(json.defaults.room) ? json.defaults.room : {};
      var missingRooms = isPlainObject(json.rooms) ? json.rooms : {};
      if (typeof missingDefaults.native_tileset === "string" && missingDefaults.native_tileset) pushSchemaError(errors, "native_tileset_declaration", "defaults.room.native_tileset requires an external tileset declaration.", "defaults.room.native_tileset");
      Object.keys(missingRooms).forEach(function (roomId) {
        if (isPlainObject(missingRooms[roomId]) && typeof missingRooms[roomId].native_tileset === "string" && missingRooms[roomId].native_tileset) pushSchemaError(errors, "native_tileset_declaration", "rooms." + roomId + ".native_tileset requires an external tileset declaration.", "rooms." + roomId + ".native_tileset");
      });
      return;
    }
    if (!isPlainObject(tileset)) {
      pushSchemaError(errors, "tileset_type", "tileset must be an object.", "tileset");
      return;
    }
    rejectUnknownKeys(tileset, topKeys, "tileset", errors);
    var topCellW = validateIntegerField(tileset, "cell_w", "tileset", 1, 512, 16, errors);
    var topCellH = validateIntegerField(tileset, "cell_h", "tileset", 1, 512, 16, errors);
    var topPadding = validateIntegerField(tileset, "padding", "tileset", 0, 64, 0, errors);
    var topSourceX = validateIntegerField(tileset, "source_x", "tileset", 0, 8191, 0, errors);
    var topSourceY = validateIntegerField(tileset, "source_y", "tileset", 0, 8191, 0, errors);
    var topSourceW = validateIntegerField(tileset, "source_w", "tileset", 0, 8192, 0, errors);
    var topSourceH = validateIntegerField(tileset, "source_h", "tileset", 0, 8192, 0, errors);
    validateBooleanField(tileset, "native_layout", "tileset", errors);

    function validateHash(value, path) {
      if (value === undefined) return;
      if (typeof value !== "string" || !/^[0-9a-fA-F]{64}$/.test(value)) pushSchemaError(errors, "asset_sha256", path + " must be a non-empty 64-digit hexadecimal SHA-256 string when present.", path);
    }

    function resolveSheet(sheet, sha, cellW, cellH, padding, sourceX, sourceY, sourceW, sourceH, geometryAuthored, path) {
      var info = { external: false, known: false, cells: 0, sheet: sheet };
      validateHash(sha, path + ".asset_sha256");
      if (typeof sheet !== "string" || !sheet) {
        pushSchemaError(errors, "sprite_sheet_required", path + ".sprite_sheet must resolve to a non-empty string.", path + ".sprite_sheet");
        return info;
      }
      if (sheet.slice(0, 8).toLowerCase() === "builtin:") {
        if (geometryAuthored) pushSchemaError(errors, "builtin_geometry", "Cell and source rectangle fields only apply to external PNG sheets.", path);
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
      if (dimensions.width > 8192 || dimensions.height > 8192) {
        pushSchemaError(errors, "png_dimensions", "PNG \"" + sheet + "\" exceeds 8192x8192.", path + ".sprite_sheet");
        return info;
      }
      var regionW = sourceW > 0 ? sourceW : dimensions.width - sourceX;
      var regionH = sourceH > 0 ? sourceH : dimensions.height - sourceY;
      if (sourceX >= dimensions.width || sourceY >= dimensions.height || regionW < cellW || regionH < cellH || regionW > dimensions.width - sourceX || regionH > dimensions.height - sourceY || (regionW + padding) % (cellW + padding) !== 0 || (regionH + padding) % (cellH + padding) !== 0) {
        pushSchemaError(errors, "png_grid", "Source rectangle " + sourceX + "," + sourceY + " " + regionW + "x" + regionH + " in PNG \"" + sheet + "\" does not fit or form a whole " + cellW + "x" + cellH + " grid with padding " + padding + ".", path);
        return info;
      }
      info.cells = ((regionW + padding) / (cellW + padding)) * ((regionH + padding) / (cellH + padding));
      info.known = true;
      if (info.cells > 8192) pushSchemaError(errors, "sheet_cell_cap", "PNG \"" + sheet + "\" defines " + info.cells + " cells; one sheet is limited to 8192.", path + ".sprite_sheet");
      var configKey = sheet.toLowerCase() + "|" + String(sha || "").toLowerCase() + "|" + cellW + "|" + cellH + "|" + padding + "|" + sourceX + "|" + sourceY + "|" + regionW + "|" + regionH;
      if (!sheetConfigs[configKey]) {
        sheetConfigs[configKey] = true;
        totalKnownCells += info.cells;
      }
      return info;
    }

    function registerNativeSheet(info, declaration, path) {
      if (!info || !info.external || typeof info.sheet !== "string") return;
      if (nativeSheetDeclarations[info.sheet]) {
        pushSchemaError(errors, "duplicate_native_sheet", "Native tileset sheet \"" + info.sheet + "\" is declared more than once; keep one geometry declaration.", path + ".sprite_sheet");
        return;
      }
      nativeSheetDeclarations[info.sheet] = {
        info: info,
        cell_w: declaration.cell_w === undefined ? 16 : declaration.cell_w,
        cell_h: declaration.cell_h === undefined ? 16 : declaration.cell_h,
        padding: declaration.padding === undefined ? 0 : declaration.padding
      };
    }

    function validateNativeSelection(value, path) {
      var declaration;
      if (value === undefined || value === null || value === "") return;
      if (typeof value !== "string") return;
      declaration = nativeSheetDeclarations[value];
      if (!declaration) {
        pushSchemaError(errors, "native_tileset_declaration", path + " must name the top-level external sprite sheet or an entry in tileset.sheets.", path);
        return;
      }
      if (declaration.info.known && declaration.info.cells < 128) {
        pushSchemaError(errors, "native_tileset_cells", path + " selects \"" + value + "\", which has " + declaration.info.cells + " cells; a native-layout sheet needs at least 128.", path);
      }
    }

    if (hasOwn(tileset, "sprite_sheet") && (typeof tileset.sprite_sheet !== "string" || !tileset.sprite_sheet)) pushSchemaError(errors, "default_sheet", "tileset.sprite_sheet must be a non-empty string when present.", "tileset.sprite_sheet");
    if (hasOwn(tileset, "asset_sha256") && !hasOwn(tileset, "sprite_sheet")) pushSchemaError(errors, "orphan_default_hash", "tileset.asset_sha256 requires tileset.sprite_sheet.", "tileset.asset_sha256");
    var topGeometryAuthored = hasOwn(tileset, "cell_w") || hasOwn(tileset, "cell_h") || hasOwn(tileset, "padding") || hasOwn(tileset, "source_x") || hasOwn(tileset, "source_y") || hasOwn(tileset, "source_w") || hasOwn(tileset, "source_h");
    var defaultSheetInfo = null;
    if (typeof tileset.sprite_sheet === "string" && tileset.sprite_sheet) {
      defaultSheetInfo = resolveSheet(tileset.sprite_sheet, tileset.asset_sha256, topCellW, topCellH, topPadding, topSourceX, topSourceY, topSourceW, topSourceH, topGeometryAuthored, "tileset");
      registerNativeSheet(defaultSheetInfo, { cell_w: topCellW, cell_h: topCellH, padding: topPadding }, "tileset");
    }
    if (tileset.sheets !== undefined) {
      if (!Array.isArray(tileset.sheets) || tileset.sheets.length > 16) pushSchemaError(errors, "sheets_array", "tileset.sheets must contain at most 16 declarations.", "tileset.sheets");
      else tileset.sheets.forEach(function (entry, index) {
        var path = "tileset.sheets[" + index + "]";
        if (!isPlainObject(entry)) { pushSchemaError(errors, "sheet_object", "Expected a sheet object.", path); return; }
        rejectUnknownKeys(entry, ["sprite_sheet", "asset_sha256", "cell_w", "cell_h", "padding", "source_x", "source_y", "source_w", "source_h"], path, errors);
        if (typeof entry.sprite_sheet !== "string" || !/^[A-Za-z0-9_-][A-Za-z0-9_.-]*\.png$/i.test(entry.sprite_sheet)) pushSchemaError(errors, "sheet_png", "Expected a direct PNG filename.", path);
        var w = validateIntegerField(entry, "cell_w", path, 1, 512, 16, errors);
        var h = validateIntegerField(entry, "cell_h", path, 1, 512, 16, errors);
        var padding = validateIntegerField(entry, "padding", path, 0, 64, 0, errors);
        var sourceX = validateIntegerField(entry, "source_x", path, 0, 8191, 0, errors);
        var sourceY = validateIntegerField(entry, "source_y", path, 0, 8191, 0, errors);
        var sourceW = validateIntegerField(entry, "source_w", path, 0, 8192, 0, errors);
        var sourceH = validateIntegerField(entry, "source_h", path, 0, 8192, 0, errors);
        var declaredInfo = resolveSheet(entry.sprite_sheet, entry.asset_sha256, w, h, padding, sourceX, sourceY, sourceW, sourceH, true, path);
        registerNativeSheet(declaredInfo, { cell_w: w, cell_h: h, padding: padding }, path);
      });
    }
    if (tileset.native_layout === true) {
      if (!defaultSheetInfo || !defaultSheetInfo.external) pushSchemaError(errors, "native_layout_sheet", "tileset.native_layout requires an external default PNG sheet.", "tileset.native_layout");
      else if (defaultSheetInfo.known && defaultSheetInfo.cells < 128) pushSchemaError(errors, "native_layout_cells", "tileset.native_layout requires at least 128 cells in the default sheet.", "tileset.native_layout");
    }
    var defaultsRoom = isPlainObject(json.defaults) && isPlainObject(json.defaults.room) ? json.defaults.room : {};
    validateNativeSelection(defaultsRoom.native_tileset, "defaults.room.native_tileset");
    if (isPlainObject(json.rooms)) Object.keys(json.rooms).forEach(function (roomId) {
      if (isPlainObject(json.rooms[roomId])) validateNativeSelection(json.rooms[roomId].native_tileset, "rooms." + roomId + ".native_tileset");
    });
    if (isPlainObject(json.layout) && json.layout.kind === ROOM_GRAPH_KIND && Array.isArray(json.layout.nodes)) {
      json.layout.nodes.forEach(function (node, index) {
        if (isPlainObject(node) && isPlainObject(node.overrides)) validateNativeSelection(node.overrides.native_tileset, "layout.nodes[" + index + "].overrides.native_tileset");
      });
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
      var sourceX = validateIntegerField(tile, "source_x", path, 0, 8191, topSourceX, errors);
      var sourceY = validateIntegerField(tile, "source_y", path, 0, 8191, topSourceY, errors);
      var sourceW = validateIntegerField(tile, "source_w", path, 0, 8192, topSourceW, errors);
      var sourceH = validateIntegerField(tile, "source_h", path, 0, 8192, topSourceH, errors);
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
      var geometryAuthored = hasOwn(tile, "cell_w") || hasOwn(tile, "cell_h") || hasOwn(tile, "padding") || hasOwn(tile, "source_x") || hasOwn(tile, "source_y") || hasOwn(tile, "source_w") || hasOwn(tile, "source_h") || (!hasOwn(tile, "sprite_sheet") && topGeometryAuthored);
      var sheetInfo = resolveSheet(sheet, sha, cellW, cellH, padding, sourceX, sourceY, sourceW, sourceH, geometryAuthored, path);
      if (sheetInfo.known && (indexValue >= sheetInfo.cells || frameCount > sheetInfo.cells - indexValue)) pushSchemaError(errors, "sprite_range", path + " sprite_index/frame_count exceeds its sheet's " + sheetInfo.cells + " cells.", path + ".sprite_index");
    });
    if (Object.keys(sheetConfigs).length > 16) pushSchemaError(errors, "sheet_config_count", "The tileset uses more than 16 unique external sheet configurations.", "tileset");
    if (totalKnownCells > 8192) warnings.push(makeIssue("warning", "aggregate_atlas_pressure", "External sheet configurations total " + totalKnownCells + " cells. Parsing can succeed, but the runtime atlas may be unable to append all of them.", { path: "tileset" }));
  }

  function validateOpponentSpawn(value, path, errors) {
    if (value !== undefined && value !== "default" && value !== "always" && value !== "never") {
      errors.push(makeIssue("error", "opponent_spawn", path + " must be default, always, or never.", { path: path }));
    }
  }

  function validAmbient(value, customAmbients) {
    return (typeof value === "string" && (AMBIENTS.indexOf(value) >= 0 || value === "fumes")) ||
      (typeof value === "string" && customAmbients && customAmbients[value] === true) ||
      (Number.isInteger(value) && value >= 0 && value <= 9);
  }

  function validateEggnoggColor(value, errors) {
    if (value === undefined) return;
    if (!Array.isArray(value) || value.length !== 3 || ![0, 1, 2].every(function (index) { var channel = value[index]; return typeof channel === "number" && Number.isFinite(channel) && channel >= 0 && channel <= 1; })) {
      errors.push(makeIssue("error", "eggnogg_color", "rules.eggnogg_color must contain exactly three finite RGB channels from 0 to 1.", { path: "rules.eggnogg_color" }));
    }
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

  function validateSpawnOverlay(spawn, path, width, height, errors, roomId,
                                room, tileset) {
    if (spawn === undefined) return;
    if (!isPlainObject(spawn)) {
      errors.push(makeIssue("error", "spawn_type", path + " must be an object.", { path: path, roomId: roomId }));
      return;
    }
    if (spawn.players !== undefined && !isPlainObject(spawn.players)) {
      errors.push(makeIssue("error", "spawn_players_type", path + ".players must be an object.", { path: path + ".players", roomId: roomId }));
    } else if (isPlainObject(spawn.players)) {
      Object.keys(spawn.players).forEach(function (player) {
        var point = spawn.players[player];
        var pointPath = path + ".players." + player;
        if (player !== "1" && player !== "2") errors.push(makeIssue("error", "spawn_player", "Spawn player must be 1 or 2.", { path: pointPath, roomId: roomId }));
        if (!isPlainObject(point) || !Number.isInteger(point.x) || !Number.isInteger(point.y) ||
            point.x < 0 || point.y < 1 || point.x >= width || point.y >= height ||
            (point.facing !== undefined && point.facing !== "left" && point.facing !== "right")) {
          errors.push(makeIssue("error", "spawn_point", pointPath + " needs in-bounds integer x/y and optional left/right facing.", { path: pointPath, roomId: roomId }));
        } else if (!isSafeSpawnFloor(room, point.x, point.y, tileset)) {
          errors.push(makeIssue("error", "spawn_floor", pointPath + " must be on a safe @ floor tile with open space above.", { path: pointPath, roomId: roomId, row: point.y, col: point.x }));
        }
      });
    }
    if (spawn.markers !== undefined) {
      if (!Array.isArray(spawn.markers)) {
        errors.push(makeIssue("error", "spawn_markers_type", path + ".markers must be an array.", { path: path + ".markers", roomId: roomId }));
      } else {
        if (spawn.markers.length > 128) errors.push(makeIssue("error", "spawn_marker_limit", "A room may contain at most 128 spawn markers.", { path: path + ".markers", roomId: roomId }));
        var seenCells = Object.create(null);
        var markerKinds = { deny: true, allow: true, allow_p1: true, allow_p2: true };
        spawn.markers.forEach(function (marker, index) {
          var valid = isPlainObject(marker) && Number.isInteger(marker.x) && Number.isInteger(marker.y) && marker.x >= 0 && marker.y >= 1 && marker.x < width && marker.y < height && markerKinds[marker.kind];
          var key = valid ? marker.x + ":" + marker.y : "";
          if (!valid || seenCells[key]) errors.push(makeIssue("error", valid ? "spawn_marker_duplicate" : "spawn_marker", "Spawn markers need a unique in-bounds x/y cell and kind deny, allow, allow_p1, or allow_p2.", { path: path + ".markers." + index, roomId: roomId }));
          else if (!isSafeSpawnFloor(room, marker.x, marker.y, tileset)) errors.push(makeIssue("error", "spawn_floor", "Respawn markers must be on a safe @ floor tile with open space above.", { path: path + ".markers." + index, roomId: roomId, row: marker.y, col: marker.x }));
          if (valid) seenCells[key] = true;
        });
      }
    }
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
    var authoredRoomFormat;
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
      validateEggnoggColor(camelOrSnake(rules, "eggnoggColor", "eggnogg_color", undefined), errors);
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

    authoredRoomFormat = document.layout && camelOrSnake(document.layout, "roomFormat", "room_format", ROOM_FORMAT);

    var authoredAmbientIds = customAmbientIds(document);
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
      var dimensions = roomDimensions(room);
      var expectedWidth = authoredRoomFormat === VARIABLE_ROOM_FORMAT ? dimensions.width : COLS;
      var expectedHeight = authoredRoomFormat === VARIABLE_ROOM_FORMAT ? dimensions.height : ROWS;
      if (typeof roomId !== "string" || !roomId) errors.push(makeIssue("error", "room_id_required", "Room " + (roomIndex + 1) + " needs a non-empty id.", { path: "rooms." + roomIndex, roomId: roomId || null }));
      else {
        if (/[\]\r\n\0]/.test(roomId) || utf8Bytes(roomId).length > 63) errors.push(makeIssue("error", "invalid_room_id", "Room id \"" + roomId + "\" cannot contain ], NUL, a line break, or exceed 63 UTF-8 bytes.", { path: "rooms." + roomIndex + ".id", roomId: roomId }));
        if (seen[roomId]) errors.push(makeIssue("error", "duplicate_room_id", "Room id \"" + roomId + "\" is used more than once.", { path: "rooms." + roomIndex + ".id", roomId: roomId }));
        seen[roomId] = true;
      }
      if (!Array.isArray(grid) ||
          (authoredRoomFormat === ROOM_FORMAT && grid.length !== ROWS) ||
          (authoredRoomFormat === VARIABLE_ROOM_FORMAT && (grid.length < MIN_ROOM_ROWS || grid.length > MAX_ROOM_ROWS))) {
        errors.push(makeIssue("error", "room_row_count", "Room \"" + (roomId || roomIndex + 1) + "\" has an invalid height.", { path: "rooms." + roomIndex + ".grid", roomId: roomId || null }));
      }
      if (Array.isArray(grid)) grid.forEach(function (row, rowIndex) {
        var text = rowString(row);
        if (expectedWidth < (authoredRoomFormat === VARIABLE_ROOM_FORMAT ? MIN_ROOM_COLS : COLS) || expectedWidth > (authoredRoomFormat === VARIABLE_ROOM_FORMAT ? MAX_ROOM_COLS : COLS) || text.length !== expectedWidth || utf8Bytes(text).length !== expectedWidth) errors.push(makeIssue("error", "row_width", "Row " + (rowIndex + 1) + " must match this room's " + expectedWidth + "-tile ASCII width.", { path: "rooms." + roomIndex + ".grid." + rowIndex, roomId: roomId || null, row: rowIndex }));
        for (var col = 0; col < text.length; col += 1) {
          var glyph = text.charAt(col);
          var nativeGlyph = resolvedGlyph(glyph);
          var cellPath = "rooms." + roomIndex + ".grid." + rowIndex + "." + col;
          if (!GLYPH_SET[glyph] && !(format === FORMAT_V2 && customSymbols[glyph])) errors.push(makeIssue("error", "invalid_glyph", "Glyph \"" + glyph + "\" is neither native nor declared by the V2 tileset.", { path: cellPath, roomId: roomId || null, row: rowIndex, col: col }));
          if ((nativeGlyph === "G" || nativeGlyph === "L" || nativeGlyph === "N" || nativeGlyph === "Y") && (rowIndex < 3 || col === 0 || col === expectedWidth - 1)) errors.push(makeIssue("error", "multicell_placement", "Glyph \"" + glyph + "\" resolves to a large native art action that needs three rows of headroom and side clearance.", { path: cellPath, roomId: roomId || null, row: rowIndex, col: col }));
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
          if (footprint && rowIndex >= footprint.up && col >= footprint.left && col + footprint.right < expectedWidth) {
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
      validateSpawnOverlay(room && room.spawn, "rooms." + roomIndex + ".spawn", expectedWidth, expectedHeight, errors, roomId || null, room, document.tileset);
      var spawnTotal = spawns.K + spawns.sword + spawns.mine;
      stats.maxResetSpawns = Math.max(stats.maxResetSpawns, spawnTotal);
      if (spawnTotal > 13) {
        var severity = spawns.K > 0 ? "error" : "warning";
        (severity === "error" ? errors : warnings).push(makeIssue(severity, "spawn_budget", "Room \"" + (roomId || roomIndex + 1) + "\" uses " + spawnTotal + "/13 reset spawns (K=" + spawns.K + ", swords=" + spawns.sword + ", mines=" + spawns.mine + ")." + (spawns.K ? " A failed K allocation is unsafe." : " This creates native pool pressure."), { path: "rooms." + roomIndex + ".grid", roomId: roomId || null }));
      }
      if (Array.isArray(grid) && grid.length === expectedHeight) {
        var solidAbove = "!@Xv";
        var lethalAbove = "12WwXvm";
        for (var y = 1; y < expectedHeight; y += 1) for (var x = 1; x < expectedWidth - 1; x += 1) {
          var here = resolvedGlyph((Array.isArray(grid[y]) ? grid[y][x] : rowString(grid[y]).charAt(x)) || " ");
          var above = resolvedGlyph((Array.isArray(grid[y - 1]) ? grid[y - 1][x] : rowString(grid[y - 1]).charAt(x)) || " ");
          if (here === "@" && solidAbove.indexOf(above) < 0 && lethalAbove.indexOf(above) < 0) candidates.push({ row: y, col: x });
        }
      }
      stats.spawnCandidates[roomId || String(roomIndex)] = candidates.length;
      if (room) validateOpponentSpawn(room.opponent_spawn, "rooms." + roomId + ".opponent_spawn", errors);
      var ambient = room && room.ambient;
      if ((ambient === 9 || ambient === "boil" || ambient === "fumes") && shallowWater === 0) warnings.push(makeIssue("warning", "boil_without_shallow_water", "Room \"" + (roomId || roomIndex + 1) + "\" uses boil/fumes ambience without any shallow-water w cells.", { path: "rooms." + roomIndex + ".ambient", roomId: roomId || null }));
      if (room && room.hook !== undefined && room.hook !== null) errors.push(makeIssue("error", "unsupported_hook", "Room hooks must be omitted or null; V2 map.lua is discovered at package level.", { path: "rooms." + roomId + ".hook", roomId: roomId || null }));
      if (room && room.ambient !== undefined && !validAmbient(room.ambient, authoredAmbientIds)) errors.push(makeIssue("error", "ambient", "Room ambient must be a built-in name, declared custom ambiance, or integer from 0 to 9.", { path: "rooms." + roomId + ".ambient", roomId: roomId || null }));
      validateAppearance(room && room.appearance, "rooms." + (roomId || roomIndex) + ".appearance", errors, roomId || null);
      roomMetrics[roomId || String(roomIndex)] = { goals: roomGoals, spawnCandidates: candidates.length };
    });
    if (rooms.length === 1) warnings.push(makeIssue("warning", "single_source_room", "This map has one reusable source room.", { path: "rooms", roomId: rooms[0] && rooms[0].id }));

    layout = document.layout;
    ids = rooms.map(function (room) { return room && room.id; });
    var graphValidation = null;
    if (!isPlainObject(layout)) errors.push(makeIssue("error", "layout_required", "A room layout is required.", { path: "layout" }));
    else if (layout.kind === ROOM_GRAPH_KIND) {
      graphValidation = validateRoomGraph(document);
      errors = errors.concat(graphValidation.errors);
      warnings = warnings.concat(graphValidation.warnings);
      if (format !== FORMAT_V2) errors.push(makeIssue("error", "room_graph_requires_v2", "Placed room layouts require eggnogg-map/v2.", { path: "layout.kind" }));
      if (rules && camelOrSnake(rules, "roundEndRooms", "round_end_rooms", "inner_only") !== "any") {
        errors.push(makeIssue("error", "room_graph_round_end", "Placed room layouts require Goal required round endings because a branched map has no automatic outermost room.", { path: "rules.round_end_rooms" }));
      }
    } else {
      var roomFormat = camelOrSnake(layout, "roomFormat", "room_format", undefined);
      if (layout.kind !== LAYOUT_KIND) errors.push(makeIssue("error", "layout_kind", "layout.kind must be \"" + LAYOUT_KIND + "\".", { path: "layout.kind" }));
      if (roomFormat !== ROOM_FORMAT && roomFormat !== VARIABLE_ROOM_FORMAT) errors.push(makeIssue("error", "room_format", "layout.room_format must be \"" + ROOM_FORMAT + "\" or \"" + VARIABLE_ROOM_FORMAT + "\".", { path: "layout.room_format" }));
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
    if (graphValidation && graphValidation.valid) {
      graphValidation.nodes.forEach(function (node) {
        if (node.id === layout.start) centerId = node.room;
      });
    }
    var centerRoom = rooms.filter(function (room) { return room && room.id === centerId; })[0];
    if (centerRoom && roomMetrics[centerId]) {
      var count = roomMetrics[centerId].spawnCandidates;
      if (count < 2) warnings.push(makeIssue("warning", "respawn_candidates", "The center source room has only " + count + " obvious native @ spawn-floor candidate" + (count === 1 ? "" : "s") + "; players need two separated candidates.", { path: "rooms." + centerId + ".grid", roomId: centerId }));
      else {
        var centerGrid = getGrid(centerRoom);
        var cols = [];
        var centerDimensions = roomDimensions(centerRoom);
        for (var cy = 1; cy < centerDimensions.height; cy += 1) for (var cx = 1; cx < centerDimensions.width - 1; cx += 1) {
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
      if (defaults.ambient !== undefined && !validAmbient(defaults.ambient, authoredAmbientIds)) errors.push(makeIssue("error", "default_ambient", "Default room ambient must be a built-in name, declared custom ambiance, or integer from 0 to 9.", { path: "defaults.room.ambient" }));
      validateOpponentSpawn(defaults.opponent_spawn, "defaults.room.opponent_spawn", errors);
      validateAppearance(defaults.appearance, "defaults.room.appearance", errors, null);
    }
    if (format === FORMAT_V2) {
      /* Raw graph errors above must remain ordinary diagnostics. Serializing
       * an invalid graph throws by design, so only run the V2 manifest parity
       * validator after the authoring layout itself is sound. */
      if (!graphValidation || graphValidation.valid) validateV2Manifest(buildDataObject(document), document.assets || (document._preserved && document._preserved.assets) || {}, errors, warnings);
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

  /* Mirror the native resolved policy for editor feedback. A room design can
   * have several placed copies, so callers should pass the placed node ID for
   * room graphs rather than guessing from the source-room order. */
  function resolveOpponentSpawn(document, roomId, nodeId) {
    document = unwrapDocument(document) || {};
    var layout = document.layout || {};
    var rooms = roomList(document);
    var room = rooms.filter(function (entry) { return entry.id === roomId; })[0];
    var node = layout.kind === ROOM_GRAPH_KIND && Array.isArray(layout.nodes) ?
      layout.nodes.filter(function (entry) { return entry.id === nodeId; })[0] : null;
    if (node) room = rooms.filter(function (entry) { return entry.id === node.room; })[0];
    if (!room) return null;
    var defaults = document.defaults && (document.defaults.room || document.defaults) || {};
    var policy = node && node.overrides && node.overrides.opponent_spawn ||
      room.opponent_spawn || defaults.opponent_spawn || "default";
    if (policy !== "default") return { policy: policy, reason: "authored" };
    if (layout.kind !== ROOM_GRAPH_KIND) {
      var order = Array.isArray(layout.order) ? layout.order : rooms.map(function (entry) { return entry.id; });
      return { policy: order.length > 1 && room.id === order[order.length - 1] ? "never" : "default",
               reason: order.length > 1 && room.id === order[order.length - 1] ? "symmetrical_outer" : "game_default" };
    }
    var nodes = layout.nodes || [];
    if (!node || nodes.length < 3 || nodes.length % 2 !== 1 || rooms.length < 2)
      return { policy: "default", reason: "game_default" };
    var middle = null;
    nodes.forEach(function (candidate) {
      if (nodes.filter(function (other) { return other.room === candidate.room; }).length === 1)
        middle = middle === null ? candidate : false;
    });
    if (!middle || middle === false) return { policy: "default", reason: "game_default" };
    var left = [], right = [];
    var paired = nodes.every(function (candidate) {
      if (candidate === middle) return true;
      if (candidate.room === middle.room || candidate.x === middle.x) return false;
      (candidate.x < middle.x ? left : right).push(candidate);
      return nodes.filter(function (other) {
        return other !== candidate && other !== middle &&
          (other.x < middle.x) !== (candidate.x < middle.x) &&
          other.room === candidate.room &&
          !!other.mirrorX !== !!candidate.mirrorX && other.y === candidate.y;
      }).length === 1;
    });
    if (!paired || left.length !== right.length || !left.length)
      return { policy: "default", reason: "game_default" };
    left.sort(function (a, b) { return a.x - b.x; });
    right.sort(function (a, b) { return b.x - a.x; });
    if (left[0].room === right[0].room && (node === left[0] || node === right[0]))
      return { policy: "never", reason: "symmetrical_outer" };
    return { policy: "default", reason: "game_default" };
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

  function nativeTilesetChoices(value) {
    var document = unwrapDocument(value) || {};
    var tileset = isPlainObject(document.tileset) ? document.tileset : {};
    var choices = [];
    var seen = Object.create(null);
    function add(source, fallbackGeometry) {
      var name;
      if (!isPlainObject(source)) return;
      name = source.sprite_sheet;
      if (typeof name !== "string" || !name || /^builtin:/i.test(name) || seen[name]) return;
      seen[name] = true;
      choices.push({
        sprite_sheet: name,
        cell_w: source.cell_w === undefined ? fallbackGeometry.cell_w : source.cell_w,
        cell_h: source.cell_h === undefined ? fallbackGeometry.cell_h : source.cell_h,
        padding: source.padding === undefined ? fallbackGeometry.padding : source.padding,
        legacy_default: source === tileset
      });
    }
    add(tileset, { cell_w: 16, cell_h: 16, padding: 0 });
    if (Array.isArray(tileset.sheets)) tileset.sheets.forEach(function (sheet) {
      add(sheet, { cell_w: 16, cell_h: 16, padding: 0 });
    });
    return choices;
  }

  function resolveRoomNativeTileset(value, room) {
    var document = unwrapDocument(value) || {};
    var defaults = document.defaults && document.defaults.room ? document.defaults.room : (document.defaults || {});
    var tileset = isPlainObject(document.tileset) ? document.tileset : {};
    var chosen = room && typeof room.native_tileset === "string" && room.native_tileset ? room.native_tileset :
      (typeof defaults.native_tileset === "string" && defaults.native_tileset ? defaults.native_tileset : null);
    if (chosen) return chosen;
    if (tileset.native_layout === true && typeof tileset.sprite_sheet === "string" && !/^builtin:/i.test(tileset.sprite_sheet)) return tileset.sprite_sheet;
    return null;
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

  function expandRoomGraphLayout(value) {
    var document = unwrapDocument(value) || {};
    var rooms = roomList(document);
    var roomIndex = Object.create(null);
    var result = validateRoomGraph(document);
    if (!result.valid) throw new Error("Cannot expand invalid room_graph: " + result.errors[0].message);
    rooms.forEach(function (room, index) {
      if (room && typeof room.id === "string") roomIndex[room.id] = index;
    });
    return result.nodes.map(function (node) {
      return {
        instanceId: node.id,
        sourceId: node.room,
        x: node.x,
        y: node.y,
        width: node.width,
        height: node.height,
        geometryMirrored: node.mirrorX,
        appearanceBank: node.appearance,
        sourceIndex: roomIndex[node.room]
      };
    });
  }

  function packageFacts(value) {
    var document = unwrapDocument(value) || {};
    var sourceRoomList = roomList(document);
    var sourceRooms = sourceRoomList.length;
    var sourceWidth = sourceRoomList.reduce(function (sum, room) { return sum + roomDimensions(room).width; }, 0);
    var graphResult = document.layout && document.layout.kind === ROOM_GRAPH_KIND ? validateRoomGraph(document) : null;
    var graphValid = graphResult && graphResult.valid;
    var finalWidth = graphValid ? graphResult.bounds.width :
      (sourceRooms ? roomDimensions(sourceRoomList[0]).width + 2 * sourceRoomList.slice(1).reduce(function (sum, room) { return sum + roomDimensions(room).width; }, 0) : 0);
    var maxHeight = graphValid ? graphResult.bounds.height :
      sourceRoomList.reduce(function (height, room) { return Math.max(height, roomDimensions(room).height); }, 0);
    return {
      sourceRooms: sourceRooms,
      finalRooms: graphValid ? graphResult.nodes.length : (sourceRooms ? sourceRooms * 2 - 1 : 0),
      sourceWidthCells: sourceWidth,
      finalWidthCells: finalWidth,
      finalWidthPixels: finalWidth * 16,
      heightCells: maxHeight,
      heightPixels: maxHeight * 16
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

  /* Pure tile-area helpers used by Greggnogg's selection tool. Keeping these
   * outside the DOM makes copy/paste and transforms use one checked path. */
  function transformTileArea(rows, operation) {
    var height, width, output, row, col;
    if (!Array.isArray(rows) || !rows.length || !Array.isArray(rows[0]) || !rows[0].length) throw new Error("Selection is empty.");
    height = rows.length; width = rows[0].length;
    for (row = 0; row < height; row += 1) {
      if (!Array.isArray(rows[row]) || rows[row].length !== width) throw new Error("Selection rows must have equal width.");
      for (col = 0; col < width; col += 1) if (typeof rows[row][col] !== "string" || rows[row][col].length !== 1) throw new Error("Selection contains an invalid tile.");
    }
    if (operation === "flip_x") return rows.map(function (line) { return line.slice().reverse(); });
    if (operation === "flip_y") return rows.slice().reverse().map(function (line) { return line.slice(); });
    if (operation !== "rotate_cw") throw new Error("Unknown selection transform.");
    output = Array.from({ length: width }, function () { return Array(height); });
    for (row = 0; row < height; row += 1) for (col = 0; col < width; col += 1) output[col][height - row - 1] = rows[row][col];
    return output;
  }

  function pasteTileArea(grid, rows, top, left) {
    var output, row, col;
    var height = Array.isArray(grid) ? grid.length : 0;
    var width = height && Array.isArray(grid[0]) ? grid[0].length : 0;
    if (!height || !width || !grid.every(function (line) { return Array.isArray(line) && line.length === width; })) throw new Error("Room grid has invalid dimensions.");
    if (!Number.isInteger(top) || !Number.isInteger(left)) throw new Error("Paste position must use whole cells.");
    /* transformTileArea performs the detached matrix validation for us. */
    rows = transformTileArea(rows, "flip_x").map(function (line) { return line.reverse(); });
    if (top < 0 || left < 0 || top + rows.length > height || left + rows[0].length > width) throw new Error("Selection does not fit in this room.");
    output = grid.map(function (line) { return line.slice(); });
    for (row = 0; row < rows.length; row += 1) for (col = 0; col < rows[row].length; col += 1) output[top + row][left + col] = rows[row][col];
    return output;
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
    ROOM_GRAPH_KIND: ROOM_GRAPH_KIND,
    MAX_ROOM_GRAPH_NODES: MAX_ROOM_GRAPH_NODES,
    MAX_ROOM_GRAPH_CONNECTIONS: MAX_ROOM_GRAPH_CONNECTIONS,
    ROOM_GRAPH_COORD_LIMIT: ROOM_GRAPH_COORD_LIMIT,
    ROOM_FORMAT: ROOM_FORMAT,
    VARIABLE_ROOM_FORMAT: VARIABLE_ROOM_FORMAT,
    MIN_ROOM_COLS: MIN_ROOM_COLS,
    MAX_ROOM_COLS: MAX_ROOM_COLS,
    MIN_ROOM_ROWS: MIN_ROOM_ROWS,
    MAX_ROOM_ROWS: MAX_ROOM_ROWS,
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
    roomDimensions: roomDimensions,
    isSafeSpawnFloor: isSafeSpawnFloor,
    resizeRoom: resizeRoom,
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
    findDuplicateJsonKeys: findDuplicateJsonKeys,
    parsePackage: parsePackage,
    parseProject: parseProject,
    transformTileArea: transformTileArea,
    pasteTileArea: pasteTileArea,
    parsePackageFiles: parsePackageFiles,
    validateDocument: validateDocument,
    resolveRoomAppearance: resolveRoomAppearance,
    resolveOpponentSpawn: resolveOpponentSpawn,
    nativeTilesetChoices: nativeTilesetChoices,
    resolveRoomNativeTileset: resolveRoomNativeTileset,
    expandMirroredLayout: expandMirroredLayout,
    expandRoomGraphLayout: expandRoomGraphLayout,
    validateRoomGraph: validateRoomGraph,
    ensureRoomGraphConnectionIds: ensureRoomGraphConnectionIds,
    createRoomGraphFromMirrored: createRoomGraphFromMirrored,
    convertMirroredToRoomGraph: convertMirroredToRoomGraph,
    convertRoomGraphToMirrored: convertRoomGraphToMirrored,
    connectRoomGraphNodes: connectRoomGraphNodes,
    roomGraphConnectionOptions: roomGraphConnectionOptions,
    roomGraphDoorwayGeometry: roomGraphDoorwayGeometry,
    autoArrangeRoomGraph: autoArrangeRoomGraph,
    disconnectRoomGraphNodes: disconnectRoomGraphNodes,
    updateRoomGraphConnection: updateRoomGraphConnection,
    disconnectRoomGraphConnection: disconnectRoomGraphConnection,
    addConnectedRoomGraphNode: addConnectedRoomGraphNode,
    nearestRoomGraphNode: nearestRoomGraphNode,
    moveRoomGraphNode: moveRoomGraphNode,
    rebuildAutomaticRoomGraphConnections: rebuildAutomaticRoomGraphConnections,
    resizeRoomGraphRoom: resizeRoomGraphRoom,
    setRoomGraphNodePresentation: setRoomGraphNodePresentation,
    setRoomGraphNodeOverrides: setRoomGraphNodeOverrides,
    removeRoomGraphNode: removeRoomGraphNode,
    serializeRoomGraphLayout: serializeRoomGraphLayout,
    packageFacts: packageFacts,
    exportProjectFiles: exportProjectFiles,
    buildPackageZip: buildPackageZip,
    crc32: crc32,
    CRC32: crc32,
    buildStoredZip: buildStoredZip,
    parseStoredZip: parseStoredZip
  };
}));
