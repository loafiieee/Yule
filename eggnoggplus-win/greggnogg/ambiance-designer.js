(function () {
  "use strict";

  var Core = window.GregCore || window.YuleEditorCore;
  var Atlas = window.GregAtlas;
  var Objects = window.GregObjects;
  var Author = window.EntityAuthor;
  var BUILTIN_SHEETS = ["builtin:tiles", "builtin:sprites", "builtin:misc", "builtin:glyphs"];
  var BUILTIN_AMBIENTS = ["none", "bugs", "clouds", "art", "flies", "drips", "dust", "bats", "bubbles", "boil"];
  var LAYER_NAMES = [
    "0 · In front of players",
    "1 · Behind players",
    "2 · Behind normal terrain",
    "3 · Behind background tiles",
    "4 · Furthest background"
  ];
  var dialog = null;
  var working = null;
  var applyChange = null;
  var selectedAmbiance = null;
  var selectedParticle = null;
  var activeTab = "ambiance";
  var previewRoomId = null;
  var previewTick = 0;
  var previewRequest = 0;
  var previewDrawing = false;
  var previewLastFrame = 0;

  function element(tag, text, parent) {
    var node = document.createElement(tag);
    if (text !== undefined) node.textContent = text;
    if (parent) parent.appendChild(node);
    return node;
  }

  function button(text, parent, action, className) {
    var node = element("button", text, parent);
    node.type = "button";
    if (className) node.className = className;
    node.addEventListener("click", function () {
      try { action(); } catch (error) { setStatus(error.message); }
    });
    return node;
  }

  function clone(value) {
    return Core && Core.deepClone ? Core.deepClone(value) : JSON.parse(JSON.stringify(value));
  }

  function ensureCatalog(documentValue) {
    if (!Array.isArray(documentValue.particles)) documentValue.particles = [];
    if (!Array.isArray(documentValue.ambiances)) documentValue.ambiances = [];
    return documentValue;
  }

  function cleanId(value, fallback) {
    var id = String(value || "").toLowerCase().trim().replace(/[^a-z0-9_.-]+/g, "_").replace(/^[^a-z]+/, "").slice(0, 32);
    return id || fallback;
  }

  function uniqueId(prefix, entries) {
    var suffix = 1;
    var candidate = prefix;
    while (entries.some(function (entry) { return entry.id === candidate; })) candidate = prefix + "_" + (++suffix);
    return candidate;
  }

  function currentAmbiance() {
    return working.ambiances.find(function (entry) { return entry.id === selectedAmbiance; }) || null;
  }

  function currentParticle() {
    return working.particles.find(function (entry) { return entry.id === selectedParticle; }) || null;
  }

  function setStatus(message) {
    if (dialog) dialog.querySelector("[data-ambiance-status]").textContent = message || "";
  }

  function numberField(labelText, value, parent, options, change) {
    var label = element("label", labelText, parent);
    var input = element("input", undefined, label);
    input.type = "number";
    input.value = String(value);
    if (options.min !== undefined) input.min = String(options.min);
    if (options.max !== undefined) input.max = String(options.max);
    if (options.step !== undefined) input.step = String(options.step);
    input.addEventListener("change", function () {
      var next = Number(input.value);
      if (!Number.isFinite(next)) { input.value = String(value); return; }
      if (options.min !== undefined) next = Math.max(options.min, next);
      if (options.max !== undefined) next = Math.min(options.max, next);
      input.value = String(next);
      change(next);
      schedulePreview();
    });
    return input;
  }

  function textField(labelText, value, parent, change, rerender) {
    var label = element("label", labelText, parent);
    var input = element("input", undefined, label);
    input.type = "text";
    input.value = value || "";
    input.addEventListener("change", function () {
      change(input.value);
      if (rerender) render();
      else schedulePreview();
    });
    return input;
  }

  function selectField(labelText, value, choices, parent, change) {
    var label = element("label", labelText, parent);
    var select = element("select", undefined, label);
    choices.forEach(function (choice) {
      var option = element("option", choice.label, select);
      option.value = choice.value;
    });
    select.value = String(value);
    select.addEventListener("change", function () { change(select.value); schedulePreview(); });
    return select;
  }

  function checkField(labelText, value, parent, change) {
    var label = element("label", undefined, parent);
    label.className = "ambiance-check";
    var input = element("input", undefined, label);
    input.type = "checkbox";
    input.checked = !!value;
    element("span", labelText, label);
    input.addEventListener("change", function () { change(input.checked); schedulePreview(); });
    return input;
  }

  function listButton(label, active, parent, action) {
    var node = button(label, parent, action);
    node.className = "ambiance-list-button" + (active ? " is-active" : "");
    node.setAttribute("aria-selected", active ? "true" : "false");
    return node;
  }

  function ensureParticle() {
    if (working.particles.length) return working.particles[0];
    var particle = {
      id: "particle",
      name: "Particle",
      visual: {
        sprite_sheet: "builtin:misc",
        sprite_index: 0,
        frame_count: 1,
        frame_ticks: 1,
        tint: [1, 1, 1, 1],
        scale_x: 1,
        scale_y: 1
      },
      lifetime_ticks: 120,
      fade_in_ticks: 0,
      fade_out_ticks: 20
    };
    working.particles.push(particle);
    selectedParticle = particle.id;
    return particle;
  }

  function addParticle() {
    var id = uniqueId("particle", working.particles);
    working.particles.push({
      id: id,
      name: "Particle",
      visual: { sprite_sheet: "builtin:misc", sprite_index: 0, frame_count: 1, frame_ticks: 1, tint: [1, 1, 1, 1], scale_x: 1, scale_y: 1 },
      lifetime_ticks: 120,
      fade_in_ticks: 0,
      fade_out_ticks: 20
    });
    selectedParticle = id;
    render();
  }

  function addAmbiance() {
    var particle = ensureParticle();
    var id = uniqueId("ambiance", working.ambiances);
    working.ambiances.push({
      id: id,
      name: "Ambiance",
      native_ambient: "none",
      emitters: [{
        particle: particle.id,
        count: 16,
        shape: "rectangle",
        area: { x: 0, y: 0, width: 528, height: 192 },
        velocity_x: { min: 0, max: 0 },
        velocity_y: { min: 0, max: 0 },
        acceleration_x: 0,
        acceleration_y: 0,
        rotation_speed: { min: 0, max: 0 },
        particle_layer: 1,
        blend: "alpha",
        mirror_with_room: true
      }]
    });
    selectedAmbiance = id;
    render();
  }

  function renameAmbiance(ambiance, requested) {
    var oldId = ambiance.id;
    var next = cleanId(requested, oldId);
    if (next !== oldId && working.ambiances.some(function (entry) { return entry.id === next; })) throw Error("Another ambiance already uses that ID.");
    ambiance.id = next;
    selectedAmbiance = next;
    (working.rooms || []).forEach(function (room) { if (room.ambient === oldId) room.ambient = next; });
    var defaults = working.defaults && (working.defaults.room || working.defaults);
    if (defaults && defaults.ambient === oldId) defaults.ambient = next;
  }

  function renameParticle(particle, requested) {
    var oldId = particle.id;
    var next = cleanId(requested, oldId);
    if (next !== oldId && working.particles.some(function (entry) { return entry.id === next; })) throw Error("Another particle already uses that ID.");
    particle.id = next;
    selectedParticle = next;
    working.ambiances.forEach(function (ambiance) {
      (ambiance.emitters || []).forEach(function (emitter) { if (emitter.particle === oldId) emitter.particle = next; });
    });
  }

  function renderParticleEditor(parent) {
    var particle = currentParticle();
    if (!particle) {
      element("p", "Create a particle to choose its picture and animation.", parent);
      return;
    }
    particle.visual = particle.visual || {};
    var visual = particle.visual;
    var identity = element("div", undefined, parent);
    identity.className = "ambiance-fields two";
    textField("Name", particle.name, identity, function (value) { particle.name = value.trim() || "Particle"; }, true);
    textField("ID", particle.id, identity, function (value) { renameParticle(particle, value); }, true);

    element("h3", "Picture and animation", parent);
    var visualFields = element("div", undefined, parent);
    visualFields.className = "ambiance-fields three";
    var sheets = BUILTIN_SHEETS.concat(Object.keys(working.assets || {}));
    var declared = working.tileset && Array.isArray(working.tileset.sheets) ? working.tileset.sheets.map(function (sheet) { return sheet.sprite_sheet; }) : [];
    declared.forEach(function (sheet) { if (sheet && sheets.indexOf(sheet) < 0) sheets.push(sheet); });
    selectField("Sprite sheet", visual.sprite_sheet || "builtin:misc", sheets.map(function (key) { return { value: key, label: key }; }), visualFields, function (value) { visual.sprite_sheet = value; });
    numberField("First frame number", visual.sprite_index || 0, visualFields, { min: 0, max: 1000000, step: 1 }, function (value) { visual.sprite_index = Math.floor(value); });
    numberField("Animation frames", visual.frame_count || 1, visualFields, { min: 1, max: 256, step: 1 }, function (value) { visual.frame_count = Math.floor(value); });
    numberField("Ticks per frame", visual.frame_ticks || 1, visualFields, { min: 1, max: 3600, step: 1 }, function (value) { visual.frame_ticks = Math.floor(value); });
    numberField("Starting width", visual.scale_x === undefined ? 1 : visual.scale_x, visualFields, { min: -64, max: 64, step: 0.05 }, function (value) { if(value===0){render();setStatus("Starting width cannot be zero.");return;}visual.scale_x = value; });
    numberField("Starting height", visual.scale_y === undefined ? 1 : visual.scale_y, visualFields, { min: -64, max: 64, step: 0.05 }, function (value) { if(value===0){render();setStatus("Starting height cannot be zero.");return;}visual.scale_y = value; });
    numberField("Ending width", visual.end_scale_x === undefined ? (visual.scale_x === undefined ? 1 : visual.scale_x) : visual.end_scale_x, visualFields, { min: -64, max: 64, step: 0.05 }, function (value) { visual.end_scale_x = value; });
    numberField("Ending height", visual.end_scale_y === undefined ? (visual.scale_y === undefined ? 1 : visual.scale_y) : visual.end_scale_y, visualFields, { min: -64, max: 64, step: 0.05 }, function (value) { visual.end_scale_y = value; });
    var tint = Array.isArray(visual.tint) && visual.tint.length === 4 ? visual.tint : (visual.tint = [1, 1, 1, 1]);
    var colourLabel = element("label", "Starting color", visualFields);
    var colour = element("input", undefined, colourLabel);
    colour.type = "color";
    colour.value = "#" + tint.slice(0, 3).map(function (channel) { return Math.round(Math.max(0, Math.min(1, channel)) * 255).toString(16).padStart(2, "0"); }).join("");
    colour.addEventListener("input", function () {
      var value = parseInt(colour.value.slice(1), 16);
      visual.tint = [((value >> 16) & 255) / 255, ((value >> 8) & 255) / 255, (value & 255) / 255, tint[3]];
      tint = visual.tint;
      schedulePreview();
    });
    numberField("Starting opacity", tint[3], visualFields, { min: 0, max: 1, step: 0.01 }, function (value) { visual.tint[3] = value; });

    var endTint = Array.isArray(visual.end_tint) && visual.end_tint.length === 4 ? visual.end_tint : tint.slice();
    var endColourLabel = element("label", "Ending color", visualFields);
    var endColour = element("input", undefined, endColourLabel);
    endColour.type = "color";
    endColour.value = "#" + endTint.slice(0, 3).map(function (channel) { return Math.round(Math.max(0, Math.min(1, channel)) * 255).toString(16).padStart(2, "0"); }).join("");
    endColour.addEventListener("input", function () {
      var value = parseInt(endColour.value.slice(1), 16);
      var alpha=Array.isArray(visual.end_tint)?visual.end_tint[3]:(Array.isArray(visual.tint)?visual.tint[3]:1);
      visual.end_tint = [((value >> 16) & 255) / 255, ((value >> 8) & 255) / 255, (value & 255) / 255, alpha];
      endTint = visual.end_tint;schedulePreview();
    });
    numberField("Ending opacity", endTint[3], visualFields, { min: 0, max: 1, step: 0.01 }, function (value) { if(!visual.end_tint)visual.end_tint=(Array.isArray(visual.tint)?visual.tint:[1,1,1,1]).slice();visual.end_tint[3] = value;endTint=visual.end_tint; });

    element("h3", "Lifetime transition", parent);
    var transition = element("div", undefined, parent);
    transition.className = "ambiance-fields three";
    numberField("Starting rotation (degrees)", visual.start_rotation === undefined ? 0 : visual.start_rotation, transition, { min: -3600, max: 3600, step: 1 }, function (value) { visual.start_rotation = value; });
    numberField("Ending rotation (degrees)", visual.end_rotation === undefined ? (visual.start_rotation || 0) : visual.end_rotation, transition, { min: -3600, max: 3600, step: 1 }, function (value) { visual.end_rotation = value; });
    selectField("Transition curve", visual.interpolation || "linear", [{value:"linear",label:"Linear"},{value:"ease_in",label:"Ease in"},{value:"ease_out",label:"Ease out"},{value:"ease_in_out",label:"Ease in and out"}], transition, function(value){visual.interpolation=value;});
    var transitionHelp=element("p","Size, color, opacity, and rotation change from their starting values to their ending values over one particle lifetime. An ending size of zero makes the particle shrink away.",parent);transitionHelp.className="ambiance-help";
    button("Reset lifetime transition",parent,function(){["end_tint","end_scale_x","end_scale_y","start_rotation","end_rotation","interpolation"].forEach(function(key){delete visual[key];});render();},"quiet-button");

    var pictureChooser = element("details", undefined, parent);
    pictureChooser.className = "ambiance-picture-chooser";
    element("summary", "Choose a picture from the sheet or import a PNG", pictureChooser);
    var picker = element("canvas", undefined, pictureChooser);
    picker.className = "ambiance-sheet-picker";
    picker.setAttribute("aria-label", "Particle sprite sheet; click a picture to choose its number");
    if (Objects && Objects.imageFor) Objects.imageFor(working, visual.sprite_sheet || "builtin:misc").then(function (image) {
      if (!picker.isConnected || currentParticle() !== particle) return;
      picker.width = image.width;
      picker.height = image.height;
      var context = picker.getContext("2d");
      context.imageSmoothingEnabled = false;
      context.drawImage(image, 0, 0);
      picker.addEventListener("click", function (event) {
        var rect = picker.getBoundingClientRect();
        var x = (event.clientX - rect.left) * image.width / rect.width;
        var y = (event.clientY - rect.top) * image.height / rect.height;
        var mapData = Core.buildDataObject(working);
        var sheets = Author && Author.parseAtlas ? Author.parseAtlas(JSON.stringify(mapData)) : {};
        if (!Author || !Author.spriteRegion) return;
        for (var index = 0; index < 8192; index++) {
          var region;
          try { region = Author.spriteRegion(visual.sprite_sheet, image, index, sheets); }
          catch (ignore) { break; }
          if (x >= region.x && x < region.x + region.w && y >= region.y && y < region.y + region.h) {
            visual.sprite_index = index;
            render();
            return;
          }
        }
      });
    }).catch(function (error) { setStatus(error.message); });

    var importLabel = element("label", "Import a particle PNG", pictureChooser);
    var file = element("input", undefined, importLabel);
    file.type = "file";
    file.accept = "image/png";
    file.addEventListener("change", function () {
      var selected = file.files && file.files[0];
      if (!selected) return;
      if (selected.size > 67108864) { setStatus("Picture exceeds 64 MiB."); return; }
      var reader = new FileReader();
      reader.onerror = function () { setStatus("The picture could not be read."); };
      reader.onload = function () {
        var image = new Image();
        image.onerror = function () { setStatus("The selected file is not a readable PNG."); };
        image.onload = function () {
          if (image.width > 4096 || image.height > 4096 || image.width % 16 || image.height % 16) {
            setStatus("Particle PNGs must be at most 4096×4096 and use a complete 16×16 grid.");
            return;
          }
          var base = String(selected.name || "particles.png").replace(/[^A-Za-z0-9._-]+/g, "_").replace(/\.png$/i, "") || "particles";
          var name = base + ".png";
          var suffix = 2;
          working.assets = working.assets || {};
          while (working.assets[name] !== undefined) name = base + "_" + (suffix++) + ".png";
          working.assets[name] = reader.result;
          working.tileset = working.tileset && typeof working.tileset === "object" ? working.tileset : { tiles: [] };
          working.tileset.sheets = Array.isArray(working.tileset.sheets) ? working.tileset.sheets : [];
          working.tileset.sheets.push({ sprite_sheet: name, cell_w: 16, cell_h: 16, padding: 0 });
          visual.sprite_sheet = name;
          visual.sprite_index = 0;
          render();
        };
        image.src = reader.result;
      };
      reader.readAsDataURL(selected);
    });

    element("h3", "Timing", parent);
    var timing = element("div", undefined, parent);
    timing.className = "ambiance-fields three";
    numberField("Lifetime in game ticks", particle.lifetime_ticks || 120, timing, { min: 1, max: 360000, step: 1 }, function (value) { particle.lifetime_ticks = Math.floor(value); });
    numberField("Fade in", particle.fade_in_ticks || 0, timing, { min: 0, max: 360000, step: 1 }, function (value) { particle.fade_in_ticks = Math.floor(value); });
    numberField("Fade out", particle.fade_out_ticks || 0, timing, { min: 0, max: 360000, step: 1 }, function (value) { particle.fade_out_ticks = Math.floor(value); });
    button("Delete particle", parent, function () {
      working.ambiances.forEach(function (ambiance) { ambiance.emitters = (ambiance.emitters || []).filter(function (emitter) { return emitter.particle !== particle.id; }); });
      working.particles.splice(working.particles.indexOf(particle), 1);
      selectedParticle = working.particles.length ? working.particles[0].id : null;
      render();
    }, "danger-button");
  }

  function rangeFields(labelText, range, parent) {
    var group = element("fieldset", undefined, parent);
    element("legend", labelText, group);
    var row = element("div", undefined, group);
    row.className = "ambiance-fields two";
    numberField("Minimum", range.min || 0, row, { min: -256, max: 256, step: 0.01 }, function (value) { range.min = value; });
    numberField("Maximum", range.max || 0, row, { min: -256, max: 256, step: 0.01 }, function (value) { range.max = value; });
  }

  function renderEmitter(emitter, index, ambiance, parent) {
    var card = element("details", undefined, parent);
    card.className = "ambiance-emitter";
    if (index === 0) card.open = true;
    element("summary", "Particle group " + (index + 1) + " · " + (emitter.particle || "choose a particle type"), card);
    var basic = element("div", undefined, card);
    basic.className = "ambiance-fields three";
    selectField("Particle", emitter.particle, working.particles.map(function (particle) { return { value: particle.id, label: particle.name + " (" + particle.id + ")" }; }), basic, function (value) { emitter.particle = value; });
    numberField("Amount on screen", emitter.count || 1, basic, { min: 1, max: 512, step: 1 }, function (value) { emitter.count = Math.floor(value); });
    selectField("Draw layer", emitter.particle_layer === undefined ? 1 : emitter.particle_layer, LAYER_NAMES.map(function (label, value) { return { value: String(value), label: label }; }), basic, function (value) { emitter.particle_layer = Number(value); });
    selectField("Blending", emitter.blend || "alpha", [{ value: "alpha", label: "Normal transparency" }, { value: "additive", label: "Additive glow" }], basic, function (value) { emitter.blend = value; });
    selectField("Spawn shape", emitter.shape || "rectangle", [{ value: "rectangle", label: "Filled rectangle" }, { value: "ellipse", label: "Filled ellipse" }, { value: "line", label: "Line from top-left to bottom-right" }], basic, function (value) { emitter.shape = value; });
    checkField("Mirror movement and pictures with mirrored rooms", emitter.mirror_with_room !== false, basic, function (value) { emitter.mirror_with_room = value; });

    element("h4", "Spawn shape bounds inside the room (pixels)", card);
    emitter.area = emitter.area || { x: 0, y: 0, width: 528, height: 192 };
    var area = element("div", undefined, card);
    area.className = "ambiance-fields four";
    [["X", "x", -4096, 4096], ["Y", "y", -4096, 4096], ["Width", "width", 0, 8192], ["Height", "height", 0, 8192]].forEach(function (entry) {
      numberField(entry[0], emitter.area[entry[1]] || 0, area, { min: entry[2], max: entry[3], step: 0.25 }, function (value) { emitter.area[entry[1]] = value; });
    });

    element("h4", "Movement", card);
    var timing = element("div", undefined, card);
    timing.className = "ambiance-fields two";
    selectField("Motion timing", emitter.motion_interpolation || "linear", [{ value: "linear", label: "Linear" }, { value: "ease_in", label: "Ease in" }, { value: "ease_out", label: "Ease out" }, { value: "ease_in_out", label: "Ease in and out" }], timing, function (value) { emitter.motion_interpolation = value; });
    var timingHelp = element("p", "Easing changes how quickly particles travel and spin during their lifetime. Linear keeps a steady clock; all choices reach the same final position.", card);
    timingHelp.className = "ambiance-help";
    var motion = element("div", undefined, card);
    motion.className = "ambiance-motion";
    emitter.velocity_x = emitter.velocity_x || { min: 0, max: 0 };
    emitter.velocity_y = emitter.velocity_y || { min: 0, max: 0 };
    emitter.rotation_speed = emitter.rotation_speed || { min: 0, max: 0 };
    rangeFields("Horizontal speed", emitter.velocity_x, motion);
    rangeFields("Vertical speed", emitter.velocity_y, motion);
    rangeFields("Rotation (degrees)", emitter.rotation_speed, motion);
    var acceleration = element("div", undefined, motion);
    acceleration.className = "ambiance-fields two";
    numberField("Horizontal acceleration", emitter.acceleration_x || 0, acceleration, { min: -64, max: 64, step: 0.001 }, function (value) { emitter.acceleration_x = value; });
    numberField("Vertical acceleration", emitter.acceleration_y || 0, acceleration, { min: -64, max: 64, step: 0.001 }, function (value) { emitter.acceleration_y = value; });
    button("Remove particle group", card, function () { ambiance.emitters.splice(index, 1); render(); }, "danger-button");
  }

  function renderAmbianceEditor(parent) {
    var ambiance = currentAmbiance();
    if (!ambiance) {
      element("p", "Create a room effect, then choose it from the room's Effect list.", parent);
      return;
    }
    var identity = element("div", undefined, parent);
    identity.className = "ambiance-fields three";
    textField("Name", ambiance.name, identity, function (value) { ambiance.name = value.trim() || "Ambiance"; }, true);
    textField("ID", ambiance.id, identity, function (value) { renameAmbiance(ambiance, value); }, true);
    selectField("Built-in effect to include", ambiance.native_ambient || "none", BUILTIN_AMBIENTS.map(function (name) { return { value: name, label: name.charAt(0).toUpperCase() + name.slice(1) }; }), identity, function (value) { ambiance.native_ambient = value; });
    element("h3", "Particle groups", parent);
    var count = (ambiance.emitters || []).reduce(function (sum, emitter) { return sum + (Number(emitter.count) || 0); }, 0);
    var budget = element("p", count + " / 512 particle lanes", parent);
    budget.className = count > 512 ? "ambiance-budget is-over" : "ambiance-budget";
    ambiance.emitters = ambiance.emitters || [];
    ambiance.emitters.forEach(function (emitter, index) { renderEmitter(emitter, index, ambiance, parent); });
    button("Add particle group", parent, function () {
      var particle = ensureParticle();
      if (ambiance.emitters.length >= 16) throw Error("An ambiance can have at most 16 emitters.");
      ambiance.emitters.push({ particle: particle.id, count: 8, shape: "rectangle", motion_interpolation: "linear", area: { x: 0, y: 0, width: 528, height: 192 }, velocity_x: { min: 0, max: 0 }, velocity_y: { min: 0, max: 0 }, acceleration_x: 0, acceleration_y: 0, rotation_speed: { min: 0, max: 0 }, particle_layer: 1, blend: "alpha", mirror_with_room: true });
      render();
    });
    button("Delete room effect", parent, function () {
      var oldId = ambiance.id;
      working.ambiances.splice(working.ambiances.indexOf(ambiance), 1);
      (working.rooms || []).forEach(function (room) { if (room.ambient === oldId) room.ambient = "none"; });
      var defaults = working.defaults && (working.defaults.room || working.defaults);
      if (defaults && defaults.ambient === oldId) defaults.ambient = "none";
      selectedAmbiance = working.ambiances.length ? working.ambiances[0].id : null;
      render();
    }, "danger-button");
  }

  function renderSidebar() {
    var ambianceList = dialog.querySelector("[data-ambiance-list]");
    var particleList = dialog.querySelector("[data-particle-list]");
    var ambianceLibrary = dialog.querySelector("[data-ambiance-library]");
    var particleLibrary = dialog.querySelector("[data-particle-library]");
    ambianceList.replaceChildren();
    particleList.replaceChildren();
    ambianceLibrary.hidden = activeTab !== "ambiance";
    particleLibrary.hidden = activeTab !== "particle";
    working.ambiances.forEach(function (ambiance) {
      listButton((ambiance.name || ambiance.id) + " · " + ambiance.id, ambiance.id === selectedAmbiance, ambianceList, function () { selectedAmbiance = ambiance.id; render(); });
    });
    working.particles.forEach(function (particle) {
      listButton((particle.name || particle.id) + " · " + particle.id, particle.id === selectedParticle, particleList, function () { selectedParticle = particle.id; render(); });
    });
  }

  function schedulePreview() {
    previewRequest++;
  }

  function drawPreview() {
    if (!dialog || !dialog.open || !Atlas || !Atlas.renderRoom || previewDrawing) return;
    var canvas = dialog.querySelector("[data-ambiance-preview]");
    var ambiance = currentAmbiance();
    var particle = currentParticle();
    var rooms = Array.isArray(working.rooms) ? working.rooms : [];
    var source = rooms.find(function (room) { return room.id === previewRoomId; }) || rooms[0] || { id: "preview", grid: Array.from({ length: 12 }, function () { return Array(33).fill(" "); }) };
    var room = clone(source);
    var ambiances = working.ambiances;
    if (activeTab === "particle" && particle) {
      var previewId = "greg_particle_preview";
      room.ambient = previewId;
      ambiances = working.ambiances.concat([{
        id: previewId,
        name: "Particle preview",
        native_ambient: "none",
        emitters: [{
          particle: particle.id,
          count: 18,
          area: { x: 16, y: 0, width: 496, height: 176 },
          velocity_x: { min: -0.08, max: 0.08 },
          velocity_y: { min: 0.08, max: 0.28 },
          acceleration_x: 0,
          acceleration_y: 0,
          rotation_speed: { min: -0.4, max: 0.4 },
          particle_layer: 1,
          blend: "alpha",
          mirror_with_room: true
        }]
      }]);
    } else room.ambient = ambiance ? ambiance.id : "none";
    var request = previewRequest;
    previewDrawing = true;
    Promise.resolve(Atlas.renderRoom(canvas, room, {
      grid: room.grid,
      ambient: room.ambient,
      appearance: Core && Core.resolveRoomAppearance ? Core.resolveRoomAppearance(working, source).primary : undefined,
      tileset: working.tileset,
      particles: working.particles,
      ambiances: ambiances,
      mapId: working.id,
      externalImages: working.assets,
      time: previewTick
    })).catch(function (error) {
      if (request === previewRequest) setStatus(error.message);
    }).then(function () {
      previewDrawing = false;
    }, function () {
      previewDrawing = false;
    });
  }

  function render() {
    renderSidebar();
    var ambiancePanel = dialog.querySelector("[data-ambiance-properties]");
    var particlePanel = dialog.querySelector("[data-particle-properties]");
    dialog.querySelector("[data-ambiance-tab]").setAttribute("aria-selected", activeTab === "ambiance" ? "true" : "false");
    dialog.querySelector("[data-particle-tab]").setAttribute("aria-selected", activeTab === "particle" ? "true" : "false");
    dialog.querySelector("[data-ambiance-tab]").classList.toggle("is-active", activeTab === "ambiance");
    dialog.querySelector("[data-particle-tab]").classList.toggle("is-active", activeTab === "particle");
    ambiancePanel.hidden = activeTab !== "ambiance";
    particlePanel.hidden = activeTab !== "particle";
    ambiancePanel.replaceChildren();
    particlePanel.replaceChildren();
    if (activeTab === "ambiance") renderAmbianceEditor(ambiancePanel);
    else renderParticleEditor(particlePanel);
    setStatus("");
    drawPreview();
  }

  function createDialog() {
    dialog = element("dialog", undefined, document.body);
    dialog.className = "ambiance-designer-dialog";
    dialog.innerHTML =
      '<header><h2>Room effects</h2><button type="button" data-ambiance-cancel aria-label="Cancel room-effect edits">×</button></header>' +
      '<nav class="ambiance-tabs" role="tablist" aria-label="Room effect editor"><button type="button" role="tab" data-ambiance-tab>Room effects</button><button type="button" role="tab" data-particle-tab>Particle types</button></nav>' +
      '<div class="ambiance-designer-body"><aside><section data-ambiance-library><h3>Room effects</h3><div data-ambiance-list></div><button type="button" data-add-ambiance>New room effect</button></section><section data-particle-library><h3>Particle types</h3><div data-particle-list></div><button type="button" data-add-particle>New particle type</button></section></aside>' +
      '<main><div class="ambiance-preview-shell"><canvas data-ambiance-preview width="528" height="192" aria-label="Animated room ambiance preview"></canvas></div><section data-ambiance-properties></section><section data-particle-properties></section></main></div>' +
      '<footer><span data-ambiance-status role="status"></span><div><button type="button" data-ambiance-discard>Discard</button><button type="button" data-ambiance-done>Done</button></div></footer>';
    dialog.querySelector("[data-ambiance-cancel]").addEventListener("click", function () { dialog.close(); });
    dialog.querySelector("[data-ambiance-discard]").addEventListener("click", function () { dialog.close(); });
    dialog.querySelector("[data-add-ambiance]").addEventListener("click", function () { try { addAmbiance(); } catch (error) { setStatus(error.message); } });
    dialog.querySelector("[data-add-particle]").addEventListener("click", function () { try { if (working.particles.length >= 64) throw Error("A map can have at most 64 particle definitions."); addParticle(); } catch (error) { setStatus(error.message); } });
    dialog.querySelector("[data-ambiance-tab]").addEventListener("click", function () { activeTab = "ambiance"; render(); });
    dialog.querySelector("[data-particle-tab]").addEventListener("click", function () { activeTab = "particle"; render(); });
    dialog.querySelector("[data-ambiance-done]").addEventListener("click", function () {
      try {
        var lanes = working.ambiances.reduce(function (sum, ambiance) { return sum + (ambiance.emitters || []).reduce(function (inner, emitter) { return inner + (Number(emitter.count) || 0); }, 0); }, 0);
        if (lanes > 512) throw Error("The map uses " + lanes + " particle lanes; the runtime limit is 512.");
        working.particles.forEach(function (particle) {
          if ((particle.fade_in_ticks || 0) > particle.lifetime_ticks || (particle.fade_out_ticks || 0) > particle.lifetime_ticks) throw Error(particle.name + " fades longer than its lifetime.");
          if (!particle.visual || !particle.visual.scale_x || !particle.visual.scale_y) throw Error(particle.name + " must have non-zero width and height scale.");
        });
        applyChange(clone(working));
        dialog.close();
      } catch (error) { setStatus(error.message); }
    });
    function animate(now) {
      window.requestAnimationFrame(animate);
      if (dialog.open && !document.hidden && now - previewLastFrame >= 32) {
        previewLastFrame = now;
        previewTick = Math.floor(now * 0.06);
        drawPreview();
      }
    }
    window.requestAnimationFrame(animate);
  }

  window.openGregAmbianceDesigner = function (documentValue, onApply, ambianceId, roomId) {
    if (!Core) throw Error("Greggnogg core is unavailable.");
    working = ensureCatalog(clone(documentValue));
    applyChange = onApply;
    selectedAmbiance = working.ambiances.some(function (entry) { return entry.id === ambianceId; }) ? ambianceId : (working.ambiances[0] && working.ambiances[0].id);
    selectedParticle = working.particles[0] && working.particles[0].id;
    activeTab = "ambiance";
    previewRoomId = roomId || null;
    previewTick = 0;
    previewRequest++;
    previewDrawing = false;
    previewLastFrame = 0;
    if (!dialog) createDialog();
    render();
    dialog.showModal();
  };
}());
