/* Dependency-free Greggnogg core tests.
 *
 * Run on this repository's Windows development environment with:
 *   cscript //nologo greggnogg\editor-core.test.js
 *
 * The small compatibility layer is only for the old JScript host. The same
 * assertions also run under Node when Node is available.
 */
(function () {
  "use strict";

  var Uint8Array = typeof globalThis !== "undefined" && globalThis.Uint8Array ? globalThis.Uint8Array : undefined;
  var Uint32Array = typeof globalThis !== "undefined" && globalThis.Uint32Array ? globalThis.Uint32Array : undefined;

  function installLegacyPolyfills() {
    if (!Object.create) Object.create = function () { return {}; };
    if (!Object.keys) Object.keys = function (object) { var keys = []; for (var key in object) if (Object.prototype.hasOwnProperty.call(object, key)) keys.push(key); return keys; };
    if (!Object.assign) Object.assign = function (target) { for (var sourceIndex = 1; sourceIndex < arguments.length; sourceIndex += 1) { var source = arguments[sourceIndex]; if (!source) continue; for (var key in source) if (Object.prototype.hasOwnProperty.call(source, key)) target[key] = source[key]; } return target; };
    if (!Array.isArray) Array.isArray = function (value) { return Object.prototype.toString.call(value) === "[object Array]"; };
    if (!Array.from) Array.from = function (value, mapper) { var out = []; for (var i = 0; i < value.length; i += 1) out.push(mapper ? mapper(value[i], i) : value[i]); return out; };
    if (!Array.prototype.forEach) Array.prototype.forEach = function (callback) { for (var i = 0; i < this.length; i += 1) if (i in this) callback(this[i], i, this); };
    if (!Array.prototype.map) Array.prototype.map = function (callback) { var out = []; for (var i = 0; i < this.length; i += 1) if (i in this) out[i] = callback(this[i], i, this); return out; };
    if (!Array.prototype.filter) Array.prototype.filter = function (callback) { var out = []; for (var i = 0; i < this.length; i += 1) if (i in this && callback(this[i], i, this)) out.push(this[i]); return out; };
    if (!Array.prototype.find) Array.prototype.find = function (callback) { for (var i = 0; i < this.length; i += 1) if (i in this && callback(this[i], i, this)) return this[i]; return undefined; };
    if (!Array.prototype.reduce) Array.prototype.reduce = function (callback, initial) { var i = 0; var value = initial; if (arguments.length < 2) { while (i < this.length && !(i in this)) i += 1; if (i >= this.length) throw new TypeError("Reduce of empty array"); value = this[i++]; } for (; i < this.length; i += 1) if (i in this) value = callback(value, this[i], i, this); return value; };
    if (!Array.prototype.some) Array.prototype.some = function (callback) { for (var i = 0; i < this.length; i += 1) if (i in this && callback(this[i], i, this)) return true; return false; };
    if (!Array.prototype.every) Array.prototype.every = function (callback) { for (var i = 0; i < this.length; i += 1) if (i in this && !callback(this[i], i, this)) return false; return true; };
    if (!Array.prototype.fill) Array.prototype.fill = function (value) { for (var i = 0; i < this.length; i += 1) this[i] = value; return this; };
    if (!Array.prototype.indexOf) Array.prototype.indexOf = function (value) { for (var i = 0; i < this.length; i += 1) if (this[i] === value) return i; return -1; };
    if (!String.prototype.trim) String.prototype.trim = function () { return this.replace(/^\s+|\s+$/g, ""); };
    if (!String.prototype.padStart) String.prototype.padStart = function (length, fill) { var result = String(this); fill = fill || " "; while (result.length < length) result = fill + result; return result.slice(-length); };
    if (!Number.isFinite) Number.isFinite = function (value) { return typeof value === "number" && isFinite(value); };
    if (!Number.isInteger) Number.isInteger = function (value) { return typeof value === "number" && isFinite(value) && Math.floor(value) === value; };
    if (typeof Uint8Array === "undefined") {
      Uint8Array = function LegacyUint8Array(value) {
        var length = typeof value === "number" ? value : (value && value.length ? value.length : 0);
        this.length = length;
        this.byteLength = length;
        for (var i = 0; i < length; i += 1) this[i] = typeof value === "number" ? 0 : value[i] & 255;
      };
      Uint8Array.prototype.set = function (source, offset) { offset = offset || 0; for (var i = 0; i < source.length; i += 1) this[offset + i] = source[i] & 255; };
      Uint8Array.prototype.slice = function (start, end) { var out = new Uint8Array(Math.max(0, (end === undefined ? this.length : end) - start)); for (var i = 0; i < out.length; i += 1) out[i] = this[start + i]; return out; };
      Uint8Array.prototype.subarray = Uint8Array.prototype.slice;
      Uint32Array = function LegacyUint32Array(value) { var length = typeof value === "number" ? value : value.length; this.length = length; for (var i = 0; i < length; i += 1) this[i] = typeof value === "number" ? 0 : value[i] >>> 0; };
    }
    if (typeof JSON === "undefined") {
      this.JSON = {
        parse: function (text) { return eval("(" + text + ")"); },
        stringify: function stringify(value, replacer, indent) {
          var gap = typeof indent === "number" ? new Array(indent + 1).join(" ") : "";
          function encode(item, depth) {
            if (item === null) return "null";
            if (typeof item === "string") return "\"" + item.replace(/\\/g, "\\\\").replace(/\"/g, "\\\"").replace(/\r/g, "\\r").replace(/\n/g, "\\n").replace(/\t/g, "\\t") + "\"";
            if (typeof item === "number" || typeof item === "boolean") return String(item);
            if (Array.isArray(item)) return "[" + item.map(function (entry) { return encode(entry, depth + 1); }).join(",") + "]";
            var keys = Object.keys(item).filter(function (key) { return typeof item[key] !== "undefined" && typeof item[key] !== "function"; });
            return "{" + keys.map(function (key) { return encode(key, depth + 1) + ":" + encode(item[key], depth + 1); }).join(",") + "}";
          }
          return encode(value, 0);
        }
      };
    }
  }

  installLegacyPolyfills.call(this);

  var core;
  // The legacy shim declares a local module below; var hoisting hides Node's module.
  if (typeof require === "function") {
    core = require("./editor-core.js");
  } else {
    var fso = new ActiveXObject("Scripting.FileSystemObject");
    var here = fso.GetParentFolderName(WScript.ScriptFullName);
    var stream = fso.OpenTextFile(fso.BuildPath(here, "editor-core.js"), 1, false, 0);
    var source = stream.ReadAll();
    stream.Close();
    var module = { exports: {} };
    eval(source);
    core = module.exports;
  }

  var passed = 0;
  function assert(condition, message) {
    if (!condition) throw new Error("FAIL: " + message);
    passed += 1;
  }
  function hasIssue(result, code) { return result.issues.some(function (issue) { return issue.code === code; }); }
  function bytesEqual(left, right) {
    if (!left || !right || left.length !== right.length) return false;
    for (var byteIndex = 0; byteIndex < left.length; byteIndex += 1) if (left[byteIndex] !== right[byteIndex]) return false;
    return true;
  }
  function prefixBytes(prefix, body) {
    var output = new Uint8Array(prefix.length + body.length);
    output.set(prefix, 0);
    output.set(body, prefix.length);
    return output;
  }
  function rowWith(ch, count) { return new Array((count || 1) + 1).join(ch); }
  function mapText(id, rows, format) { return "; " + (format || core.FORMAT_V1) + "\n\n[" + id + "]\n" + rows.map(function (row) { return "\"" + row + "\""; }).join("\n") + "\n"; }
  function pngHeader(width, height) {
    return new Uint8Array([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 0x49, 0x48, 0x44, 0x52,
      width >>> 24 & 255, width >>> 16 & 255, width >>> 8 & 255, width & 255,
      height >>> 24 & 255, height >>> 16 & 255, height >>> 8 & 255, height & 255]);
  }

  assert(core && core.FORMAT_V1 === "eggnogg-map/v1", "core loads in this JavaScript host");
  assert(core.GLYPH_WHITELIST === " !#()*+-.12:=?@ACEFGHIKLNOPQSTWXYZ^_`cefilmqstuvwx|~", "V1 allowlist matches the native loader");

  var fresh = core.createDefaultDocument();
  var freshValidation = core.validateDocument(fresh);
  assert(freshValidation.valid, "default project is exportable");
  assert(freshValidation.stats.finalRooms === 3, "two source rooms expand to three final rooms");
  var jsonText = core.serializeDataJson(fresh);
  var nativeMapText = core.serializeMapText(fresh);
  var roundTrip = core.parsePackage(jsonText, nativeMapText);
  assert(roundTrip.valid, "serialized V1 package parses and validates");
  assert(roundTrip.document.rooms.length === 2, "V1 room bijection round-trips");
  var v2Fresh = core.upgradeToV2(fresh);
  assert(v2Fresh.format === core.FORMAT_V2 && core.validateDocument(v2Fresh).valid, "a native project can upgrade to a valid canonical V2 shell");
  v2Fresh.tileset.tiles.push({ id: "decor", symbol: "$", name: "Decor", sprite_sheet: "builtin:tiles", sprite_index: 0x2d, collision: "pass_through" });
  v2Fresh.rooms[0].grid[0][0] = "$";
  assert(core.validateDocument(v2Fresh).valid, "a V2 built-in decorative tile validates and can be painted");
  var croppedSheetDoc = core.deepClone(v2Fresh);
  croppedSheetDoc.assets = { "atlas.png": pngHeader(100, 70) };
  croppedSheetDoc.tileset.tiles[0].sprite_sheet = "atlas.png";
  croppedSheetDoc.tileset.tiles[0].cell_w = 16;
  croppedSheetDoc.tileset.tiles[0].cell_h = 16;
  croppedSheetDoc.tileset.tiles[0].source_x = 3;
  croppedSheetDoc.tileset.tiles[0].source_y = 5;
  croppedSheetDoc.tileset.tiles[0].source_w = 32;
  croppedSheetDoc.tileset.tiles[0].source_h = 16;
  croppedSheetDoc.tileset.tiles[0].sprite_index = 1;
  assert(core.validateDocument(croppedSheetDoc).valid, "a tile can crop an arbitrary grid-aligned rectangle from a non-grid PNG");
  var croppedJson = JSON.parse(core.serializeDataJson(croppedSheetDoc));
  assert(croppedJson.tileset.tiles[0].source_x === 3 && croppedJson.tileset.tiles[0].source_w === 32, "tile source rectangles survive JSON export");
  croppedSheetDoc.tileset.tiles[0].source_w = 31;
  assert(hasIssue(core.validateDocument(croppedSheetDoc), "png_grid"), "a cropped source rectangle must contain whole authored cells");
  assert(JSON.parse(core.serializeDataJson(v2Fresh)).format === core.FORMAT_V2, "generated V2 manifests serialize as V2");
  assert(core.serializeMapText(v2Fresh).indexOf("; " + core.FORMAT_V2) === 0, "generated V2 map text receives the V2 marker");
  var roomSheetDoc = core.deepClone(v2Fresh);
  roomSheetDoc.assets = { "cave.png": pngHeader(256, 128), "ice.png": pngHeader(512, 64) };
  roomSheetDoc.tileset.sheets = [
    { sprite_sheet: "cave.png", cell_w: 16, cell_h: 16, padding: 0 },
    { sprite_sheet: "ice.png", cell_w: 16, cell_h: 16, padding: 0 }
  ];
  roomSheetDoc.defaults.native_tileset = "cave.png";
  roomSheetDoc.rooms[1].native_tileset = "ice.png";
  assert(core.validateDocument(roomSheetDoc).valid, "declared 128-cell native sheets can be selected per map and room");
  assert(core.resolveRoomNativeTileset(roomSheetDoc, roomSheetDoc.rooms[0]) === "cave.png" && core.resolveRoomNativeTileset(roomSheetDoc, roomSheetDoc.rooms[1]) === "ice.png", "room native sheet resolution applies room, map, then legacy precedence");
  var roomSheetJson = JSON.parse(core.serializeDataJson(roomSheetDoc));
  assert(roomSheetJson.defaults.room.native_tileset === "cave.png" && roomSheetJson.rooms.outer_1.native_tileset === "ice.png", "per-room native sheet choices survive generated JSON export");
  var importedRoomSheets = core.parsePackage(JSON.stringify(roomSheetJson), core.serializeMapText(roomSheetDoc), { assets: roomSheetDoc.assets });
  assert(importedRoomSheets.valid && importedRoomSheets.document.defaults.native_tileset === "cave.png" && importedRoomSheets.document.rooms[1].native_tileset === "ice.png", "per-room native sheet choices survive package import");
  roomSheetDoc.rooms[1].native_tileset = "";
  assert(core.resolveRoomNativeTileset(roomSheetDoc, roomSheetDoc.rooms[1]) === "cave.png", "an empty room native sheet inherits the map choice");
  roomSheetDoc.rooms[1].native_tileset = "missing.png";
  assert(hasIssue(core.validateDocument(roomSheetDoc), "native_tileset_declaration"), "room native sheets must match a declared external sheet");
  roomSheetDoc.rooms[1].native_tileset = "cave.png";
  roomSheetDoc.assets["cave.png"] = pngHeader(16, 16);
  assert(hasIssue(core.validateDocument(roomSheetDoc), "native_tileset_cells"), "room native sheets require the complete 128-cell native prefix");
  var legacyRoomSheetDoc = core.deepClone(v2Fresh);
  legacyRoomSheetDoc.tileset.sprite_sheet = "legacy.png";
  legacyRoomSheetDoc.tileset.native_layout = true;
  legacyRoomSheetDoc.tileset.cell_w = 16;
  legacyRoomSheetDoc.tileset.cell_h = 16;
  legacyRoomSheetDoc.tileset.padding = 0;
  legacyRoomSheetDoc.assets = { "legacy.png": pngHeader(256, 128) };
  assert(core.resolveRoomNativeTileset(legacyRoomSheetDoc, legacyRoomSheetDoc.rooms[0]) === "legacy.png", "legacy top-level native_layout remains the final room-sheet fallback");
  var opaqueId = core.deepClone(fresh);
  opaqueId.id = "  opaque id  ";
  assert(JSON.parse(core.serializeDataJson(opaqueId)).id === opaqueId.id, "V1 stable ids are serialized verbatim instead of being trimmed");
  var nulRoomId = core.deepClone(fresh);
  nulRoomId.rooms[0].id = "center\0hidden";
  nulRoomId.layout.order[0] = nulRoomId.rooms[0].id;
  assert(hasIssue(core.validateDocument(nulRoomId), "invalid_room_id"), "in-memory room ids cannot export an embedded native string terminator");
  assert(core.parseMapText(nativeMapText.replace(/\n/g, "\r\n")).valid, "CRLF data.map parses after native-style CR stripping");
  var crRows = [];
  for (var cr = 0; cr < core.ROWS; cr += 1) crRows.push(cr === 0 ? rowWith(" ", 10) + "\r" + rowWith(" ", 23) : rowWith(" ", 33));
  assert(core.parseMapText(mapText("cr_room", crRows)).valid, "interior CR is discarded instead of treated as a line break");
  var bomPackage = core.parsePackageFiles({
    "bom/data.json": prefixBytes(new Uint8Array([0xEF, 0xBB, 0xBF]), core.utf8Bytes(jsonText)),
    "bom/data.map": core.utf8Bytes(nativeMapText)
  });
  assert(!bomPackage.valid && hasIssue(bomPackage, "json_bom"), "file-based import retains a UTF-8 BOM so native-incompatible JSON is rejected");

  var duplicateJson = "{\"format\":\"eggnogg-map/v1\",\"name\":\"First name\",\"\\u006eame\":\"Second name\",\"author\":\"Greg\",\"layout\":{\"kind\":\"mirrored_source_rooms\",\"room_format\":\"vanilla_33x12\",\"order\":[\"center\",\"outer_1\"]}}";
  var duplicateResult = core.parsePackage(duplicateJson, nativeMapText);
  assert(hasIssue(duplicateResult, "duplicate_json_key") && !duplicateResult.valid, "escaped-equivalent duplicate JSON object keys are rejected");
  var escapedNameJson = jsonText.replace("\"Untitled Map\"", "\"\\u00e9\"");
  var escapedName = core.parsePackage(escapedNameJson, nativeMapText);
  assert(escapedName.valid && escapedName.document.name === "?", "non-ASCII JSON unicode escapes follow the native parser's question-mark conversion");
  var unicodeDuplicateJson = jsonText.replace(/}\n$/, ",\n  \"\\u00e9\": 1,\n  \"\\u2603\": 2\n}\n");
  var unicodeDuplicate = core.parsePackage(unicodeDuplicateJson, nativeMapText);
  assert(!unicodeDuplicate.valid && hasIssue(unicodeDuplicate, "duplicate_json_key"), "distinct non-ASCII unicode escapes that collide natively are rejected as duplicate keys");
  var nulEscapeJson = jsonText.replace(/}\n$/, ",\n  \"future\": \"\\u0000\"\n}\n");
  var nulEscape = core.parsePackage(nulEscapeJson, nativeMapText);
  assert(!nulEscape.valid && hasIssue(nulEscape, "json_nul_escape"), "a NUL escape in even an unknown JSON field is a native parse error");

  var trailing = core.parseMapText(nativeMapText.replace(/"\n/, "\"  \n"));
  assert(hasIssue(trailing, "invalid_quoted_row"), "row trailing whitespace is rejected like the native parser");
  var invalidGlyph = core.parseMapText(nativeMapText.replace("                                 ", "                $                "));
  assert(hasIssue(invalidGlyph, "invalid_glyph"), "unknown V1 glyph is a hard error");

  var unsafeTentacle = core.deepClone(fresh);
  unsafeTentacle.rooms[0].grid[0][5] = "T";
  assert(hasIssue(core.validateDocument(unsafeTentacle), "tentacle_headroom"), "unsafe native T placement is blocked");
  assert(core.TILE_BY_GLYPH.G.footprint.anchorY === 3 && core.TILE_BY_GLYPH.L.footprint.anchorY === 3, "large-art metadata places the source marker one row below its three-row draw footprint");
  var artFootprint = core.deepClone(fresh);
  artFootprint.rooms[0].grid[3][5] = "G";
  artFootprint.rooms[0].grid[3][4] = "#";
  assert(!hasIssue(core.validateDocument(artFootprint), "native_footprint_overlap"), "large art does not overwrite authored cells beside its source marker");
  artFootprint.rooms[0].grid[0][4] = "#";
  assert(hasIssue(core.validateDocument(artFootprint), "native_footprint_overlap"), "large art overlap checks cover all three rows strictly above its marker");
  var kBudget = core.deepClone(fresh);
  for (var k = 0; k < 14; k += 1) kBudget.rooms[0].grid[5][k + 1] = "K";
  var kResult = core.validateDocument(kBudget);
  assert(!kResult.valid && hasIssue(kResult, "spawn_budget"), "K room over the 13-slot reset budget is blocked");
  var swordBudget = core.deepClone(fresh);
  for (var s = 0; s < 14; s += 1) swordBudget.rooms[0].grid[5][s + 1] = "*";
  var swordResult = core.validateDocument(swordBudget);
  assert(swordResult.valid && hasIssue(swordResult, "spawn_budget"), "non-K reset pool pressure is a warning, not a parser error");

  var appearanceDoc = core.deepClone(fresh);
  appearanceDoc.defaults.appearance = { primary: { fg1: [1, 0, 0] }, mirror: { fg1: [0, 0, 1] } };
  appearanceDoc.rooms[0].appearance = { primary: { fg1: [0, 1, 0] } };
  var appearance = core.resolveRoomAppearance(appearanceDoc, appearanceDoc.rooms[0]);
  assert(appearance.primary.fg1[1] === 1 && appearance.mirror.fg1[1] === 1, "omitted room mirror recopies the resolved room primary");
  var emptyAppearanceDoc = core.deepClone(fresh);
  emptyAppearanceDoc.rooms[0].appearance = {};
  var emptyAppearanceJson = JSON.parse(core.serializeDataJson(emptyAppearanceDoc));
  assert(Object.prototype.hasOwnProperty.call(emptyAppearanceJson.rooms.center, "appearance"), "authored empty appearance survives serialization because presence affects mirror inheritance");

  var v2Rows = [];
  for (var vr = 0; vr < core.ROWS; vr += 1) v2Rows.push(vr === 9 ? ">                                " : (vr >= 10 ? rowWith("@", 33) : rowWith(" ", 33)));
  var v2Map = mapText("center", v2Rows, core.FORMAT_V2);
  var v2Json = "{\n  \"format\": \"eggnogg-map/v2\",\n  \"id\": \"v2_test\",\n  \"name\": \"V2 test\",\n  \"author\": \"Greg\",\n  \"layout\": {\"kind\": \"mirrored_source_rooms\", \"room_format\": \"vanilla_33x12\", \"order\": [\"center\"]},\n  \"tileset\": {\"tiles\": [{\"id\": \"block\", \"symbol\": \">\", \"sprite_sheet\": \"builtin:tiles\", \"sprite_index\": 0, \"collision\": \"solid\"}]}\n}\n";
  var v2 = core.parsePackage(v2Json, v2Map, { mapLua: "" });
  assert(v2.valid && !v2.editable && v2.preserved, "V2 custom-symbol package is detected as read-only preserved content");
  var v2Files = core.exportProjectFiles(v2.document);
  assert(v2Files["data.json"] === v2Json && v2Files["data.map"] === v2Map, "V2 manifest and map text are exported byte-for-byte");
  assert(Object.prototype.hasOwnProperty.call(v2Files, "map.lua") && v2Files["map.lua"] === "", "present empty map.lua remains present");

  var conflictingPresetJson = v2Json.replace("\"collision\": \"solid\"", "\"collision\": \"solid\", \"native_glyph\": \"x\"");
  var conflictingPreset = core.parsePackage(conflictingPresetJson, v2Map);
  assert(!conflictingPreset.valid && hasIssue(conflictingPreset, "native_glyph_conflict"), "V2 collision presets reject a conflicting authored native_glyph like the content registry");

  var tinyPng = new Uint8Array([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 0x49, 0x48, 0x44, 0x52, 0, 0, 0, 16, 0, 0, 0, 16]);
  var defaultHash = "bf2f0ca3935efb3bf466aafa116e2502339f5f6544e1f3ed2a57f990a33d28ac";
  var overrideSheetJson = v2Json.replace("\"tileset\": {", "\"tileset\": {\"sprite_sheet\": \"default.png\", \"asset_sha256\": \"" + defaultHash + "\",").replace("\"sprite_sheet\": \"builtin:tiles\", ", "");
  /* Re-add the per-tile exception after the textual fixture edit. */
  overrideSheetJson = overrideSheetJson.replace("\"sprite_index\": 0", "\"sprite_sheet\": \"builtin:tiles\", \"sprite_index\": 0");
  var overrideSheet = core.parsePackage(overrideSheetJson, v2Map, { assets: { "default.png": tinyPng } });
  assert(overrideSheet.valid && !hasIssue(overrideSheet, "builtin_hash"), "a tile sheet exception does not inherit the default external sheet's SHA-256 pin");

  var rawLua = new Uint8Array([45, 45, 32, 0x80, 10]);
  var rawPng = new Uint8Array([1, 2, 3, 4]);
  var rawV2Files = {
    "v2_raw/data.json": core.utf8Bytes(v2Json),
    "v2_raw/data.map": core.utf8Bytes(v2Map),
    "v2_raw/map.lua": rawLua,
    "v2_raw/unreferenced.png": rawPng
  };
  var rawV2 = core.parsePackageFiles(rawV2Files);
  var rawV2Export = core.exportProjectFiles(rawV2.document);
  assert(rawV2.valid && bytesEqual(rawV2Export["data.json"], rawV2Files["v2_raw/data.json"]) && bytesEqual(rawV2Export["data.map"], rawV2Files["v2_raw/data.map"]), "file-based V2 import preserves original manifest and map bytes");
  assert(bytesEqual(rawV2Export["map.lua"], rawLua) && bytesEqual(rawV2Export["unreferenced.png"], rawPng), "file-based V2 import preserves opaque map.lua and PNG bytes");
  var rawV2Zip = core.parseStoredZip(core.buildPackageZip(rawV2.document).bytes);
  assert(bytesEqual(rawV2Zip["v2_raw/map.lua"], rawLua) && bytesEqual(rawV2Zip["v2_raw/unreferenced.png"], rawPng), "V2 preserved bytes survive the complete ZIP export path");

  var quoteRows = [];
  for (var qr = 0; qr < core.ROWS; qr += 1) quoteRows.push(qr === 0 ? "\"" + rowWith(" ", 32) : rowWith(" ", 33));
  var quoteParsed = core.parseMapText(mapText("quote", quoteRows, core.FORMAT_V2), { format: core.FORMAT_V2, customSymbols: ["\""] });
  assert(quoteParsed.valid, "V2 printable quote symbol is parsed using the last quote as delimiter");

  var layout = core.expandMirroredLayout(fresh);
  assert(layout.length === 3 && layout[0].appearanceBank === "mirror" && layout[2].geometryMirrored, "expanded arena uses mirror colours on the left and mirrored geometry on the right");

  var graphSource = core.deepClone(fresh);
  graphSource.layout.roomFormat = core.VARIABLE_ROOM_FORMAT;
  core.resizeRoom(graphSource.rooms[0], 47, 18);
  core.resizeRoom(graphSource.rooms[1], 12, 7);
  var convertedGraph = core.createRoomGraphFromMirrored(graphSource);
  var convertedGraphResult = core.validateRoomGraph(graphSource, convertedGraph);
  assert(convertedGraph.kind === core.ROOM_GRAPH_KIND && convertedGraph.nodes.length === 3 && convertedGraph.connections.length === 2, "mirrored layouts convert to an explicit final-room graph");
  assert(convertedGraph.nodes[0].room === "outer_1" && convertedGraph.nodes[2].room === "outer_1" && convertedGraph.nodes[2].mirrorX === true, "conversion keeps reusable source rooms and right-side mirroring explicit");
  assert(convertedGraph.nodes[1].x === 12 && convertedGraph.nodes[2].x === 59 && convertedGraph.start === "center", "conversion retains mixed-width room geometry and the center start");
  assert(convertedGraphResult.valid && JSON.stringify(convertedGraphResult.bounds) === '{"x":0,"y":0,"width":71,"height":18}', "converted mixed-size graph validates with exact world bounds");
  var convertedDocument = core.convertMirroredToRoomGraph(graphSource);
  assert(convertedDocument !== graphSource && convertedDocument.rooms !== graphSource.rooms && convertedDocument.layout.kind === core.ROOM_GRAPH_KIND, "document conversion is detached and preserves source room data");
  assert(core.resolveOpponentSpawn(graphSource, "outer_1").policy === "never" &&
    core.resolveOpponentSpawn(graphSource, "center").policy === "default", "mirrored editor reports the native outer-room spawn default");
  var spawnGraph = core.deepClone(convertedDocument);
  spawnGraph.layout.start = "right_1";
  spawnGraph.layout.nodes = [spawnGraph.layout.nodes[1], spawnGraph.layout.nodes[2], spawnGraph.layout.nodes[0]];
  assert(core.resolveOpponentSpawn(spawnGraph, "outer_1", "left_1").reason === "symmetrical_outer" &&
    core.resolveOpponentSpawn(spawnGraph, "outer_1", "right_1").policy === "never" &&
    core.resolveOpponentSpawn(spawnGraph, "center", "center").policy === "default", "reordered graph and side-room start retain native spawn feedback");
  spawnGraph.layout.nodes[1].overrides = { opponent_spawn: "always" };
  assert(core.resolveOpponentSpawn(spawnGraph, "outer_1", "right_1").policy === "always", "placed-copy Always is shown above the native outer default");
  spawnGraph.defaults.opponent_spawn = "never";
  spawnGraph.rooms[0].opponent_spawn = "default";
  assert(core.resolveOpponentSpawn(spawnGraph, "center", "center").policy === "default" &&
    core.resolveOpponentSpawn(spawnGraph, "outer_1", "left_1").policy === "never", "room default overrides map Never while outer Game default stays spawn-free");
  assert(convertedDocument.format === core.FORMAT_V2 && convertedDocument.rules.roundEndRooms === "any", "room graph conversion selects the package format and goal-based round policy required by runtime");
  var graphObjectSource = core.deepClone(graphSource);
  graphObjectSource.entities = { schema: 2, capacity: 8, types: [{ key: "demo:orb", regions: [] }], placements: [
    { name: "orb", type: "demo:orb", room: "outer_1", side: "both", x: 8, y: 12 }
  ] };
  var convertedObjects = core.convertMirroredToRoomGraph(graphObjectSource).entities.placements;
  assert(convertedObjects.length === 2 && convertedObjects[0].instance === "left_1" && convertedObjects[0].name === "orb" &&
    convertedObjects[1].instance === "right_1" && convertedObjects[1].name === "orb.mirror" &&
    convertedObjects.every(function (placement) { return placement.side === undefined; }),
    "room graph conversion migrates mirrored object copies to exact placed-room targets");
  var symmetricAgain = core.convertRoomGraphToMirrored(core.convertMirroredToRoomGraph(graphObjectSource));
  assert(symmetricAgain.layout.kind === core.LAYOUT_KIND && symmetricAgain.entities.placements.length === 1 &&
    symmetricAgain.entities.placements[0].name === "orb" && symmetricAgain.entities.placements[0].side === "both",
    "the layout toggle round-trips an unchanged symmetrical graph and merges matching object copies");
  var asymmetricToggleSource = core.convertMirroredToRoomGraph(graphSource);
  asymmetricToggleSource.layout.nodes[0].y = 1;
  var restoredSymmetry = core.convertRoomGraphToMirrored(asymmetricToggleSource);
  var restoredGraph = core.convertMirroredToRoomGraph(restoredSymmetry);
  assert(restoredSymmetry.layout.kind === core.LAYOUT_KIND && restoredGraph.layout.nodes[0].y === 0 &&
    core.validateRoomGraph(restoredGraph).valid,
    "switching a dragged generated pair to Symmetrical restores aligned positions and doorways");
  var expandedGraph = core.expandRoomGraphLayout(convertedDocument);
  assert(expandedGraph[0].instanceId === "left_1" && expandedGraph[1].sourceId === "center" && expandedGraph[2].geometryMirrored, "room graph expansion exposes final instances without duplicating source rooms");
  assert(core.packageFacts(convertedDocument).finalRooms === 3 && core.packageFacts(convertedDocument).finalWidthCells === 71, "package facts use explicit graph instances and bounds");
  var graphJson = core.serializeDataJson(convertedDocument);
  var graphData = JSON.parse(graphJson);
  assert(graphData.layout.kind === core.ROOM_GRAPH_KIND && graphData.layout.nodes.length === 3 &&
    graphData.layout.connections.length === 2 && graphData.rules.round_end_rooms === "any",
    "room graph documents export the canonical playable manifest instead of falling back to a mirrored line");
  assert(graphData.layout.connections[0].players === undefined && graphData.layout.connections[0].focus === undefined,
    "default doorway traversal controls stay omitted from the native manifest");
  var graphRoundTrip = core.parsePackage(graphJson, core.serializeMapText(convertedDocument), { assets: {} });
  assert(graphRoundTrip.valid && graphRoundTrip.document.layout.kind === core.ROOM_GRAPH_KIND &&
    graphRoundTrip.document.layout.nodes[2].mirrorX === true &&
    graphRoundTrip.document.layout.connections[0].players === "both" &&
    graphRoundTrip.document.layout.connections[0].focus === "go",
    "room graph manifests import with placed instances, mirror flags, and traversal defaults intact");
  var policyDocument = core.updateRoomGraphConnection(convertedDocument, 0, { players: "go", focus: "crossing" });
  var policyJson = core.serializeDataJson(policyDocument);
  var policyData = JSON.parse(policyJson);
  assert(policyData.layout.connections[0].players === "go" && policyData.layout.connections[0].focus === "crossing",
    "non-default doorway traversal controls serialize on their connection");
  var policyRoundTrip = core.parsePackage(policyJson, core.serializeMapText(policyDocument), { assets: {} });
  assert(policyRoundTrip.valid && policyRoundTrip.document.layout.connections[0].players === "go" &&
    policyRoundTrip.document.layout.connections[0].focus === "crossing",
    "doorway traversal controls survive package export and import");

  var verticalGraph = {
    kind: core.ROOM_GRAPH_KIND,
    roomFormat: core.VARIABLE_ROOM_FORMAT,
    start: "center",
    nodes: [
      { id: "center", room: "center", x: 0, y: 0, mirrorX: false, appearance: "primary" },
      { id: "branch", room: "outer_1", x: 10, y: 18, mirrorX: false, appearance: "primary" }
    ],
    connections: [
      { from: "center", fromSide: "bottom", fromOffset: 10, to: "branch", toSide: "top", toOffset: 0, span: 4, oneWay: false }
    ]
  };
  assert(core.validateRoomGraph(graphSource, verticalGraph).valid, "room graph validation accepts aligned vertical branches");
  var authoringDocument = core.deepClone(graphSource);
  authoringDocument.layout = core.deepClone(verticalGraph);
  authoringDocument.entities = { schema: 2, capacity: 8, types: [{ key: "demo:orb", regions: [] }], placements: [
    { name: "branch_orb", type: "demo:orb", room: "outer_1", instance: "branch", x: 8, y: 12 }
  ] };
  var rebuiltSymmetric = core.convertRoomGraphToMirrored(authoringDocument);
  assert(rebuiltSymmetric.layout.kind === core.LAYOUT_KIND && rebuiltSymmetric.layout.order[0] === "center" && rebuiltSymmetric.layout.order[1] === "outer_1",
    "a free layout with one independent instance per room design can rebuild the automatic symmetrical line");
  assert(rebuiltSymmetric.entities.placements[0].instance === undefined && rebuiltSymmetric.entities.placements[0].side === "both",
    "switching an independent free-layout room to Symmetrical makes its object placement mirror with the room");
  var withSideRoom = core.addConnectedRoomGraphNode(authoringDocument, {
    room: "center",
    from: "branch",
    fromSide: "right",
    id: "side room"
  });
  var sideNode = withSideRoom.layout.nodes[2];
  assert(sideNode.id === "side_room" && sideNode.x === 22 && sideNode.y === 18 &&
    withSideRoom.layout.connections[1].span === 7,
    "connected-room authoring derives a safe id, exact touching position and largest shared opening");
  assert(authoringDocument.layout.nodes.length === 2 && core.validateRoomGraph(withSideRoom).valid,
    "connected-room authoring is immutable and returns a fully valid graph");
  var keyboardRooms = [
    { id: "center", x: 0, y: 0, width: 10, height: 10 },
    { id: "right", x: 12, y: 0, width: 10, height: 10 },
    { id: "down", x: 0, y: 12, width: 10, height: 10 },
    { id: "far_right", x: 40, y: 0, width: 10, height: 10 }
  ];
  assert(core.nearestRoomGraphNode(keyboardRooms, keyboardRooms[0], "ArrowRight").id === "right" &&
    core.nearestRoomGraphNode(keyboardRooms, keyboardRooms[0], "ArrowDown").id === "down" &&
    core.nearestRoomGraphNode(keyboardRooms, keyboardRooms[0], "ArrowLeft") === null,
    "keyboard room navigation chooses the nearest placed room in the requested direction");
  var conversionConflict = core.deepClone(withSideRoom);
  conversionConflict.layout.nodes[2].mirrorX = true;
  conversionConflict.layout.nodes[2].overrides = { ambient: "none" };
  try {
    core.convertRoomGraphToMirrored(conversionConflict);
    assert(false, "a free layout with incompatible copy data must explain its refusal");
  } catch (conversionError) {
    assert(/side_room/.test(conversionError.message) && /horizontally flipped/.test(conversionError.message) &&
      /copy-only settings for ambient/.test(conversionError.message) && /Room design "center" is used by 2 placed rooms/.test(conversionError.message),
    "symmetrical conversion lists each named, actionable conflict instead of only the first generic failure");
  }
  var presentedSideRoom = core.setRoomGraphNodePresentation(withSideRoom, "side_room", { mirrorX: true, appearance: "mirror" });
  assert(presentedSideRoom.layout.nodes[2].mirrorX === true && presentedSideRoom.layout.nodes[2].appearance === "mirror" &&
    withSideRoom.layout.nodes[2].mirrorX === false,
    "placed-room geometry and color-bank choices update independently without mutating the source graph");
  var overriddenSideRoom = core.setRoomGraphNodeOverrides(withSideRoom, "side_room", { ambient: "bats", opponent_spawn: "never" });
  assert(overriddenSideRoom.layout.nodes[2].overrides.ambient === "bats" &&
    overriddenSideRoom.layout.nodes[2].overrides.opponent_spawn === "never" &&
    withSideRoom.layout.nodes[2].overrides === undefined,
    "placed-room ambience and opponent spawn overrides are instance-owned and immutable");
  var serializedOverrides = core.serializeRoomGraphLayout(overriddenSideRoom).nodes[2].overrides;
  assert(serializedOverrides.ambient === "bats" && serializedOverrides.opponent_spawn === "never",
    "placed-room overrides serialize inside their exact graph node");
  var centerRoom = withSideRoom.rooms.find(function (room) { return room.id === "center"; });
  var safeStart = null;
  var centerSize = core.roomDimensions(centerRoom);
  for (var sy = 1; sy < centerSize.height && !safeStart; sy += 1) {
    for (var sx = 1; sx < centerSize.width - 1; sx += 1) {
      if (core.isSafeSpawnFloor(centerRoom, sx, sy, withSideRoom.tileset)) {
        safeStart = { x: sx, y: sy, facing: "left" };
        break;
      }
    }
  }
  assert(safeStart, "graph source room has a safe floor for placed-copy starts");
  var placedStart = core.setRoomGraphNodeOverrides(withSideRoom, "center", {
    spawn: { players: { "1": safeStart }, markers: [{ x: safeStart.x, y: safeStart.y, kind: "allow" }] }
  });
  placedStart.format = core.FORMAT_V2;
  placedStart.rules.roundEndRooms = "any";
  var placedStartJson = core.serializeDataJson(placedStart);
  var placedStartRoundTrip = core.parsePackage(placedStartJson, core.serializeMapText(placedStart), { assets: placedStart.assets });
  assert(JSON.parse(placedStartJson).layout.nodes[0].overrides.spawn.players["1"].facing === "left" &&
    placedStartRoundTrip.valid,
    "placed-copy starts and respawn markers survive export and import: " + JSON.stringify(placedStartRoundTrip.errors || []));
  var badPlacedStart = core.deepClone(placedStart);
  badPlacedStart.layout.nodes[0].overrides.spawn.players["1"].y = centerSize.height;
  assert(hasIssue(core.validateRoomGraph(badPlacedStart), "spawn_point"),
    "placed-copy starts are validated against their source room geometry");
  var clearedSideRoom = core.setRoomGraphNodeOverrides(overriddenSideRoom, "side_room", { ambient: undefined, opponent_spawn: undefined });
  assert(clearedSideRoom.layout.nodes[2].overrides === undefined,
    "clearing every placed-room override restores inheritance without an empty manifest object");
  var invalidInstanceOverride = core.deepClone(withSideRoom);
  invalidInstanceOverride.layout.nodes[2].overrides = { opponent_spawn: "sometimes" };
  assert(hasIssue(core.validateRoomGraph(invalidInstanceOverride), "opponent_spawn"),
    "placed-room overrides use the same strict policy validation as source rooms");
  var narrowedDoorway = core.updateRoomGraphConnection(withSideRoom, 1, { fromOffset: 1, toOffset: 1, span: 4, oneWay: true, players: "player1", focus: "crossing" });
  assert(narrowedDoorway.layout.connections[1].fromOffset === 1 && narrowedDoorway.layout.connections[1].span === 4 &&
    narrowedDoorway.layout.connections[1].oneWay === true && narrowedDoorway.layout.connections[1].players === "player1" &&
    narrowedDoorway.layout.connections[1].focus === "crossing" && core.validateRoomGraph(narrowedDoorway).valid,
    "doorway geometry, direction, crossing player, and room focus are editable with validation");
  var verticalDoorwayGeometry = core.roomGraphDoorwayGeometry(narrowedDoorway, 1);
  assert(verticalDoorwayGeometry.x1 === 22 && verticalDoorwayGeometry.x2 === 22 &&
    verticalDoorwayGeometry.y1 === 19 && verticalDoorwayGeometry.y2 === 23 &&
    verticalDoorwayGeometry.centerY === 21 && verticalDoorwayGeometry.oneWay === true,
    "doorway geometry exposes the exact authored vertical opening for the layout canvas");
  var horizontalDoorwayGeometry = core.roomGraphDoorwayGeometry(authoringDocument, 0);
  assert(horizontalDoorwayGeometry.y1 === 18 && horizontalDoorwayGeometry.y2 === 18 &&
    horizontalDoorwayGeometry.x1 === 10 && horizontalDoorwayGeometry.x2 === 14,
    "doorway geometry exposes the exact authored horizontal opening for the layout canvas");
  var movedSideRoom = core.moveRoomGraphNode(withSideRoom, "side_room", 22, 20);
  assert(movedSideRoom.layout.nodes[2].y === 20 && movedSideRoom.layout.connections[1].fromOffset === 2 &&
    movedSideRoom.layout.connections[1].toOffset === 0 && movedSideRoom.layout.connections[1].span === 5 &&
    core.validateRoomGraph(movedSideRoom).valid,
    "moving a placed room along a connected edge recalculates the shared doorway");
  var movedPolicyRoom = core.moveRoomGraphNode(narrowedDoorway, "side_room", 22, 20);
  assert(movedPolicyRoom.layout.connections[1].players === "player1" && movedPolicyRoom.layout.connections[1].focus === "crossing",
    "automatic doorway geometry rebuilds preserve authored traversal controls");
  var detachedMoveRejected = false;
  var detachedRoom = core.moveRoomGraphNode(withSideRoom, "side_room", 80, 18);
  assert(detachedRoom.layout.nodes[2].x === 80 && detachedRoom.layout.connections.length === 1 &&
    hasIssue(core.validateRoomGraph(detachedRoom), "room_graph_unreachable") && withSideRoom.layout.nodes[2].x === 22,
    "free room movement removes stale adjacency, reports a disconnected map, and leaves the original graph untouched");
  var reattachedRoom = core.moveRoomGraphNode(detachedRoom, "side_room", 22, 18);
  assert(reattachedRoom.layout.connections.length === 3 && core.validateRoomGraph(reattachedRoom).valid,
    "every touching room edge creates its full automatic connection when a room is dragged back");
  try { core.moveRoomGraphNode(withSideRoom, "side_room", 11, 18); }
  catch (moveError) { detachedMoveRejected = /overlap/.test(moveError.message); }
  assert(detachedMoveRejected, "free room movement still rejects overlapping room rectangles");
  var resizedPlacedRoom = core.resizeRoomGraphRoom(withSideRoom, "outer_1", 16, 9);
  assert(core.roomDimensions(resizedPlacedRoom.rooms[1]).width === 16 &&
    resizedPlacedRoom.layout.nodes[2].x === 26 && core.validateRoomGraph(resizedPlacedRoom).valid,
    "placed room designs can resize and shift rooms beyond their old edge while automatic adjacency stays valid");
  var removedLeaf = core.removeRoomGraphNode(withSideRoom, "side_room");
  assert(removedLeaf.layout.nodes.length === 2 && removedLeaf.layout.connections.length === 1 &&
    core.validateRoomGraph(removedLeaf).valid,
    "a placed leaf room can be removed without deleting its reusable room design");
  var occupiedLeaf = core.deepClone(withSideRoom);
  occupiedLeaf.entities = { placements: [{ instance: "side_room" }] };
  var removedOccupiedLeaf = core.removeRoomGraphNode(occupiedLeaf, "side_room");
  assert(removedOccupiedLeaf.layout.nodes.length === 2 && removedOccupiedLeaf.entities.placements[0].instance === "side_room" &&
    hasIssue(core.validateRoomGraph(removedOccupiedLeaf), "room_graph_unknown_instance"),
    "removing an occupied room keeps its instance-scoped objects available for validation and reassignment");
  var articulationDocument = core.deepClone(withSideRoom);
  articulationDocument.entities.placements = [];
  var removedArticulation = core.removeRoomGraphNode(articulationDocument, "branch");
  assert(removedArticulation.layout.nodes.length === 2 && removedArticulation.layout.connections.length === 0 &&
    hasIssue(core.validateRoomGraph(removedArticulation), "room_graph_unreachable"),
    "removing an articulation room succeeds and leaves disconnected-room diagnostics to validation");
  var removedFromInvalidGraph = core.removeRoomGraphNode(removedArticulation, "side_room");
  assert(removedFromInvalidGraph.layout.nodes.length === 1 && core.validateRoomGraph(removedFromInvalidGraph).valid,
    "room deletion remains available while the current layout already has diagnostics");
  var removedStart = core.removeRoomGraphNode(withSideRoom, "center");
  assert(removedStart.layout.start === "branch" && removedStart.layout.nodes[0].id === "branch",
    "removing the start room deterministically selects the first remaining room as the new start");
  var lastRoomRemovalRejected = false;
  try { core.removeRoomGraphNode(removedFromInvalidGraph, "center"); }
  catch (removeError) { lastRoomRemovalRejected = /at least one/.test(removeError.message); }
  assert(lastRoomRemovalRejected, "room deletion keeps the one-room minimum required by the graph schema");
  var loopDocument = core.deepClone(graphSource);
  loopDocument.layout = {
    kind: core.ROOM_GRAPH_KIND, roomFormat: core.VARIABLE_ROOM_FORMAT, start: "a",
    nodes: [
      { id: "a", room: "outer_1", x: 0, y: 0 },
      { id: "b", room: "outer_1", x: 12, y: 0 },
      { id: "c", room: "outer_1", x: 0, y: 7 },
      { id: "d", room: "outer_1", x: 12, y: 7 }
    ],
    connections: [
      { from: "a", fromSide: "right", fromOffset: 0, to: "b", toSide: "left", toOffset: 0, span: 7, oneWay: false },
      { from: "a", fromSide: "bottom", fromOffset: 0, to: "c", toSide: "top", toOffset: 0, span: 12, oneWay: false },
      { from: "c", fromSide: "right", fromOffset: 0, to: "d", toSide: "left", toOffset: 0, span: 7, oneWay: false }
    ]
  };
  var loopOptions = core.roomGraphConnectionOptions(loopDocument, "b");
  assert(loopOptions.length === 1 && loopOptions[0].to === "d" && loopOptions[0].fromSide === "bottom" && loopOptions[0].span === 12,
    "layout authoring discovers an unconnected touching edge for loop creation");
  loopOptions[0].oneWay = true;
  var loopConnected = core.connectRoomGraphNodes(loopDocument, loopOptions[0]);
  assert(core.validateRoomGraph(loopConnected).valid && loopConnected.layout.connections[3].oneWay === true,
    "a discovered touching edge can close a valid one-way room loop");
  var loopReopened = core.disconnectRoomGraphNodes(loopConnected, "b", "d");
  assert(core.validateRoomGraph(loopReopened).valid && loopReopened.layout.connections.length === 3,
    "a redundant loop doorway can be removed without changing room placement");
  var requiredDoorwayRejected = false;
  try { core.disconnectRoomGraphNodes(loopDocument, "a", "b"); }
  catch (doorwayError) { requiredDoorwayRejected = /break the room layout/.test(doorwayError.message); }
  assert(requiredDoorwayRejected, "a doorway cannot be removed when it would make a placed room unreachable");
  var bidirectionalLoop = core.deepClone(loopConnected);
  bidirectionalLoop.layout.connections[3].oneWay = false;
  var stableDoorId = bidirectionalLoop.layout.connections[1].id;
  var renumberedLoop = core.disconnectRoomGraphConnection(bidirectionalLoop, 0);
  assert(stableDoorId && renumberedLoop.layout.connections[0].id === stableDoorId,
    "removing an earlier doorway changes its native number without retargeting its stable editor id");
  var serializedGraph = core.serializeRoomGraphLayout(withSideRoom);
  assert(serializedGraph.room_format === "variable_cells" &&
    serializedGraph.nodes[2].mirror_x === false &&
    /^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$/.test(serializedGraph.connections[1].id) &&
    serializedGraph.connections[1].from_side === "right" &&
    serializedGraph.connections[1].to_side === "left" &&
    serializedGraph.connections[1].one_way === false,
    "room graph serialization emits the canonical snake-case native manifest shape");
  var overlappingConnectionRejected = false;
  try {
    core.connectRoomGraphNodes(withSideRoom, {
      from: "branch", fromSide: "right", fromOffset: 0,
      to: "side_room", toSide: "left", toOffset: 0, span: 1
    });
  } catch (connectionError) {
    overlappingConnectionRejected = /Multiple connections overlap/.test(connectionError.message);
  }
  assert(overlappingConnectionRejected, "connection authoring rejects ambiguous edge ranges before changing the document");
  var duplicateDoorIds = core.ensureRoomGraphConnectionIds(loopConnected);
  duplicateDoorIds.layout.connections[1].id = duplicateDoorIds.layout.connections[0].id;
  assert(hasIssue(core.validateRoomGraph(duplicateDoorIds), "room_graph_duplicate_connection_id"),
    "room graph validation rejects duplicate stable doorway ids");
  var displacedGraph = core.deepClone(authoringDocument);
  displacedGraph.layout.nodes[1].x = 444;
  displacedGraph.layout.nodes[1].y = -222;
  var repairedGraph = core.autoArrangeRoomGraph(displacedGraph);
  assert(core.validateRoomGraph(repairedGraph).valid && repairedGraph.layout.nodes[0].x === 0 &&
    repairedGraph.layout.nodes[0].y === 0 && repairedGraph.layout.nodes[1].x === 10 &&
    repairedGraph.layout.nodes[1].y === 18 && displacedGraph.layout.nodes[1].x === 444,
    "arrangement repair derives exact room coordinates from doorway geometry without mutating its input");
  var displacedLoop = core.deepClone(loopConnected);
  displacedLoop.layout.nodes.forEach(function (node, index) { node.x = 900 - index * 71; node.y = -800 + index * 43; });
  var repairedLoop = core.autoArrangeRoomGraph(displacedLoop);
  assert(core.validateRoomGraph(repairedLoop).valid && repairedLoop.layout.nodes.every(function (node) { return node.x >= 0 && node.y >= 0; }),
    "arrangement repair solves branches and consistent cycles then normalizes them into positive canvas space");
  var inconsistentLoop = core.deepClone(loopConnected);
  inconsistentLoop.layout.connections[3].fromOffset = 1;
  inconsistentLoop.layout.connections[3].span = 11;
  var inconsistentLoopRejected = false;
  try { core.autoArrangeRoomGraph(inconsistentLoop); }
  catch (repairError) { inconsistentLoopRejected = /inconsistent room loop/.test(repairError.message); }
  assert(inconsistentLoopRejected && inconsistentLoop.layout.nodes[0].x === 0,
    "arrangement repair rejects contradictory cycles atomically instead of moving only part of the map");
  var disconnectedRepair = core.deepClone(authoringDocument);
  disconnectedRepair.layout.connections = [];
  var disconnectedRepairRejected = false;
  try { core.autoArrangeRoomGraph(disconnectedRepair); }
  catch (repairError) { disconnectedRepairRejected = /disconnected/.test(repairError.message); }
  assert(disconnectedRepairRejected, "arrangement repair explains when doorways do not connect every placed room");
  var invalidGraph = core.deepClone(verticalGraph);
  invalidGraph.nodes[1].y = 17;
  assert(hasIssue(core.validateRoomGraph(graphSource, invalidGraph), "room_graph_overlap"), "room graph validation rejects overlapping room rectangles");
  invalidGraph = core.deepClone(verticalGraph);
  invalidGraph.nodes[1].x = 11;
  assert(hasIssue(core.validateRoomGraph(graphSource, invalidGraph), "room_graph_doorway_alignment"), "room graph validation rejects misaligned doorway cells");
  invalidGraph = core.deepClone(verticalGraph);
  invalidGraph.connections[0].fromOffset = 46;
  invalidGraph.connections[0].span = 2;
  assert(hasIssue(core.validateRoomGraph(graphSource, invalidGraph), "room_graph_connection_range"), "room graph validation rejects openings outside a room edge");
  invalidGraph = core.deepClone(verticalGraph);
  invalidGraph.connections[0].players = "winner";
  assert(hasIssue(core.validateRoomGraph(graphSource, invalidGraph), "room_graph_players"), "room graph validation rejects unknown crossing-player policies");
  invalidGraph = core.deepClone(verticalGraph);
  invalidGraph.connections[0].focus = "camera";
  assert(hasIssue(core.validateRoomGraph(graphSource, invalidGraph), "room_graph_focus"), "room graph validation rejects unknown room-focus policies");
  invalidGraph = core.deepClone(verticalGraph);
  invalidGraph.connections = [];
  assert(hasIssue(core.validateRoomGraph(graphSource, invalidGraph), "room_graph_unreachable"), "room graph validation rejects disconnected instances");
  invalidGraph = core.deepClone(verticalGraph);
  invalidGraph.connections.push(core.deepClone(invalidGraph.connections[0]));
  assert(hasIssue(core.validateRoomGraph(graphSource, invalidGraph), "room_graph_ambiguous_exit"), "room graph validation rejects overlapping exits on the same edge cells");
  invalidGraph = core.deepClone(verticalGraph);
  invalidGraph.nodes[1].room = "missing";
  assert(hasIssue(core.validateRoomGraph(graphSource, invalidGraph), "room_graph_unknown_room"), "room graph validation rejects unknown source-room references");
  invalidGraph = core.deepClone(verticalGraph);
  invalidGraph.connections[0].oneWay = true;
  invalidGraph.connections[0].from = "branch";
  invalidGraph.connections[0].fromSide = "top";
  invalidGraph.connections[0].fromOffset = 0;
  invalidGraph.connections[0].to = "center";
  invalidGraph.connections[0].toSide = "bottom";
  invalidGraph.connections[0].toOffset = 10;
  assert(hasIssue(core.validateRoomGraph(graphSource, invalidGraph), "room_graph_unreachable"), "one-way connections participate in directed start reachability");

  var archive = core.buildPackageZip(fresh);
  var zipFiles = core.parseStoredZip(archive.bytes);
  assert(Object.keys(zipFiles).length === 2, "stored ZIP exporter round-trips two direct package files");
  var fromFiles = core.parsePackageFiles(zipFiles);
  assert(fromFiles.valid && fromFiles.document.name === fresh.name, "ZIP entries import through the package-file adapter");

  var badRule = core.deepClone(fresh);
  badRule.rules.roundEndRooms = "any_room";
  assert(hasIssue(core.validateDocument(badRule), "round_end_rooms"), "legacy any_room spelling is rejected");
  var classicWithoutGoal = core.deepClone(fresh);
  classicWithoutGoal.rooms.forEach(function (room) {
    room.grid.forEach(function (row) {
      for (var goalCol = 0; goalCol < row.length; goalCol += 1) if (row[goalCol] === "E" || row[goalCol] === "^") row[goalCol] = " ";
    });
  });
  assert(!hasIssue(core.validateDocument(classicWithoutGoal), "missing_inner_goal"), "classic route does not falsely require a center-room Eggnogg goal");
  var goalRequired = core.deepClone(classicWithoutGoal);
  goalRequired.rules.roundEndRooms = "any";
  assert(hasIssue(core.validateDocument(goalRequired), "missing_round_end_trigger"), "goal-required mode warns when no normal round-end trigger exists");
  goalRequired.rules.scoreTarget = 3;
  assert(!hasIssue(core.validateDocument(goalRequired), "missing_round_end_trigger"), "a score target is a valid round-end trigger in goal-required mode");
  assert(core.packageFacts(fresh).finalWidthPixels === 1584, "final arena pixel width follows (2n-1)*33*16");

  var previewUri = core.buildPreviewUri(fresh);
  assert(core.TILE_METADATA.i.frame === 0x3e,
    "crowd tile inspector reports its native backdrop frame");
  assert(core.base64UrlEncode(core.packPreviewBytes(core.utf8Bytes("{}"), core.utf8Bytes("xxxxxxxxxx"))) ===
    "R0dQMQIAAAAKAAAACHt9eABg",
    "preview compressor matches the native overlapping-match fixture");
  assert(previewUri.indexOf("yule://preview/v1z/") === 0 &&
    previewUri.indexOf("=") < 0 && previewUri.indexOf("%") < 0,
    "V1 preview compiles to a compact unpadded base64url launch link");
  var previewTarget = previewUri.slice("yule://preview/v1z/".length);
  assert(previewTarget.indexOf("/") < 0 && previewTarget.length < core.PREVIEW_TARGET_CAP && previewUri.length < 8000,
    "preview link contains one bounded compressed package payload");
  var nineRoomPreview = core.deepClone(fresh);
  while (nineRoomPreview.rooms.length < 9) {
    var previewRoomId = "outer_" + nineRoomPreview.rooms.length;
    nineRoomPreview.rooms.push(core.createRoom(previewRoomId));
    nineRoomPreview.layout.order.push(previewRoomId);
  }
  assert(core.buildPreviewUri(nineRoomPreview).length < 8000,
    "maximum-room ordinary maps remain within the reliable protocol-link budget");
  var previewV2Rejected = false;
  try { core.buildPreviewUri(core.upgradeToV2(fresh)); } catch (previewError) { previewV2Rejected = true; }
  assert(previewV2Rejected, "preview URI refuses V2 packages at the authoring boundary");

  var authoredV2 = core.upgradeToV2(fresh);
  authoredV2.tileset.tiles.push({ id: "painted", symbol: ">", name: "Painted", sprite_sheet: "painted.png", sprite_index: 0, cell_w: 16, cell_h: 16, padding: 0, collision: "pass_through" });
  authoredV2.assets = { "painted.png": tinyPng };
  authoredV2.mapLuaPresent = true;
  authoredV2.mapLua = "map.on_tick(function() map.state.t = map.tick() end)\n";
  authoredV2.rooms[0].grid[5][5] = ">";
  var authoredValidation = core.validateDocument(authoredV2);
  assert(authoredValidation.valid, "generated editable V2 projects validate custom PNG tiles and map.lua together");
  var authoredFiles = core.exportProjectFiles(authoredV2);
  assert(bytesEqual(authoredFiles["painted.png"], tinyPng) && authoredFiles["map.lua"] === authoredV2.mapLua, "generated V2 export includes package-owned PNGs and map.lua");
  var authoredZip = core.parseStoredZip(core.buildPackageZip(authoredV2).bytes);
  assert(bytesEqual(authoredZip[authoredV2.id + "/painted.png"], tinyPng) && bytesEqual(authoredZip[authoredV2.id + "/map.lua"], core.utf8Bytes(authoredV2.mapLua)), "generated V2 assets and script survive ZIP packaging");

  var ambianceDoc = core.deepClone(authoredV2);
  ambianceDoc.particles = [{
    id: "snowflake", name: "Snowflake", lifetime_ticks: 180, fade_in_ticks: 10, fade_out_ticks: 20,
    visual: { sprite_sheet: "builtin:misc", sprite_index: 0, frame_count: 2, frame_ticks: 4, tint: [0.8, 0.9, 1, 0.75], end_tint: [0.2, 0.4, 1, 0], scale_x: 1.25, scale_y: 1.25, end_scale_x: 0, end_scale_y: 2, start_rotation: -45, end_rotation: 180, interpolation: "ease_in_out" }
  }];
  ambianceDoc.ambiances = [{
    id: "snow", name: "Snow", native_ambient: "dust", emitters: [{
      particle: "snowflake", count: 32, area: { x: 0, y: -16, width: 528, height: 208 },
      velocity_x: { min: -0.2, max: 0.2 }, velocity_y: { min: 0.3, max: 0.7 },
      acceleration_x: 0, acceleration_y: 0.001, rotation_speed: { min: -1, max: 1 },
      particle_layer: 1, blend: "alpha", mirror_with_room: true, shape: "ellipse", motion_interpolation: "ease_in"
    }]
  }];
  ambianceDoc.rooms[0].ambient = "snow";
  var ambianceValidation = core.validateDocument(ambianceDoc);
  assert(ambianceValidation.valid, "editable V2 custom particles and room ambiance validate together");
  var ambianceJson = core.serializeDataJson(ambianceDoc);
  var ambianceData = JSON.parse(ambianceJson);
  assert(ambianceData.particles[0].id === "snowflake" && ambianceData.ambiances[0].emitters[0].count === 32 &&
    ambianceData.ambiances[0].emitters[0].shape === "ellipse" &&
    ambianceData.ambiances[0].emitters[0].motion_interpolation === "ease_in",
    "particle shapes, motion curves, and ambiance catalogs serialize into data.json");
  var ambianceRoundTrip = core.parsePackage(ambianceJson, core.serializeMapText(ambianceDoc), { assets: ambianceDoc.assets });
  assert(ambianceRoundTrip.valid && ambianceRoundTrip.document.rooms[0].ambient === "snow", "custom room ambiance imports and resolves its declared id");
  ambianceDoc.ambiances[0].emitters[0].shape = "triangle";
  assert(hasIssue(core.validateDocument(ambianceDoc), "emitter_shape"), "unknown particle emitter shape rejects");
  ambianceDoc.ambiances[0].emitters[0].shape = "line";
  assert(core.validateDocument(ambianceDoc).valid, "line particle emitters remain valid after editing");
  ambianceDoc.ambiances[0].emitters[0].motion_interpolation = "bounce";
  assert(hasIssue(core.validateDocument(ambianceDoc), "emitter_motion_interpolation"), "unknown emitter motion curve rejects");
  ambianceDoc.ambiances[0].emitters[0].motion_interpolation = "ease_in";
  ambianceDoc.particles[0].visual.interpolation = "bounce";
  assert(hasIssue(core.validateDocument(ambianceDoc), "particle_interpolation"), "unknown particle interpolation rejects");
  ambianceDoc.particles[0].visual.interpolation = "ease_in_out";
  ambianceDoc.particles[0].visual.end_tint = [1, 2, 1, 1];
  assert(hasIssue(core.validateDocument(ambianceDoc), "particle_end_tint"), "ending particle colors validate every channel");
  ambianceDoc.particles[0].visual.end_tint = [0.2, 0.4, 1, 0];
  ambianceDoc.particles[0].visual.sprite_index = 63;
  ambianceDoc.particles[0].visual.frame_count = 2;
  assert(hasIssue(core.validateDocument(ambianceDoc), "particle_sprite_range"), "built-in particle animation ranges cannot exceed their sprite sheet");
  ambianceDoc.particles[0].visual.sprite_index = 0;
  ambianceDoc.particles[0].visual.frame_count = 2;
  ambianceDoc.ambiances[0].emitters[0].particle = "missing";
  assert(hasIssue(core.validateDocument(ambianceDoc), "emitter_particle"), "unknown emitter particle references fail authoring validation");
  ambianceDoc.ambiances[0].emitters[0].particle = "snowflake";
  ambianceDoc.ambiances[0].emitters[0].count = 513;
  assert(!core.validateDocument(ambianceDoc).valid, "per-emitter particle lane limits are enforced");
  ambianceDoc.ambiances[0].emitters[0].count = 32;
  ambianceDoc.rooms[0].ambient = "undeclared_weather";
  assert(hasIssue(core.validateDocument(ambianceDoc), "ambient"), "room settings reject an undeclared custom ambiance id");

  var forcedGoal = core.deepClone(fresh);
  assert(JSON.parse(core.serializeDataJson(forcedGoal)).rules.eggnogg_color === undefined, "automatic goal color remains omitted");
  forcedGoal.rules.eggnoggColor = [0, 0.123456789, 1];
  var forcedJson = core.serializeDataJson(forcedGoal);
  var forcedParsed = core.parsePackage(forcedJson, core.serializeMapText(forcedGoal));
  assert(forcedParsed.valid, "forced goal color package validates");
  assert(JSON.stringify(forcedParsed.document.rules.eggnoggColor) === "[0,0.123456789,1]", "forced goal RGB round-trips without quantization");
  assert(core.serializeDataJson(forcedParsed.document) === forcedJson, "forced color manifest round-trips canonically");
  [null, "#ffffff", [0, 1], [0, 0, 0, 1], [-0.1, 0, 0], [1.1, 0, 0], ["0", 0, 0], [NaN, 0, 0]].forEach(function (invalid) {
    forcedGoal.rules.eggnoggColor = invalid;
    assert(!core.validateDocument(forcedGoal).valid, "invalid authored goal color rejected: " + String(invalid));
    var invalidManifest = JSON.parse(forcedJson);
    invalidManifest.rules.eggnogg_color = invalid;
    assert(!core.parsePackage(JSON.stringify(invalidManifest), core.serializeMapText(forcedGoal)).valid, "invalid imported goal color rejected: " + String(invalid));
  });
  delete forcedGoal.rules.eggnoggColor;
  assert(JSON.parse(core.serializeDataJson(forcedGoal)).rules.eggnogg_color === undefined, "resetting automatic removes manifest override");

  var spawnDoc = core.deepClone(fresh);
  var spawnId = spawnDoc.rooms[0].id;
  spawnDoc.defaults.opponent_spawn = "always";
  var spawnMap = core.serializeMapText(spawnDoc);
  var spawnJson = core.serializeDataJson(spawnDoc);
  var spawnParsed = core.parsePackage(spawnJson, spawnMap);
  assert(spawnParsed.valid, "opponent spawn defaults import successfully");
  assert(spawnParsed.document.defaults.opponent_spawn === "always", "map spawn default preserved");
  assert(!Object.prototype.hasOwnProperty.call(spawnParsed.document.rooms[0], "opponent_spawn"), "absent room override retains inheritance");
  assert(core.serializeDataJson(spawnParsed.document) === spawnJson, "inherited spawn policy canonical roundtrip");
  ["default", "always", "never"].forEach(function (policy) {
    spawnDoc.rooms[0].opponent_spawn = policy;
    var serialized = core.serializeDataJson(spawnDoc);
    var parsed = core.parsePackage(serialized, spawnMap);
    assert(parsed.valid && parsed.document.rooms[0].opponent_spawn === policy, "explicit spawn policy retained: " + policy);
    assert(JSON.parse(serialized).rooms[spawnId].opponent_spawn === policy, "explicit native overrides map default: " + policy);
    assert(core.serializeDataJson(parsed.document) === serialized, "spawn override canonical roundtrip: " + policy);
  });
  [null, true, 0, "", "ALWAYS", "inherit", [], {}].forEach(function (invalid) {
    spawnDoc.rooms[0].opponent_spawn = invalid;
    assert(!core.validateDocument(spawnDoc).valid, "invalid authored room spawn rejected");
    var raw = JSON.parse(spawnJson);
    raw.rooms[spawnId].opponent_spawn = invalid;
    var result = core.parsePackage(JSON.stringify(raw), spawnMap);
    assert(!result.valid && result.errors.some(function (e) { return e.path === "rooms." + spawnId + ".opponent_spawn"; }), "invalid imported room spawn has precise diagnostic");
    delete spawnDoc.rooms[0].opponent_spawn;
    spawnDoc.defaults.opponent_spawn = invalid;
    assert(!core.validateDocument(spawnDoc).valid, "invalid authored default spawn rejected");
    raw = JSON.parse(spawnJson);
    raw.defaults.room.opponent_spawn = invalid;
    assert(!core.parsePackage(JSON.stringify(raw), spawnMap).valid, "invalid imported default spawn rejected");
  });
  delete spawnDoc.defaults.opponent_spawn;
  assert(JSON.parse(core.serializeDataJson(spawnDoc)).defaults.room.opponent_spawn === undefined, "legacy maps omit spawn policy");
  spawnDoc.defaults.opponent_spawn = "default";
  spawnDoc.rooms[1].opponent_spawn = "default";
  var spawnZipFiles = core.parseStoredZip(core.buildPackageZip(spawnDoc).bytes);
  var spawnZipParsed = core.parsePackageFiles(spawnZipFiles);
  assert(spawnZipParsed.valid &&
    core.resolveOpponentSpawn(spawnZipParsed.document, spawnDoc.rooms[1].id).policy ===
      core.resolveOpponentSpawn(spawnDoc, spawnDoc.rooms[1].id).policy,
    "exported ZIP and editor preview resolve the same outer-room opponent policy");

  var selectedTiles = [["a", "b", "c"], ["d", "e", "f"]];
  assert(JSON.stringify(core.transformTileArea(selectedTiles, "flip_x")) === '[["c","b","a"],["f","e","d"]]', "selection flips horizontally");
  assert(JSON.stringify(core.transformTileArea(selectedTiles, "flip_y")) === '[["d","e","f"],["a","b","c"]]', "selection flips vertically");
  assert(JSON.stringify(core.transformTileArea(selectedTiles, "rotate_cw")) === '[["d","a"],["e","b"],["f","c"]]', "selection rotates clockwise and swaps dimensions");
  var selectionGrid = core.createBlankGrid();
  var pastedSelection = core.pasteTileArea(selectionGrid, selectedTiles, 2, 4);
  assert(pastedSelection[2][4] === "a" && pastedSelection[3][6] === "f" && selectionGrid[2][4] === " ", "selection paste is bounded and leaves its input detached");
  var rejectedSelection = false;
  try { core.pasteTileArea(selectionGrid, selectedTiles, 11, 31); } catch (selectionError) { rejectedSelection = true; }
  assert(rejectedSelection, "selection paste rejects room overflow");
  rejectedSelection = false;
  try { core.transformTileArea([["a"], ["b", "c"]], "flip_x"); } catch (selectionError2) { rejectedSelection = true; }
  assert(rejectedSelection, "selection transform rejects ragged input");

  var variableDoc = core.deepClone(fresh);
  variableDoc.layout.roomFormat = core.VARIABLE_ROOM_FORMAT;
  core.resizeRoom(variableDoc.rooms[0], 47, 18);
  core.resizeRoom(variableDoc.rooms[1], 12, 7);
  variableDoc.rooms[0].grid[16] = new Array(47).fill("@");
  variableDoc.rooms[0].grid[17] = new Array(47).fill("@");
  variableDoc.rooms[0].spawn = {
    players: { "1": { x: 4, y: 16, facing: "right" }, "2": { x: 42, y: 16, facing: "left" } },
    markers: [{ x: 4, y: 16, kind: "allow_p1" }, { x: 42, y: 16, kind: "allow_p2" }, { x: 20, y: 16, kind: "deny" }]
  };
  var variableMap = core.serializeMapText(variableDoc);
  var variableJson = core.serializeDataJson(variableDoc);
  var variableParsed = core.parsePackage(variableJson, variableMap);
  assert(variableParsed.valid, "mixed room dimensions and spawn overlays validate");
  assert(variableParsed.document.rooms[0].grid.length === 18 && variableParsed.document.rooms[0].grid[0].length === 47, "large room dimensions round-trip");
  assert(variableParsed.document.rooms[1].grid.length === 7 && variableParsed.document.rooms[1].grid[0].length === 12, "small room dimensions round-trip");
  assert(variableParsed.document.rooms[0].spawn.markers.length === 3, "spawn markers round-trip outside tile data");
  assert(core.packageFacts(variableDoc).finalWidthCells === 71 && core.packageFacts(variableDoc).heightCells === 18, "mixed-size final arena facts use per-room dimensions");
  core.resizeRoom(variableDoc.rooms[0], 40, 12);
  assert(variableDoc.rooms[0].spawn.players["1"].y === 10 && !variableDoc.rooms[0].spawn.players["2"], "bottom-left resize translates and clips spawn points");
  var invalidVariable = core.deepClone(variableParsed.document);
  invalidVariable.rooms[0].spawn.markers.push({ x: 999, y: 1, kind: "allow" });
  assert(hasIssue(core.validateDocument(invalidVariable), "spawn_marker"), "out-of-bounds respawn marker is rejected");
  invalidVariable = core.deepClone(variableParsed.document);
  invalidVariable.rooms[0].spawn.markers = Array.from({ length: 129 }, function (_, index) { return { x: index % 47, y: Math.floor(index / 47), kind: "deny" }; });
  assert(hasIssue(core.validateDocument(invalidVariable), "spawn_marker_limit"), "spawn marker runtime limit is diagnosed");
  invalidVariable = core.deepClone(variableParsed.document);
  invalidVariable.rooms[0].spawn.markers[0].y = 15;
  assert(hasIssue(core.validateDocument(invalidVariable), "spawn_floor"), "spawn markers on empty cells are rejected");
  invalidVariable = core.deepClone(variableParsed.document);
  invalidVariable.rooms[0].grid[15][4] = "_";
  assert(hasIssue(core.validateDocument(invalidVariable), "spawn_floor"), "spawn markers below solid ceilings are rejected");
  invalidVariable = core.deepClone(variableParsed.document);
  invalidVariable.rooms[0].grid[15][4] = "K";
  assert(hasIssue(core.validateDocument(invalidVariable), "spawn_floor"), "spawn markers below native physics-hazard spawners are rejected");

  var message = "Greggnogg editor-core tests passed: " + passed;
  if (typeof WScript !== "undefined") WScript.Echo(message);
  else if (typeof console !== "undefined") console.log(message);
}());
