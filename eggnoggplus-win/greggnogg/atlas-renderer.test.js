/* Dependency-free structural tests. Run with:
 *   cscript //nologo greggnogg\atlas-renderer.test.js
 */

if (!Array.prototype.fill) {
  Array.prototype.fill = function (value) {
    var i;
    for (i = 0; i < this.length; i += 1) this[i] = value;
    return this;
  };
}

if (!Array.prototype.forEach) {
  Array.prototype.forEach = function (callback, thisArg) {
    var i;
    for (i = 0; i < this.length; i += 1) {
      if (i in this) callback.call(thisArg, this[i], i, this);
    }
  };
}

if (!Array.isArray) {
  Array.isArray = function (value) { return value instanceof Array; };
}

function fail(message) {
  throw new Error(message);
}

function assert(condition, message) {
  if (!condition) fail(message);
}

function assertEqual(actual, expected, message) {
  if (actual !== expected) {
    fail(message + " (expected " + expected + ", got " + actual + ")");
  }
}

function assertNear(actual, expected, epsilon, message) {
  if (Math.abs(actual - expected) > epsilon) {
    fail(message + " (expected " + expected + ", got " + actual + ")");
  }
}

function blankRoom() {
  var rows = [];
  var y;
  var x;
  for (y = 0; y < GregAtlas.ROWS; y += 1) {
    rows[y] = [];
    for (x = 0; x < GregAtlas.COLS; x += 1) rows[y][x] = " ";
  }
  return rows;
}

function loadRenderer() {
  var fso = new ActiveXObject("Scripting.FileSystemObject");
  var directory = fso.GetParentFolderName(WScript.ScriptFullName);
  var path = fso.BuildPath(directory, "atlas-renderer.js");
  var stream = fso.OpenTextFile(path, 1, false);
  var source = stream.ReadAll();
  stream.Close();
  eval(source);
}

function testExportedApi() {
  assert(typeof GregAtlas === "object", "GregAtlas export is missing");
  assertEqual(GregAtlas.COLS, 33, "room width constant");
  assertEqual(GregAtlas.ROWS, 12, "room height constant");
  assertEqual(GregAtlas.CELL, 16, "native cell-size constant");
  assertEqual(GregAtlas.MAP_DRAW_ORDER.join(","), "1,0,-1", "Ghidra map-bank draw order");
  assertEqual(GregAtlas.PREVIEW_DRAW_STAGES.join(","),
    "map:1,particle:4,particle:3,particle:2,map:0,particle:1,actors,map:-1,particle:0",
    "full native map, particle, actor and foreground schedule");
  assert(typeof GregAtlas.loadAssets === "function", "loadAssets export");
  assert(typeof GregAtlas.renderRoom === "function", "renderRoom export");
  assert(typeof GregAtlas.renderGlyph === "function", "renderGlyph export");
  assert(typeof GregAtlas.drawGlyph === "function", "drawGlyph export");
  assert(typeof GregAtlas._buildCells === "function", "structural test hook");
  assert(typeof GregAtlas._nativeSeedByte === "function", "terrain-seed test hook");
  assert(typeof GregAtlas._nativeWallFlip === "function", "automatic-wall flip test hook");
  assert(typeof GregAtlas._nativeWaterTransform === "function", "water-transform test hook");
  assert(typeof GregAtlas._nativeCompositeFlips === "function", "composite-flip test hook");
  assert(typeof GregAtlas._nativeChandelierGeometry === "function", "chandelier-geometry test hook");
  assert(typeof GregAtlas._nativeChandelierGlow === "function", "chandelier-glow test hook");
  assert(typeof GregAtlas._nativeSpinnerAlphaBytes === "function", "spinner-alpha test hook");
  assert(typeof GregAtlas._nativeSunPosition === "function", "sun-position test hook");
  assert(typeof GregAtlas._previewEnemyColour === "function", "goal-preview contrast test hook");
}

function testGoalPreviewContrast() {
  var neutral = { bg1: [0.5, 0.5, 0.5, 1], bg2: [0.5, 0.5, 0.5, 1] };
  var goal = GregAtlas._previewEnemyColour(neutral);
  assert(goal && (Math.abs(goal[0] - 0.5) > 0.2 || Math.abs(goal[1] - 0.5) > 0.2 || Math.abs(goal[2] - 0.5) > 0.2),
    "E and ^ fallback colour cannot blend into the neutral default background");
}

