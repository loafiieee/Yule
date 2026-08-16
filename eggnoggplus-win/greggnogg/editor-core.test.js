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
    if (!Array.isArray) Array.isArray = function (value) { return Object.prototype.toString.call(value) === "[object Array]"; };
    if (!Array.prototype.forEach) Array.prototype.forEach = function (callback) { for (var i = 0; i < this.length; i += 1) if (i in this) callback(this[i], i, this); };
    if (!Array.prototype.map) Array.prototype.map = function (callback) { var out = []; for (var i = 0; i < this.length; i += 1) if (i in this) out[i] = callback(this[i], i, this); return out; };
    if (!Array.prototype.filter) Array.prototype.filter = function (callback) { var out = []; for (var i = 0; i < this.length; i += 1) if (i in this && callback(this[i], i, this)) out.push(this[i]); return out; };
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
  if (typeof module === "object" && module.exports && typeof require === "function") {
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
  var archive = core.buildPackageZip(fresh);
  var zipFiles = core.parseStoredZip(archive.bytes);
  assert(Object.keys(zipFiles).length === 2, "stored ZIP exporter round-trips two direct package files");
  var fromFiles = core.parsePackageFiles(zipFiles);
  assert(fromFiles.valid && fromFiles.document.name === fresh.name, "ZIP entries import through the package-file adapter");

  var badRule = core.deepClone(fresh);
  badRule.rules.roundEndRooms = "any_room";
  assert(hasIssue(core.validateDocument(badRule), "round_end_rooms"), "legacy any_room spelling is rejected");
  assert(core.packageFacts(fresh).finalWidthPixels === 1584, "final arena pixel width follows (2n-1)*33*16");

  var message = "Greggnogg editor-core tests passed: " + passed;
  if (typeof WScript !== "undefined") WScript.Echo(message);
  else if (typeof console !== "undefined") console.log(message);
}());
