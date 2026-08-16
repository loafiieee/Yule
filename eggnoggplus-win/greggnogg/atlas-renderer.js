(function (root, factory) {
  "use strict";

  var api = factory(root);
  if (typeof module === "object" && module.exports) module.exports = api;
  if (root) root.GregAtlas = api;
}(typeof self !== "undefined" ? self : this, function (root) {
  "use strict";

  /*
   * Greggnogg's native-map preview renderer.
   *
   * The static frame recipes below come from the recovered EGGNOGG+ map
   * builder and tile actions. Stateful actors and particles use deterministic
   * editor loops, but their atlas frames, colour sources and layer placement
   * follow the recovered native routines. This remains a preview rather than
   * a full emulation of the game loop.
   *
   * All art is sampled directly from the game's original PNG atlases. Those
   * indexed PNGs already contain a tRNS alpha entry, so browser-decoded alpha
   * is used as-is. In particular, this file never chroma-keys magenta pixels.
   */

  var COLS = 33;
  var ROWS = 12;
  var CELL = 16;
  var TILE_COLUMNS = 8;
  var TILE_ROWS = 16;
  var FONT_CELL = 8;
  var FONT_STEP = 9;
  var MAP_DRAW_ORDER = [1, 0, -1];
  var PREVIEW_DRAW_STAGES = [
    "map:1", "particle:4", "particle:3", "particle:2", "map:0",
    "particle:1", "actors", "map:-1", "particle:0"
  ];
  var AMBIENT_NAMES = ["none", "bugs", "clouds", "art", "flies", "drips", "dust", "bats", "bubbles", "boil"];
  var NATIVE_SPINNER_ALPHA_BYTES = [38, 147, 255, 107];

  var ENGINE_DEFAULTS = {
    fg1: [0.5, 0.5, 0.5],
    fg2: [0.5, 0.5, 0.5],
    bg1: [0.5, 0.5, 0.5],
    bg2: [0.5, 0.5, 0.5],
    water: [0.0, 0.5, 0.75],
    water_hi: [0.9, 0.9, 0.9],
    special: [0.0, 0.5, 0.75],
    special2: [0.9, 0.9, 0.9]
  };

  var STATIC_RECIPES = {
    "#": { frame: 0x2d, colour: "fg2" },
    "(": { frame: 0x6a, colour: "fg2" },
    ")": { frame: 0x6b, colour: "fg2" },
    "-": { frame: 0x3c, colour: "fg2" },
    ":": { frame: 0x3d, colour: "fg2" },
    "=": { frame: 0x1f, colour: "fg2", randomFlip: true },
    "F": { frame: 0x59, colour: "fg2" },
    "H": { frame: 0x06, colour: "fg2" },
    "I": { frame: 0x1e, colour: "fg2", randomFlip: true },
    "Q": { frames: [0x46, 0x47], colour: "fg2", randomFlip: true },
    "S": { frame: 0x07, colour: "fg2" },
    "Z": { frame: 0x05, colour: "fg2", randomFlip: true },
    "`": { frame: 0x56, colour: "fg2", randomFlip: true },
    "e": { frame: 0x40, colour: "fg2" },
    "q": { frame: 0x6f, colour: "fg2", randomFlip: true },
    "u": { frame: 0x6c, colour: "fg2" },
    "x": { frame: 0x56, colour: "fg2" },
    "|": { frame: 0x26, colour: "fg2" }
  };

  var LARGE_ART = {
    G: [
      [0x28, 0x29, 0x2a],
      [0x30, 0x31, 0x32],
      [0x38, 0x39, 0x3a]
    ],
    L: [
      [0x40, 0x41],
      [0x48, 0x49],
      [0x50, 0x51]
    ],
    N: [
      [0x44, 0x45],
      [0x4c, 0x4d],
      [0x54, 0x55]
    ],
    Y: [
      [0x42, 0x43],
      [0x4a, 0x4b],
      [0x52, 0x53]
    ]
  };

  var TENTACLE_FRAMES = {
    T: [0x23, 0x2b, 0x33, 0x3b],
    t: [0x23, 0x2b, 0x33],
    s: [0x23, 0x2b]
  };

  /* Kept as data as well as draw code so the recovered atlas selections can
   * be regression-tested without a browser canvas. A zero frame override in
   * mapgen means "keep the tile definition's default"; it does not write frame
   * zero. That distinction is why terrain uses defaults 1/2, A uses 0x16, and
   * the chandelier action works relative to default 0x18. `deep-water`
   * deliberately reflects Yule's high-water hook: it replaces vanilla's heavy
   * frame 0x05 with the tintable water sprite while preserving the transform. */
  var NATIVE_FRAME_RECIPES = {
    floor: { sheet: "tiles", frames: [0x01, 0x03] },
    wall: { sheet: "tiles", frames: [0x02] },
    ceiling: { sheet: "tiles", frames: [0x02, 0x22] },
    "hanging-spikes": { sheet: "tiles", frames: [0x02, 0x22, 0x34] },
    "ground-spikes": { sheet: "tiles", frames: [0x01, 0x03, 0x04] },
    mine: { sheet: "tiles", frames: [0x01, 0x03, 0x35] },
    "deep-water": { sheet: "tiles", frames: [0x65], colour: "water_hi" },
    "shallow-water": { sheet: "tiles", frames: [0x65], colour: "water" },
    waterfall: { sheet: "tiles", frames: [0x60, 0x61, 0x62, 0x63] },
    "spikeball-marker": { sheet: "tiles", frames: [0x6d] },
    sun: { sheet: "misc", frames: [0x11] },
    goal: { sheet: "tiles", frames: [0x65] },
    spinny: { sheet: "tiles", frames: [0x16] },
    chandelier: { sheet: "tiles", frames: [0x19, 0x18, 0x20, 0x21, 0x15] },
    "scrolling-decor": { sheet: "tiles", frames: [0x2e] }
  };

  var builtinAssets = null;
  var builtinPromise = null;
  var builtinKey = "";
  var externalAssets = dictionary();
  var externalSources = dictionary();
  var tintedCache = dictionary();
  var crowdMaskCache = dictionary();
  var surfaceSerial = 1;
  var imageSerial = typeof WeakMap !== "undefined" ? new WeakMap() : null;

  function dictionary() {
    return Object.create ? Object.create(null) : {};
  }

  function scriptAssetBase() {
    var script;
    if (typeof document === "undefined") return "assets/game/";
    script = document.currentScript;
    if (script && script.src && typeof URL !== "undefined") {
      try {
        return new URL("assets/game/", script.src).href;
      } catch (ignore) {
        /* Fall through to a document-relative path. */
      }
    }
    return "assets/game/";
  }

  var DEFAULT_BASE = scriptAssetBase();

  function joinUrl(base, name) {
    if (typeof URL !== "undefined" && typeof document !== "undefined") {
      try {
        return new URL(name, base || document.baseURI).href;
      } catch (ignore) {
        /* A simple join still works for normal relative paths. */
      }
    }
    base = base || "";
    return base.replace(/\/?$/, "/") + name;
  }

  function isImage(value) {
    return !!value && typeof value === "object" &&
      typeof value.width === "number" && typeof value.height === "number";
  }

  function loadImage(source, label) {
    if (isImage(source)) return Promise.resolve(source);
    return new Promise(function (resolve, reject) {
      var image;
      if (typeof Image === "undefined") {
        reject(new Error("GregAtlas needs a browser Image implementation to load " + label + "."));
        return;
      }
      image = new Image();
      image.decoding = "async";
      image.onload = function () { resolve(image); };
      image.onerror = function () {
        reject(new Error("Could not load the original EGGNOGG+ " + label + " atlas from " + source + "."));
      };
      image.src = source;
    });
  }

  function normalizeLoadOptions(options) {
    var result = {};
    if (typeof options === "string") result.baseUrl = options;
    else if (options && typeof options === "object") result = options;
    return result;
  }

  function loadExternalEntries(entries) {
    var keys;
    if (!entries || typeof entries !== "object") return Promise.resolve();
    keys = Object.keys(entries);
    return Promise.all(keys.map(function (key) {
      var source = entries[key];
      if (externalAssets[key] && externalSources[key] === source) return externalAssets[key];
      return loadImage(source, "map-owned " + key).then(function (image) {
        externalAssets[key] = image;
        externalSources[key] = source;
        return image;
      });
    })).then(function () { return undefined; });
  }

  function loadAssets(options) {
    var settings = normalizeLoadOptions(options);
    var supplied = settings.images || settings.assets;
    var base = settings.baseUrl || DEFAULT_BASE;
    var urls = settings.urls || {};
    var sources = {
      tiles: supplied && supplied.tiles ? supplied.tiles : (urls.tiles || joinUrl(base, "tiles.png")),
      sprites: supplied && supplied.sprites ? supplied.sprites : (urls.sprites || joinUrl(base, "sprites.png")),
      misc: supplied && supplied.misc ? supplied.misc : (urls.misc || joinUrl(base, "misc.png")),
      font: supplied && (supplied.font || supplied.font8x8 || supplied.glyphs) ?
        (supplied.font || supplied.font8x8 || supplied.glyphs) :
        (urls.font || urls.font8x8 || urls.glyphs || joinUrl(base, "font8x8.png"))
    };
    var key = [sources.tiles, sources.sprites, sources.misc, sources.font].map(String).join("|");

    if (!builtinPromise || key !== builtinKey) {
      builtinKey = key;
      builtinAssets = null;
      tintedCache = dictionary();
      crowdMaskCache = dictionary();
      builtinPromise = Promise.all([
        loadImage(sources.tiles, "tiles"),
        loadImage(sources.sprites, "sprites"),
        loadImage(sources.misc, "misc"),
        loadImage(sources.font, "font8x8")
      ]).then(function (images) {
        if (images[0].width !== 128 || images[0].height !== 256) {
          throw new Error("The original tiles.png must be 128x256 pixels.");
        }
        if (images[1].width !== 128 || images[1].height !== 256) {
          throw new Error("The original sprites.png must be 128x256 pixels.");
        }
        if (images[2].width !== 128 || images[2].height !== 128) {
          throw new Error("The original misc.png must be 128x128 pixels.");
        }
        if (images[3].width !== 145 || images[3].height !== 145) {
          throw new Error("The original font8x8.png must be 145x145 pixels.");
        }
        builtinAssets = {
          tiles: images[0],
          sprites: images[1],
          misc: images[2],
          font: images[3],
          font8x8: images[3],
          glyphs: images[3],
          external: externalAssets
        };
        return builtinAssets;
      }, function (error) {
        builtinPromise = null;
        builtinAssets = null;
        throw error;
      });
    }

    return builtinPromise.then(function (assets) {
      return loadExternalEntries(settings.external || settings.externalImages).then(function () {
        assets.external = externalAssets;
        return assets;
      });
    });
  }

  function makeSurface(width, height) {
    var canvas;
    if (typeof OffscreenCanvas !== "undefined") return new OffscreenCanvas(width, height);
    if (typeof document !== "undefined" && document.createElement) {
      canvas = document.createElement("canvas");
      canvas.width = width;
      canvas.height = height;
      return canvas;
    }
    throw new Error("GregAtlas needs Canvas or OffscreenCanvas support.");
  }

  function contextOf(target) {
    var context;
    if (target && typeof target.drawImage === "function") return target;
    if (!target || typeof target.getContext !== "function") {
      throw new TypeError("Expected a canvas element or a 2D canvas context.");
    }
    context = target.getContext("2d");
    if (!context) throw new Error("The canvas does not provide a 2D context.");
    return context;
  }

  function finite(value, fallback) {
    value = Number(value);
    return isFinite(value) ? value : fallback;
  }

  function clamp(value, low, high) {
    return Math.max(low, Math.min(high, value));
  }

  function parseColour(value, fallback) {
    var match;
    var scale;
    var channels;
    if (typeof value === "string") {
      match = value.trim().match(/^#([0-9a-f]{3}|[0-9a-f]{6})$/i);
      if (match) {
        if (match[1].length === 3) {
          return [
            parseInt(match[1].charAt(0) + match[1].charAt(0), 16) / 255,
            parseInt(match[1].charAt(1) + match[1].charAt(1), 16) / 255,
            parseInt(match[1].charAt(2) + match[1].charAt(2), 16) / 255,
            1
          ];
        }
        return [
          parseInt(match[1].slice(0, 2), 16) / 255,
          parseInt(match[1].slice(2, 4), 16) / 255,
          parseInt(match[1].slice(4, 6), 16) / 255,
          1
        ];
      }
    }
    if (value && typeof value.length === "number" && value.length >= 3) {
      channels = [finite(value[0], 0), finite(value[1], 0), finite(value[2], 0)];
      scale = channels[0] > 1 || channels[1] > 1 || channels[2] > 1 ? 255 : 1;
      return [
        clamp(channels[0] / scale, 0, 1),
        clamp(channels[1] / scale, 0, 1),
        clamp(channels[2] / scale, 0, 1),
        value.length > 3 ? clamp(finite(value[3], 1), 0, 1) : 1
      ];
    }
    return fallback ? fallback.slice() : [1, 1, 1, 1];
  }

  function colourCss(colour) {
    return "rgb(" +
      Math.round(clamp(colour[0], 0, 1) * 255) + "," +
      Math.round(clamp(colour[1], 0, 1) * 255) + "," +
      Math.round(clamp(colour[2], 0, 1) * 255) + ")";
  }

  function colourKey(colour) {
    return [
      Math.round(colour[0] * 255),
      Math.round(colour[1] * 255),
      Math.round(colour[2] * 255)
    ].join(",");
  }

  function resolveAppearance(room, options) {
    var source = options.appearance || (room && room.appearance) || {};
    var defaults = options.defaultsAppearance || {};
    var wantMirror = !!options.mirrored;
    var bank;
    var defaultBank;
    var result = {};

    if (source.primary || source.mirror) {
      bank = wantMirror && source.mirror ? source.mirror : (source.primary || source.mirror || {});
    } else {
      bank = source;
    }
    if (defaults.primary || defaults.mirror) {
      defaultBank = wantMirror && defaults.mirror ? defaults.mirror :
        (defaults.primary || defaults.mirror || {});
    } else {
      defaultBank = defaults;
    }
    Object.keys(ENGINE_DEFAULTS).forEach(function (name) {
      var fallback = parseColour(defaultBank[name], ENGINE_DEFAULTS[name].concat(1));
      result[name] = parseColour(bank[name], fallback);
    });
    return result;
  }

  function imageId(image) {
    var id;
    if (!imageSerial) return String(image && image.src || "image");
    id = imageSerial.get(image);
    if (!id) {
      id = "i" + surfaceSerial;
      surfaceSerial += 1;
      imageSerial.set(image, id);
    }
    return id;
  }

  function tintedRegion(image, sx, sy, sw, sh, colour) {
    var key = imageId(image) + ":" + [sx, sy, sw, sh, colourKey(colour)].join(":");
    var canvas = tintedCache[key];
    var context;
    if (canvas) return canvas;
    canvas = makeSurface(sw, sh);
    context = canvas.getContext("2d");
    context.imageSmoothingEnabled = false;
    context.clearRect(0, 0, sw, sh);
    context.drawImage(image, sx, sy, sw, sh, 0, 0, sw, sh);
    context.globalCompositeOperation = "multiply";
    context.fillStyle = colourCss(colour);
    context.fillRect(0, 0, sw, sh);
    context.globalCompositeOperation = "destination-in";
    context.drawImage(image, sx, sy, sw, sh, 0, 0, sw, sh);
    context.globalCompositeOperation = "source-over";
    tintedCache[key] = canvas;
    return canvas;
  }

  function frameCoordinates(image, index, cellW, cellH, padding) {
    var columns = Math.floor((image.width + padding) / (cellW + padding));
    if (columns < 1 || index < 0) return null;
    return {
      x: (index % columns) * (cellW + padding),
      y: Math.floor(index / columns) * (cellH + padding),
      w: cellW,
      h: cellH
    };
  }

  function drawRegion(context, image, region, x, y, width, height, settings) {
    var colour = settings.colour || [1, 1, 1, 1];
    var source = tintedRegion(image, region.x, region.y, region.w, region.h, colour);
    var angle = finite(settings.angle, 0) * Math.PI / 180;
    var scaleX = finite(settings.scaleX, 1);
    var scaleY = finite(settings.scaleY, 1);
    var flipX = settings.flipX ? -1 : 1;
    var flipY = settings.flipY ? -1 : 1;

    context.save();
    context.imageSmoothingEnabled = false;
    context.globalAlpha *= clamp(finite(settings.alpha, colour[3]), 0, 1);
    context.translate(x + width * 0.5, y + height * 0.5);
    context.rotate(angle);
    context.scale(scaleX * flipX, scaleY * flipY);
    context.drawImage(source, -width * 0.5, -height * 0.5, width, height);
    context.restore();
  }

  function drawFrame(context, assets, sheetName, index, x, y, colour, settings) {
    var image = assets[sheetName];
    var geometry = assets.geometry && assets.geometry[sheetName];
    var cellW = geometry ? geometry.cellW : CELL;
    var cellH = geometry ? geometry.cellH : CELL;
    var padding = geometry ? geometry.padding : 0;
    var region;
    settings = settings || {};
    if (!image) return false;
    region = frameCoordinates(image, index, cellW, cellH, padding);
    if (!region || region.y + region.h > image.height) return false;
    settings.colour = colour || [1, 1, 1, 1];
    drawRegion(context, image, region, x, y, CELL, CELL, settings);
    return true;
  }

  /* load_gfx creates runtime sprites 0x80..0xff by scanning the shipped
   * sprites.png and copying only exact RGB(128,128,128) pixels into a white,
   * opaque second half. Crowd sprites 0xb0.. therefore are masks derived from
   * shipped cells 0x30.., not the visible character art in those cells. */
  function crowdMaskSurface(image, sourceIndex) {
    var key = imageId(image) + ":crowd-mask:" + sourceIndex;
    var surface = crowdMaskCache[key];
    if (surface) return surface;
    var region = frameCoordinates(image, sourceIndex, CELL, CELL, 0);
    if (!region) return null;
    surface = makeSurface(CELL, CELL);
    var context = surface.getContext("2d");
    context.clearRect(0, 0, CELL, CELL);
    context.drawImage(image, region.x, region.y, CELL, CELL, 0, 0, CELL, CELL);
    try {
      var pixels = context.getImageData(0, 0, CELL, CELL);
      for (var offset = 0; offset < pixels.data.length; offset += 4) {
        if (pixels.data[offset] === 128 && pixels.data[offset + 1] === 128 && pixels.data[offset + 2] === 128) {
          pixels.data[offset] = 255; pixels.data[offset + 1] = 255; pixels.data[offset + 2] = 255; pixels.data[offset + 3] = 255;
        } else pixels.data[offset + 3] = 0;
      }
      context.putImageData(pixels, 0, 0);
    } catch (ignore) {
      /* A supplied cross-origin preview asset cannot be pixel-inspected. The
       * packaged original is same-origin, so this is only a defensive path. */
      context.clearRect(0, 0, CELL, CELL);
    }
    crowdMaskCache[key] = surface;
    return surface;
  }

  function drawCrowdMask(context, assets, sourceIndex, x, y, colour, flipX) {
    var mask;
    try {
      mask = assets.sprites && crowdMaskSurface(assets.sprites, sourceIndex);
      if (!mask) return false;
      drawRegion(context, mask, { x: 0, y: 0, w: CELL, h: CELL }, x, y, CELL, CELL,
        { colour: colour, flipX: !!flipX });
      return true;
    } catch (ignore) {
      /* Crowd masks are a browser-side reconstruction of load_gfx's generated
       * sprite half. A missing/tainted canvas must not abort the room draw. */
      return false;
    }
  }

  /* The native f and waterfall actions animate by changing the source rectangle
   * of an atlas sprite, then drawing that half-size source at 2x. They are not
   * frame animations. Keeping the crop inside the original 16px atlas cell
   * matches tile_scroll_ex without manufacturing derived images. */
  function drawHalfFrame(context, assets, sheetName, index, x, y, colour, offsetX, offsetY, settings) {
    var image = assets[sheetName];
    var region;
    var crop;
    settings = settings || {};
    if (!image) return false;
    region = frameCoordinates(image, index, CELL, CELL, 0);
    if (!region) return false;
    crop = {
      /* tiledef_update rounds its animated atlas origins to signed shorts. */
      x: region.x + clamp(Math.round(finite(offsetX, 0)), 0, CELL / 2),
      y: region.y + clamp(Math.round(finite(offsetY, 0)), 0, CELL / 2),
      w: CELL / 2,
      h: CELL / 2
    };
    settings.colour = colour || [1, 1, 1, 1];
    drawRegion(context, image, crop, x, y, CELL, CELL, settings);
    return true;
  }

  function mixColour(a, b, amount, alpha) {
    amount = clamp(finite(amount, 0.5), 0, 1);
    return [
      a[0] + (b[0] - a[0]) * amount,
      a[1] + (b[1] - a[1]) * amount,
      a[2] + (b[2] - a[2]) * amount,
      alpha === undefined ? a[3] + (b[3] - a[3]) * amount : alpha
    ];
  }

  function scaledColour(colour, scale, alpha) {
    return [
      clamp(colour[0] * scale, 0, 1),
      clamp(colour[1] * scale, 0, 1),
      clamp(colour[2] * scale, 0, 1),
      alpha === undefined ? colour[3] : alpha
    ];
  }

  function fract(value) {
    return value - Math.floor(value);
  }

  function nativeWaterTransform(kind, time, x) {
    var radians = Math.PI / 180;
    var horizontalWave = 0.5 +
      0.5 * Math.sin((finite(time, 0) * 6 + finite(x, 0) * 30) * radians);
    if (kind === "deep-water") {
      return {
        offsetY: 0,
        scaleY: 3 + horizontalWave +
          4 * (0.5 + 0.5 * Math.sin(finite(time, 0) * radians))
      };
    }
    return { offsetY: (1 - horizontalWave) * 8, scaleY: 2 };
  }

  function nativeCompositeFlips(byte2) {
    var accent = !(finite(byte2, 0) & 1);
    return {
      base: false,
      accent: accent,
      /* ceiling_action does not restore the turtle after its lip. Hanging v's
       * later white spike tip therefore inherits the same horizontal flip. */
      tip: accent
    };
  }

  function hsvColour(hue, saturation, value, alpha) {
    var chroma;
    var sector;
    var part;
    var match;
    var rgb;
    hue = ((finite(hue, 0) % 360) + 360) % 360;
    saturation = clamp(finite(saturation, 0), 0, 1);
    value = clamp(finite(value, 0), 0, 1);
    chroma = value * saturation;
    sector = hue / 60;
    part = chroma * (1 - Math.abs((sector % 2) - 1));
    if (sector < 1) rgb = [chroma, part, 0];
    else if (sector < 2) rgb = [part, chroma, 0];
    else if (sector < 3) rgb = [0, chroma, part];
    else if (sector < 4) rgb = [0, part, chroma];
    else if (sector < 5) rgb = [part, 0, chroma];
    else rgb = [chroma, 0, part];
    match = value - chroma;
    return [rgb[0] + match, rgb[1] + match, rgb[2] + match, alpha === undefined ? 1 : alpha];
  }

  function nativeDayFraction(options) {
    var supplied = options && (options.dayFraction !== undefined ?
      options.dayFraction : options.timeOfDay);
    var now;
    if (supplied !== undefined && isFinite(Number(supplied))) return fract(Number(supplied));
    now = new Date();
    return (now.getHours() * 3600 + now.getMinutes() * 60 + now.getSeconds() +
      now.getMilliseconds() / 1000) / 86400;
  }

  function nativeChandelierGeometry(time, sourceX, amplitude, originX, originY) {
    var phase = (finite(time, 0) * 2.5 + finite(sourceX, 0) * 30) * Math.PI / 180;
    var angleDegrees = Math.sin(phase) * finite(amplitude, 30);
    var angle = angleDegrees * Math.PI / 180;
    /* turtle_move uses -sin(theta) for canvas X and +cos(theta) for canvas Y. */
    var dx = -Math.sin(angle);
    var dy = Math.cos(angle);
    var pivot = { x: finite(originX, 0), y: finite(originY, 0) - 8 };
    var bulb = { x: pivot.x + dx * 40, y: pivot.y + dy * 40 };
    return {
      angleDegrees: angleDegrees,
      p1: { x: pivot.x + dx * 8, y: pivot.y + dy * 8 },
      p2: { x: pivot.x + dx * 24, y: pivot.y + dy * 24 },
      bulb: bulb,
      flare: { x: bulb.x + dx * 4, y: bulb.y + dy * 4 }
    };
  }

  function nativeChandelierColour(angle, saturation, value, alpha) {
    var radians = Math.PI / 180;
    var floor;
    saturation = clamp(finite(saturation, 0), 0, 1);
    value = clamp(finite(value, 0), 0, 1);
    floor = 1 - saturation;
    function channel(offset) {
      return ((Math.sin((finite(angle, 0) + offset) * radians) * 0.5 + 0.5) *
        saturation + floor) * value;
    }
    return [channel(0), channel(120), channel(240), alpha === undefined ? 1 : alpha];
  }

  function nativeChandelierGlow(time, dayFraction) {
    var ticks = finite(time, 0);
    var wave = Math.sin(ticks * Math.PI / 180);
    var fastWave = Math.sin(ticks * 123 * Math.PI / 180);
    /* frnd(0,.05) is runtime-only; its .025 mean gives a stable editor view. */
    var q = (0.7625 + 0.125 * wave + 0.0625 * fastWave + 0.025) *
      (0.9 + 0.1 * wave);
    var source = nativeChandelierColour(
      720 * (fract(finite(dayFraction, 0)) + 0.125), 0.75, 1, 1);
    var bulb = [source[0] * source[0], source[1] * source[1], source[2] * source[2], 1];
    var remainder = 1 - q;
    return {
      q: q,
      bulb: bulb,
      flare: [bulb[0] * remainder, bulb[1] * 0.75 * remainder,
        bulb[2] * 0.375 * remainder, 1]
    };
  }

  function nativeSunPosition(width, height, iconOnly, localX, localY) {
    if (iconOnly) return { x: finite(localX, 0), y: finite(localY, 0) };
    /* sky_glow_action ignores the O cell and emits at (game_w/2, game_h/4).
     * drawFrame receives the 16px sprite's top-left rather than its centre. */
    return {
      x: finite(width, COLS * CELL) * 0.5 - CELL * 0.5,
      y: finite(height, ROWS * CELL) * 0.25 - CELL * 0.5
    };
  }

  function drawFontGlyph(context, assets, glyph, x, y, colour, settings) {
    var image = assets.font || assets.font8x8 || assets.glyphs;
    var code = String(glyph || "?").charCodeAt(0) & 0xff;
    var region = {
      x: 1 + (code & 15) * FONT_STEP,
      y: 1 + (code >> 4) * FONT_STEP,
      w: FONT_CELL,
      h: FONT_CELL
    };
    settings = settings || {};
    settings.colour = colour;
    drawRegion(context, image, region, x + 4, y + 4, FONT_CELL, FONT_CELL, settings);
  }

  function hashCell(x, y, glyph) {
    var hash = 2166136261;
    var text = String(x) + ":" + String(y) + ":" + glyph;
    var i;
    for (i = 0; i < text.length; i += 1) {
      hash ^= text.charCodeAt(i);
      hash = Math.imul ? Math.imul(hash, 16777619) : (hash * 16777619);
    }
    return hash >>> 0;
  }

  function nativeWorldX(x, options) {
    var offset;
    var localX = Math.floor(finite(x, 0));
    options = options || {};
    /* mapgen reverses the source column before it computes the destination
     * tile's byte arguments. Keep the seed attached to that destination. */
    if (options.mirrored) localX = COLS - 1 - localX;
    if (options.worldXOffset !== undefined && options.worldXOffset !== null) {
      offset = Math.floor(finite(options.worldXOffset, 0));
    } else {
      offset = Math.floor(finite(options.worldRoomIndex, 0)) * COLS;
    }
    return offset + localX;
  }

  function nativeSeedByte(x, y, options) {
    var worldX = nativeWorldX(x, options);
    var row = Math.floor(finite(y, 0));
    /* mapgen_plot_room starts each room at room*0x5cd (33*0x2d),
     * advances 0x2d per column and 0x3d per row, then XORs 0x3039. */
    return ((worldX * 0x2d + row * 0x3d) ^ 0x3039) & 0xff;
  }

  function nativeWallByte(worldX, y) {
    /* Native onein(5) stores -1 one fifth of the time and +1 otherwise.
     * A coordinate hash makes the editor stable while retaining that split. */
    return hashCell(worldX, y, "native-wall-onein5") % 5 === 0 ? 0xff : 0x01;
  }

  function nativeWallFlip(byte2) {
    var raw = Math.floor(finite(byte2, 0)) & 0xff;
    var signedScale = raw === 0 ? 1 : (raw > 0x7f ? raw - 0x100 : raw);
    var colouringScale = raw & 1 ? -1 : 1;
    return signedScale * colouringScale < 0;
  }

  function blankGrid() {
    var result = [];
    var y;
    for (y = 0; y < ROWS; y += 1) result.push(new Array(COLS).fill(" "));
    return result;
  }

  function coerceGrid(gridOrRoom) {
    var source = gridOrRoom && gridOrRoom.grid ? gridOrRoom.grid : gridOrRoom;
    var result = blankGrid();
    var row;
    var y;
    var x;
    if (!source || typeof source.length !== "number") return result;
    for (y = 0; y < Math.min(ROWS, source.length); y += 1) {
      row = typeof source[y] === "string" ? source[y].split("") : source[y];
      if (!row || typeof row.length !== "number") continue;
      for (x = 0; x < Math.min(COLS, row.length); x += 1) {
        result[y][x] = typeof row[x] === "string" && row[x].length ? row[x].charAt(0) : " ";
      }
    }
    return result;
  }

  function customDefinitionMap(options) {
    var definitions = options.tileDefinitions || options.tiles ||
      (options.tileset && options.tileset.tiles) || [];
    var result = dictionary();
    if (Array.isArray(definitions)) {
      definitions.forEach(function (definition) {
        if (definition && typeof definition.symbol === "string") {
          result[definition.symbol.charAt(0)] = definition;
        }
      });
    } else if (definitions && typeof definitions === "object") {
      Object.keys(definitions).forEach(function (key) {
        var definition = definitions[key];
        if (definition) result[definition.symbol || key] = definition;
      });
    }
    return result;
  }

  function customNativeGlyph(definition, glyph) {
    if (!definition) return glyph;
    if (typeof definition.native_glyph === "string" && definition.native_glyph.length) {
      return definition.native_glyph.charAt(0);
    }
    if (definition.collision === "solid") return "@";
    if (definition.collision === "hazard") return "X";
    if (definition.collision === "pass_through" || definition.collision === "passthrough") return "x";
    return glyph;
  }

  function makeCell(glyph, kind, solid, layer) {
    return {
      glyph: glyph,
      sourceGlyph: glyph,
      kind: kind,
      solid: !!solid,
      layer: layer || 0,
      /* The map cell's literal byte1, before any tile action substitutes a
       * visual. Waterfall generation tests this byte—not collision/type—to
       * decide whether it replaces the already-plotted cell above. */
      nativeFrameByte: 0,
      nativeByte2: 0,
      nativeWorldX: 0,
      custom: null,
      customReplace: false,
      nativeMirror: false
    };
  }

  function setCell(cells, x, y, cell, options) {
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return;
    cell.x = x;
    cell.y = y;
    cell.nativeWorldX = nativeWorldX(x, options);
    cells[y][x] = cell;
  }

  function emitLargeArt(cells, glyph, x, y, options) {
    var matrix = LARGE_ART[glyph];
    var width = matrix[0].length;
    /* G is centred on its source marker; the two-column lee() art extends
     * toward source-left. All four native generators write into the three
     * rows strictly above the marker, leaving the marker cell itself empty. */
    var left = glyph === "G" ? x - 1 : x - (width - 1);
    var top = y - 3;
    var row;
    var col;
    var cell;
    for (row = 0; row < matrix.length; row += 1) {
      for (col = 0; col < matrix[row].length; col += 1) {
        cell = makeCell(glyph, "direct", false, 0);
        cell.frame = matrix[row][col];
        cell.nativeFrameByte = cell.frame;
        cell.colour = glyph === "G" ? "special2" : "fg2";
        cell.nativeMirror = true;
        setCell(cells, left + col, top + row, cell, options);
      }
    }
  }

  function emitTentacle(cells, glyph, x, y, options) {
    var frames = TENTACLE_FRAMES[glyph];
    var top = y - frames.length + 1;
    var i;
    var cell;
    for (i = 0; i < frames.length; i += 1) {
      cell = makeCell(glyph, "tentacle", false, -1);
      cell.frame = frames[i];
      cell.nativeFrameByte = cell.frame;
      cell.colour = "enemy";
      cell.segment = i;
      cell.segmentCount = frames.length;
      cell.markerX = x;
      cell.markerY = y;
      cell.nativeMirror = true;
      setCell(cells, x, top + i, cell, options);
    }
  }

  function emitNative(cells, grid, glyph, x, y, options) {
    var cell;
    var above;
    var aboveSolid;
    var seed = nativeSeedByte(x, y, options);
    var worldX = nativeWorldX(x, options);
    if (glyph === " " || glyph === ".") {
      setCell(cells, x, y, makeCell(glyph, "empty", false, 0), options);
      return;
    }
    if (LARGE_ART[glyph]) {
      emitLargeArt(cells, glyph, x, y, options);
      return;
    }
    if (TENTACLE_FRAMES[glyph]) {
      emitTentacle(cells, glyph, x, y, options);
      return;
    }
    if (glyph === "@") {
      above = y > 0 ? cells[y - 1][x] : null;
      /* is_tile_solid(..., 1) treats an out-of-map lookup as solid. A top-row
       * @ therefore starts as a wall rather than a floor. */
      aboveSolid = y === 0 || !!(above && above.solid);
      cell = makeCell(glyph, aboveSolid ? "auto-wall" : "floor", true, 0);
      cell.nativeFrameByte = aboveSolid ? 0x02 : 0x01;
      if (aboveSolid) {
        cell.nativeByte2 = nativeWallByte(worldX, y);
        cell.nativeWallFlip = nativeWallFlip(cell.nativeByte2);
      } else {
        cell.nativeByte2 = seed;
      }
      setCell(cells, x, y, cell, options);
      return;
    }
    if (glyph === "!") {
      cell = makeCell(glyph, "fixed-floor", true, 0);
      cell.nativeFrameByte = 0x01;
      cell.nativeByte2 = seed;
    }
    else if (glyph === "_") {
      cell = makeCell(glyph, "ceiling", true, 0);
      cell.nativeFrameByte = 0x02;
      cell.nativeByte2 = seed;
    }
    else if (glyph === "X") {
      cell = makeCell(glyph, "ground-spikes", true, 0);
      cell.nativeFrameByte = 0x01;
    }
    else if (glyph === "v") {
      cell = makeCell(glyph, "hanging-spikes", true, 0);
      cell.nativeFrameByte = 0x02;
    }
    else if (glyph === "m") {
      cell = makeCell(glyph, "mine", true, 0);
      cell.nativeFrameByte = 0x01;
    }
    else if (glyph === "~") {
      cell = makeCell(glyph, "waterfall", false, 0);
      cell.nativeFrameByte = 0x60;
      /* mapgen stores source-row parity in byte2; waterfall_action uses that
       * byte as its native horizontal flip. */
      cell.nativeParityFlip = !!(y & 1);
    }
    else if (glyph === "W") {
      cell = makeCell(glyph, "deep-water", false, -1);
      cell.nativeFrameByte = 0x05;
    }
    else if (glyph === "w") {
      cell = makeCell(glyph, "shallow-water", false, 0);
      cell.nativeFrameByte = 0x05;
    }
    else if (glyph === "*") cell = makeCell(glyph, "sword-marker", false, "actor");
    else if (glyph === "K") cell = makeCell(glyph, "spikeball-marker", false, "actor");
    /* sky_glow_action emits misc[0x11] particles on native particle layer 3. */
    else if (glyph === "O") {
      cell = makeCell(glyph, "sun", false, "particle3");
      cell.nativeFrameByte = 0x21;
    }
    else if (glyph === "E") {
      cell = makeCell(glyph, "still-goal", false, 0);
      cell.nativeFrameByte = 0x65;
    }
    else if (glyph === "^") {
      cell = makeCell(glyph, "waving-goal", false, 0);
      cell.nativeFrameByte = 0x65;
    }
    else if (glyph === "1" || glyph === "2") {
      cell = makeCell(glyph, "team-goal", false, -1);
      cell.nativeFrameByte = 0x05;
    }
    else if (glyph === "i") {
      cell = makeCell(glyph, "crowd", false, 1);
      cell.nativeFrameByte = 0x3e;
      cell.nativeByte2 = (hashCell(worldX, y, "crowd-seed") & 0xff) - 128;
    }
    else if (glyph === "l") {
      cell = makeCell(glyph, "score-light", false, 0);
      cell.nativeFrameByte = 0x02;
    }
    else if (glyph === "+") {
      cell = makeCell(glyph, "mushrooms", false, 0);
      cell.nativeFrameByte = 0x5d;
    }
    else if (glyph === "A") {
      cell = makeCell(glyph, "spinny", false, 0);
      cell.nativeFrameByte = 0x16;
    }
    else if (glyph === "C" || glyph === "c") {
      cell = makeCell(glyph, "chandelier", false, -1);
      cell.nativeFrameByte = 0x18;
    }
    else if (glyph === "f") {
      cell = makeCell(glyph, "scrolling-decor", false, 0);
      cell.nativeFrameByte = 0x2e;
    }
    else if (glyph === "P") {
      cell = makeCell(glyph, "puzzley", false, 0);
      cell.nativeFrameByte = 0x08;
    }
    else if (glyph === "?") cell = makeCell(glyph, "noop", false, 0);
    else if (STATIC_RECIPES[glyph]) {
      cell = makeCell(glyph, "static", false, 0);
      cell.nativeFrameByte = STATIC_RECIPES[glyph].frames ?
        STATIC_RECIPES[glyph].frames[0] : STATIC_RECIPES[glyph].frame;
    }
    else cell = makeCell(glyph, "unknown", false, 0);
    setCell(cells, x, y, cell, options);

    if (glyph === "~" && y > 0 && cells[y - 1][x] && cells[y - 1][x].nativeFrameByte === 0) {
      cell = makeCell(glyph, "waterfall-top", false, 0);
      cell.nativeFrameByte = 0x62;
      /* Native cap byte2 is random. Keep frame and flip on separate stable bits
       * so the preview shows both independent choices without runtime RNG. */
      cell.nativeCapFlip = !!(hashCell(x, y - 1, "waterfall-cap-flip") & 1);
      setCell(cells, x, y - 1, cell, options);
    }
  }

  function buildCells(grid, options) {
    var definitions = customDefinitionMap(options);
    var cells = [];
    var y;
    var x;
    var glyph;
    var definition;
    var nativeGlyph;
    var cell;
    for (y = 0; y < ROWS; y += 1) cells.push(new Array(COLS));

    /* Native generation is top-to-bottom. That ordering matters for @. */
    for (y = 0; y < ROWS; y += 1) {
      for (x = 0; x < COLS; x += 1) {
        glyph = grid[y][x];
        definition = definitions[glyph];
        nativeGlyph = customNativeGlyph(definition, glyph);
        emitNative(cells, grid, nativeGlyph, x, y, options);
        cell = cells[y][x];
        if (definition && cell) {
          cell.sourceGlyph = glyph;
          cell.custom = definition;
          cell.customReplace = definition.native_visual !== "underlay";
          if (definition.collision === "solid" || definition.collision === "hazard") cell.solid = true;
          else if (definition.collision === "pass_through" || definition.collision === "passthrough") cell.solid = false;
        }
      }
    }

    /* A generated wall whose lower edge is exposed uses the ceiling/bottom lip. */
    for (y = 0; y < ROWS; y += 1) {
      for (x = 0; x < COLS; x += 1) {
        cell = cells[y][x];
        /* The same solid-out-of-map rule applies below the last row, so a
         * bottom-edge wall stays a wall instead of gaining a ceiling lip. */
        if (cell && cell.kind === "auto-wall" && y < ROWS - 1 &&
            (!cells[y + 1][x] || !cells[y + 1][x].solid)) {
          cell.kind = "ceiling";
          cell.nativeGeneratedBottom = true;
        }
      }
    }
    return cells;
  }

  function paletteColour(palette, name) {
    return palette[name] || [1, 1, 1, 1];
  }

  function colourLuminance(colour) {
    var parsed = parseColour(colour, [0, 0, 0, 1]);
    function channel(value) {
      value = clamp(finite(value, 0), 0, 1);
      return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * channel(parsed[0]) + 0.7152 * channel(parsed[1]) + 0.0722 * channel(parsed[2]);
  }

  function previewEnemyColour(palette) {
    var backgrounds = [paletteColour(palette || {}, "bg1"), paletteColour(palette || {}, "bg2")];
    var dark = [0.12, 0.14, 0.18, 1];
    var light = [0.96, 0.92, 0.82, 1];
    function weakestContrast(candidate) {
      var candidateLuminance = colourLuminance(candidate);
      var weakest = Infinity;
      backgrounds.forEach(function (background) {
        var backgroundLuminance = colourLuminance(background);
        var ratio = (Math.max(candidateLuminance, backgroundLuminance) + 0.05) /
          (Math.min(candidateLuminance, backgroundLuminance) + 0.05);
        weakest = Math.min(weakest, ratio);
      });
      return weakest;
    }
    return weakestContrast(light) >= weakestContrast(dark) ? light : dark;
  }

  function renderFloor(context, assets, x, y, palette, settings, baseColour) {
    var recipe = NATIVE_FRAME_RECIPES.floor;
    /* floor_action moves one native unit upward for the unflipped base, then
     * returns home and applies byte2's parity flip only to frame 3. */
    drawFrame(context, assets, recipe.sheet, recipe.frames[0], x, y - 1,
      baseColour || paletteColour(palette, "fg1"), {});
    drawFrame(context, assets, recipe.sheet, recipe.frames[1], x, y,
      paletteColour(palette, "fg2"), settings || {});
  }

  function renderCeiling(context, assets, x, y, palette, settings, baseFrame) {
    var recipe = NATIVE_FRAME_RECIPES.ceiling;
    /* ceiling_action uses the cell's tiledef-default byte1=2 for its unflipped
     * base and applies byte2's parity only to the frame-0x22 lip. */
    drawFrame(context, assets, recipe.sheet,
      baseFrame === undefined ? recipe.frames[0] : baseFrame,
      x, y, paletteColour(palette, "fg1"), {});
    drawFrame(context, assets, recipe.sheet, recipe.frames[1], x, y,
      paletteColour(palette, "fg2"), settings || {});
  }

  function nativeSheetAssets(assets, options) {
    var tileset = options.tileset || {};
    var sheetName = tileset.sprite_sheet;
    var image;
    if (!tileset.native_layout || !sheetName || /^builtin:/i.test(sheetName)) return assets;
    image = (options.externalImages && options.externalImages[sheetName]) ||
      (assets.external && assets.external[sheetName]);
    if (!image) return assets;
    return {
      tiles: image,
      sprites: assets.sprites,
      misc: assets.misc,
      font: assets.font,
      font8x8: assets.font8x8,
      glyphs: assets.glyphs,
      external: assets.external,
      geometry: {
        tiles: {
          cellW: Math.max(1, Math.floor(finite(tileset.cell_w, CELL))),
          cellH: Math.max(1, Math.floor(finite(tileset.cell_h, CELL))),
          padding: Math.max(0, Math.floor(finite(tileset.padding, 0)))
        }
      }
    };
  }

  function renderSun(context, assets, x, y, palette, time, iconOnly) {
    var sunFrame = NATIVE_FRAME_RECIPES.sun.frames[0];
    var sun = paletteColour(palette, "special2");
    var previewScale = iconOnly ? 0.72 : 1;
    var remainder;
    var brightness;

    /* sky_glow_action emits one solid misc[0x11] sun plus a five-tick trail.
     * The trail contains each ticks%5 phase exactly once: scale 1.5..3.5,
     * brightness .25..05, and 72-degree rotational steps. */
    context.save();
    context.globalCompositeOperation = "lighter";
    for (remainder = 4; remainder >= 0; remainder -= 1) {
      brightness = 0.25 - remainder * 0.05;
      drawFrame(context, assets, "misc", sunFrame, x, y, sun, {
        alpha: brightness,
        angle: time * 0.25 + remainder * 72,
        scaleX: (1.5 + remainder * 0.5) * previewScale,
        scaleY: (1.5 + remainder * 0.5) * previewScale
      });
    }
    /* The one-tick solid emission uses the same additive particle path. */
    drawFrame(context, assets, "misc", sunFrame, x, y, sun, {
      scaleX: previewScale,
      scaleY: previewScale
    });
    context.restore();
  }

  function renderSpinner(context, assets, cell, x, y, palette, time) {
    var baseAngle = time * 23 + cell.nativeWorldX * 45;
    /* quad_batch stores alpha as an unchecked byte. The fourth 1.425 alpha
     * therefore wraps to 107 instead of clamping to opaque. */
    var i;
    for (i = 0; i < 4; i += 1) {
      drawFrame(context, assets, "tiles", NATIVE_FRAME_RECIPES.spinny.frames[0], x, y,
        paletteColour(palette, "bg1"), {
        angle: baseAngle + i * 20,
        scaleX: 4,
        scaleY: 4,
        alpha: NATIVE_SPINNER_ALPHA_BYTES[i] / 255
      });
    }
  }

  function renderChandelier(context, assets, cell, x, y, palette, time, options) {
    var wide = cell.sourceGlyph === "C";
    var amplitude = wide ? 30 : 5;
    var originX = x + CELL * 0.5;
    var originY = y + CELL * 0.5;
    var geometry = nativeChandelierGeometry(time, cell.nativeWorldX, amplitude, originX, originY);
    var dayFraction = nativeDayFraction(options);
    var glow = nativeChandelierGlow(time, dayFraction);
    var structuralFlip = dayFraction >= 0.5;
    var spin = time * 31;
    var chandelierFrames = NATIVE_FRAME_RECIPES.chandelier.frames;

    /* Exact recovered geometry: +8 to the pivot, then 8/16/16 down the
     * swinging chain. The ceiling hook is drawn back at the source cell. */
    drawFrame(context, assets, "tiles", chandelierFrames[0], geometry.p1.x - 8, geometry.p1.y - 8,
      paletteColour(palette, "fg2"), { angle: geometry.angleDegrees, flipX: structuralFlip });
    drawFrame(context, assets, "tiles", chandelierFrames[0], geometry.p2.x - 8, geometry.p2.y - 8,
      paletteColour(palette, "fg2"), { angle: geometry.angleDegrees, flipX: structuralFlip });
    drawFrame(context, assets, "tiles", chandelierFrames[1], geometry.bulb.x - 8, geometry.bulb.y - 8,
      paletteColour(palette, "fg2"), { angle: geometry.angleDegrees, flipX: structuralFlip });

    /* Native queues frame 0x20 three times at one angle and colour. Its following
     * turtle_move(-4) travels farther down the already-swinging rope before the
     * two counter-rotating frame-0x21 flares are queued. */
    drawFrame(context, assets, "tiles", chandelierFrames[2], geometry.bulb.x - 8, geometry.bulb.y - 8,
      glow.bulb, { angle: geometry.angleDegrees });
    /* Although the hook is enqueued after the additive sprites in the action,
     * native flushes the complete normal queue before its additive queue. */
    drawFrame(context, assets, "tiles", chandelierFrames[4], x, y,
      paletteColour(palette, "fg2"), {});
    /* sprite_batch_plot's third argument selects the additive queue. Native
     * draws one normal frame-0x20 bulb, then two additive copies and both
     * frame-0x21 flares through that second queue. */
    context.save();
    context.globalCompositeOperation = "lighter";
    drawFrame(context, assets, "tiles", chandelierFrames[2], geometry.bulb.x - 8, geometry.bulb.y - 8,
      glow.bulb, { angle: geometry.angleDegrees });
    drawFrame(context, assets, "tiles", chandelierFrames[2], geometry.bulb.x - 8, geometry.bulb.y - 8,
      glow.bulb, { angle: geometry.angleDegrees });
    drawFrame(context, assets, "tiles", chandelierFrames[3], geometry.flare.x - 8, geometry.flare.y - 8,
      glow.flare, { angle: spin, scaleX: 4 * glow.q, scaleY: 4 * glow.q });
    drawFrame(context, assets, "tiles", chandelierFrames[3], geometry.flare.x - 8, geometry.flare.y - 8,
      glow.flare, { angle: -spin, scaleX: 4 * glow.q, scaleY: 4 * glow.q });
    context.restore();
  }

  function renderNativeCell(context, assets, cell, x, y, palette, options) {
    var settings = {};
    var hash = hashCell(cell.nativeWorldX, cell.y, cell.sourceGlyph);
    var time = finite(options.time, 0);
    var recipe;
    var frame;
    var sway;
    var colour;
    var transform;
    var flips;
    var teamColours = options.teamColours || options.teamColors || {
      "1": [0.95, 0.28, 0.22, 1],
      "2": [0.25, 0.55, 1.0, 1]
    };
    var suppliedEnemyColour = options.enemyColour || options.enemyColor || teamColours.enemy;
    var enemyColour = suppliedEnemyColour ? parseColour(suppliedEnemyColour, previewEnemyColour(palette)) :
      previewEnemyColour(palette);

    if (cell.nativeMirror && options.mirrored) settings.flipX = true;
    switch (cell.kind) {
      case "empty":
      case "noop":
        return;
      case "floor":
        flips = nativeCompositeFlips(cell.nativeByte2);
        settings.flipX = flips.accent;
        renderFloor(context, assets, x, y, palette, settings);
        return;
      case "fixed-floor":
        flips = nativeCompositeFlips(cell.nativeByte2);
        settings.flipX = flips.accent;
        /* Tile type 3 keeps its white primary bank; floor_action switches only
         * the frame-3 accent to the shared fg2 bank. */
        renderFloor(context, assets, x, y, palette, settings, [1, 1, 1, 1]);
        return;
      case "auto-wall":
        /* Type-1 @ walls keep their tile-definition default frame 2;
         * their signed +/-1 byte2 and colouring_action's parity flip combine
         * into the stored native 80/20 orientation. */
        settings.flipX = nativeWallFlip(cell.nativeByte2);
        drawFrame(context, assets, NATIVE_FRAME_RECIPES.wall.sheet,
          NATIVE_FRAME_RECIPES.wall.frames[0], x, y,
          paletteColour(palette, "fg1"), settings);
        return;
      case "ceiling":
        flips = nativeCompositeFlips(cell.nativeByte2);
        settings.flipX = flips.accent;
        renderCeiling(context, assets, x, y, palette, settings, NATIVE_FRAME_RECIPES.ceiling.frames[0]);
        return;
      case "ground-spikes":
        recipe = NATIVE_FRAME_RECIPES["ground-spikes"];
        /* Unlike hanging v, the native X branch never reads byte2 or changes
         * turtle X scale: base, accent and white tip are all unflipped. */
        drawFrame(context, assets, recipe.sheet, recipe.frames[0], x, y - 1,
          paletteColour(palette, "fg1"), {});
        drawFrame(context, assets, recipe.sheet, recipe.frames[1], x, y,
          paletteColour(palette, "fg2"), {});
        drawFrame(context, assets, recipe.sheet, recipe.frames[2], x, y - 3,
          [1, 1, 1, 1], {});
        return;
      case "hanging-spikes":
        /* v has byte2=0. The frame-0x22 lip flips, and the later white tip
         * inherits that transform because the native action never restores it. */
        flips = nativeCompositeFlips(0);
        renderCeiling(context, assets, x, y, palette, { flipX: flips.accent },
          NATIVE_FRAME_RECIPES["hanging-spikes"].frames[0]);
        recipe = NATIVE_FRAME_RECIPES["hanging-spikes"];
        drawFrame(context, assets, recipe.sheet, recipe.frames[2], x, y + CELL,
          [1, 1, 1, 1], { flipX: flips.tip });
        return;
      case "mine":
        /* The editor intentionally previews the inactive native mine. Its only
         * runtime blink is the armed-state overlay, not an idle animation. */
        recipe = NATIVE_FRAME_RECIPES.mine;
        drawFrame(context, assets, recipe.sheet, recipe.frames[0], x, y - 1, paletteColour(palette, "fg1"), settings);
        drawFrame(context, assets, recipe.sheet, recipe.frames[1], x, y, paletteColour(palette, "fg2"), settings);
        drawFrame(context, assets, recipe.sheet, recipe.frames[2], x, y, [1, 1, 1, 1], settings);
        return;
      case "deep-water":
        recipe = NATIVE_FRAME_RECIPES["deep-water"];
        transform = nativeWaterTransform(cell.kind, time, cell.nativeWorldX);
        settings.scaleY = transform.scaleY;
        /* Yule's hooked_high_water_action keeps this native transform but
         * replaces vanilla's heavy fallback texture with tiles[0x65]. */
        drawFrame(context, assets, recipe.sheet, recipe.frames[0], x, y,
          paletteColour(palette, recipe.colour), settings);
        return;
      case "shallow-water":
        recipe = NATIVE_FRAME_RECIPES["shallow-water"];
        transform = nativeWaterTransform(cell.kind, time, cell.nativeWorldX);
        settings.scaleY = transform.scaleY;
        drawFrame(context, assets, recipe.sheet, recipe.frames[0], x, y + transform.offsetY,
          paletteColour(palette, recipe.colour), settings);
        return;
      case "waterfall":
        recipe = NATIVE_FRAME_RECIPES.waterfall;
        frame = fract(-Math.floor(time) / 8) * 8;
        settings.flipX = !!cell.nativeParityFlip !== !!options.mirrored;
        drawHalfFrame(context, assets, recipe.sheet, recipe.frames[0], x, y, paletteColour(palette, "water"), 0, frame, settings);
        drawHalfFrame(context, assets, recipe.sheet, recipe.frames[1], x, y, paletteColour(palette, "water_hi"), 0, frame, settings);
        return;
      case "waterfall-top":
        recipe = NATIVE_FRAME_RECIPES.waterfall;
        frame = recipe.frames[2 + (hash & 1)];
        settings.flipX = !!cell.nativeCapFlip;
        drawFrame(context, assets, "tiles", frame, x, y, paletteColour(palette, "bg1"), settings);
        return;
      case "sword-marker":
        settings.alpha = 0.76;
        drawFrame(context, assets, "misc", 0x00, x, y, paletteColour(palette, "special2"), settings);
        return;
      case "spikeball-marker":
        recipe = NATIVE_FRAME_RECIPES["spikeball-marker"];
        drawFrame(context, assets, recipe.sheet, recipe.frames[0], x, y, [1, 1, 1, 1], settings);
        return;
      case "sun":
        transform = nativeSunPosition(COLS * CELL, ROWS * CELL, !!options.glyphPreview, x, y);
        renderSun(context, assets, transform.x, transform.y, palette, time, !!options.glyphPreview);
        return;
      case "still-goal":
        drawFrame(context, assets, "tiles", NATIVE_FRAME_RECIPES.goal.frames[0], x, y, enemyColour, settings);
        return;
      case "waving-goal":
        sway = 0.5 + Math.sin((time * 3 + cell.nativeWorldX * 15) * Math.PI / 180) * 0.5;
        drawFrame(context, assets, "tiles", NATIVE_FRAME_RECIPES.goal.frames[0], x, y + 4 + sway * 8, enemyColour, settings);
        return;
      case "team-goal":
        colour = parseColour(teamColours[cell.sourceGlyph], paletteColour(palette, "special"));
        drawFrame(context, assets, "tiles", NATIVE_FRAME_RECIPES.goal.frames[0], x, y, colour, settings);
        return;
      case "crowd":
        drawFrame(context, assets, "tiles", 0x3e, x, y, paletteColour(palette, "bg1"), settings);
        /* Native mode 2 draws two left/right mask figures at the cell centre,
         * then another pair eight pixels above. They are full-size synthesized
         * masks, not four scaled character sprites and not a looping jump.
         * crowd_timer changes their seeded pose range during match events; the
         * editor shows the ordinary idle state. */
        var crowdSeed = cell.nativeByte2 || 1;
        var crowdColourA = scaledColour(nativeChandelierColour(crowdSeed,
          unitHash(crowdSeed, cell.nativeWorldX, "crowd-sat-a"), 1, 1), 0.5, 1);
        var crowdColourB = scaledColour(nativeChandelierColour(crowdSeed * 123,
          unitHash(crowdSeed, cell.y, "crowd-sat-b"), 1, 1), 0.5, 1);
        for (var crowdRow = 0; crowdRow < 2; crowdRow += 1) {
          var rowHash = hashCell(cell.nativeWorldX + crowdRow * 17, cell.y, "crowd-idle");
          var leftSprite = 0x30 + (rowHash % 5);
          var rightSprite = 0x30 + ((rowHash >>> 5) % 3);
          var leftJitterX = ((rowHash >>> 9) % 5) - 2;
          var rightJitterX = ((rowHash >>> 13) % 5) - 2;
          var leftJitterY = ((rowHash >>> 17) % 3) - 1;
          var rightJitterY = ((rowHash >>> 19) % 3) - 1;
          var rowY = y - crowdRow * 8;
          drawCrowdMask(context, assets, leftSprite, x - 4 + leftJitterX,
            rowY + leftJitterY, crowdColourA, !!(rowHash & 0x400000));
          drawCrowdMask(context, assets, rightSprite, x + 4 + rightJitterX,
            rowY + rightJitterY, crowdColourB, !!(rowHash & 0x800000));
        }
        return;
      case "score-light":
        frame = Math.floor(time / 30) & 1 ? 0x5c : 0x5b;
        drawFrame(context, assets, "tiles", frame, x, y, paletteColour(palette, "special2"), settings);
        return;
      case "mushrooms":
        frame = 0x5d + (hash % 3);
        drawFrame(context, assets, "tiles", frame, x, y, paletteColour(palette, "special"), settings);
        return;
      case "spinny":
        renderSpinner(context, assets, cell, x, y, palette, time);
        return;
      case "chandelier":
        renderChandelier(context, assets, cell, x, y, palette, time, options);
        return;
      case "scrolling-decor":
        sway = Math.sin(time * 0.25 * Math.PI / 180) * 64;
        recipe = NATIVE_FRAME_RECIPES["scrolling-decor"];
        drawHalfFrame(context, assets, recipe.sheet, recipe.frames[0], x, y, paletteColour(palette, "special"),
          fract(sway / 8) * 8,
          ((time * 0.1) % 8 + 8) % 8, settings);
        return;
      case "tentacle":
        var tentacleA = 0.5 + 0.5 * Math.sin((time * 3 + cell.markerX * 123) * Math.PI / 180);
        var tentacleB = 0.5 + 0.5 * Math.sin((time + cell.markerX * 123) * Math.PI / 180);
        var tentacleDx = Math.sin((time * 30 + cell.markerX * 90) * Math.PI / 180) *
          tentacleB * tentacleB * tentacleB;
        var tentacleDy = 8 + 16 * Math.sin(time * Math.PI / 180) -
          16 * Math.sin((time * 3 + cell.markerX * 123) * Math.PI / 180);
        var tentacleC = 0.5 + 0.5 * Math.sin((time * 5 + cell.markerX * 45 + 360 * tentacleA) * Math.PI / 180);
        settings.flipX = (!!(cell.markerX & 1) !== (tentacleC < 0.5)) !== !!options.mirrored;
        drawFrame(context, assets, "tiles", cell.frame,
          x + tentacleDx,
          y - tentacleDy,
          enemyColour, settings);
        return;
      case "puzzley":
        frame = 0x08 + ((Math.floor(time / 24) + (hash & 3)) & 3);
        drawFrame(context, assets, "tiles", frame, x, y, paletteColour(palette, "bg2"), settings);
        return;
      case "direct":
        drawFrame(context, assets, "tiles", cell.frame, x, y,
          paletteColour(palette, cell.colour || "fg2"), settings);
        return;
      case "static":
        recipe = STATIC_RECIPES[cell.glyph];
        if (!recipe) return;
        frame = recipe.frames ? recipe.frames[hash % recipe.frames.length] : recipe.frame;
        if (recipe.randomFlip) settings.flipX = !!(hash & 1);
        drawFrame(context, assets, "tiles", frame, x, y,
          paletteColour(palette, recipe.colour || "fg2"), settings);
        return;
      default:
        drawFontGlyph(context, assets, cell.sourceGlyph, x, y,
          paletteColour(palette, "special2"), { alpha: 0.84 });
    }
  }

  function ambientNumber(value) {
    var name;
    if (typeof value === "number" && isFinite(value)) return clamp(Math.floor(value), 0, 9);
    name = String(value === undefined || value === null ? "none" : value).toLowerCase();
    if (name === "fumes") name = "boil";
    return Math.max(0, AMBIENT_NAMES.indexOf(name));
  }

  function unitHash(a, b, label) {
    return (hashCell(a, b, label) & 0xffff) / 0xffff;
  }

  function loopPhase(time, period, phase) {
    var value = (time + phase) % period;
    if (value < 0) value += period;
    return value / period;
  }

  function renderAmbientLayer(context, assets, grid, ambient, layer, palette, time, width, height) {
    var i;
    var phase;
    var px;
    var py;
    var speed;
    var scale;
    var frame;
    var tint;
    var direction;
    var x;
    var y;
    var waterCells;

    if (ambient === 1 && layer === 1) {
      /* Native bugs emit on ticks divisible by 16 with a one-in-five gate.
       * Keep a small deterministic set of those long-lived particles rather
       * than the old always-on swarm. */
      for (i = 0; i < 3; i += 1) {
        phase = loopPhase(time, 320, unitHash(i, 1, "bug-phase") * 320);
        px = unitHash(i, 2, "bug-x") * width + Math.sin((time + i * 31) * 0.04) * 10;
        py = phase * (height + 24) - 16;
        drawFrame(context, assets, "misc", 0x1e, px - 8, py - 8, [1, 1, 1, 1], {
          scaleX: 0.55,
          scaleY: 0.55,
          angle: Math.sin((time + i * 17) * 0.08) * 20,
          alpha: 0.82
        });
      }
      return;
    }

    if (ambient === 2 && (layer === 3 || layer === 4)) {
      /* Cloud size controls whether the native particle uses layer 3 or 4. */
      for (i = 0; i < 10; i += 1) {
        scale = 2 + unitHash(i, 3, "cloud-size") * 2;
        if ((scale >= 3 ? 4 : 3) !== layer) continue;
        /* Source-room previews are the left/centre runtime occurrence, where
         * cloud_particle always enters from the left and travels right. */
        direction = 1;
        speed = 0.75 * scale;
        phase = loopPhase(time * speed, width + 180, unitHash(i, 4, "cloud-phase") * (width + 180));
        px = direction > 0 ? phase * (width + 180) - 90 : width + 90 - phase * (width + 180);
        py = 18 + unitHash(i, 5, "cloud-y") * (height - 52);
        tint = mixColour(paletteColour(palette, "bg1"), paletteColour(palette, "bg2"), 0.5, 0.62);
        drawFrame(context, assets, "tiles", 0x2f, px - 8, py - 8, tint, {
          scaleX: scale,
          scaleY: 1 + unitHash(i, 6, "cloud-height") * 0.5,
          flipX: direction < 0,
          alpha: 0.56
        });
      }
      return;
    }

    if (ambient === 3 && layer === 3) {
      tint = scaledColour(mixColour(paletteColour(palette, "bg1"), paletteColour(palette, "bg2"), 0.5), 0.78, 0.52);
      for (i = 0; i < 8; i += 1) {
        direction = i & 1 ? 1 : -1;
        phase = loopPhase(time, 260, unitHash(i, 7, "art-phase") * 260);
        px = unitHash(i, 8, "art-x") * width;
        py = direction > 0 ? height + 18 - phase * (height + 36) : -18 + phase * (height + 36);
        /* art_particle_draw alternates the 1A/1B and 1C/1D pairs while the
         * particle travels vertically; it is not a static decorative tile. */
        frame = direction > 0 ? 0x1a + ((Math.floor(time / 8) + i) & 1) :
          0x1c + ((Math.floor(time / 8) + i) & 1);
        drawFrame(context, assets, "tiles", frame, px - 8, py - 8, tint, {
          flipX: !!(i & 2),
          angle: Math.sin((time + i * 20) * 0.025) * 8,
          alpha: 0.52
        });
      }
      return;
    }

    if (ambient === 4 && layer === 2) {
      tint = paletteColour(palette, "bg1");
      for (i = 0; i < 14; i += 1) {
        px = unitHash(i, 9, "fly-x") * width + Math.sin((time + i * 29) * 0.11) * 12;
        py = unitHash(i, 10, "fly-y") * height + Math.cos((time + i * 41) * 0.09) * 9;
        frame = 0x22 + ((Math.floor(time / 3) + i) & 1);
        drawFrame(context, assets, "misc", frame, px - 8, py - 8, tint, { scaleX: 0.55, scaleY: 0.55, alpha: 0.76 });
        drawFrame(context, assets, "misc", 0x24, px - 8, py - 8, [1, 1, 1, 1], { scaleX: 0.55, scaleY: 0.55, alpha: 0.55 });
      }
      return;
    }

    if ((ambient === 5 && layer === 1) || (ambient === 6 && layer === 2)) {
      tint = ambient === 5 ? paletteColour(palette, "water") : [0.05, 0.04, 0.04, 0.58];
      for (i = 0; i < (ambient === 5 ? 4 : 5); i += 1) {
        phase = loopPhase(time * (ambient === 5 ? 1.2 : 0.42), height + 28,
          unitHash(i, 11, ambient === 5 ? "drip-phase" : "dust-phase") * (height + 28));
        px = unitHash(i, 12, ambient === 5 ? "drip-x" : "dust-x") * width;
        py = phase * (height + 28) - 14;
        drawFrame(context, assets, "misc", 0x08, px - 8, py - 8, tint, {
          scaleX: ambient === 5 ? 0.42 : 0.3,
          scaleY: ambient === 5 ? 0.75 : 0.3,
          alpha: ambient === 5 ? 0.88 : 0.42
        });
      }
      return;
    }

    if (ambient === 7 && layer === 2) {
      for (i = 0; i < 3; i += 1) {
        direction = i & 1 ? 1 : -1;
        phase = loopPhase(time * (0.9 + unitHash(i, 13, "bat-speed") * 0.6), width + 80,
          unitHash(i, 14, "bat-phase") * (width + 80));
        px = direction > 0 ? phase * (width + 80) - 40 : width + 40 - phase * (width + 80);
        py = 28 + unitHash(i, 15, "bat-y") * (height * 0.55) + Math.sin((time + i * 53) * 0.12) * 8;
        frame = 0x4e + ((Math.floor(time / 4) + i) & 1);
        drawFrame(context, assets, "tiles", frame, px - 8, py - 8, paletteColour(palette, "fg2"), {
          flipX: direction < 0,
          scaleX: 0.8 + unitHash(i, 16, "bat-scale") * 0.5,
          scaleY: 0.8 + unitHash(i, 16, "bat-scale") * 0.5,
          alpha: 0.82
        });
      }
      return;
    }

    if (ambient === 8 && layer === 2) {
      for (i = 0; i < 7; i += 1) {
        phase = loopPhase(time * (0.45 + unitHash(i, 17, "bubble-speed") * 0.3), 260,
          unitHash(i, 18, "bubble-phase") * 260);
        px = unitHash(i, 19, "bubble-x") * width + Math.sin((time + i * 47) * 0.055) * 7;
        py = height + 12 - phase * (height * 0.42 + 28);
        frame = unitHash(i, 20, "bubble-kind") < 0.75 ? 0x68 : 0x69;
        drawFrame(context, assets, "tiles", frame, px - 8, py - 8, paletteColour(palette, "water_hi"), {
          scaleX: 0.55 + unitHash(i, 21, "bubble-scale") * 0.45,
          scaleY: 0.55 + unitHash(i, 21, "bubble-scale") * 0.45,
          alpha: 0.78
        });
      }
      return;
    }

    if (ambient === 9 && layer === 0) {
      waterCells = [];
      for (y = 0; y < ROWS; y += 1) {
        for (x = 0; x < COLS; x += 1) if (grid[y][x] === "w") waterCells.push({ x: x, y: y });
      }
      /* The game samples one random map coordinate each tick and only emits
       * when it happens to hit native shallow water. It does not animate every
       * w cell at once. Keep a handful of short-lived deterministic samples. */
      for (i = 0; i < Math.min(5, waterCells.length); i += 1) {
        var position = waterCells[Math.floor(unitHash(i, 22, "boil-cell") * waterCells.length)];
        phase = loopPhase(time, 60, unitHash(i, 23, "boil-phase") * 60);
        if (phase > 0.35) continue;
        px = position.x * CELL + 8 + Math.sin((time + i * 43) * 0.09) * 4;
        py = position.y * CELL + 8 - phase * 30;
        frame = unitHash(i, 24, "boil-kind") < 0.8 ? 0x70 : 0x71 + ((Math.floor(time / 6) + i) & 1);
        drawFrame(context, assets, "tiles", frame, px - 8, py - 8, paletteColour(palette, "water_hi"), {
          scaleX: frame === 0x70 ? 0.45 : 0.7,
          scaleY: frame === 0x70 ? 0.45 : 0.7,
          alpha: 0.8 * (1 - phase)
        });
      }
    }
  }

  function sheetForDefinition(definition, assets, options) {
    var tileset = options.tileset || {};
    var key = definition.sprite_sheet || tileset.sprite_sheet;
    var image;
    if (!key) return null;
    if (key === "builtin:tiles") image = assets.tiles;
    else if (key === "builtin:sprites") image = assets.sprites;
    else if (key === "builtin:misc") image = assets.misc;
    else if (key === "builtin:glyphs") image = assets.font || assets.glyphs;
    else image = (options.externalImages && options.externalImages[key]) ||
      (assets.external && assets.external[key]);
    return image ? { key: key, image: image } : null;
  }

  function inheritedNumber(definition, tileset, key, fallback) {
    if (definition[key] !== undefined) return finite(definition[key], fallback);
    if (tileset[key] !== undefined) return finite(tileset[key], fallback);
    return fallback;
  }

  function customAnimationFrame(definition, time, x, y) {
    var count = Math.max(1, Math.floor(finite(definition.frame_count, 1)));
    var ticks = Math.max(1, Math.floor(finite(definition.frame_ticks, 1)));
    var phase = definition.random_phase ? hashCell(x, y, definition.symbol || definition.id || "?") % count : 0;
    var step = Math.floor(Math.max(0, time) / ticks) + phase;
    var mode = definition.animation || "loop";
    var span;
    if (mode === "once") return Math.min(step, count - 1);
    if (mode === "ping_pong" && count > 1) {
      span = count * 2 - 2;
      step %= span;
      return step < count ? step : span - step;
    }
    return step % count;
  }

  function renderCustomCell(context, assets, cell, x, y, options) {
    var definition = cell.custom;
    var tileset = options.tileset || {};
    var sheet = sheetForDefinition(definition, assets, options);
    var cellW;
    var cellH;
    var padding;
    var index;
    var region;
    var tint;
    var scaleX;
    var scaleY;
    var settings;
    if (!sheet) return false;

    if (/^builtin:/i.test(sheet.key)) {
      cellW = sheet.key === "builtin:glyphs" ? FONT_CELL : CELL;
      cellH = cellW;
      padding = sheet.key === "builtin:glyphs" ? 1 : 0;
    } else {
      cellW = Math.max(1, Math.floor(inheritedNumber(definition, tileset, "cell_w", CELL)));
      cellH = Math.max(1, Math.floor(inheritedNumber(definition, tileset, "cell_h", CELL)));
      padding = Math.max(0, Math.floor(inheritedNumber(definition, tileset, "padding", 0)));
    }
    index = Math.max(0, Math.floor(finite(definition.sprite_index, 0))) +
      customAnimationFrame(definition, finite(options.time, 0), cell.x, cell.y);
    if (sheet.key === "builtin:glyphs") {
      region = {
        x: 1 + (index & 15) * FONT_STEP,
        y: 1 + ((index >> 4) & 15) * FONT_STEP,
        w: FONT_CELL,
        h: FONT_CELL
      };
    } else {
      region = frameCoordinates(sheet.image, index, cellW, cellH, padding);
    }
    if (!region || region.x + region.w > sheet.image.width || region.y + region.h > sheet.image.height) return false;

    tint = parseColour(definition.tint, [1, 1, 1, 1]);
    scaleX = finite(definition.scale_x, 1);
    scaleY = finite(definition.scale_y, 1);
    settings = {
      colour: tint,
      alpha: tint[3],
      angle: finite(definition.angle_degrees, 0),
      scaleX: scaleX,
      scaleY: scaleY,
      flipX: !!(options.mirrored && definition.mirror_with_room)
    };
    drawRegion(
      context,
      sheet.image,
      region,
      x + finite(definition.offset_x, 0) + (CELL - cellW) * 0.5,
      y + finite(definition.offset_y, 0) + (CELL - cellH) * 0.5,
      cellW,
      cellH,
      settings
    );
    return true;
  }

  function prepareAssets(options) {
    if (options.assets && options.assets.tiles && options.assets.sprites && options.assets.misc) {
      return Promise.resolve(options.assets);
    }
    if (builtinAssets) return Promise.resolve(builtinAssets);
    return loadAssets(options.assetOptions || {});
  }

  function renderRoomReady(canvas, gridOrRoom, options, assets) {
    var room = gridOrRoom && gridOrRoom.grid ? gridOrRoom : null;
    var grid = coerceGrid(gridOrRoom);
    var context = contextOf(canvas);
    var palette = resolveAppearance(room, options);
    var cells = buildCells(grid, options);
    var drawAssets = nativeSheetAssets(assets, options);
    var width = COLS * CELL;
    var height = ROWS * CELL;
    var ambient = ambientNumber(options.ambient !== undefined ? options.ambient : (room && room.ambient));
    var time = finite(options.time, 0);
    var gradient;
    var y;
    var x;
    var drawX;
    var cell;
    var customDrawn;

    function drawCellLayer(targetLayer) {
      for (y = 0; y < ROWS; y += 1) {
        for (x = 0; x < COLS; x += 1) {
          cell = cells[y][x];
          if (!cell || cell.layer !== targetLayer) continue;
          drawX = (options.mirrored ? COLS - 1 - x : x) * CELL;
          customDrawn = false;
          if (!cell.custom || !cell.customReplace) {
            renderNativeCell(context, drawAssets, cell, drawX, y * CELL, palette, options);
          }
          if (cell.custom) {
            customDrawn = renderCustomCell(context, assets, cell, drawX, y * CELL, options);
            if (cell.customReplace && !customDrawn) {
              /* Match the runtime's fail-closed native fallback. */
              renderNativeCell(context, drawAssets, cell, drawX, y * CELL, palette, options);
            }
          }
        }
      }
    }

    if (canvas && typeof canvas.getContext === "function" && options.resize !== false) {
      if (canvas.width !== width) canvas.width = width;
      if (canvas.height !== height) canvas.height = height;
      context = contextOf(canvas);
    }
    context.save();
    context.imageSmoothingEnabled = false;
    context.clearRect(0, 0, width, height);
    if (options.background !== false) {
      gradient = context.createLinearGradient(0, 0, 0, height);
      gradient.addColorStop(0, colourCss(palette.bg1));
      gradient.addColorStop(1, colourCss(palette.bg2));
      context.fillStyle = gradient;
      context.fillRect(0, 0, width, height);
    }

    /* Exact native schedule: background map bank, back particle banks, normal
     * map, layer-1 particles, actors, foreground map, then layer-0 particles. */
    drawCellLayer(1);
    renderAmbientLayer(context, drawAssets, grid, ambient, 4, palette, time, width, height);
    renderAmbientLayer(context, drawAssets, grid, ambient, 3, palette, time, width, height);
    drawCellLayer("particle3");
    renderAmbientLayer(context, drawAssets, grid, ambient, 2, palette, time, width, height);
    drawCellLayer(0);
    renderAmbientLayer(context, drawAssets, grid, ambient, 1, palette, time, width, height);
    drawCellLayer("actor");
    drawCellLayer(-1);
    renderAmbientLayer(context, drawAssets, grid, ambient, 0, palette, time, width, height);
    context.restore();
    return canvas;
  }

  function renderRoom(canvas, gridOrRoom, options) {
    options = options || {};
    return prepareAssets(options).then(function (assets) {
      return renderRoomReady(canvas, gridOrRoom, options, assets);
    });
  }

  function representativeCell(glyph) {
    var cell;
    var cells;
    if (LARGE_ART[glyph]) {
      cell = makeCell(glyph, "direct", false, 0);
      cell.frame = LARGE_ART[glyph][1][Math.floor(LARGE_ART[glyph][1].length / 2)];
      cell.colour = glyph === "G" ? "special2" : "fg2";
      return cell;
    }
    if (TENTACLE_FRAMES[glyph]) {
      cell = makeCell(glyph, "tentacle", false, 0);
      cell.frame = TENTACLE_FRAMES[glyph][TENTACLE_FRAMES[glyph].length - 1];
      cell.colour = "enemy";
      cell.segment = TENTACLE_FRAMES[glyph].length - 1;
      cell.segmentCount = TENTACLE_FRAMES[glyph].length;
      cell.markerX = 0;
      cell.markerY = cell.segment;
      return cell;
    }
    cells = [[]];
    emitNative(cells, [[glyph]], glyph, 0, 0);
    return cells[0][0] || makeCell(glyph, "unknown", false, 0);
  }

  function previewCell(glyph, options) {
    var grid = blankGrid();
    var cells;
    var y = 6;
    var x = 16;
    grid[y][x] = glyph;
    cells = buildCells(grid, options || {});
    if (LARGE_ART[glyph]) return representativeCell(glyph);
    if (TENTACLE_FRAMES[glyph]) return representativeCell(glyph);
    return cells[y][x] || makeCell(glyph, "unknown", false, 0);
  }

  function drawGlyphReady(target, glyph, x, y, options, assets) {
    var context = contextOf(target);
    var palette = resolveAppearance(null, options);
    var cell = previewCell(String(glyph || " ").charAt(0), options);
    var definition = customDefinitionMap(options)[String(glyph || " ").charAt(0)];
    var nativeAssets = nativeSheetAssets(assets, options);
    var customDrawn = false;
    cell.x = 0;
    cell.y = 0;
    cell.sourceGlyph = String(glyph || " ").charAt(0);
    if (definition) {
      cell.custom = definition;
      cell.customReplace = definition.native_visual !== "underlay";
    }
    if (!cell.custom || !cell.customReplace) renderNativeCell(context, nativeAssets, cell, x, y, palette, options);
    if (cell.custom) customDrawn = renderCustomCell(context, assets, cell, x, y, options);
    if (cell.customReplace && !customDrawn) renderNativeCell(context, nativeAssets, cell, x, y, palette, options);
    return target;
  }

  function drawGlyph(target, glyph, x, y, options) {
    if (x && typeof x === "object") {
      options = x;
      x = 0;
      y = 0;
    } else if (y && typeof y === "object") {
      options = y;
      y = 0;
    }
    options = options || {};
    x = finite(x, 0);
    y = finite(y, 0);
    return prepareAssets(options).then(function (assets) {
      return drawGlyphReady(target, glyph, x, y, options, assets);
    });
  }

  function renderGlyph(canvas, glyph, options) {
    var context;
    var size;
    options = options || {};
    options.glyphPreview = true;
    if (!canvas || typeof canvas.getContext !== "function") {
      return Promise.reject(new TypeError("renderGlyph expects a canvas element."));
    }
    size = Math.max(CELL, Math.floor(finite(options.size, CELL)));
    if (options.resize !== false) {
      canvas.width = size;
      canvas.height = size;
    }
    context = contextOf(canvas);
    context.clearRect(0, 0, canvas.width, canvas.height);
    return drawGlyph(canvas, glyph, Math.floor((canvas.width - CELL) / 2),
      Math.floor((canvas.height - CELL) / 2), options).then(function () { return canvas; });
  }

  function clearCaches() {
    builtinAssets = null;
    builtinPromise = null;
    builtinKey = "";
    externalAssets = dictionary();
    externalSources = dictionary();
    tintedCache = dictionary();
    crowdMaskCache = dictionary();
  }

  function copyNativeFrameRecipes() {
    var copy = {};
    var key;
    var recipe;
    for (key in NATIVE_FRAME_RECIPES) {
      if (!Object.prototype.hasOwnProperty.call(NATIVE_FRAME_RECIPES, key)) continue;
      recipe = NATIVE_FRAME_RECIPES[key];
      copy[key] = { sheet: recipe.sheet, frames: recipe.frames.slice() };
      if (recipe.colour) copy[key].colour = recipe.colour;
    }
    return copy;
  }

  return {
    COLS: COLS,
    ROWS: ROWS,
    CELL: CELL,
    MAP_DRAW_ORDER: MAP_DRAW_ORDER.slice(),
    PREVIEW_DRAW_STAGES: PREVIEW_DRAW_STAGES.slice(),
    NATIVE_FRAME_RECIPES: copyNativeFrameRecipes(),
    ENGINE_DEFAULTS: ENGINE_DEFAULTS,
    loadAssets: loadAssets,
    renderRoom: renderRoom,
    renderGlyph: renderGlyph,
    drawGlyph: drawGlyph,
    clearCaches: clearCaches,
    /* Exposed for small browser self-tests and editor diagnostics. */
    _coerceGrid: coerceGrid,
    _buildCells: buildCells,
    _nativeSeedByte: nativeSeedByte,
    _nativeWorldX: nativeWorldX,
    _nativeWallFlip: nativeWallFlip,
    _nativeWaterTransform: nativeWaterTransform,
    _nativeSpinnerAlphaBytes: function () { return NATIVE_SPINNER_ALPHA_BYTES.slice(); },
    _nativeCompositeFlips: nativeCompositeFlips,
    _nativeChandelierGeometry: nativeChandelierGeometry,
    _nativeChandelierGlow: nativeChandelierGlow,
    _nativeSunPosition: nativeSunPosition,
    _previewEnemyColour: previewEnemyColour
  };
}));