function testNativeTerrainSeed() {
  assertEqual(GregAtlas._nativeSeedByte(0, 0, {}), 0x39, "native terrain seed at room origin");
  assertEqual(GregAtlas._nativeSeedByte(1, 0, {}), 0x14, "native terrain seed advances 45 per column");
  assertEqual(GregAtlas._nativeSeedByte(0, 1, {}), 0x04, "native terrain seed advances 61 per row");
  assertEqual(GregAtlas._nativeSeedByte(1, 1, {}), 0x53, "native terrain seed combines row and column before XOR");
  assertEqual(GregAtlas._nativeSeedByte(0, 0, { worldRoomIndex: 1 }), 0xf4,
    "native terrain seed includes the 33-column destination-room offset");
  assertEqual(GregAtlas._nativeWorldX(0, { worldRoomIndex: 2, mirrored: true }), 98,
    "mirrored preview uses the right-side room and reversed destination column");
  assertEqual(GregAtlas._nativeWallFlip(0x01), true, "+1 wall byte is flipped after colouring_action");
  assertEqual(GregAtlas._nativeWallFlip(0xff), false, "-1 wall byte cancels colouring_action's parity flip");
}

function testRecoveredFrameRecipes() {
  var recipes = GregAtlas.NATIVE_FRAME_RECIPES;
  assert(recipes && typeof recipes === "object", "native frame recipes export");
  assertEqual(recipes.floor.frames.join(","), "1,3", "floor uses persisted native base/accent frames");
  assertEqual(recipes.wall.frames.join(","), "2", "automatic wall retains native default frame");
  assertEqual(recipes.ceiling.frames.join(","), "2,34", "ceiling uses native base/lip frames");
  assertEqual(recipes["ground-spikes"].frames.join(","), "1,3,4", "ground-spike composite frames");
  assertEqual(recipes["hanging-spikes"].frames.join(","), "2,34,52", "hanging-spike composite frames");
  assertEqual(recipes.mine.frames.join(","), "1,3,53", "inactive mine composite frames");
  assertEqual(recipes["deep-water"].frames[0], 0x65, "Yule high water uses tintable water frame");
  assertEqual(recipes["deep-water"].colour, "water_hi", "Yule high water uses highlight colour");
  assertEqual(recipes.waterfall.frames.join(","), "96,97,98,99", "waterfall overlay and cap frames");
  assertEqual(recipes["spikeball-marker"].frames[0], 0x6d, "K uses native tiles frame");
  assertEqual(recipes.sun.sheet, "misc", "sun comes from misc atlas");
  assertEqual(recipes.sun.frames[0], 0x11, "sun misc frame");
  assertEqual(recipes.spinny.frames[0], 0x16, "spinner retains tile definition frame 0x16");
  assertEqual(recipes.chandelier.frames.join(","), "25,24,32,33,21",
    "chandelier action offsets are relative to persisted frame 0x18");
  assertEqual(recipes["scrolling-decor"].frames[0], 0x2e, "scrolling decor source frame");
  assertEqual(GregAtlas._nativeSpinnerAlphaBytes().join(","), "38,147,255,107",
    "spinner preserves quad_batch's unchecked alpha-byte conversion");
}

function testPersistedNativeFrameBytes() {
  var grid = blankRoom();
  var cells;
  grid[1][1] = "!";
  grid[1][2] = "_";
  grid[1][3] = "X";
  grid[1][4] = "m";
  grid[1][5] = "A";
  grid[1][6] = "C";
  grid[1][7] = "c";
  grid[1][8] = "P";
  grid[1][9] = "E";
  grid[1][10] = "^";
  grid[7][14] = "@";
  grid[8][14] = "@";
  grid[9][14] = "@";
  cells = GregAtlas._buildCells(grid, {});

  assertEqual(cells[1][1].nativeFrameByte, 0x01, "! retains floor default frame");
  assertEqual(cells[1][2].nativeFrameByte, 0x02, "_ retains ceiling default frame");
  assertEqual(cells[1][3].nativeFrameByte, 0x01, "X retains floor default frame");
  assertEqual(cells[1][4].nativeFrameByte, 0x01, "m retains floor default frame");
  assertEqual(cells[1][5].nativeFrameByte, 0x16, "A retains spinny default frame");
  assertEqual(cells[1][6].nativeFrameByte, 0x18, "C retains chandelier default frame");
  assertEqual(cells[1][7].nativeFrameByte, 0x18, "c retains chandelier default frame");
  assertEqual(cells[1][8].nativeFrameByte, 0x08, "P retains puzzley default frame");
  assertEqual(cells[1][9].nativeFrameByte, 0x65, "E retains goal default frame");
  assertEqual(cells[1][10].nativeFrameByte, 0x65, "^ retains goal default frame");
  assertEqual(cells[7][14].nativeFrameByte, 0x01, "automatic floor retains frame 1");
  assertEqual(cells[8][14].nativeFrameByte, 0x02, "automatic wall retains frame 2");
  assertEqual(cells[9][14].nativeFrameByte, 0x02, "automatic ceiling retains frame 2");
}

function testReportedTileLayersAndParity() {
  var grid = blankRoom();
  var cells;
  grid[2][1] = "W";
  grid[2][2] = "w";
  grid[2][3] = "X";
  grid[2][4] = "m";
  grid[2][5] = "v";
  grid[2][6] = "C";
  grid[2][7] = "O";
  grid[2][11] = "i";
  grid[4][8] = "~";
  grid[5][8] = "~";
  grid[6][9] = "f";
  grid[6][10] = "K";
  grid[6][12] = "*";
  cells = GregAtlas._buildCells(grid, {});

  assertEqual(cells[2][1].kind, "deep-water", "W recipe kind");
  assertEqual(cells[2][1].layer, -1, "high water draws in foreground bank");
  assertEqual(cells[2][2].layer, 0, "shallow water stays in normal map bank");
  assert(cells[2][3].solid && cells[2][4].solid && cells[2][5].solid,
    "spikes and mines retain native solid collision");
  assertEqual(cells[2][3].kind, "ground-spikes", "X recipe kind");
  assertEqual(cells[2][4].kind, "mine", "m recipe kind");
  assertEqual(cells[2][5].kind, "hanging-spikes", "v recipe kind");
  assertEqual(cells[2][6].layer, -1, "chandelier draws in foreground bank");
  assertEqual(cells[2][7].kind, "sun", "O is the native sun effect");
  assertEqual(cells[2][7].layer, "particle3", "sun renders in native particle layer 3");
  assertEqual(cells[2][11].layer, 1, "crowd uses its recovered background draw mode");
  assertEqual(cells[4][8].nativeParityFlip, false, "even waterfall row keeps native orientation");
  assertEqual(cells[5][8].nativeParityFlip, true, "odd waterfall row receives native parity flip");
  assertEqual(cells[6][9].kind, "scrolling-decor", "f uses scrolling crop recipe");
  assertEqual(cells[6][10].kind, "spikeball-marker", "K uses spikeball marker recipe");
  assertEqual(cells[6][10].layer, "actor", "K preview ghost uses the native actor stage");
  assertEqual(cells[6][12].layer, "actor", "sword preview ghost uses the native actor stage");
}

function testNativeWaterTransforms() {
  var shallowPeak = GregAtlas._nativeWaterTransform("shallow-water", 15, 0);
  var shallowTrough = GregAtlas._nativeWaterTransform("shallow-water", 45, 0);
  var staggered = GregAtlas._nativeWaterTransform("shallow-water", 0, 3);
  var deep = GregAtlas._nativeWaterTransform("deep-water", 30, 0);

  assertNear(shallowPeak.offsetY, 0, 0.000001, "shallow water native peak offset");
  assertNear(shallowTrough.offsetY, 8, 0.000001, "shallow water native trough offset");
  assertNear(staggered.offsetY, 0, 0.000001, "shallow water phase staggers by 30 degrees per column");
  assertEqual(shallowPeak.scaleY, 2, "shallow water native vertical scale");
  assertNear(deep.scaleY, 6.5, 0.000001, "high-water native 3+a+4b scale with six-tick horizontal phase");
}

function testNativeCompositeFlips() {
  var even = GregAtlas._nativeCompositeFlips(0);
  var odd = GregAtlas._nativeCompositeFlips(1);
  assertEqual(even.base, false, "floor/ceiling base never receives byte2 flip");
  assertEqual(even.accent, true, "even byte2 flips frame3/frame22");
  assertEqual(even.tip, true, "hanging-spike tip inherits the ceiling-lip flip");
  assertEqual(odd.accent, false, "odd byte2 leaves frame3/frame22 unflipped");
  assertEqual(odd.tip, false, "unflipped ceiling lip leaves the hanging tip unflipped");
}

function testNativeChandelierAndSun() {
  var geometry = GregAtlas._nativeChandelierGeometry(9, 0, 30, 100, 100);
  var glow = GregAtlas._nativeChandelierGlow(0, 0);
  var roomSun = GregAtlas._nativeSunPosition(528, 192, false, 16, 32);
  var glyphSun = GregAtlas._nativeSunPosition(528, 192, true, 16, 32);

  assert(geometry.angleDegrees > 0, "fixture produces a positive chandelier swing");
  assert(geometry.p1.x < 100, "positive swing moves the first chain segment toward negative canvas X");
  assert(geometry.bulb.x < geometry.p1.x, "later chain segments continue along the same native ray");
  assert(geometry.flare.x < geometry.bulb.x, "turtle_move(-4) advances the flare down the rope");
  assertNear(glow.q, 0.70875, 0.000001, "deterministic chandelier glow uses mean native random term");
  assertNear(glow.bulb[0], 1, 0.000001, "chandelier frame-0x20 native red channel");
  assertNear(glow.bulb[1], 0.19140625, 0.000001, "chandelier frame-0x20 native green channel");
  assertNear(glow.bulb[2], 0.19140625, 0.000001, "chandelier frame-0x20 native blue channel");
  assertNear(glow.flare[0], 0.29125, 0.000001, "chandelier frame-0x21 native red attenuation");
  assertNear(glow.flare[1], 0.041810302734375, 0.000001, "chandelier frame-0x21 native green attenuation");
  assertNear(glow.flare[2], 0.0209051513671875, 0.000001, "chandelier frame-0x21 native blue attenuation");
  assertEqual(roomSun.x, 256, "room sun is centred horizontally, independent of O marker");
  assertEqual(roomSun.y, 40, "room sun is centred at one-quarter canvas height");
  assertEqual(glyphSun.x, 16, "glyph preview keeps a locally centred sun icon");
  assertEqual(glyphSun.y, 32, "glyph preview keeps the requested local Y");
}

function testAutomaticTerrain() {
  var grid = blankRoom();
  var cells;
  grid[0][2] = "@";
  grid[7][16] = "@";
  grid[8][16] = "@";
  grid[9][16] = "@";
  grid[10][28] = "@";
  grid[11][28] = "@";
  cells = GregAtlas._buildCells(grid, {});

  assertEqual(cells[0][2].kind, "ceiling",
    "top-edge @ treats out-of-map support as solid, then gains an exposed bottom lip");
  assertEqual(cells[7][16].kind, "floor", "first @ becomes a floor");
  assertEqual(cells[8][16].kind, "auto-wall", "supported interior @ becomes a wall");
  assertEqual(cells[9][16].kind, "ceiling", "exposed wall bottom gains the ceiling lip");
  assertEqual(cells[10][28].kind, "floor", "penultimate-row unsupported @ becomes a floor");
  assertEqual(cells[11][28].kind, "auto-wall",
    "bottom-edge @ treats the below-map lookup as solid and remains a wall");
  assert(cells[7][16].solid && cells[8][16].solid && cells[9][16].solid,
    "all automatic-terrain forms remain solid");
}

function testLargeMuralFootprint() {
  var grid = blankRoom();
  var cells;
  grid[5][8] = "G";
  cells = GregAtlas._buildCells(grid, {});

  assertEqual(cells[2][7].frame, 0x28, "G top-left frame");
  assertEqual(cells[2][8].frame, 0x29, "G top-center frame");
  assertEqual(cells[3][8].frame, 0x31, "G center frame");
  assertEqual(cells[4][9].frame, 0x3a, "G bottom-right frame");
  assertEqual(cells[2][7].kind, "direct", "G emits atlas-frame cells");
  assertEqual(cells[2][7].layer, 0, "large art uses its native normal draw mode");
  assert(!cells[5][8], "G source marker remains below the generated mural");
}

function testTwoColumnArtFootprint() {
  var grid = blankRoom();
  var cells;
  grid[8][12] = "L";
  cells = GregAtlas._buildCells(grid, {});

  assertEqual(cells[5][11].frame, 0x40, "L starts three rows above and source-left");
  assertEqual(cells[5][12].frame, 0x41, "L top-right frame");
  assertEqual(cells[7][11].frame, 0x50, "L bottom-left frame remains above marker");
  assertEqual(cells[7][12].frame, 0x51, "L bottom-right frame remains above marker");
  assert(!cells[8][12], "L source marker remains below the generated art");
}

function testTentacleFootprint() {
  var grid = blankRoom();
  var cells;
  grid[3][20] = "T";
  cells = GregAtlas._buildCells(grid, {});

  assertEqual(cells[0][20].frame, 0x23, "T top frame");
  assertEqual(cells[1][20].frame, 0x2b, "T upper-middle frame");
  assertEqual(cells[2][20].frame, 0x33, "T lower-middle frame");
  assertEqual(cells[3][20].frame, 0x3b, "T anchored bottom frame");
}

function testWaterfallBackfill() {
  var grid = blankRoom();
  var cells;
  grid[6][25] = "~";
  grid[7][25] = "~";
  cells = GregAtlas._buildCells(grid, {});

  assertEqual(cells[5][25].kind, "waterfall-top", "first waterfall back-fills empty cell above");
  assert(typeof cells[5][25].nativeCapFlip === "boolean", "waterfall cap preserves a stable native byte2 flip");
  assertEqual(cells[6][25].kind, "waterfall", "stacked waterfall does not replace its upper segment");
  assertEqual(cells[7][25].kind, "waterfall", "lower stacked waterfall remains a waterfall");
}

function testWaterfallBackfillUsesRawFrameByte() {
  var grid = blankRoom();
  var cells;
  grid[4][12] = "E"; /* Its zero parser override preserves native default 0x65. */
  grid[5][12] = "~";
  grid[4][14] = "#"; /* Nonzero frame 0x2d must survive. */
  grid[5][14] = "~";
  cells = GregAtlas._buildCells(grid, {});

  assertEqual(cells[4][12].kind, "still-goal",
    "waterfall preserves E because its persisted native frame is nonzero");
  assertEqual(cells[4][12].nativeFrameByte, 0x65,
    "E keeps its tile-definition frame when the parser supplies no override");
  assertEqual(cells[4][14].kind, "static",
    "waterfall preserves an above cell whose stored frame is nonzero");
  assertEqual(cells[4][14].nativeFrameByte, 0x2d,
    "static decoration retains its native frame byte");
}

function testV2Binding() {
  var grid = blankRoom();
  var cells;
  var definition = {
    id: "test-solid",
    symbol: "$",
    collision: "solid",
    sprite_sheet: "custom.png",
    sprite_index: 2
  };
  grid[5][5] = "$";
  cells = GregAtlas._buildCells(grid, { tileDefinitions: [definition] });

  assert(cells[5][5].solid, "solid V2 collision preset reaches generated native cell");
  assert(cells[5][5].custom === definition, "V2 definition remains bound to its source cell");
  assert(cells[5][5].customReplace, "V2 visual defaults to replace mode");
  assertEqual(cells[5][5].kind, "floor", "solid preset uses automatic native terrain fallback");
}

try {
  loadRenderer();
  testExportedApi();
  testGoalPreviewContrast();
  testNativeTerrainSeed();
  testRecoveredFrameRecipes();
  testPersistedNativeFrameBytes();
  testReportedTileLayersAndParity();
  testNativeWaterTransforms();
  testNativeCompositeFlips();
  testNativeChandelierAndSun();
  testAutomaticTerrain();
  testLargeMuralFootprint();
  testTwoColumnArtFootprint();
  testTentacleFootprint();
  testWaterfallBackfill();
  testWaterfallBackfillUsesRawFrameByte();
  testV2Binding();
  WScript.Echo("atlas-renderer tests: OK");
  WScript.Quit(0);
} catch (error) {
  WScript.StdErr.WriteLine("atlas-renderer tests: FAILED");
  WScript.StdErr.WriteLine(error && error.message ? error.message : String(error));
  WScript.Quit(1);
}
