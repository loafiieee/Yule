(() => {
  "use strict";

  const p = (name, type, description, requirement = "") =>
    [name, type, description, requirement];

  const idFor = name => `api-${name.toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "")}`;

  const fn = (name, signature, summary, params, returns, example, options = {}) => ({
    id: options.id || idFor(name),
    name,
    signature,
    summary,
    params: params || [],
    returns: returns || "No values.",
    example: example || "",
    details: options.details || "",
    errors: options.errors || "",
    notes: options.notes || [],
    tags: options.tags || [],
    kind: options.kind || "function",
    exampleTitle: options.exampleTitle || ""
  });

  const prop = (name, type, summary, example = "", options = {}) =>
    fn(name, `${name} : ${type}`, summary, [], `A ${type} value.`, example, {
      ...options,
      kind: "property",
      tags: [...(options.tags || []), "read-only"]
    });

  const callback = (name, when, callbackSignature, example, options = {}) =>
    fn(name, `${name}(callback)`, `Registers the callback that runs ${when}.`,
      [p("callback", callbackSignature, "Function invoked by the framework.")],
      options.single
        ? "No values. Registration replaces the previous callback for this mod."
        : "A subscription handle whose idempotent remove() method returns true once and false thereafter.",
      example,
      {
        ...options,
        details: options.details || "Register during the entry script so the lifecycle is established before the first frame.",
        notes: [
          ...(options.notes || []),
          ...(options.single ? [] : ["Keep the handle when dynamically reconfiguring a mod; every remaining subscription is removed automatically on unload."]),
          "Callback failures are logged against the owning mod instead of escaping into the native game loop."
        ]
      });

  const groups = [];
  const add = group => groups.push(group);

  add({
    id: "mod",
    label: "mod",
    page: "api/runtime.html",
    description: "Per-package metadata, lifecycle registration, logging, and source-file loading. Every ordinary mod receives its own mod table.",
    lifecycle: "The table exists while the package is loaded. Handler registrations and transient state are discarded on unload or reload.",
    entries: [
      prop("mod.id", "string", "Canonical manifest id for the currently executing package.", "mod.log(\"loaded \" .. mod.id)"),
      prop("mod.name", "string", "Human-readable manifest name.", "mod.log(mod.name .. \" is ready\")"),
      prop("mod.version", "string", "Package version copied from mod.json.", "mod.log(\"version \" .. mod.version)"),
      prop("mod.framework_api", "integer", "Compatibility alias for the framework API major exposed by this build.", "assert(mod.framework_api == mod.api.major)", { notes: ["Retained for API-1 mods. New code should use mod.api.major and capability discovery."] }),
      prop("mod.framework_api_revision", "integer", "Compatibility convenience field for the additive API revision.", "assert(mod.framework_api_revision == mod.api.revision)", { notes: ["Prefer mod.api.revision in new code."] }),
      callback("mod.on_load", "after the entry script finishes and the package becomes active", "function()", `mod.on_load(function()
  mod.log("initialization complete")
end)`, { single: true }),
      callback("mod.on_unload", "before disable, hot reload, or framework shutdown", "function()", `mod.on_unload(function()
  mod.audio.stop_music()
end)`, {
        single: true,
        notes: ["Use this to release logical ownership and stop presentation work. Native resources registered to the mod are also owner-cleaned."]
      }),
      callback("mod.on_frame", "once per rendered frame", "function()", `mod.on_frame(function()
  mod.ui.begin_overlay()
  mod.ui.text_at("FPS overlay", 12, 12)
  mod.ui.end_overlay()
end)`, {
        tags: ["cosmetic"],
        notes: ["Rendering frequency is not deterministic. Do not advance gameplay state here."]
      }),
      callback("mod.on_tick", "immediately before a native gameplay update consumes commands", "function()", `mod.on_tick(function()
  if mod.input.pressed("dash") then
    mod.game.set_input(1, mod.game.CMD_RIGHT, 1)
  end
end)`, {
        tags: ["gameplay"],
        notes: ["Registering this handler permanently classifies the loaded mod instance as gameplay-affecting.", "The handler is suspended during managed online play."]
      }),
      callback("mod.on_tick_post", "immediately after a native gameplay update", "function()", `mod.on_tick_post(function()
  local state = mod.game.snapshot(1)
  -- inspect the completed tick
end)`, {
        tags: ["gameplay"],
        notes: ["Registering this handler classifies the mod as gameplay-affecting and suspends it online."]
      }),
      callback("mod.on_event", "for each translated SDL input/window event", "function(event) -> boolean?", `mod.on_event(function(event)
  if event.type == "keydown" and event.sym == "f8" then
    return true -- consume it
  end
  return false
end)`, {
        params: undefined,
        details: "The event table may contain type, sym, scancode, mod, x, y, button, and other type-specific fields. A synthetic delta_time event exposes mutable value seconds.",
        notes: ["Return true to consume the event.", "Changing delta_time is gameplay-affecting and is refused while online safety is active."]
      }),
      fn("mod.on_layout", "mod.on_layout(state_name, callback)",
        "Registers a callback that runs while the named native or custom screen lays out its controls.",
        [p("state_name", "string", "State name such as main, options, options_paused, remap1, remap2, mods, game, or a custom state."),
         p("callback", "function()", "Layout mutation callback.")],
        "A subscription handle whose idempotent remove() method returns true once and false thereafter.",
        `local layout_subscription = mod.on_layout("main", function()
  local button = mod.ui.find_button_by_label("OPTIONS")
  if button then mod.ui.button_set_pos_ptr(button, 40, 420) end
end)
-- layout_subscription:remove()`,
        { tags: ["cosmetic"], notes: ["Multiple state-specific registrations may coexist and are owner-cleaned on unload.", "Pointer-oriented button calls are native-build-sensitive; prefer owner-safe native_button helpers for new controls."] }),
      fn("mod.log", "mod.log(message)", "Writes an informational line tagged with this mod id.",
        [p("message", "any", "Value converted to text.")], "No values.",
        `mod.log("map count = " .. mod.game.map_count())`),
      fn("mod.warn", "mod.warn(message)", "Writes a warning line tagged with this mod id.",
        [p("message", "any", "Value converted to text.")], "No values.",
        `mod.warn("optional texture was not found")`),
      fn("mod.error", "mod.error(message)", "Writes an error line tagged with this mod id.",
        [p("message", "any", "Value converted to text.")], "No values.",
        `mod.error("configuration is invalid")`),
      fn("mod.info", "mod.info()", "Returns manifest metadata and live framework classification state for the package.",
        [], "A table containing id, name, version, author, description, entry, api_version, api_revision, enabled, folder_path, gameplay_affecting, suspended_online, and gameplay_reason.",
        `local info = mod.info()
mod.log(("enabled=%s suspended=%s"):format(
  tostring(info.enabled), tostring(info.suspended_online)))`,
        { tags: ["read-only"] }),
      fn("mod.get_path", "mod.get_path([relative])", "Resolves the package directory or a path inside it.",
        [p("relative", "string", "Optional mod-relative path.", "optional")],
        "An absolute Windows path string.",
        `local icon_path = mod.get_path("assets/icon.png")`,
        { tags: ["read-only"], notes: ["Use framework asset APIs for registration; this helper only constructs a path."] }),
      fn("mod.dofile", "mod.dofile(relative)", "Loads and executes another Lua source file in the same isolated mod environment.",
        [p("relative", "string", "Lua file below the package directory.")],
        "The loaded chunk's returned values, or a Lua error if loading/execution fails.",
        `local helpers = mod.dofile("lib/helpers.lua")`,
        { errors: "Invalid paths, missing files, syntax errors, and runtime errors raise through the calling mod.", notes: ["Prefer this over require when the file must share this mod's environment and ownership."] })
    ]
  });

  add({
    id: "api",
    label: "mod.api",
    page: "api/runtime.html",
    description: "Machine-readable framework compatibility and feature discovery. Major changes are breaking; revisions and new capability identifiers are additive.",
    entries: [
      prop("mod.api.major", "integer", "Current public API major. Public removals or incompatible contract changes require a new major.", "assert(mod.api.major == 1)"),
      prop("mod.api.revision", "integer", "Current additive revision within this major.", "if mod.api.revision >= 1 then mod.log(\"capability manifests supported\") end"),
      prop("mod.api.capabilities", "string[]", "Sorted array of stable capability identifiers supplied by this build.", `for _, capability in ipairs(mod.api.capabilities) do
  mod.log(capability)
end`, { notes: ["Treat the array as read-only. Use has() for branch decisions and api_requires for mandatory load-time requirements."] }),
      fn("mod.api.has", "mod.api.has(capability)", "Checks a stable capability identifier without raising for an unknown or malformed name.", [p("capability", "string", "Lowercase identifier such as fs.pick_file.")], "Boolean.", `if mod.api.has("fs.pick_file") then
  -- Offer an import button.
end`, { tags: ["read-only"] }),
      fn("mod.api.require", "mod.api.require(capability)", "Asserts a capability at runtime when a conditional module is loaded.", [p("capability", "string", "Lowercase stable capability identifier.")], "true when available; false plus an explanation when well-formed but unavailable.", `local ok, err = mod.api.require("content.tiles.v1")
if not ok then mod.warn(err) end`, { errors: "Malformed identifiers raise a Lua argument error. For a hard package requirement, declare api_requires in mod.json so loading fails before any entry-script side effects.", tags: ["read-only"] })
    ]
  });

  add({
    id: "config",
    label: "config",
    page: "api/runtime.html",
    description: "Line-based user configuration declared by a mod manifest. It is exposed as the config global in each ordinary mod environment.",
    lifecycle: "Values load before the entry script. set writes the in-memory value and persists it through the framework configuration path.",
    entries: [
      prop("config.path", "string", "Absolute path to the package configuration file.", `mod.log(config.path)`),
      fn("config.get", "config.get(key [, default])", "Reads a typed configuration value by key.",
        [p("key", "string", "Configuration key."), p("default", "boolean | number | string | nil", "Value returned when the key is absent.", "optional")],
        "The stored boolean, number, or string; otherwise default.",
        `local opacity = config.get("opacity", 0.85)`,
        { tags: ["read-only"] }),
      fn("config.set", "config.set(key, value)", "Updates and persists a configuration value.",
        [p("key", "string", "Configuration key."), p("value", "boolean | number | string", "New value.")],
        "No values.",
        `config.set("show_hitboxes", true)`,
        { notes: ["Use values representable by the line-based configuration format."] }),
      fn("config.on_action", "config.on_action(key, callback)", "Binds an action-type Mods menu row to a callback.",
        [p("key", "string", "Manifest configuration item key with type action."), p("callback", "function()", "Function invoked when the user activates the row.")],
        "A subscription handle whose idempotent remove() method returns true once and false thereafter.",
        `local clear_subscription = config.on_action("clear_cache", function()
  mod.audio.clear_generated()
end)`,
        { notes: ["Actions stop while a gameplay-affecting mod is suspended online.", "Action subscriptions are owner-cleaned on unload."] })
    ]
  });

  add({
    id: "storage",
    label: "storage / mod.storage",
    page: "api/runtime.html",
    description: "Mod-owned persistent key/value storage with explicit saves and transactional schema migration.",
    lifecycle: "Storage loads before the entry script. Values remain dirty in memory until save or a successful migration commit.",
    entries: [
      prop("storage.path", "string", "Absolute storage file path, normally storage.cfg.", `mod.log(storage.path)`),
      fn("storage.get", "storage.get(key [, default])", "Reads one stored value.",
        [p("key", "string", "Storage key."), p("default", "boolean | number | string | nil", "Fallback for an absent key.", "optional")],
        "Stored boolean, number, or string; otherwise default.",
        `local launches = storage.get("launches", 0)`, { tags: ["read-only"] }),
      fn("storage.set", "storage.set(key, value)", "Changes an in-memory storage value.",
        [p("key", "string", "Storage key."), p("value", "boolean | number | string", "Persistable value.")],
        "No values. Call storage.save() to force persistence.",
        `storage.set("launches", storage.get("launches", 0) + 1)`),
      fn("storage.delete", "storage.delete(key)", "Removes a key from storage.",
        [p("key", "string", "Storage key to remove.")], "No values.",
        `storage.delete("legacy_cache")`),
      fn("storage.save", "storage.save()", "Atomically writes the current storage table.",
        [], "true on success; false plus an error string on failure.",
        `local ok, err = storage.save()
if not ok then mod.error(err) end`,
        { errors: "Write or replacement failures return false and leave the previous committed file intact." }),
      fn("storage.schema", "storage.schema()", "Returns the current integer schema version.",
        [], "Integer schema version; zero for an unversioned store.",
        `if storage.schema() < 2 then
  mod.warn("migration required")
end`, { tags: ["read-only"] }),
      fn("storage.set_schema", "storage.set_schema(version)", "Sets the in-memory schema version.",
        [p("version", "integer", "Non-negative target schema.")], "No values. Call save to persist it.",
        `storage.set_schema(2)
storage.save()`),
      fn("storage.migrate", "storage.migrate(target, callback)", "Runs a transactional storage migration and commits only on success.",
        [p("target", "integer", "Target schema version."), p("callback", "function(from, target)", "Migration body; use storage get/set/delete inside it.")],
        "true when already current or successfully migrated; false plus an error when the callback or commit fails.",
        `local ok, err = storage.migrate(2, function(from, target)
  if from < 2 then
    storage.set("display_mode", storage.get("mode", "compact"))
    storage.delete("mode")
  end
end)`,
        { errors: "A callback error or failed commit restores the pre-migration values and schema.", notes: ["Do not perform unrelated external side effects inside the callback; only the storage transaction can roll back."] })
    ]
  });
  {
    const storageGroup = groups.find(group => group.id === "storage");
    const primary = [...storageGroup.entries];
    for (const entry of primary) {
      const aliasName = entry.name.replace(/^storage/, "mod.storage");
      storageGroup.entries.push({
        ...entry,
        id: idFor(aliasName),
        name: aliasName,
        signature: entry.signature.replace(/^storage/, "mod.storage"),
        summary: `Alias of ${entry.name} on the mod table. ${entry.summary}`,
        example: entry.example.replaceAll("storage.", "mod.storage."),
        kind: entry.kind === "property" ? "property alias" : "function alias",
        tags: [...(entry.tags || []), "alias"]
      });
    }
  }

  add({
    id: "interop",
    label: "interop / mod.interop",
    page: "api/runtime.html",
    description: "Process-local service exchange between enabled mods with version constraints and owner-aware function guards.",
    lifecycle: "Published services disappear when the provider unloads. Snapshotted functions stop working while the provider is disabled or suspended online.",
    entries: [
      fn("interop.provide", "interop.provide(namespace, version, api_table)", "Publishes a versioned service owned by the current mod.",
        [p("namespace", "string", "Globally unique service name, normally author.mod:service."), p("version", "string", "Semantic version."), p("api_table", "table", "Values and functions to expose.")],
        "true on success; false plus an error string when the namespace/version/table is invalid or conflicts.",
        `interop.provide("loaf.camera:grid", "1.0.0", {
  snap = function(x) return math.floor(x / 16) * 16 end
})`,
        { notes: ["Tables are recursively snapshotted; later mutation of the source table does not silently change consumers."] }),
      fn("interop.require", "interop.require(namespace [, range])", "Resolves a compatible published service.",
        [p("namespace", "string", "Service namespace."), p("range", "string", "Optional semantic-version constraint such as >=1 <2.", "optional")],
        "API table and provider version on success; nil plus an error string otherwise.",
        `local grid, version = interop.require("loaf.camera:grid", ">=1 <2")
if grid then local x = grid.snap(37) end`,
        { tags: ["read-only"], errors: "Missing, disabled, suspended, or version-incompatible providers return nil plus a diagnostic." })
    ]
  });
  {
    const interopGroup = groups.find(group => group.id === "interop");
    const primary = [...interopGroup.entries];
    for (const entry of primary) {
      const aliasName = entry.name.replace(/^interop/, "mod.interop");
      interopGroup.entries.push({
        ...entry,
        id: idFor(aliasName),
        name: aliasName,
        signature: entry.signature.replace(/^interop/, "mod.interop"),
        summary: `Alias of ${entry.name} on the mod table. ${entry.summary}`,
        example: entry.example.replaceAll("interop.", "mod.interop."),
        kind: "function alias",
        tags: [...(entry.tags || []), "alias"]
      });
    }
  }

  add({
    id: "console",
    label: "mod.console",
    page: "api/runtime.html",
    description: "Owner-scoped, namespaced commands integrated with the native console's autocomplete and help.",
    lifecycle: "Each command belongs to its registering mod and is removed automatically on unload or reload.",
    entries: [
      fn("mod.console.register", "mod.console.register(local_name, spec)", "Registers mod.<mod-id>.<local-name> with help metadata and a raw or typed Lua handler.",
        [p("local_name", "string", "Package-local command name using letters, numbers, dots, underscores, or hyphens."),
         p("spec", "table", "help, optional usage, arguments='raw' or typed descriptors, gameplay flag, and handler function.")],
        "A handle containing canonical name and idempotent remove(); nil plus an error for duplicate/allocation failure.",
        `local command = mod.console.register("greet", {
  help = "Greet a player.",
  arguments = {
    { name="name", type="string" },
    { name="excited", type="boolean", optional=true },
  },
  handler = function(name, excited)
    return "Hello, " .. name .. (excited and "!" or ".")
  end,
})`,
        { notes: ["The console exposes this example as mod.<manifest-id>.greet.", "Typed values are validated before the handler runs; quoted string tokens may contain spaces.", "gameplay=true classifies and suspends the owning mod under managed online safety."] })
    ]
  });

  add({
    id: "input",
    label: "mod.input",
    page: "api/runtime.html",
    description: "Named, user-rebindable mod actions with held and frame-edge queries.",
    lifecycle: "Register bindings during load. Persisted overrides are applied after the entry script registers its names.",
    entries: [
      fn("mod.input.bind", "mod.input.bind(key, default_binding, label)", "Registers a named input action for this mod.",
        [p("key", "string", "Stable package-local action key."), p("default_binding", "string", "Key or controller binding name."), p("label", "string", "Text shown in Options → Mods.")],
        "true on success; false plus an error for invalid or duplicate declarations.",
        `mod.input.bind("toggle_overlay", "f8", "Toggle debug overlay")`),
      fn("mod.input.get", "mod.input.get(key)", "Returns the current binding name.",
        [p("key", "string", "Registered action key.")], "Binding string, or nil for an unknown action.",
        `mod.log(mod.input.get("toggle_overlay") or "unbound")`, { tags: ["read-only"] }),
      fn("mod.input.set", "mod.input.set(key, binding)", "Changes and persists an action binding.",
        [p("key", "string", "Registered action key."), p("binding", "string", "Recognized binding name.")],
        "true on success; false plus an error for an unknown action or binding.",
        `mod.input.set("toggle_overlay", "f9")`),
      fn("mod.input.clear", "mod.input.clear(key)", "Removes a user override and restores the declared default.",
        [p("key", "string", "Registered action key.")], "true on success; false for an unknown action.",
        `mod.input.clear("toggle_overlay")`),
      fn("mod.input.down", "mod.input.down(key)", "Tests whether the action is currently held.",
        [p("key", "string", "Registered action key.")], "Boolean.",
        `if mod.input.down("slow_pan") then camera_speed = 0.25 end`, { tags: ["read-only"] }),
      fn("mod.input.pressed", "mod.input.pressed(key)", "Tests the current frame's up-to-down edge.",
        [p("key", "string", "Registered action key.")], "Boolean; true only on the press edge.",
        `if mod.input.pressed("toggle_overlay") then visible = not visible end`, { tags: ["read-only"] }),
      fn("mod.input.released", "mod.input.released(key)", "Tests the current frame's down-to-up edge.",
        [p("key", "string", "Registered action key.")], "Boolean; true only on the release edge.",
        `if mod.input.released("charge") then fire_charge() end`, { tags: ["read-only"] }),
      fn("mod.input.list", "mod.input.list()", "Lists every registered action and its current conflict state.",
        [], "Array of tables with key, label, binding, and conflict.",
        `for _, row in ipairs(mod.input.list()) do
  mod.log(("%s = %s"):format(row.key, row.binding))
end`, { tags: ["read-only"] })
    ]
  });

  add({
    id: "ui",
    label: "mod.ui",
    page: "api/ui.html",
    description: "Immediate-mode drawing, responsive widgets, custom screens, native menu controls, and pointer-level compatibility helpers.",
    lifecycle: "Draw between begin_overlay and end_overlay from on_frame or a custom state's render callback. Owner-safe native controls are removed with the mod.",
    entries: []
  });

  const ui = groups.find(group => group.id === "ui").entries;
  ui.push(
    fn("mod.ui.state_name", "mod.ui.state_name()", "Returns the canonical current game/custom-screen name.", [], "String such as main, options, game, mods, or a custom state id.", `if mod.ui.state_name() == "game" then draw_hud() end`, { tags: ["read-only"] }),
    fn("mod.ui.state_ptr", "mod.ui.state_ptr()", "Returns the native pointer value for the current state.", [], "Integer pointer, or zero when no state is active.", `local ptr = mod.ui.state_ptr()`, { tags: ["read-only"], notes: ["Native addresses are build-specific. Prefer state_name or is_state unless integrating with audited native code."] }),
    fn("mod.ui.is_state", "mod.ui.is_state(name)", "Tests the current state by canonical name.", [p("name", "string", "Native or custom state name.")], "Boolean.", `if mod.ui.is_state("main") then draw_menu_badge() end`, { tags: ["read-only"] }),
    fn("mod.ui.screen_size", "mod.ui.screen_size()", "Returns the current drawable UI dimensions.", [], "width, height as two numbers.", `local w, h = mod.ui.screen_size()
mod.ui.text_at("right", w - 80, h - 24)`, { tags: ["read-only"] }),
    fn("mod.ui.mouse_pos", "mod.ui.mouse_pos()", "Returns the mouse position in framework UI coordinates.", [], "x, y as numbers.", `local x, y = mod.ui.mouse_pos()`, { tags: ["read-only"] }),
    fn("mod.ui.mouse_buttons", "mod.ui.mouse_buttons([button])", "Reads current mouse-button state.", [p("button", "integer | string", "Optional button selector; omit for the complete state.", "optional")], "A boolean for one button or a table describing all buttons.", `if mod.ui.mouse_buttons("left") then dragging = true end`, { tags: ["read-only"] }),
    fn("mod.ui._set_default_cursor_visible", "mod.ui._set_default_cursor_visible(visible)", "Internal bridge used by custom-state helpers to control the framework-owned native cursor.", [p("visible", "boolean", "Desired default cursor visibility.")], "No values.", `-- Internal: custom-state code calls this for you.`, { tags: ["internal"], notes: ["Not a stable public authoring surface. Use define_state cursor options."] }),
    fn("mod.ui._draw_native_cursor", "mod.ui._draw_native_cursor([options])", "Internal low-level native-cursor draw bridge.", [p("options", "table", "Internal cursor transform options.", "optional")], "No values.", `-- Internal: prefer mod.ui.draw_cursor().`, { tags: ["internal"], notes: ["Direct use can double-render the cursor."] }),
    fn("mod.ui.hitbox", "mod.ui.hitbox(x, y, width, height [, point_x, point_y])", "Tests whether a point lies inside a rectangle.", [p("x", "number", "Left edge."), p("y", "number", "Top edge."), p("width", "number", "Width."), p("height", "number", "Height."), p("point_x", "number", "Test X; defaults to mouse X.", "optional"), p("point_y", "number", "Test Y; defaults to mouse Y.", "optional")], "Boolean.", `local hovered = mod.ui.hitbox(20, 20, 160, 32)`, { tags: ["read-only"] }),
    fn("mod.ui.rect", "mod.ui.rect(x, y, width, height [, options])", "Draws a filled rectangle with optional color/style values.", [p("x, y", "number", "Top-left position."), p("width, height", "number", "Rectangle size."), p("options", "table", "color and style overrides.", "optional")], "No values.", `mod.ui.rect(20, 20, 220, 80, { color={0.05,0.06,0.07,0.9} })`, { tags: ["cosmetic"] }),
    fn("mod.ui.fill_rect", "mod.ui.fill_rect(x, y, width, height [, color])", "Low-level filled-rectangle alias.", [p("x, y", "number", "Top-left position."), p("width, height", "number", "Rectangle size."), p("color", "RGBA table", "Optional normalized color.", "optional")], "No values.", `mod.ui.fill_rect(20, 20, 100, 24, {0.2,0.7,0.6,1})`, { tags: ["cosmetic"] }),
    fn("mod.ui.border", "mod.ui.border(x, y, width, height [, options])", "Draws a rectangular border.", [p("x, y", "number", "Top-left position."), p("width, height", "number", "Border bounds."), p("options", "table", "Color and thickness options.", "optional")], "No values.", `mod.ui.border(20, 20, 220, 80, { color={1,1,1,0.4} })`, { tags: ["cosmetic"] }),
    fn("mod.ui.stroke_rect", "mod.ui.stroke_rect(x, y, width, height [, color])", "Low-level outline-rectangle alias.", [p("x, y", "number", "Top-left position."), p("width, height", "number", "Border bounds."), p("color", "RGBA table", "Optional normalized color.", "optional")], "No values.", `mod.ui.stroke_rect(20, 20, 100, 24, {1,1,1,1})`, { tags: ["cosmetic"] }),
    fn("mod.ui.line", "mod.ui.line(x1, y1, x2, y2 [, options])", "Draws a line segment.", [p("x1, y1", "number", "Start point."), p("x2, y2", "number", "End point."), p("options", "table", "Color/style options.", "optional")], "No values.", `mod.ui.line(20, 50, 240, 50, { color={1,0.5,0.2,1} })`, { tags: ["cosmetic"] }),
    fn("mod.ui.measure_text", "mod.ui.measure_text(text [, scale])", "Measures native-font text before layout.", [p("text", "string", "Text to measure."), p("scale", "number", "Text scale.", "optional")], "width, height as numbers.", `local tw, th = mod.ui.measure_text("READY", 1.2)`, { tags: ["read-only"] }),
    fn("mod.ui.readable_scale", "mod.ui.readable_scale([requested])", "Clamps or derives a scale that stays legible for the current drawable size.", [p("requested", "number", "Preferred scale.", "optional")], "Normalized text scale number.", `local scale = mod.ui.readable_scale(0.9)`, { tags: ["read-only"] }),
    fn("mod.ui.text_scale_factor", "mod.ui.text_scale_factor([requested])", "Alias of readable_scale.", [p("requested", "number", "Preferred scale.", "optional")], "Normalized text scale number.", `local scale = mod.ui.text_scale_factor(1)`, { tags: ["read-only"], notes: ["Use readable_scale in new code."] }),
    fn("mod.ui.flush", "mod.ui.flush()", "Flushes queued immediate-mode geometry to the active renderer.", [], "No values.", `mod.ui.rect(0, 0, 80, 20)
mod.ui.flush()`, { tags: ["cosmetic"], notes: ["Normally end_overlay handles flushing. Call directly only when ordering against native rendering requires it."] }),
    fn("mod.ui.begin_overlay", "mod.ui.begin_overlay()", "Begins a framework overlay draw batch.", [], "No values.", `mod.ui.begin_overlay()
-- draw calls
mod.ui.end_overlay()`, { tags: ["cosmetic"] }),
    fn("mod.ui.end_overlay", "mod.ui.end_overlay()", "Finishes and submits the current overlay batch.", [], "No values.", `mod.ui.begin_overlay()
mod.ui.text_at("hello", 10, 10)
mod.ui.end_overlay()`, { tags: ["cosmetic"], notes: ["Balance each successful begin_overlay with end_overlay in the same callback."] }),
    fn("mod.ui.sheet_base", "mod.ui.sheet_base(sheet_id)", "Returns the first global sprite id assigned to a registered mod sheet.", [p("sheet_id", "string", "Owner-local spritesheet id.")], "Integer base id, or nil plus an error.", `local base = mod.ui.sheet_base("icons")`, { tags: ["read-only"] }),
    fn("mod.ui.sprite_id", "mod.ui.sprite_id(sheet_id, index)", "Resolves a sheet-local cell index to a global sprite id.", [p("sheet_id", "string", "Owner-local spritesheet id."), p("index", "integer", "Zero-based cell index.")], "Integer sprite id, or nil plus an error.", `local warning = mod.ui.sprite_id("icons", 3)`, { tags: ["read-only"] }),
    fn("mod.ui.draw_sprite", "mod.ui.draw_sprite(sprite_id, x, y [, options])", "Draws a native or mod-registered sprite with presentation transforms.", [p("sprite_id", "integer", "Global sprite id."), p("x, y", "number", "Draw position."), p("options", "table", "scale, scale_x, scale_y, angle, tint, origin, flip, and related presentation values.", "optional")], "No values.", `mod.ui.draw_sprite(icon, 120, 80, {
  scale=2, tint={1, 0.8, 0.7, 1}
})`, { tags: ["cosmetic"] }),
    fn("mod.ui.layout", "mod.ui.layout([options])", "Creates a responsive immediate-mode layout cursor.", [p("options", "table", "Origin, width, padding, gap, scale, and column settings.", "optional")], "Layout table consumed by cursor, next_row, and high-level widgets.", `local layout = mod.ui.layout({ x=24, y=24, width=360, gap=8 })`, { tags: ["read-only"] }),
    fn("mod.ui.cursor", "mod.ui.cursor(layout)", "Returns the current insertion position for a layout.", [p("layout", "table", "Layout returned by ui.layout.")], "x, y as numbers.", `local x, y = mod.ui.cursor(layout)`, { tags: ["read-only"] }),
    fn("mod.ui.next_row", "mod.ui.next_row(layout [, height])", "Advances a layout cursor to the next row.", [p("layout", "table", "Mutable layout cursor."), p("height", "number", "Consumed row height.", "optional")], "Updated y position.", `mod.ui.text("Title", layout)
mod.ui.next_row(layout, 28)`, { tags: ["cosmetic"] }),
    fn("mod.ui.text", "mod.ui.text(text, layout [, options])", "Draws text at a layout cursor and advances it.", [p("text", "string", "Text to draw."), p("layout", "table", "Layout cursor."), p("options", "table", "Scale, color, and spacing.", "optional")], "Text width and height.", `mod.ui.text("Settings", layout, { scale=1.2 })`, { tags: ["cosmetic"] }),
    fn("mod.ui.text_at", "mod.ui.text_at(text, x, y [, scale, r, g, b, a])", "Draws native-font text at an explicit position.", [p("text", "string", "Text to draw."), p("x, y", "number", "Position."), p("scale", "number", "Optional scale.", "optional"), p("r, g, b, a", "number", "Optional normalized color channels.", "optional")], "No values.", `mod.ui.text_at("P1", 18, 18, 1, 1, 0.8, 0.2, 1)`, { tags: ["cosmetic"] }),
    fn("mod.ui.button", "mod.ui.button(id, label, layout [, options])", "Draws an immediate-mode button at a layout cursor.", [p("id", "string", "Stable widget id."), p("label", "string", "Visible label."), p("layout", "table", "Layout cursor."), p("options", "table", "Size, disabled, tooltip, and style overrides.", "optional")], "true on activation; false otherwise.", `if mod.ui.button("apply", "Apply", layout) then save() end`, { tags: ["cosmetic"] }),
    fn("mod.ui.button_at", "mod.ui.button_at(id, label, x, y, width, height [, options])", "Draws an immediate-mode button at explicit bounds.", [p("id", "string", "Stable widget id."), p("label", "string", "Visible label."), p("x, y", "number", "Top-left position."), p("width, height", "number", "Hit and draw size."), p("options", "table", "Disabled and style options.", "optional")], "true on activation; false otherwise.", `if mod.ui.button_at("ok", "OK", 40, 90, 120, 28) then close() end`, { tags: ["cosmetic"] }),
    fn("mod.ui.native_button", "mod.ui.native_button(id, label, action [, options])", "Creates or updates an owner-tracked native menu button.", [p("id", "string", "Stable mod-local control id."), p("label", "string", "Native button text."), p("action", "function | string", "Activation callback or supported action."), p("options", "table", "State, position, size, and layout options.", "optional")], "Opaque owner-safe button handle, or nil plus an error.", `local button = mod.ui.native_button("open", "MY SCREEN", function()
  mod.ui.enter_state("settings")
end)`, { tags: ["cosmetic"], notes: ["Prefer this surface to raw pointer helpers. The framework removes the control when its owner unloads."] }),
    fn("mod.ui.native_set_pos", "mod.ui.native_set_pos(handle, x, y)", "Moves an owner-tracked native button.", [p("handle", "button handle", "Handle returned by native_button."), p("x, y", "number", "New position.")], "true on success; false plus an error otherwise.", `mod.ui.native_set_pos(button, 48, 360)`, { tags: ["cosmetic"] }),
    fn("mod.ui.native_set_layout", "mod.ui.native_set_layout(handle, layout)", "Applies native layout metadata to an owner-tracked button.", [p("handle", "button handle", "Owned native button."), p("layout", "table", "Native layout fields.")], "true on success; false plus an error.", `mod.ui.native_set_layout(button, { anchor="bottom-left", order=3 })`, { tags: ["cosmetic"] }),
    fn("mod.ui.native_resize", "mod.ui.native_resize(handle, width, height)", "Resizes an owner-tracked native button.", [p("handle", "button handle", "Owned native button."), p("width, height", "number", "New size.")], "true on success; false plus an error.", `mod.ui.native_resize(button, 190, 28)`, { tags: ["cosmetic"] }),
    fn("mod.ui.native_set_text_scale", "mod.ui.native_set_text_scale(handle, scale)", "Changes native label scale for an owner-tracked button.", [p("handle", "button handle", "Owned native button."), p("scale", "number", "Positive label scale.")], "true on success; false plus an error.", `mod.ui.native_set_text_scale(button, 0.85)`, { tags: ["cosmetic"] }),
    fn("mod.ui.native_hide", "mod.ui.native_hide(handle [, hidden])", "Hides or shows an owner-tracked native button.", [p("handle", "button handle", "Owned native button."), p("hidden", "boolean", "Defaults to true.", "optional")], "true on success; false plus an error.", `mod.ui.native_hide(button, not available)`, { tags: ["cosmetic"] }),
    fn("mod.ui.native_remove", "mod.ui.native_remove(handle)", "Removes an owner-tracked native button immediately.", [p("handle", "button handle", "Owned native button.")], "true on success; false plus an error.", `mod.ui.native_remove(button)`, { tags: ["cosmetic"] })
  );

  const pointerUi = [
    ["find_button_by_label", "mod.ui.find_button_by_label(label)", [p("label", "string", "Exact native label.")], "Integer native button pointer or nil.", `local ptr = mod.ui.find_button_by_label("OPTIONS")`, "Finds a native button by visible label."],
    ["find_button_by_action_ptr", "mod.ui.find_button_by_action_ptr(action_ptr)", [p("action_ptr", "integer", "Native action-function address.")], "Integer native button pointer or nil.", `local ptr = mod.ui.find_button_by_action_ptr(0x401000)`, "Finds a native button by its action pointer."],
    ["button_rect_ptr", "mod.ui.button_rect_ptr(pointer)", [p("pointer", "integer", "Native button pointer.")], "x, y, width, height or nil plus an error.", `local x, y, w, h = mod.ui.button_rect_ptr(ptr)`, "Reads native button bounds."],
    ["button_set_pos_ptr", "mod.ui.button_set_pos_ptr(pointer, x, y)", [p("pointer", "integer", "Native button pointer."), p("x, y", "number", "New position.")], "true on success; false plus an error.", `mod.ui.button_set_pos_ptr(ptr, 40, 400)`, "Moves a native button through a raw pointer."],
    ["button_set_label_ptr", "mod.ui.button_set_label_ptr(pointer, label)", [p("pointer", "integer", "Native button pointer."), p("label", "string", "New text.")], "true on success; false plus an error.", `mod.ui.button_set_label_ptr(ptr, "SETTINGS")`, "Changes a native button label through a raw pointer."],
    ["button_invoke_ptr", "mod.ui.button_invoke_ptr(pointer)", [p("pointer", "integer", "Native button pointer.")], "true on success; false plus an error.", `mod.ui.button_invoke_ptr(ptr)`, "Invokes a native button action through a raw pointer."],
    ["button_activate_ptr", "mod.ui.button_activate_ptr(pointer)", [p("pointer", "integer", "Native button pointer.")], "true on success; false plus an error.", `mod.ui.button_activate_ptr(ptr)`, "Alias-compatible native button activation bridge."],
    ["button_resize_ptr", "mod.ui.button_resize_ptr(pointer, width, height)", [p("pointer", "integer", "Native button pointer."), p("width, height", "number", "New size.")], "true on success; false plus an error.", `mod.ui.button_resize_ptr(ptr, 180, 28)`, "Resizes a native button through a raw pointer."],
    ["button_hide_ptr", "mod.ui.button_hide_ptr(pointer [, hidden])", [p("pointer", "integer", "Native button pointer."), p("hidden", "boolean", "Defaults to true.", "optional")], "true on success; false plus an error.", `mod.ui.button_hide_ptr(ptr, true)`, "Changes native button visibility through a raw pointer."],
    ["button_remove_ptr", "mod.ui.button_remove_ptr(pointer)", [p("pointer", "integer", "Native button pointer.")], "true on success; false plus an error.", `mod.ui.button_remove_ptr(ptr)`, "Removes a native button through a raw pointer."]
  ];
  pointerUi.forEach(([name, signature, params, returns, example, summary]) => ui.push(
    fn(`mod.ui.${name}`, signature, summary, params, returns, example, {
      tags: ["legacy"],
      notes: ["Raw pointers and native layout assumptions are build-specific. Prefer native_button and its owner-safe helpers."]
    })
  ));

  ui.push(
    fn("mod.ui.create_state", "mod.ui.create_state(name, spec)", "Creates a registered custom screen from lifecycle callbacks.", [p("name", "string", "Unique custom state id."), p("spec", "table", "enter, update, render, event, leave, and cursor fields.")], "true on success; false plus an error.", `mod.ui.create_state("inspector", {
  render=function() draw_inspector() end,
  event=function(event) return event.type == "keydown" end
})`, { tags: ["cosmetic"], notes: ["define_state is the higher-level authoring helper and is preferred for new code."] }),
    fn("mod.ui.register_state", "mod.ui.register_state(name, spec)", "Alias of create_state.", [p("name", "string", "Unique custom state id."), p("spec", "table", "Lifecycle callback table.")], "true on success; false plus an error.", `mod.ui.register_state("inspector", spec)`, { tags: ["cosmetic"], notes: ["Use define_state for the normalized high-level contract."] }),
    fn("mod.ui.enter_state", "mod.ui.enter_state(name)", "Switches to a registered custom screen.", [p("name", "string", "Custom state id.")], "true on success; false plus an error.", `mod.ui.enter_state("inspector")`, { tags: ["cosmetic"], notes: ["Gameplay-relevant transitions are refused during the online safety window."] }),
    fn("mod.ui.leave_state", "mod.ui.leave_state()", "Leaves the active custom screen and returns to its previous owner state.", [], "true on success; false when no custom screen is active.", `if back_pressed then mod.ui.leave_state() end`, { tags: ["cosmetic"] }),
    fn("mod.ui.goto_main_menu", "mod.ui.goto_main_menu()", "Requests the native main menu.", [], "true on success; false when the transition is unavailable or unsafe.", `mod.ui.goto_main_menu()`, { tags: ["cosmetic"], notes: ["The framework refuses unsafe transitions while managed online play owns the match lifecycle."] })
  );

  const uiHelpers = [
    ["push_style", "mod.ui.push_style(style)", [p("style", "table", "Partial style overrides.")], "The merged current style table.", `mod.ui.push_style({ accent={0.3,0.9,0.8,1} })`, "Pushes a merged style onto the per-mod style stack."],
    ["pop_style", "mod.ui.pop_style()", [], "The new current style table.", `mod.ui.pop_style()`, "Pops one style layer without removing the base style."],
    ["current_style", "mod.ui.current_style()", [], "A defensive copy of the current style.", `local style = mod.ui.current_style()`, "Returns the resolved style at the top of the stack."],
    ["define_theme", "mod.ui.define_theme(name, style)", [p("name", "string", "Theme id."), p("style", "table", "Theme values.")], "No values.", `mod.ui.define_theme("mint", { accent={0.2,0.9,0.7,1} })`, "Registers a named theme."],
    ["style_color", "mod.ui.style_color(key [, fallback])", [p("key", "string", "Style color key."), p("fallback", "RGBA table", "Fallback color.", "optional")], "Resolved RGBA table.", `local accent = mod.ui.style_color("accent")`, "Returns a color from the current style."],
    ["theme", "mod.ui.theme(style)", [p("style", "table", "Partial style values.")], "Merged style table.", `local card = mod.ui.theme({ pad=12 })`, "Merges a style with the current theme without pushing it."],
    ["set_theme", "mod.ui.set_theme(name)", [p("name", "string", "Previously defined theme id.")], "true on success; false for an unknown theme.", `mod.ui.set_theme("mint")`, "Replaces the base style with a named theme."],
    ["wrap_text", "mod.ui.wrap_text(text, max_width [, scale])", [p("text", "string", "Text to wrap."), p("max_width", "number", "Maximum rendered width."), p("scale", "number", "Text scale.", "optional")], "Array of wrapped line strings.", `local lines = mod.ui.wrap_text(description, 280, 0.9)`, "Wraps text using native-font measurements."],
    ["text_wrapped", "mod.ui.text_wrapped(text, x, y, width [, options])", [p("text", "string", "Text to draw."), p("x, y", "number", "Top-left position."), p("width", "number", "Maximum line width."), p("options", "table", "Scale, color, and line gap.", "optional")], "Total drawn height.", `mod.ui.text_wrapped(help, 24, 80, 300, { scale=0.9 })`, "Wraps and draws a paragraph."],
    ["tooltip", "mod.ui.tooltip(text [, options])", [p("text", "string", "Tooltip text."), p("options", "table", "Position, max width, and style.", "optional")], "No values.", `if hovered then mod.ui.tooltip("Open diagnostics") end`, "Draws a pointer-adjacent tooltip."],
    ["icon_button", "mod.ui.icon_button(id, icon, x, y, width, height [, options])", [p("id", "string", "Stable widget id."), p("icon", "integer", "Sprite id."), p("x, y", "number", "Position."), p("width, height", "number", "Bounds."), p("options", "table", "Disabled, tint, tooltip, and style.", "optional")], "true when activated.", `if mod.ui.icon_button("refresh", refresh_icon, 20, 20, 32, 32) then reload() end`, "Draws a sprite-backed button."],
    ["panel", "mod.ui.panel(x, y, width, height [, options])", [p("x, y", "number", "Position."), p("width, height", "number", "Panel bounds."), p("options", "table", "Background, border, padding, and style.", "optional")], "A content layout table for the panel interior.", `local body = mod.ui.panel(20, 20, 300, 180, { title="Inspector" })`, "Draws a panel and returns its content layout."],
    ["progress_bar", "mod.ui.progress_bar(id, value, x, y, width, height [, options])", [p("id", "string", "Stable widget id."), p("value", "number", "Normalized progress."), p("x, y", "number", "Position."), p("width, height", "number", "Bounds."), p("options", "table", "Colors, label, and style.", "optional")], "Clamped normalized value.", `mod.ui.progress_bar("load", progress, 20, 60, 240, 16)`, "Draws a bounded progress meter."],
    ["close_button", "mod.ui.close_button(id, x, y, size [, options])", [p("id", "string", "Stable widget id."), p("x, y", "number", "Position."), p("size", "number", "Square size."), p("options", "table", "Style and disabled state.", "optional")], "true when activated.", `if mod.ui.close_button("close", 280, 20, 24) then mod.ui.leave_state() end`, "Draws a compact close control."],
    ["tabs", "mod.ui.tabs(id, tabs, selected [, options])", [p("id", "string", "Stable widget id."), p("tabs", "array", "Labels or item tables."), p("selected", "integer | string", "Current selection."), p("options", "table", "Bounds and style.", "optional")], "New selection and a changed boolean.", `tab, changed = mod.ui.tabs("section", {"Map","Entities"}, tab)`, "Draws a tab/segmented selection row."],
    ["segmented", "mod.ui.segmented(id, items, selected [, options])", [p("id", "string", "Stable widget id."), p("items", "array", "Labels or item tables."), p("selected", "integer | string", "Current selection."), p("options", "table", "Bounds and style.", "optional")], "New selection and a changed boolean.", `mode = select(1, mod.ui.segmented("mode", {"A","B"}, mode))`, "Alias of tabs for segmented controls."],
    ["swatch_grid", "mod.ui.swatch_grid(id, colors, selected [, options])", [p("id", "string", "Stable widget id."), p("colors", "array", "RGBA colors or labeled color items."), p("selected", "integer", "Current one-based index."), p("options", "table", "Cell and grid geometry.", "optional")], "New index and a changed boolean.", `skin = select(1, mod.ui.swatch_grid("skin", palette, skin))`, "Draws a selectable color grid."],
    ["item_grid", "mod.ui.item_grid(id, items, selected [, options])", [p("id", "string", "Stable widget id."), p("items", "array", "Sprite/label item tables."), p("selected", "integer", "Current one-based index."), p("options", "table", "Cell and grid geometry.", "optional")], "New index and a changed boolean.", `hat = select(1, mod.ui.item_grid("hats", hats, hat))`, "Draws a selectable item grid."],
    ["slider", "mod.ui.slider(id, value, min_value, max_value [, options])", [p("id", "string", "Stable widget id."), p("value", "number", "Current value."), p("min_value", "number", "Lower bound."), p("max_value", "number", "Upper bound."), p("options", "table", "Step, width, label, and formatting.", "optional")], "New value and a changed boolean.", `volume, changed = mod.ui.slider("volume", volume, 0, 1, { step=0.05 })`, "Draws a pointer-adjustable bounded slider."],
    ["checkbox", "mod.ui.checkbox(id, value [, options])", [p("id", "string", "Stable widget id."), p("value", "boolean", "Current value."), p("options", "table", "Label and style.", "optional")], "New boolean and a changed boolean.", `enabled, changed = mod.ui.checkbox("grid", enabled, { label="Show grid" })`, "Draws a compact checkbox with one stable hit target."],
    ["cursor_sprite", "mod.ui.cursor_sprite([index])", [p("index", "integer", "Optional native cursor variant.", "optional")], "Table describing sprite id, scale, tint, hotspot, and shadow.", `local cursor = mod.ui.cursor_sprite()`, "Reads the live native cursor presentation."],
    ["draw_cursor", "mod.ui.draw_cursor([options])", [p("options", "table", "Position and presentation overrides.", "optional")], "No values.", `mod.ui.draw_cursor()`, "Draws the framework cursor while preserving vanilla scale, color pulse, shadow, and hotspot."],
    ["tile_preview", "mod.ui.tile_preview(id, frame, tile [, x, y, scale, options])", [p("id", "string", "Stable preview id."), p("frame", "integer", "Animation frame."), p("tile", "integer | table", "Sprite/tile description."), p("x, y", "number", "Optional explicit position.", "optional"), p("scale", "number", "Optional preview scale.", "optional"), p("options", "table", "Background and style.", "optional")], "Preview bounds table.", `mod.ui.tile_preview("spring", frame, spring_tile, 20, 20, 3)`, "Draws an isolated tile or content-registry preview."],
    ["define_state", "mod.ui.define_state(name, spec)", [p("name", "string", "Unique custom state id."), p("spec", "table", "enter, update, render, event, leave, and cursor fields.")], "true on success; false plus an error.", `mod.ui.define_state("settings", {
  render=draw_settings,
  event=handle_event,
  cursor=true
})`, "Defines a normalized custom screen and cursor policy."]
  ];
  uiHelpers.forEach(([name, signature, params, returns, example, summary]) => ui.push(
    fn(`mod.ui.${name}`, signature, summary, params, returns, example, { tags: ["cosmetic"] })
  ));

  add({
    id: "assets",
    label: "mod.assets",
    page: "api/ui.html",
    description: "Owner-scoped PNG spritesheets and bounded color-mask generation for mod UI and overlays.",
    lifecycle: "Registrations belong to the mod and are released on unload. A batch defers atlas rebuild until commit.",
    entries: [
      fn("mod.assets.load_spritesheet", "mod.assets.load_spritesheet(id, path, options)", "Registers a grid spritesheet and allocates global sprite ids.", [p("id", "string", "Mod-local sheet id."), p("path", "string", "Mod-relative PNG path."), p("options", "table", "cell_w, cell_h, padding, count, and related grid fields.")], "Sheet metadata table on success; nil plus an error otherwise.", `local sheet = mod.assets.load_spritesheet("icons", "assets/icons.png", {
  cell_w=16, cell_h=16, padding=0
})`, { errors: "Invalid paths, unsafe geometry, image decode failure, capacity exhaustion, or conflicting ids fail without a partial live sheet.", notes: ["The global native atlas is bounded to 8,192 sprites."] }),
      fn("mod.assets.begin_batch", "mod.assets.begin_batch()", "Begins a transactional multi-sheet registration batch.", [], "true on success; false when a batch is already active.", `mod.assets.begin_batch()
mod.assets.load_spritesheet("icons", "icons.png", {cell_w=16,cell_h=16})
mod.assets.load_spritesheet("badges", "badges.png", {cell_w=24,cell_h=24})
mod.assets.end_batch()`),
      fn("mod.assets.end_batch", "mod.assets.end_batch()", "Commits the current spritesheet batch and rebuilds once.", [], "true on success; false plus an error on validation or rebuild failure.", `local ok, err = mod.assets.end_batch()`, { errors: "Failure keeps the previous committed registrations; use cancel_batch after an interrupted authoring flow." }),
      fn("mod.assets.cancel_batch", "mod.assets.cancel_batch()", "Discards the pending spritesheet batch.", [], "true when a batch was cancelled; false when none was active.", `if not valid then mod.assets.cancel_batch() end`),
      fn("mod.assets.build_color_masks", "mod.assets.build_color_masks(source, options)", "Splits exact source colors into a transparent base image and white-alpha mask layers.", [p("source", "string", "Mod-relative PNG path."), p("options", "table", "key, out_dir, and masks/layers mapping safe names to RGB lists.")], "Table with base, w, h, layers, and counts; nil plus an error on failure.", `local masks = mod.assets.build_color_masks("character.png", {
  key="hero",
  masks={ skin={{255,204,170}}, cloth={{32,96,200}} }
})`, { notes: ["Source images are capped at 2048×2048.", "At most eight mask layers and 128 exact colors per layer are accepted."] }),
      fn("mod.assets.sprite_id", "mod.assets.sprite_id(sheet_id, index)", "Resolves a local cell index to a global sprite id.", [p("sheet_id", "string", "Registered sheet id."), p("index", "integer", "Zero-based cell index.")], "Integer sprite id, or nil plus an error.", `local icon = mod.assets.sprite_id("icons", 2)`, { tags: ["read-only"] }),
      fn("mod.assets.info", "mod.assets.info([sheet_id])", "Inspects registered spritesheets.", [p("sheet_id", "string", "Optional mod-local id.", "optional")], "One metadata table, or an array of every owned sheet when omitted.", `for _, sheet in ipairs(mod.assets.info()) do mod.log(sheet.id) end`, { tags: ["read-only"] })
    ]
  });

  add({
    id: "font",
    label: "mod.font",
    page: "api/ui.html",
    description: "Native 8×8 font-atlas extension for owner-scoped icon glyphs.",
    entries: [
      fn("mod.font.font_loaded", "mod.font.font_loaded()", "Reports whether the native font atlas is live.", [], "Boolean.", `if mod.font.font_loaded() then mod.log("font ready") end`, { tags: ["read-only"] }),
      fn("mod.font.unregister_glyph", "mod.font.unregister_glyph(byte)", "Removes this mod's glyph declaration and cached allocation, then refreshes the remaining overlays.", [p("byte", "integer", "Exact glyph byte 0..255.")], "removed, restart_required; false plus an error for invalid input or an inactive owner.", `local removed, restart = mod.font.unregister_glyph(0x91)`, { notes: ["API revision 6; capability font.unregister. Other owners' declarations remain intact."] }),
      fn("mod.font.alloc_glyph", "mod.font.alloc_glyph(path)", "Allocates the next free extended byte and registers an 8×8 PNG glyph.", [p("path", "string", "Mod-relative 8×8 PNG path.")], "Allocated integer byte in 0x80..0xFF; nil plus an error on failure.", `local byte = mod.font.alloc_glyph("assets/key.png")
local text = "PRESS " .. string.char(byte)`, { notes: ["Allocation is owner-tracked and can trigger a live atlas reload."] }),
      fn("mod.font.register_glyph", "mod.font.register_glyph(byte, path [, options])", "Registers an 8×8 PNG at an explicit font byte.", [p("byte", "integer", "Byte value clamped to 0..255."), p("path", "string", "Mod-relative PNG path."), p("options", "table", "Set override=true to replace another owner's registration.", "optional")], "true on success; false plus an error.", `mod.font.register_glyph(0x91, "assets/controller.png")`, { notes: ["Explicit cross-owner overrides should be opt-in and treated as compatibility-sensitive."] })
    ]
  });

  add({
    id: "texture",
    label: "mod.texture",
    page: "api/ui.html",
    description: "Resource-pack style replacement of known native atlas PNGs.",
    lifecycle: "Registrations are owner-scoped and watched. Same-size validated replacements can reload while the relevant atlas is live.",
    entries: []
  });
  const texture = groups.find(group => group.id === "texture").entries;
  texture.push(
    fn("mod.texture.unregister", "mod.texture.unregister(target_path)", "Removes this mod's replacement and refreshes the remaining overlays or native image.", [p("target_path", "string", "Canonical data target or shorthand such as tiles.png.")], "removed, restart_required; false plus an error for invalid input or an inactive owner.", `local removed, restart = mod.texture.unregister("tiles.png")`, { notes: ["API revision 6; capability texture.unregister. Does not remove mod.assets spritesheets."] }),
    fn("mod.texture.loaded", "mod.texture.loaded(target_path)", "Reports whether a native target atlas has been loaded.", [p("target_path", "string", "Installed data path such as data/sprites.png.")], "Boolean.", `if mod.texture.loaded("data/sprites.png") then mod.log("sprites live") end`, { tags: ["read-only"] }),
    fn("mod.texture.register", "mod.texture.register(target_path, replacement_path [, options])", "Registers a same-size PNG replacement for an explicit native data target.", [p("target_path", "string", "Supported installed data path."), p("replacement_path", "string", "Mod-relative PNG."), p("options", "table", "override ownership option.", "optional")], "true on success; false plus an error.", `mod.texture.register("data/sprites.png", "pack/sprites.png")`, { errors: "Unsupported target, unsafe path, size mismatch, decode failure, or ownership conflict returns false." }),
    fn("mod.texture.sprites_loaded", "mod.texture.sprites_loaded()", "Reports whether the native sprite atlas is live.", [], "Boolean.", `if mod.texture.sprites_loaded() then mod.texture.reload_all() end`, { tags: ["read-only"] })
  );
  [
    ["register_spritesheet", "data/sprites.png", "sprite"],
    ["register_tilesheet", "data/tiles.png", "tile"],
    ["register_miscsheet", "data/misc.png", "miscellaneous"],
    ["register_glowsheet", "data/glow.png", "glow"]
  ].forEach(([name, target, label]) => texture.push(
    fn(`mod.texture.${name}`, `mod.texture.${name}(replacement_path [, options])`,
      `Registers a ${label} atlas replacement for ${target}.`,
      [p("replacement_path", "string", "Mod-relative same-size PNG."), p("options", "table", "override ownership option.", "optional")],
      "true on success; false plus an error.",
      `mod.texture.${name}("pack/${target.split("/").pop()}")`,
      { notes: ["This is a convenience wrapper around mod.texture.register."] })
  ));
  texture.push(fn("mod.texture.reload_all", "mod.texture.reload_all()", "Revalidates and reapplies every owned texture and font registration.", [], "Boolean success plus a report table containing reload/failure/restart counts.", `local ok, report = mod.texture.reload_all()
mod.log(("reloaded %d textures"):format(report.textures_reloaded))`, { errors: "Invalid replacements remain unapplied and are counted in the report; a restart-required count indicates live native replacement was unsafe." }));

  add({
    id: "anim",
    label: "mod.anim",
    page: "api/ui.html",
    description: "Pure-Lua frame-sequence helpers layered over mod.ui.draw_sprite.",
    entries: [
      fn("mod.anim.frame_strip", "mod.anim.frame_strip(options)", "Builds a contiguous sprite-id frame sequence.", [p("options", "table", "base/start sprite id, count, step, and optional ordering.")], "Array of sprite ids.", `local frames = mod.anim.frame_strip({ base=sheet.base, count=6 })`, { tags: ["read-only"] }),
      fn("mod.anim.frame_table", "mod.anim.frame_table(options)", "Normalizes an explicit frame list with optional per-frame durations.", [p("options", "table", "frames plus timing/default fields.")], "Normalized frame descriptor array.", `local frames = mod.anim.frame_table({ frames={idle, blink, idle}, ticks={20,2,20} })`, { tags: ["read-only"] }),
      fn("mod.anim.new", "mod.anim.new(options)", "Creates a mutable animation instance.", [p("options", "table", "frames, fps/ticks, loop, pingpong, start frame, and playback fields.")], "Animation instance with update, frame, draw, and reset methods.", `local flame = mod.anim.new({ frames=frames, fps=12, loop=true })`),
      fn("mod.anim.frame", "mod.anim.frame(animation)", "Returns the current sprite frame.", [p("animation", "animation", "Instance from anim.new.")], "Current sprite id and frame index.", `local sprite, index = mod.anim.frame(flame)`, { tags: ["read-only"] }),
      fn("mod.anim.update", "mod.anim.update(animation, delta)", "Advances animation time.", [p("animation", "animation", "Instance from anim.new."), p("delta", "number", "Elapsed seconds or configured tick unit.")], "Current sprite id and whether the visible frame changed.", `mod.anim.update(flame, dt)`),
      fn("mod.anim.draw", "mod.anim.draw(animation, x, y [, options])", "Draws the current frame with mod.ui.draw_sprite.", [p("animation", "animation", "Instance from anim.new."), p("x, y", "number", "Draw position."), p("options", "table", "Sprite transform/tint options.", "optional")], "Current sprite id.", `mod.anim.draw(flame, 120, 80, { scale=2 })`, { tags: ["cosmetic"] }),
      fn("animation:update", "animation:update(delta)", "Instance-method form of mod.anim.update.", [p("delta", "number", "Elapsed time.")], "Current sprite id and changed flag.", `flame:update(dt)`),
      fn("animation:frame", "animation:frame()", "Instance-method form of mod.anim.frame.", [], "Current sprite id and frame index.", `local sprite = flame:frame()`, { tags: ["read-only"] }),
      fn("animation:draw", "animation:draw(x, y [, options])", "Instance-method form of mod.anim.draw.", [p("x, y", "number", "Draw position."), p("options", "table", "Sprite options.", "optional")], "Current sprite id.", `flame:draw(120, 80, { scale=2 })`, { tags: ["cosmetic"] }),
      fn("animation:reset", "animation:reset([frame_index])", "Resets playback time and selects a starting frame.", [p("frame_index", "integer", "Optional one-based frame index.", "optional")], "The animation instance.", `flame:reset(1)`)
    ]
  });

  add({
    id: "audio",
    label: "mod.audio",
    page: "api/audio.html",
    description: "Owner-scoped sound effects, music, generated bytebeat audio, cache control, and mixer diagnostics.",
    lifecycle: "Channels, music, and generated chunks are tracked per mod and cleaned up when the package unloads.",
    entries: [
      fn("mod.audio.play_sfx", "mod.audio.play_sfx(path_or_builtin [, options])", "Plays a mod WAV/audio asset or a named built-in Eggnogg sound.", [p("path_or_builtin", "string", "Mod-relative file or built-in id."), p("options", "table", "loops, ticks, and volume.", "optional")], "true on success; false plus an error.", `mod.audio.play_sfx("sword_ching", { volume=0.65 })`, { errors: "Missing files, unavailable backend, channel exhaustion, decode, or mixer failure returns false.", notes: ["Built-ins include pip, noise, thump, shred, fm, ringmod, warble, creepy, pulse, and sword_ching."] }),
      fn("mod.audio.play_music", "mod.audio.play_music(path [, options])", "Starts a mod-owned music file.", [p("path", "string", "Mod-relative music path."), p("options", "table", "loops and volume.", "optional")], "true on success; false plus an error.", `mod.audio.play_music("audio/theme.ogg", { loops=-1, volume=0.7 })`, { notes: ["Starting a new music track replaces the current framework-owned music playback."] }),
      fn("mod.audio.stop_music", "mod.audio.stop_music()", "Stops framework music playback.", [], "true when the stop request is accepted.", `mod.audio.stop_music()`),
      fn("mod.audio.set_music_volume", "mod.audio.set_music_volume(volume)", "Sets this mod's music volume multiplier.", [p("volume", "number", "Normalized 0..1 volume.")], "The clamped volume.", `mod.audio.set_music_volume(0.55)`),
      fn("mod.audio.set_sfx_volume", "mod.audio.set_sfx_volume(volume)", "Sets this mod's sound-effect volume multiplier.", [p("volume", "number", "Normalized 0..1 volume.")], "The clamped volume.", `mod.audio.set_sfx_volume(0.8)`),
      fn("mod.audio.play_bytebeat", "mod.audio.play_bytebeat(expression [, options])", "Compiles/renders a bounded expression into a mod-owned generated sound.", [p("expression", "string", "Bounded bytebeat expression."), p("options", "table", "duration, sample_rate, output_rate, loops, volume, mode, and cache fields.", "optional")], "true plus generated-audio metadata; false plus a compile/render/backend error.", `local ok, info = mod.audio.play_bytebeat(
  "t*((t>>12|t>>8)&63&t>>4)",
  { duration=4, sample_rate=8000, loops=0, volume=0.6 }
)`, { notes: ["The ordinary Lua API uses the bounded native expression grammar; playlist Dollchan JavaScript is a separate isolated runtime.", "Generated PCM and chunk counts are bounded per mod."] }),
      fn("mod.audio.bytebeat_info", "mod.audio.bytebeat_info(expression [, options])", "Validates a bounded expression and reports render requirements without playing it.", [p("expression", "string", "Bounded bytebeat expression."), p("options", "table", "Duration/rate/mode options.", "optional")], "Metadata table on success; nil plus an error.", `local info, err = mod.audio.bytebeat_info("t&t>>8", { duration=2 })`, { tags: ["read-only"] }),
      fn("mod.audio.stop_sfx", "mod.audio.stop_sfx([channel])", "Stops one owned channel or every owned sound-effect channel.", [p("channel", "integer", "Optional mixer channel.", "optional")], "Number of channels stopped.", `mod.audio.stop_sfx()`),
      fn("mod.audio.clear_generated", "mod.audio.clear_generated()", "Releases this mod's cached generated-audio chunks.", [], "Number of chunks cleared.", `local cleared = mod.audio.clear_generated()`),
      fn("mod.audio.status", "mod.audio.status()", "Returns mixer/backend, volume, channel, music, and generated-cache diagnostics.", [], "Status table including readiness, backend, volumes, owned channels, cached/generated chunk counts, PCM bytes, and limits.", `local status = mod.audio.status()
mod.log(("backend=%s chunks=%d"):format(status.backend, status.generated_chunks))`, { tags: ["read-only"] })
    ]
  });

  add({
    id: "game",
    label: "mod.game",
    page: "api/game.html",
    description: "Native gameplay telemetry, deterministic state access, input control, bot integration, map selection, and presentation overrides.",
    lifecycle: "Read-only telemetry is generally available. Mutating calls classify the owner as gameplay-affecting and fail while managed online safety suspends it.",
    entries: []
  });
  const game = groups.find(group => group.id === "game").entries;
  const gameRead = (name, signature, summary, params, returns, example, options = {}) =>
    game.push(fn(`mod.game.${name}`, signature, summary, params, returns, example, { ...options, tags: [...(options.tags || []), "read-only"] }));
  const gameMut = (name, signature, summary, params, returns, example, options = {}) =>
    game.push(fn(`mod.game.${name}`, signature, summary, params, returns, example, { ...options, tags: [...(options.tags || []), "gameplay", "mutating"], notes: [...(options.notes || []), "Refused while gameplay-affecting mods are suspended for managed online play."] }));

  gameRead("snapshot", "mod.game.snapshot(player [, include_tiles])", "Captures structured player, room, entity, enemy, and optional tile telemetry.", [p("player", "integer", "Player number/index accepted by the framework."), p("include_tiles", "boolean", "Include the room tile grid.", "optional")], "Snapshot table, or nil plus an error for an invalid player/native state.", `local state = mod.game.snapshot(1, false)
if state then mod.log(state.x .. "," .. state.y) end`);
  gameRead("sword_snapshot", "mod.game.sword_snapshot()", "Captures held-sword flags and active native sword records.", [], "Sword snapshot table.", `local swords = mod.game.sword_snapshot()`);
  gameRead("entities", "mod.game.entities([current_room_only [, include_players]])", "Lists active native thing records.", [p("current_room_only", "boolean", "Exclude other rooms.", "optional"), p("include_players", "boolean", "Include player-backed records.", "optional")], "Array of stable telemetry tables.", `for _, entity in ipairs(mod.game.entities(true, false)) do
  mod.log(entity.type)
end`);
  gameRead("tick_count", "mod.game.tick_count()", "Returns the framework's observed gameplay-tick counter.", [], "Integer tick count.", `local tick = mod.game.tick_count()`);
  gameRead("native_tick", "mod.game.native_tick()", "Returns the native deterministic tick global.", [], "Unsigned integer tick.", `local native_tick = mod.game.native_tick()`);
  gameMut("set_native_tick", "mod.game.set_native_tick(value)", "Writes the native deterministic tick global.", [p("value", "integer", "New tick value.")], "true on success; false plus an error.", `mod.game.set_native_tick(120)`, { notes: ["Low-level rollback/testing primitive; arbitrary use can invalidate timers and deterministic state."] });
  gameRead("rng_seed", "mod.game.rng_seed()", "Returns the native gameplay RNG seed.", [], "Unsigned integer seed.", `local seed = mod.game.rng_seed()`);
  gameMut("set_rng_seed", "mod.game.set_rng_seed(value)", "Writes the native gameplay RNG seed.", [p("value", "integer", "New seed.")], "true on success; false plus an error.", `mod.game.set_rng_seed(0x12345678)`, { notes: ["Use only for controlled deterministic modes or tests."] });
  gameRead("native_state", "mod.game.native_state()", "Returns a compact table of core native match state.", [], "Table of current native state fields.", `local state = mod.game.native_state()`);
  gameRead("player_colour", "mod.game.player_colour(player [, clothing])", "Reads a player's current native RGBA skin or clothing color.", [p("player", "integer", "Player index."), p("clothing", "boolean | integer", "Select clothing instead of skin.", "optional")], "RGBA table.", `local skin = mod.game.player_colour(1, false)`);
  gameRead("player_color", "mod.game.player_color(player [, clothing])", "US-spelling alias of player_colour.", [p("player", "integer", "Player index."), p("clothing", "boolean | integer", "Color slot.", "optional")], "RGBA table.", `local skin = mod.game.player_color(1)`);
  gameRead("player_colour_index", "mod.game.player_colour_index(player [, clothing])", "Reads the built-in palette index for one player color slot.", [p("player", "integer", "Player index."), p("clothing", "boolean | integer", "Color slot.", "optional")], "Integer palette index.", `local index = mod.game.player_colour_index(1, true)`);
  gameRead("player_color_index", "mod.game.player_color_index(player [, clothing])", "US-spelling alias of player_colour_index.", [p("player", "integer", "Player index."), p("clothing", "boolean | integer", "Color slot.", "optional")], "Integer palette index.", `local index = mod.game.player_color_index(1, true)`);
  gameMut("set_player_colour_index", "mod.game.set_player_colour_index(player, clothing, index)", "Changes one player's built-in palette selection.", [p("player", "integer", "Player index."), p("clothing", "boolean | integer", "Skin/clothing slot."), p("index", "integer", "Palette index.")], "true on success; false plus an error.", `mod.game.set_player_colour_index(1, true, 4)`, { tags: ["cosmetic"] });
  gameMut("set_player_color_index", "mod.game.set_player_color_index(player, clothing, index)", "US-spelling alias of set_player_colour_index.", [p("player", "integer", "Player index."), p("clothing", "boolean | integer", "Skin/clothing slot."), p("index", "integer", "Palette index.")], "true on success; false plus an error.", `mod.game.set_player_color_index(1, true, 4)`, { tags: ["cosmetic"] });
  gameMut("set_player_render_colours", "mod.game.set_player_render_colours(player, skin, clothing)", "Writes clamped transient RGBA render colors.", [p("player", "integer", "Player index."), p("skin", "RGBA table", "Skin color."), p("clothing", "RGBA table", "Clothing color.")], "true on success; false plus an error.", `mod.game.set_player_render_colours(1, {1,.8,.7,1}, {.2,.6,1,1})`, { tags: ["cosmetic"], notes: ["Presentation override is transient and cleared/suspended online."] });
  gameMut("set_player_render_colors", "mod.game.set_player_render_colors(player, skin, clothing)", "US-spelling alias of set_player_render_colours.", [p("player", "integer", "Player index."), p("skin", "RGBA table", "Skin color."), p("clothing", "RGBA table", "Clothing color.")], "true on success; false plus an error.", `mod.game.set_player_render_colors(1, skin, clothes)`, { tags: ["cosmetic"] });
  gameMut("set_player_body_hidden", "mod.game.set_player_body_hidden(player, hidden)", "Sets a transient player-body visibility override.", [p("player", "integer", "Player index."), p("hidden", "boolean", "Visibility override.")], "true on success; false plus an error.", `mod.game.set_player_body_hidden(1, true)`, { tags: ["cosmetic"], notes: ["Swords and other entities are separate presentation objects."] });
  gameRead("player_body_hidden", "mod.game.player_body_hidden(player)", "Reads the transient body visibility override.", [p("player", "integer", "Player index.")], "Boolean.", `local hidden = mod.game.player_body_hidden(1)`);
  gameMut("set_player_sword_idle_offset", "mod.game.set_player_sword_idle_offset(player, x, y)", "Sets a transient presentation offset for the held idle sword.", [p("player", "integer", "Player index."), p("x, y", "number", "Pixel offset.")], "true on success; false plus an error.", `mod.game.set_player_sword_idle_offset(1, 0, -3)`, { tags: ["cosmetic"] });
  gameRead("state_checksum", "mod.game.state_checksum()", "Computes the framework deterministic gameplay checksum.", [], "Unsigned checksum integer.", `mod.log(string.format("%08X", mod.game.state_checksum()))`);
  gameRead("full_state_blob", "mod.game.full_state_blob()", "Captures the low-level serialized rollback state.", [], "Binary Lua string, or nil plus an error.", `local blob, err = mod.game.full_state_blob()`, { notes: ["Versioned/internal rollback surface; do not persist as a cross-version save format."] });
  gameMut("apply_full_state_blob", "mod.game.apply_full_state_blob(blob)", "Transactionally applies a low-level rollback state blob.", [p("blob", "binary string", "Blob from the same compatible build/layout.")], "true on success; false plus an error.", `local ok, err = mod.game.apply_full_state_blob(blob)`, { notes: ["Failed validation must not partially mutate native state."] });
  gameRead("full_state_size", "mod.game.full_state_size()", "Returns the currently required serialized rollback-state byte count.", [], "Integer byte count.", `local bytes = mod.game.full_state_size()`);
  gameMut("block_next_tick", "mod.game.block_next_tick([blocked])", "Arms or clears suppression of the next native gameplay update.", [p("blocked", "boolean", "Defaults to true.", "optional")], "Previous armed state.", `mod.game.block_next_tick(true)`, { notes: ["Advanced synchronization/debug primitive."] });
  gameMut("simulate_ticks", "mod.game.simulate_ticks(count [, arg0])", "Runs bounded native simulation ticks through the canonical tick guard.", [p("count", "integer", "Number of ticks to simulate."), p("arg0", "integer", "Optional native update argument.", "optional")], "true plus completed count; false plus an error.", `local ok, completed = mod.game.simulate_ticks(10)`, { notes: ["Uses the current native state and input environment; callers own restoration if this is speculative."] });
  gameRead("poll_cmds", "mod.game.poll_cmds(player)", "Returns the effective command mask after framework overrides.", [p("player", "integer", "Player index.")], "Integer command bitmask.", `local cmds = mod.game.poll_cmds(1)`);
  gameRead("poll_cmds_raw", "mod.game.poll_cmds_raw(player)", "Returns the native command mask before framework overrides.", [p("player", "integer", "Player index.")], "Integer command bitmask.", `local raw = mod.game.poll_cmds_raw(1)`);
  gameMut("set_input", "mod.game.set_input(player, mask [, ticks [, replace]])", "Queues tick-synchronous bot input.", [p("player", "integer", "Player index."), p("mask", "integer", "CMD_* bitmask."), p("ticks", "integer", "Duration in gameplay ticks.", "optional"), p("replace", "boolean", "Replace instead of merge.", "optional")], "true on success; false plus an error.", `mod.game.set_input(2, mod.game.CMD_LEFT | mod.game.CMD_ATTACK, 1, true)`, { notes: ["Preferred deterministic bot-control surface."] });
  gameMut("input_override", "mod.game.input_override(player, mask [, polls [, replace]])", "Applies the legacy poll-counted command override.", [p("player", "integer", "Player index."), p("mask", "integer", "CMD_* bitmask."), p("polls", "integer", "Number of command polls.", "optional"), p("replace", "boolean", "Replace instead of merge.", "optional")], "true on success; false plus an error.", `mod.game.input_override(2, mod.game.CMD_JUMP, 2)`, { tags: ["legacy"], notes: ["Prefer set_input because poll counts are less directly tied to deterministic ticks."] });
  gameMut("input_clear", "mod.game.input_clear(player)", "Clears queued input for one player.", [p("player", "integer", "Player index.")], "true on success; false plus an error.", `mod.game.input_clear(2)`);
  gameRead("input_status", "mod.game.input_status(player)", "Inspects queued command overrides.", [p("player", "integer", "Player index.")], "Status table containing masks, remaining ticks/polls, replace mode, and raw-input blocking.", `local status = mod.game.input_status(2)`);
  gameMut("block_raw_input", "mod.game.block_raw_input(player, blocked)", "Suppresses native local input before framework overrides.", [p("player", "integer", "Player index."), p("blocked", "boolean", "Whether to suppress raw input.")], "true on success; false plus an error.", `mod.game.block_raw_input(2, true)`);
  gameMut("apply_snapshot", "mod.game.apply_snapshot(snapshot)", "Applies compatible player/entity telemetry back to native state.", [p("snapshot", "table", "Snapshot produced by mod.game.snapshot.")], "true on success; false plus an error.", `local ok, err = mod.game.apply_snapshot(saved)`, { notes: ["Lower-level experimental bridge; full rollback work should use full_state_blob."] });
  gameMut("apply_sword_snapshot", "mod.game.apply_sword_snapshot(snapshot)", "Applies a compatible sword snapshot.", [p("snapshot", "table", "Value from sword_snapshot.")], "true on success; false plus an error.", `mod.game.apply_sword_snapshot(swords)`);
  gameMut("register_bot_provider", "mod.game.register_bot_provider()", "Marks the current package as the active bot provider.", [], "true on success; false plus an error.", `mod.game.register_bot_provider()`, { notes: ["Provider registration is gameplay-affecting."] });
  gameMut("register_menu_mode", "mod.game.register_menu_mode(spec)", "Adds a framework-managed play mode to the main menu.", [p("spec", "table", "id, label, color, and on_activate callback.")], "true on success; false plus an error.", `mod.game.register_menu_mode({
  id="training_ai", label="AI TRAINING",
  on_activate=function() mod.game.arm_ai_match(2, true) end
})`, { notes: ["At most eight custom menu modes are accepted."] });
  gameMut("arm_ai_match", "mod.game.arm_ai_match(ai_player [, training])", "Arms the next native match for bot control.", [p("ai_player", "integer", "Player controlled by the bot."), p("training", "boolean", "Training behavior flag.", "optional")], "true on success; false plus an error.", `mod.game.arm_ai_match(2, true)`);
  gameRead("ai_match", "mod.game.ai_match()", "Reports the currently armed/active AI match state.", [], "Table describing active, ai_player, and training state.", `local ai = mod.game.ai_match()`);
  gameMut("start_match", "mod.game.start_match([selector])", "Starts a local native match using an optional map selector.", [p("selector", "integer", "Map selector; current value when omitted.", "optional")], "true on success; false plus an error.", `mod.game.start_match(0)`, { notes: ["State transitions are refused while managed online owns the match lifecycle."] });
  gameRead("map_count", "mod.game.map_count()", "Returns the number of available native/custom map selectors.", [], "Integer count.", `for i=0,mod.game.map_count()-1 do ... end`);
  gameRead("map_size", "mod.game.map_size()", "Returns current map dimensions.", [], "width, height in world units.", `local w, h = mod.game.map_size()`);
  gameRead("combat_ledger", "mod.game.combat_ledger()", "Returns monotonic death, score, and match-end counters used for robust result observation.", [], "Table with deaths0, deaths1, scores0, scores1, match_ends, and last_winner.", `local ledger = mod.game.combat_ledger()`);
  gameRead("room_tiles", "mod.game.room_tiles([room])", "Returns a complete row-major tile grid for one room.", [p("room", "integer", "Room index; current room when omitted.", "optional")], "Table with dimensions and tile triplets.", `local room = mod.game.room_tiles()`);
  gameRead("room_tile", "mod.game.room_tile(column, row [, room])", "Reads one native room tile.", [p("column", "integer", "Tile column."), p("row", "integer", "Tile row."), p("room", "integer", "Room index; current room when omitted.", "optional")], "Tile id/variant/flags values or nil plus an error.", `local id, variant, flags = mod.game.room_tile(4, 8)`);
  gameRead("tile_solid", "mod.game.tile_solid(tile_id)", "Queries native solidity metadata for a tile id.", [p("tile_id", "integer", "Native tile id.")], "Boolean.", `if mod.game.tile_solid(id) then ... end`);
  gameRead("camera", "mod.game.camera()", "Returns native camera position and gameplay viewport dimensions.", [], "Table with x, y, w, and h.", `local camera = mod.game.camera()`);
  gameRead("is_solid", "mod.game.is_solid(x, y)", "Queries native point solidity.", [p("x, y", "number", "World position.")], "Boolean.", `if mod.game.is_solid(next_x, next_y) then turn() end`);
  gameRead("is_pos_solid", "mod.game.is_pos_solid(x, y)", "Alias of is_solid.", [p("x, y", "number", "World position.")], "Boolean.", `local blocked = mod.game.is_pos_solid(x, y)`);
  gameMut("set_map_selector", "mod.game.set_map_selector(selector)", "Writes the native map selector.", [p("selector", "integer", "Selector value.")], "true on success; false plus an error.", `mod.game.set_map_selector(2)`);
  gameRead("get_map_selector", "mod.game.get_map_selector()", "Reads the native map selector.", [], "Integer selector.", `local selector = mod.game.get_map_selector()`);
  game.push(fn("mod.game.CMD_*", "mod.game.CMD_ATTACK | CMD_JUMP | CMD_RIGHT | CMD_LEFT | CMD_UP | CMD_DOWN | CMD_MENU", "Command-mask constants used by input APIs.", [], "Integer bit constants: attack 0x01, jump 0x02, right 0x04, left 0x08, up 0x10, down 0x20, menu 0x40.", `local attack_right = mod.game.CMD_ATTACK | mod.game.CMD_RIGHT`, { kind: "constants", tags: ["read-only"] }));

  add({
    id: "online",
    label: "mod.online",
    page: "api/game.html",
    description: "Read-only rollback status and managed online mod-suspension state.",
    lifecycle: "Managed online owns gameplay-mod suspension from match assignment through cleanup.",
    entries: [
      fn("mod.online.status", "mod.online.status()", "Returns online transport and mod-suspension state.", [], "Table containing gameplay_mods_suspended, active, connected, mode, local/remote player, state synchronization, and start-state status.", `local online = mod.online.status()
if online.gameplay_mods_suspended then return end`, { tags: ["read-only"] })
    ]
  });

  add({
    id: "content",
    label: "mod.content",
    page: "api/content.html",
    description: "Owner-scoped transactional registration and lookup of symbolic custom tile definitions.",
    lifecycle: "A transaction validates all definitions before commit. Committed content is removed automatically when the owning mod unloads.",
    entries: [
      prop("mod.content.owner", "string", "Canonical owner id used to qualify local content ids.", `mod.log(mod.content.owner)`),
      fn("mod.content.begin", "mod.content.begin()", "Creates an empty owner-scoped content transaction.", [], "Transaction object, or nil plus an error when registration is unavailable.", `local tx, err = mod.content.begin()
assert(tx, err)`, { tags: ["gameplay"], notes: ["Beginning content registration classifies the mod as gameplay-affecting."] }),
      fn("transaction:register_tile", "transaction:register_tile(definition)", "Stages one declarative tile definition.", [p("definition", "table", "id/name plus sprite, collision, force, animation, native visual, transform, tint, and optional integrity fields.")], "true on success; false plus an error on validation failure.", `local ok, err = tx:register_tile({
  id="spring", name="Spring",
  sprite_sheet="tiles", sprite_index=128,
  collision="solid", native_visual="mine"
})`, { tags: ["gameplay"], errors: "Invalid ids, ownership, sprite ranges, collision/force enums, geometry, hashes, or capacity fail without committing the transaction.", notes: ["Definitions are declarative. Complex deterministic behavior belongs in map-local Lua."] }),
      fn("transaction:commit", "transaction:commit()", "Atomically replaces the owner's complete committed definition set with the staged set.", [], "true on success; false plus an error on conflict or validation failure.", `local ok, err = tx:commit()
assert(ok, err)`, { tags: ["gameplay"], notes: ["An empty committed transaction removes the owner's prior set.", "A committed or aborted transaction cannot be reused."] }),
      fn("transaction:abort", "transaction:abort()", "Discards every staged definition.", [], "true when the transaction was open; false when already closed.", `if not package_ok then tx:abort() end`, { tags: ["gameplay"] }),
      fn("mod.content.qualify", "mod.content.qualify(local_or_qualified_id)", "Returns the canonical owner-qualified content key.", [p("local_or_qualified_id", "string", "Local id or already qualified owner:id.")], "Qualified key string; nil plus an error for an invalid id/owner.", `local key = mod.content.qualify("spring")`, { tags: ["read-only"] }),
      fn("mod.content.find_tile", "mod.content.find_tile(id)", "Looks up a committed tile definition.", [p("id", "string", "Local or qualified tile id.")], "Immutable definition table, or nil when missing.", `local spring = mod.content.find_tile("spring")
mod.log(spring.collision)`, { tags: ["read-only"] }),
      fn("mod.content.fingerprint", "mod.content.fingerprint()", "Computes identity and generation information for the complete process content registry.", [], "SHA-256 hexadecimal string, committed tile count, and numeric registry generation.", `local sha256, count, generation = mod.content.fingerprint()
mod.log(("%s tiles=%d generation=%d"):format(sha256, count, generation))`, { tags: ["read-only"], notes: ["The digest covers every map/mod owner, not only the calling mod.", "Use for diagnostics/compatibility, not as a package signature or cryptographic trust proof."] })
    ]
  });

  add({
    id: "fs",
    label: "mod.fs",
    page: "api/services.html",
    description: "Synchronous user-mediated Windows file/folder selection and bounded recursive lookup.",
    entries: [
      fn("mod.fs.pick_file", "mod.fs.pick_file(options)", "Opens an owner-bound native file picker with caller-defined file-type filters.", [p("options", "table", "Required table containing title, filters, allow_all, and filter_index fields.")], "Absolute UTF-8 selected path; nil plus \"cancelled\" when the user closes the dialog, or nil plus a native-dialog error.", `local path, err = mod.fs.pick_file({
  title = "Choose a map package",
  filters = {
    { name = "Yule maps (*.zip;*.json)", patterns = { "*.zip", "*.json" } },
    { name = "JSON maps (*.json)", patterns = { "*.json" } }
  },
  allow_all = false,
  filter_index = 1
})
if path then mod.log("selected " .. path) end`, { details: "<code>title</code> defaults to <code>Select file</code>. <code>filters</code> is an optional array of at most 16 tables; every filter requires a display <code>name</code> and an array of 1–16 filename <code>patterns</code>. With no filters, an All files entry is supplied automatically. Set <code>allow_all = true</code> to append it to a custom list. <code>filter_index</code> is a one-based initial selection and must name an available entry.", errors: "Malformed option types, invalid UTF-8, unsafe filename patterns, excessive counts/lengths, or an out-of-range filter index raise a Lua argument error before native UI opens. A disabled owner returns nil plus an explanation.", notes: ["This call opens modal native UI; invoke it only from an explicit user action, never from on_frame/on_tick.", "Patterns are filename expressions such as *.png or data-??.json. Paths, control characters, separators, and shell metacharacters are rejected.", "The returned Windows path supports Unicode and can exceed legacy MAX_PATH."] }),
      fn("mod.fs.pick_character_file", "mod.fs.pick_character_file([title])", "Deprecated API-v1 compatibility wrapper that opens the old ZIP/JSON character-package picker.", [p("title", "string", "Dialog title.", "optional")], "Absolute UTF-8 selected path; nil plus \"cancelled\" or a native-dialog error.", `-- Existing API-v1 mods remain compatible.
-- New code should call mod.fs.pick_file instead.
local path, err = mod.fs.pick_character_file("Import fighter")`, { tags: ["deprecated"], notes: ["This wrapper remains for API 1 compatibility; it is not the general framework picker.", "Invoke only from an explicit user action."] }),
      fn("mod.fs.pick_folder", "mod.fs.pick_folder([title])", "Opens an owner-bound Unicode native folder picker.", [p("title", "string", "UTF-8 dialog title; defaults to Select folder.", "optional")], "Absolute UTF-8 selected path; nil plus \"cancelled\" or an encoding error.", `local folder = mod.fs.pick_folder("Choose package folder")`, { notes: ["Invoke only from an explicit user action.", "A disabled owner cannot reopen a retained picker closure."] }),
      fn("mod.fs.find_file", "mod.fs.find_file(root [, name])", "Recursively finds the first named file below a selected directory.", [p("root", "string", "Absolute search root."), p("name", "string", "Filename; defaults to character.json.", "optional")], "Absolute file path; nil plus \"not found\".", `local manifest, err = mod.fs.find_file(folder, "character.json")`, { tags: ["read-only"], notes: ["Traversal is bounded to depth 12 and does not follow directory reparse points/junctions."] })
    ]
  });

  add({
    id: "json",
    label: "mod.json",
    page: "api/services.html",
    description: "Strict bounded JSON parsing and deterministic encoding for HTTP, interop, configuration imports, and other untrusted structured data.",
    entries: [
      prop("mod.json.null", "sentinel", "Unique value used to preserve JSON null without confusing it with absent Lua nil.", `local payload = { value = mod.json.null }
assert(mod.json.encode(payload) == '{"value":null}')`, { notes: ["Use mod.json.is_null after decoding. The sentinel is not userdata supplied by a remote payload."] }),
      fn("mod.json.encode", "mod.json.encode(value)", "Encodes a bounded Lua value as deterministic compact JSON.", [p("value", "boolean | finite number | UTF-8 string | table | mod.json.null", "Root value to encode.")], "JSON string; nil plus a path-aware diagnostic on failure.", `local body, err = mod.json.encode({
  action = "join",
  tags = mod.json.array({ "casual", "public" }),
  note = mod.json.null
})`, { details: "Object keys are emitted in bytewise lexical order. Dense positive-integer tables become arrays; string-keyed tables become objects; an untagged empty table becomes an object. Use array()/object() to preserve empty-container intent.", errors: "Rejects nil, functions, threads, full/light userdata other than the null sentinel, non-finite numbers, invalid UTF-8, mixed/sparse tables, unsupported keys, cycles, depth beyond 32, more than 65,536 nodes, strings beyond 256 KiB, and output beyond 1 MiB. Diagnostics identify the value path.", notes: ["Encoding never invokes tostring, metamethods, __pairs, or user code.", "Deterministic key order is useful for tests and caching, but is not a signature/canonical-JSON standard."] }),
      fn("mod.json.decode", "mod.json.decode(text)", "Parses strict full-document JSON into bounded Lua values.", [p("text", "string", "Complete UTF-8 JSON document, at most 1 MiB.")], "Decoded value; nil plus a byte-offset and path-aware diagnostic on failure. JSON null becomes mod.json.null; arrays and objects retain explicit container kind.", `local value, err = mod.json.decode('{"players":["a","b"],"next":null}')
if not value then
  mod.warn(err)
elseif mod.json.is_null(value.next) then
  mod.log("no next page")
end`, { errors: "Rejects invalid UTF-8/escapes/surrogates/numbers, duplicate object keys, trailing data, excessive depth/nodes/string/input size, and non-finite double results.", notes: ["The parser never evaluates Lua or JavaScript.", "Duplicate keys fail instead of silently choosing a winner."] }),
      fn("mod.json.array", "mod.json.array([values])", "Creates an explicitly tagged JSON array, shallow-copying an optional table.", [p("values", "table", "Optional source table.", "optional")], "New table tagged as an array.", `local empty = mod.json.array()
assert(mod.json.encode(empty) == "[]")`, { notes: ["Encoding still requires consecutive positive integer keys.", "The source table and returned table are distinct; nested values are not deep-copied."] }),
      fn("mod.json.object", "mod.json.object([values])", "Creates an explicitly tagged JSON object, shallow-copying an optional table.", [p("values", "table", "Optional source table.", "optional")], "New table tagged as an object.", `local object = mod.json.object({ enabled = true })
assert(mod.json.encode(object) == '{"enabled":true}')`, { notes: ["Encoding still requires string keys."] }),
      fn("mod.json.is_null", "mod.json.is_null(value)", "Checks whether a value is the framework JSON-null sentinel.", [p("value", "any", "Value to inspect.")], "Boolean.", `if mod.json.is_null(decoded.value) then
  decoded.value = "fallback"
end`, { tags: ["read-only"] })
    ]
  });

  add({
    id: "net",
    label: "mod.net",
    page: "api/services.html",
    description: "Process-wide bounded nonblocking TCP client slots.",
    lifecycle: "Four slots are shared process-wide. Close each owned slot when finished; disconnect/error invalidates its state.",
    entries: [
      fn("mod.net.connect", "mod.net.connect(host, port)", "Starts a nonblocking DNS/TCP connection.", [p("host", "string", "Hostname or address."), p("port", "integer", "TCP port.")], "Slot integer; nil plus an error if resolution/start fails or no slot is free.", `local slot, err = mod.net.connect("example.com", 47778)`, { notes: ["Connection completion is observed with check/connecting/connected."] }),
      fn("mod.net.check", "mod.net.check(slot)", "Polls one pending or established TCP slot.", [p("slot", "integer", "Slot returned by connect.")], "\"connecting\", \"connected\", or \"failed\".", `if mod.net.check(slot) == "connected" then send_hello() end`, { tags: ["read-only"] }),
      fn("mod.net.send", "mod.net.send(slot, data)", "Attempts a nonblocking TCP send.", [p("slot", "integer", "Connected slot."), p("data", "string", "Bytes to send.")], "true plus bytes accepted; false plus an error when disconnected or failed.", `local ok, bytes = mod.net.send(slot, '{"type":"ping"}\\n')`, { notes: ["A short byte count means the caller must retain and retry the unsent suffix."] }),
      fn("mod.net.recv", "mod.net.recv(slot)", "Receives up to 4,096 currently available TCP bytes.", [p("slot", "integer", "Connected slot.")], "Data string; nil when no data is ready; false on error/disconnect.", `local chunk = mod.net.recv(slot)
if chunk == false then mod.net.close(slot) end`, { tags: ["read-only"], notes: ["TCP is a stream: buffer and frame messages yourself."] }),
      fn("mod.net.close", "mod.net.close(slot)", "Closes and releases a TCP slot.", [p("slot", "integer", "Slot to close.")], "No values.", `mod.net.close(slot)`),
      fn("mod.net.connected", "mod.net.connected(slot)", "Tests whether a slot is established.", [p("slot", "integer", "Slot id.")], "Boolean.", `if mod.net.connected(slot) then ... end`, { tags: ["read-only"] }),
      fn("mod.net.connecting", "mod.net.connecting(slot)", "Tests whether a slot is still connecting.", [p("slot", "integer", "Slot id.")], "Boolean.", `if not mod.net.connecting(slot) then ... end`, { tags: ["read-only"] })
    ]
  });

  add({
    id: "http",
    label: "mod.http",
    page: "api/services.html",
    description: "Asynchronous WinHTTP GET requests for trusted bounded endpoints.",
    lifecycle: "Eight workers are shared process-wide, but handles and cleanup are owner-scoped. Cancel marks a request immediately; its slot is not freed or reused until the worker has actually finished.",
    entries: [
      fn("mod.http.get", "mod.http.get(url)", "Starts an asynchronous HTTP or HTTPS GET.", [p("url", "string", "Strict UTF-8 HTTP(S) URL, at most the native 2,048-wide-character buffer.")], "Opaque owner-bound request handle; nil plus an error when invalid or capacity is exhausted.", `local request, err = mod.http.get("https://example.com/version.json?channel=stable")`, { notes: ["Query strings are preserved in the request target; client-only fragments are stripped.", "Embedded URL credentials and non-HTTP(S) schemes are rejected.", "Resolve/connect/send/receive timeouts are 10 seconds.", "At most five redirects are followed, and HTTPS-to-HTTP downgrades are refused.", "Bodies larger than 8 MiB are rejected from Content-Length and while streaming.", "This general trusted-mod API does not enforce the updater's stricter host allowlist."] }),
      fn("mod.http.poll", "mod.http.poll(handle)", "Polls one asynchronous request.", [p("handle", "integer", "Handle returned by get.")], "\"pending\"; or \"done\", body, response; or \"error\", message[, response].", `local state, value, response = mod.http.poll(request)
if state == "done" then
  mod.log(("HTTP %d from %s"):format(response.status, response.url))
  consume(value, response.headers["content-type"])
end`, { tags: ["read-only"], notes: ["Only HTTP status 200 is treated as success; other HTTP responses return error plus their response table.", "The response table contains status, status_text when supplied, final url, redirected, redirect_count, and a bounded headers table.", "Exposed headers are content-type, content-length, etag, last-modified, cache-control, and location; cookies and authentication headers are never exposed.", "A handle is valid only for the mod that created it and is retired after its terminal poll."] }),
      fn("mod.http.cancel", "mod.http.cancel(handle)", "Cancels an owned request without blocking the game thread.", [p("handle", "integer", "Request handle.")], "No values.", `mod.http.cancel(request)`, { notes: ["Cancellation does not synchronously kill a WinHTTP worker already executing; the framework defers memory/slot cleanup until that worker publishes completion.", "All outstanding requests are canceled automatically when their owning mod unloads."] })
    ]
  });

  add({
    id: "os",
    label: "os override",
    page: "api/runtime.html",
    description: "The standard Lua os table is copied per mod, with one framework-owned safety override.",
    entries: [
      fn("os.exit", "os.exit([code_or_success])", "Routes deliberate process termination through the framework crash-report path.", [p("code_or_success", "integer | boolean | nil", "Exit status: nil/true = 0, false = 1, integer is used directly.", "optional")], "Does not return.", `-- Deliberately fatal; normally report an error instead.
-- os.exit(2)`, { tags: ["internal"], notes: ["Do not use for ordinary mod errors. mod.error logs safely; returning from a callback keeps the game alive."] })
    ]
  });

  add({
    id: "map",
    label: "map",
    page: "api/map-lua.html",
    kind: "Map-local Lua API",
    description: "Deterministic callbacks, bounded state, contact sensors, random values, and tick queries inside a V2 map behavior VM.",
    lifecycle: "Each map package owns a separate restricted VM. The runtime serializes map.state and relevant contact/tile state for rollback.",
    entries: [
      fn("map.on_contact", "map.on_contact(tile_id, callback)", "Registers a callback for every tick an eligible object overlaps a matching scripted tile.", [p("tile_id", "string", "Symbolic tile id/key."), p("callback", "function(object, tile)", "Contact callback.")], "No values.", `map.on_contact("spring", function(object, tile)
  object:set_velocity(object.vx, -7)
  tile:set_sprite(129, 4)
end)`, { tags: ["gameplay"], notes: ["Use on_enter for one-shot contact; on_contact runs continuously while overlap remains."] }),
      fn("map.on_enter", "map.on_enter(tile_id, callback)", "Registers a callback for the first tick of a new object/tile overlap.", [p("tile_id", "string", "Symbolic tile id/key."), p("callback", "function(object, tile)", "Enter callback.")], "No values.", `map.on_enter("fan", function(object)
  object:add_velocity(0, -2)
end)`, { tags: ["gameplay"] }),
      fn("map.on_leave", "map.on_leave(tile_id, callback)", "Registers a callback when a previously tracked object leaves the tile sensor.", [p("tile_id", "string", "Symbolic tile id/key."), p("callback", "function(object, tile)", "Leave callback.")], "No values.", `map.on_leave("slow_zone", function(object)
  object:clear_velocity_limits()
end)`, { tags: ["gameplay"] }),
      fn("map.on_tick", "map.on_tick(callback)", "Registers the map-wide deterministic tick callback.", [p("callback", "function()", "Runs once per gameplay tick.")], "No values.", `map.on_tick(function()
  map.state.pulse = (map.state.pulse or 0) + 1
end)`, { tags: ["gameplay"], notes: ["Keep work bounded and deterministic; the VM has instruction/time budgets."] }),
      fn("map.sensor", "map.sensor(tile_ref, options)", "Configures the one bounded contact sensor for a scripted tile binding.", [p("tile_ref", "string", "One-byte source symbol or canonical qualified tile key."), p("options", "table", "tile_box, object_box, objects, contact_scope, and mirror_with_room.")], "No values. Invalid/duplicate registration raises a load error.", `map.sensor("}", {
  tile_box={left=0, top=0, right=1, bottom=1},
  object_box="body",
  objects={"alive_player", "sword"},
  contact_scope="binding",
  mirror_with_room=true
})`, { tags: ["gameplay"], notes: ["Sensors may be registered only while map.lua loads.", "Sensors detect verified existing native objects; they do not create custom entities or combat hitboxes.", "Tile bounds are quantized to 1/256 cell and object filters are strictly enumerated."] }),
      fn("map.random", "map.random([min [, max]])", "Returns deterministic map-local pseudo-random values.", [p("min", "integer", "Upper bound when max is omitted, or lower bound.", "optional"), p("max", "integer", "Inclusive upper bound.", "optional")], "With no args: normalized number. With one/two integer args: bounded integer.", `local direction = map.random(0, 1) == 0 and -1 or 1`, { tags: ["gameplay"], notes: ["State is rollback-serialized. Standard math.random/randomseed are removed."] }),
      fn("map.has_tile", "map.has_tile(reference)", "Checks whether a custom tile binding is declared in the script manifest.", [p("reference", "string", "One-byte symbol or qualified tile key.")], "Boolean. Unknown or malformed names return false; non-string arguments are errors.", `if map.has_tile("demo:spring") then\n  map.on_enter("demo:spring", function(object, tile)\n    map.state.entries = (map.state.entries or 0) + 1\n  end)\nend`, { tags: ["read-only"], notes: ["Map API version 7. Available during loading and callbacks. Checks declarations, not placement in the active room; ordinary unbound native glyphs return false."] }),
      fn("map.tick", "map.tick()", "Returns the current deterministic map-script tick.", [], "Unsigned integer tick.", `local tick = map.tick()`, { tags: ["read-only"] }),
      fn("map.every", "map.every(interval [, phase])", "Tests whether the rollback clock matches a periodic tick.", [p("interval", "integer", "Period in ticks, 1..4294967295."), p("phase", "integer", "Offset 0..interval-1; defaults to zero.", "optional")], "Boolean; true when tick modulo interval equals phase.", `map.on_tick(function()\n  if map.every(60, 30) then\n    map.state.pulses = (map.state.pulses or 0) + 1\n  end\nend)`, { tags: ["read-only"], notes: ["Map API version 6. Phase zero includes tick zero. This predicate neither registers callbacks nor consumes RNG; repeated calls in one tick return the same result."] }),
      fn("map.state", "map.state[key] : nil | boolean | finite number | string", "Bounded deterministic key/value state serialized with the map runtime.", [p("key", "string", "1–31 byte key without NUL."), p("value", "nil | boolean | number | string", "nil deletes; strings are at most 63 bytes.")], "Reading returns the stored value or nil. Assignment stores/deletes it.", `map.state.activations = (map.state.activations or 0) + 1`, { kind: "property", tags: ["gameplay", "mutating"], notes: ["At most 64 entries are stored.", "Ordinary mutable globals and captured locals are not persistence surfaces."] }),
      fn("object.x / object.y", "object.x, object.y : finite number", "Writable contacted-object world position fields.", [], "Reading returns numbers; assignment updates the pending verified object view.", `object.y = object.y - 1`, { kind: "properties", tags: ["gameplay", "mutating"], notes: ["Callbacks operate on a deterministic pre-callback contact sample; movement does not remove later callbacks in the same tick."] }),
      fn("object.vx / object.vy", "object.vx, object.vy : finite number", "Writable contacted-object velocity fields.", [], "Reading returns numbers; assignment replaces the corresponding pending velocity.", `object.vy = -4`, { kind: "properties", tags: ["gameplay", "mutating"] }),
      fn("object.kind / object.id / object.contact_radius", "object.kind : string; object.id : integer; object.contact_radius : number", "Read-only contacted-object identity and verified native contact radius.", [], "kind is player, dead_body, sword, or hazard; id is stable for the tracked lifecycle; contact_radius is in pixels.", `if object.kind == "player" then
  object.vy = -4
end`, { kind: "properties", tags: ["read-only"] }),
      fn("object:set_velocity", "object:set_velocity(vx, vy)", "Replaces a contacted object's verified velocity.", [p("vx", "number", "Finite horizontal velocity."), p("vy", "number", "Finite vertical velocity.")], "No values. Invalid/stale values raise a callback error.", `object:set_velocity(object.vx, -7.5)`, { tags: ["gameplay"], notes: ["Object filters prevent inappropriate corpse/sword/player/hazard mutations unless the sensor explicitly admits that class."] }),
      fn("object:add_velocity", "object:add_velocity(delta_vx, delta_vy)", "Adds finite deltas to a contacted object's verified velocity.", [p("delta_vx", "number", "Horizontal delta."), p("delta_vy", "number", "Vertical delta.")], "No values. Non-finite/overflowing results raise a callback error.", `object:add_velocity(0, -0.35)`, { tags: ["gameplay"] }),
      fn("object:set_velocity_limits", "object:set_velocity_limits(limits, duration_ticks)", "Applies rollback-owned deterministic velocity clamps to the contacted object lifecycle.", [p("limits", "table", "One or more min_vx, max_vx, min_vy, max_vy values in -64..64."), p("duration_ticks", "integer", "1..1,000,000 ticks.")], "No values. Invalid bounds, lifecycle, duration, or capacity raises a callback error.", `object:set_velocity_limits({ min_vy=-4, max_vx=3 }, 8)`, { tags: ["gameplay"], notes: ["The record replaces prior limits for the same object lifecycle and clamps after native physics/callbacks."] }),
      fn("object:clear_velocity_limits", "object:clear_velocity_limits()", "Clears map-script velocity clamps from the object lifecycle.", [], "No values.", `object:clear_velocity_limits()`, { tags: ["gameplay"] }),
      fn("tile.x / tile.y / tile.key / tile.symbol / tile.mirrored", "tile.x, tile.y : integer; tile.key, tile.symbol : string; tile.mirrored : boolean", "Read-only identity, cell position, binding key/symbol, and room-mirroring state for the contacted tile.", [], "Field values for the exact contacted map cell.", `local push = tile.mirrored and -0.25 or 0.25`, { kind: "properties", tags: ["read-only"] }),
      fn("tile:set_sprite", "tile:set_sprite(sprite_index, duration_ticks [, options])", "Applies a rollback-safe temporary sprite override to this exact tile cell.", [p("sprite_index", "integer", "Cell index 0..configured maximum in the tile's declared sheet."), p("duration_ticks", "integer", "1..1,000,000 ticks before automatic reset."), p("options", "table", "Optional finite offset_x/offset_y in -4096..4096 destination pixels.", "optional")], "No values. Invalid index, duration, options, or capacity raises a callback error.", `tile:set_sprite(129, 16, { offset_y=8 })`, { tags: ["gameplay"], notes: ["Offsets are fixed to 1/256 pixel.", "The override does not move terrain, collision, sensors, or spawned entities."] }),
      fn("tile:reset_sprite", "tile:reset_sprite()", "Clears this tile cell's scripted sprite override immediately.", [], "No values.", `tile:reset_sprite()`, { tags: ["gameplay"] })
    ]
  });

  /* Exact low-level UI contracts. These entries are patched after the
     higher-level table is assembled so aliases and helper functions remain
     grouped together in the rendered reference. */
  const revise = (name, patch) => {
    const entry = entriesByName().get(name);
    if (!entry) throw new Error(`missing API entry ${name}`);
    Object.assign(entry, patch);
  };
  function entriesByName() {
    return new Map(groups.flatMap(group => group.entries).map(entry => [entry.name, entry]));
  }

  revise("mod.ui.mouse_buttons", {
    signature: "mod.ui.mouse_buttons([button])",
    params: [p("button", "integer", "1=left, 2=middle, 3=right. Omit/other to query all.", "optional")],
    returns: "For one selected button: down, pressed booleans. When omitted: left_down, left_pressed, right_down, right_pressed, middle_down, middle_pressed.",
    example: `local down, pressed = mod.ui.mouse_buttons(1)
if pressed then mod.log("left click") end`
  });
  revise("mod.ui._draw_native_cursor", {
    signature: "mod.ui._draw_native_cursor()",
    params: [],
    returns: "Boolean indicating whether the native cursor was drawn.",
    example: `-- Internal: prefer mod.ui.draw_cursor().`
  });
  revise("mod.ui.hitbox", {
    signature: "mod.ui.hitbox([id,] x, y, width, height [, button_or_options])",
    params: [
      p("id", "string", "Optional compatibility/widget id.", "optional"),
      p("x, y", "number", "Top-left bounds."),
      p("width, height", "number", "Positive hitbox size."),
      p("button_or_options", "integer | table", "Mouse button number or {button=n}; defaults to left.", "optional")
    ],
    returns: "hovered, clicked, down as three booleans.",
    example: `local hovered, clicked, down = mod.ui.hitbox(
  "row", 20, 20, 180, 28, {button=1}
)`
  });
  for (const name of ["mod.ui.rect", "mod.ui.fill_rect"]) {
    revise(name, {
      signature: `${name}(x, y, width, height [, options_or_r, g, b, a])`,
      params: [
        p("x, y", "number", "Top-left position."),
        p("width, height", "number", "Rectangle size."),
        p("options_or_r", "table | number", "Options with bg/fill/color/alpha, or red channel.", "optional"),
        p("g, b, a", "number", "Remaining normalized channels for positional form.", "optional")
      ],
      returns: "No values.",
      example: `${name}(20, 20, 220, 80, { fill={0.05,0.06,0.07,0.9} })`
    });
  }
  for (const name of ["mod.ui.border", "mod.ui.stroke_rect"]) {
    revise(name, {
      signature: `${name}(x, y, width, height [, options_or_line_width, r, g, b, a])`,
      params: [
        p("x, y", "number", "Top-left position."),
        p("width, height", "number", "Border bounds."),
        p("options_or_line_width", "table | number", "Options with border/color/line_width/alpha, or numeric width.", "optional"),
        p("r, g, b, a", "number", "Normalized channels for positional form.", "optional")
      ],
      returns: "No values.",
      example: `${name}(20, 20, 220, 80, { line_width=2, border={1,1,1,.4} })`
    });
  }
  revise("mod.ui.line", {
    signature: "mod.ui.line(x1, y1, x2, y2 [, options_or_line_width, r, g, b, a])",
    params: [
      p("x1, y1", "number", "Start point."),
      p("x2, y2", "number", "End point."),
      p("options_or_line_width", "table | number", "Options with color/line_width/alpha, or numeric width.", "optional"),
      p("r, g, b, a", "number", "Normalized channels for positional form.", "optional")
    ],
    returns: "No values.",
    example: `mod.ui.line(20, 50, 240, 50, { line_width=2, color={1,.5,.2,1} })`
  });
  revise("mod.ui.measure_text", {
    signature: "mod.ui.measure_text(text [, options_or_scale])",
    params: [p("text", "string", "Text to measure."), p("options_or_scale", "table | number", "Options containing scale, or the scale directly.", "optional")],
    returns: "Measured width and height.",
    example: `local width, height = mod.ui.measure_text("READY", {scale=1.2})`
  });
  revise("mod.ui.sheet_base", {
    returns: "Integer base id, or nil for an unknown sheet.",
    errors: ""
  });
  revise("mod.ui.sprite_id", {
    signature: "mod.ui.sprite_id(sheet_or_base, index)",
    params: [p("sheet_or_base", "string | integer", "Built-in/mod sheet name or an already resolved base id."), p("index", "integer", "Zero-based cell index.")],
    returns: "Integer sprite id; nil plus an error for an unknown mod sheet/out-of-range mod cell.",
    example: `local warning = mod.ui.sprite_id("icons", 3)`
  });
  revise("mod.ui.draw_sprite", {
    signature: "mod.ui.draw_sprite(sprite_or_descriptor, x, y [, options])",
    params: [
      p("sprite_or_descriptor", "integer | table", "Global sprite id, {sprite=id}, {id=id}, or {sheet=name_or_base,index=n}. A descriptor may also contain draw options."),
      p("x, y", "number", "Native sprite-center draw position."),
      p("options", "table", "Optional flip, layer, scale/scale_x/scale_y, angle, color/tint, and alpha overrides.", "optional")
    ],
    returns: "true when a live sprite was plotted; false when the id/renderer is unavailable.",
    example: `mod.ui.draw_sprite(
  {sheet="icons", index=3},
  120, 80,
  {scale=2, tint={1, .8, .7, 1}}
)`
  });
  revise("mod.ui.layout", {
    signature: "mod.ui.layout(x, y [, row_height, gap, width, text_scale])",
    params: [
      p("x, y", "number", "Initial stateful layout cursor."),
      p("row_height", "number", "Clamped to at least 8; defaults to 30.", "optional"),
      p("gap", "number", "Non-negative row gap; defaults to 6.", "optional"),
      p("width", "number", "Default button width, at least 20; defaults to 260.", "optional"),
      p("text_scale", "number", "Clamped to 0.4..3; defaults to 1.", "optional")
    ],
    returns: "No values. Configures the mod's internal layout cursor.",
    example: `mod.ui.layout(24, 24, 30, 6, 360, 1)
mod.ui.text("Settings")
if mod.ui.button("save", "Save") then save() end`
  });
  revise("mod.ui.cursor", {
    signature: "mod.ui.cursor([x [, y]])",
    params: [p("x", "number", "Optional new cursor X.", "optional"), p("y", "number", "Optional new cursor Y.", "optional")],
    returns: "Current cursor x and y.",
    example: `mod.ui.cursor(24, 120)
local x, y = mod.ui.cursor()`
  });
  revise("mod.ui.next_row", {
    signature: "mod.ui.next_row([rows])",
    params: [p("rows", "integer", "Number of row-height-plus-gap steps; minimum/default 1.", "optional")],
    returns: "No values.",
    example: `mod.ui.next_row(2)`
  });
  revise("mod.ui.text", {
    signature: "mod.ui.text(text [, options_or_r, g, b, scale, a])",
    params: [
      p("text", "string", "Text drawn at the stateful layout cursor."),
      p("options_or_r", "table | number", "Options with color/scale/alpha, or red channel.", "optional"),
      p("g, b, scale, a", "number", "Remaining positional color/scale/alpha values.", "optional")
    ],
    returns: "No values. Advances the cursor by one configured row.",
    example: `mod.ui.text("Settings", { scale=1.2, color={1,.8,.2,1} })`
  });
  revise("mod.ui.text_at", {
    signature: "mod.ui.text_at(text, x, y [, options_or_scale, r, g, b, a])",
    params: [
      p("text", "string", "Text to draw."),
      p("x, y", "number", "Explicit native-font baseline position."),
      p("options_or_scale", "table | number", "Options with scale/color/alpha, or the numeric scale.", "optional"),
      p("r, g, b, a", "number", "Normalized positional color channels.", "optional")
    ],
    returns: "No values.",
    example: `mod.ui.text_at("P1", 18, 18, {
  scale=1, color={1, .8, .2, 1}
})`
  });
  revise("mod.ui.button", {
    signature: "mod.ui.button(id, label [, width, height])",
    params: [
      p("id", "string", "Stable widget id."),
      p("label", "string", "Visible label."),
      p("width", "number", "Defaults to configured layout width.", "optional"),
      p("height", "number", "Defaults to configured row height.", "optional")
    ],
    returns: "Boolean; true on a left-button press inside the control.",
    example: `mod.ui.layout(24, 24)
if mod.ui.button("apply", "Apply", 180, 30) then apply() end`
  });
  revise("mod.ui.button_at", {
    signature: "mod.ui.button_at(id, label, x, y [, width, height])",
    params: [
      p("id", "string", "Stable widget id."),
      p("label", "string", "Visible label."),
      p("x, y", "number", "Top-left position."),
      p("width, height", "number", "Optional bounds; non-positive values derive from label text.", "optional")
    ],
    returns: "Boolean; true on a left-button press inside the control.",
    example: `if mod.ui.button_at("ok", "OK", 40, 90, 120, 28) then close() end`
  });
  revise("mod.ui.native_button", {
    signature: "mod.ui.native_button(id, label, grid_x, grid_y [, layout_x, layout_y])",
    params: [
      p("id", "string", "Stable mod-local id within the current menu state."),
      p("label", "string", "Native button text."),
      p("grid_x, grid_y", "number", "Native button grid position."),
      p("layout_x, layout_y", "number", "Native button layout metadata; each defaults to 5.", "optional")
    ],
    returns: "Boolean; true once when the owner-tracked native button reports activation.",
    example: `if mod.ui.native_button("open", "MY SCREEN", 5, 8, 5, 5) then
  mod.ui.enter_state("settings")
end`,
    details: "Creates or updates an owner-tracked native menu button for the current menu state. Calls in non-menu states are ignored with a one-time warning.",
    notes: ["The id, not a returned handle, identifies the button to native_set_* helpers.", "The framework removes owner records on unload."]
  });
  revise("mod.ui.native_set_pos", {
    signature: "mod.ui.native_set_pos(id, x, y)",
    params: [p("id", "string", "Owner-tracked id in the current menu state."), p("x, y", "number", "Native center position.")],
    returns: "true when a live owned button was updated; false otherwise.",
    example: `mod.ui.native_set_pos("open", 48, 360)`
  });
  revise("mod.ui.native_set_layout", {
    signature: "mod.ui.native_set_layout(id, layout_x, layout_y)",
    params: [p("id", "string", "Owner-tracked id."), p("layout_x, layout_y", "number", "Stored native layout metadata.")],
    returns: "true when the owned record exists; false otherwise.",
    example: `mod.ui.native_set_layout("open", 5, 6)`
  });
  revise("mod.ui.native_resize", {
    signature: "mod.ui.native_resize(id, width, height [, shrink])",
    params: [p("id", "string", "Owner-tracked id."), p("width, height", "number", "Positive native dimensions."), p("shrink", "number", "Non-negative native text-fit inset; defaults to 4.", "optional")],
    returns: "true when a live owned button was updated; false otherwise.",
    example: `mod.ui.native_resize("open", 190, 28, 4)`
  });
  revise("mod.ui.native_set_text_scale", {
    signature: "mod.ui.native_set_text_scale(id, scale_x [, scale_y])",
    params: [p("id", "string", "Owner-tracked id."), p("scale_x", "number", "Clamped to 0.1..3."), p("scale_y", "number", "Defaults to scale_x and is clamped to 0.1..3.", "optional")],
    returns: "true when a live owned button was updated; false otherwise.",
    example: `mod.ui.native_set_text_scale("open", 0.85)`
  });
  revise("mod.ui.native_hide", {
    signature: "mod.ui.native_hide(id, hidden)",
    params: [p("id", "string", "Owner-tracked id."), p("hidden", "boolean", "Desired hidden flag.")],
    returns: "true when a live owned button was updated; false otherwise.",
    example: `mod.ui.native_hide("open", not available)`
  });
  revise("mod.ui.native_remove", {
    signature: "mod.ui.native_remove(id)",
    params: [p("id", "string", "Owner-tracked id in the current menu state.")],
    returns: "true when the owner record was removed; false when not found.",
    summary: "Removes the framework's owner record for a tracked native button.",
    details: "This does not free arbitrary game-owned button memory. If a live control must disappear immediately, hide it first; native state reconstruction also clears stale controls.",
    example: `mod.ui.native_hide("open", true)
mod.ui.native_remove("open")`
  });
  revise("mod.ui.find_button_by_label", {
    signature: "mod.ui.find_button_by_label(label [, nth])",
    params: [p("label", "string", "Case-insensitive native label."), p("nth", "integer", "One-based matching occurrence; defaults to 1.", "optional")],
    returns: "Numeric native button pointer, or nil.",
    example: `local second = mod.ui.find_button_by_label("OPTIONS", 2)`
  });
  revise("mod.ui.find_button_by_action_ptr", {
    signature: "mod.ui.find_button_by_action_ptr(action_ptr [, nth])",
    params: [p("action_ptr", "integer", "Native action-function address."), p("nth", "integer", "One-based matching occurrence; defaults to 1.", "optional")],
    returns: "Numeric native button pointer, or nil.",
    example: `local ptr = mod.ui.find_button_by_action_ptr(action_address, 1)`
  });
  for (const name of ["mod.ui.button_invoke_ptr", "mod.ui.button_activate_ptr"]) {
    revise(name, {
      signature: `${name}(pointer [, event_code])`,
      params: [p("pointer", "integer", "Validated native button pointer."), p("event_code", "integer", "Defaults to activation code 3.", "optional")],
      returns: "Native integer result; nil plus an error when the pointer/action/dispatcher is unavailable.",
      example: `local result, err = ${name}(ptr, 3)`
    });
  }
  revise("mod.ui.button_resize_ptr", {
    signature: "mod.ui.button_resize_ptr(pointer, width, height [, shrink])",
    params: [p("pointer", "integer", "Validated native button pointer."), p("width, height", "number", "Positive native dimensions."), p("shrink", "number", "Non-negative inset; defaults to 4.", "optional")],
    returns: "true on success; false for invalid input.",
    example: `mod.ui.button_resize_ptr(ptr, 180, 28, 4)`
  });
  revise("mod.ui.button_hide_ptr", {
    signature: "mod.ui.button_hide_ptr(pointer, hidden)",
    params: [p("pointer", "integer", "Validated native button pointer."), p("hidden", "boolean", "Desired hidden flag.")],
    returns: "true on success; false for an invalid pointer.",
    example: `mod.ui.button_hide_ptr(ptr, true)`
  });
  revise("mod.ui.button_rect_ptr", {
    signature: "mod.ui.button_rect_ptr(pointer)",
    params: [p("pointer", "integer", "Validated native button pointer.")],
    returns: "center_x, center_y, width, height; or nil for an invalid pointer.",
    example: `local cx, cy, width, height = mod.ui.button_rect_ptr(ptr)`
  });
  revise("mod.ui.button_remove_ptr", {
    signature: "mod.ui.button_remove_ptr(pointer)",
    params: [p("pointer", "integer", "Validated native button pointer.")],
    returns: "true on success; false for an invalid pointer.",
    summary: "Retires a native button by hiding it, shrinking it, and moving it off-screen.",
    details: "The game does not expose an owner-safe destructor for arbitrary native buttons. This compatibility helper makes the target inert without freeing native memory.",
    example: `mod.ui.button_remove_ptr(ptr)`
  });

  revise("mod.ui.create_state", {
    signature: "mod.ui.create_state(name)",
    params: [p("name", "string", "Unique custom state id.")],
    returns: "Boolean from native custom-state registration.",
    summary: "Registers a named native custom-state slot.",
    details: "This is the low-level registration primitive. It does not accept or store callbacks; use define_state(name, spec) for lifecycle routing.",
    example: `assert(mod.ui.create_state("inspector"))`,
    notes: ["Most mods should use define_state so update, render, event, enter, leave, and cursor behavior are wired automatically."]
  });
  revise("mod.ui.register_state", {
    signature: "mod.ui.register_state(name)",
    params: [p("name", "string", "Unique custom state id.")],
    returns: "Boolean from native custom-state registration.",
    summary: "Exact alias of the low-level create_state(name) primitive.",
    details: "Like create_state, this only reserves the native state name and does not accept callbacks.",
    example: `assert(mod.ui.register_state("inspector"))`,
    notes: ["Use define_state(name, spec) for the normal callback-driven authoring contract."]
  });

  revise("mod.ui.define_theme", {
    returns: "true when name is non-empty and style is a table; false otherwise."
  });
  revise("mod.ui.pop_style", {
    returns: "The style table that was popped. At the base layer, returns a copy of that base without removing it."
  });
  revise("mod.ui.tooltip", {
    returns: "true when a non-empty tooltip was drawn; false for empty input."
  });
  revise("mod.ui.icon_button", {
    returns: "activated, hovered as two booleans. A disabled control never activates."
  });
  revise("mod.ui.panel", {
    signature: "mod.ui.panel([id,] x, y, width, height [, options])",
    params: [
      p("id", "string", "Optional id that enables panel hit testing.", "optional"),
      p("x, y", "number", "Top-left position."),
      p("width, height", "number", "Panel bounds."),
      p("options", "table", "Fill, inner panel, accent edge, border, hitbox, disabled state, and tooltip settings.", "optional")
    ],
    returns: "clicked, hovered, down as three booleans. Without id, all remain false.",
    example: `local clicked, hovered = mod.ui.panel(
  "inspector", 20, 20, 300, 180,
  {accent_edge="left"}
)`
  });
  revise("mod.ui.progress_bar", {
    signature: "mod.ui.progress_bar([id,] value, x, y, width, height [, options])",
    params: [
      p("id", "string", "Optional id for interactive hit testing.", "optional"),
      p("value", "number", "Value clamped to 0..1."),
      p("x, y", "number", "Top-left position."),
      p("width, height", "number", "Meter bounds."),
      p("options", "table", "interactive, vertical, reverse, fill, rail, border, label, and tooltip settings.", "optional")
    ],
    returns: "clamped_value, changed, hovered.",
    example: `volume, changed = mod.ui.progress_bar(
  "volume", volume, 20, 60, 240, 16,
  {interactive=true}
)`
  });
  revise("mod.ui.close_button", {
    returns: "activated, hovered, down as three booleans."
  });
  revise("mod.ui.cursor_sprite", {
    returns: "Resolved sprite id from the built-in misc sheet, or nil when sprite lookup is unavailable.",
    example: `local cursor_sprite_id = mod.ui.cursor_sprite(7)`
  });
  revise("mod.ui.draw_cursor", {
    returns: "true when the native cursor, configured sprite, or crosshair fallback was drawn; false otherwise."
  });
  revise("mod.ui.tile_preview", {
    signature: "mod.ui.tile_preview(tile_id [, frame, arg, x, y, scale, tile_y, options])",
    params: [
      p("tile_id", "integer | numeric string", "Zero-based cell in the built-in tiles sheet."),
      p("frame", "any", "Reserved compatibility argument; currently ignored.", "optional"),
      p("arg", "any", "Reserved compatibility argument; currently ignored.", "optional"),
      p("x, y", "number", "Draw position; each defaults to zero.", "optional"),
      p("scale", "number", "Sprite scale; defaults to 1.", "optional"),
      p("tile_y", "any", "Reserved compatibility argument; currently ignored.", "optional"),
      p("options", "table", "Additional draw_sprite options.", "optional")
    ],
    returns: "true when the preview sprite was drawn; false when lookup/rendering is unavailable.",
    summary: "Draws one built-in tiles-sheet cell through the compatibility preview helper.",
    example: `mod.ui.tile_preview(12, nil, nil, 80, 80, 3)`
  });
  revise("mod.ui.define_state", {
    returns: "true after storing the spec and installing lifecycle routing; false plus \"state name required\" only for an empty/non-string name.",
    details: "On state changes, leave receives the next name and enter receives the previous name. update receives a real-time dt clamped to 0..0.25, render receives no arguments, and event receives the framework event table. A table-valued cursor config draws a custom cursor; cursor=false suppresses the default.",
    notes: ["The helper calls low-level create_state(name), but the current implementation does not propagate that primitive's false result.", "update/render routing runs from on_frame and is presentation-timed, not deterministic gameplay timing."]
  });
  revise("mod.ui._set_default_cursor_visible", {
    signature: "mod.ui._set_default_cursor_visible([visible])",
    params: [p("visible", "boolean", "Desired default cursor visibility; defaults to true.", "optional")],
    returns: "No values."
  });

  revise("mod.input.bind", {
    signature: "mod.input.bind(key [, default_binding, label])",
    params: [
      p("key", "string", "Stable owner-local action key."),
      p("default_binding", "string", "Named keyboard binding; defaults to unbound.", "optional"),
      p("label", "string", "Human-readable label; defaults to key.", "optional")
    ],
    returns: "The effective binding name; nil plus an error when the default name or declaration is invalid.",
    example: `local binding, err = mod.input.bind("inspect", "F3", "Open inspector")
if not binding then mod.error(err) end`
  });
  revise("mod.input.clear", {
    returns: "true after clearing and saving; false plus an error for an unknown action or persistence refusal."
  });

  for (const name of ["mod.on_frame", "mod.on_tick", "mod.on_tick_post", "mod.on_event"]) {
    revise(name, {
      returns: "No values. Every registration appends another callback for this mod."
    });
  }
  revise("config.set", {
    returns: "true after conversion/clamping and save; false for an unknown key, action row, or invalid option; false plus an error when online suspension refuses the change.",
    notes: ["The value is converted according to the manifest schema. Integer/float rows are clamped, and options rows reject values outside their declared list."]
  });
  revise("config.on_action", {
    returns: "No values. Repeated registrations append handlers for the same action key.",
    notes: ["Registering a key that is absent or not type=action logs a warning but still installs the handler.", "Handlers stop while a gameplay-affecting mod is suspended online."]
  });

  Object.assign(groups.find(group => group.id === "storage"), {
    description: "Mod-owned persistent boolean, number, and string values with automatic atomic saves and schema migration.",
    lifecycle: "Storage loads before the entry script. Outside a migration callback, set, delete, and set_schema save immediately; migrate suppresses intermediate writes and saves once after a successful callback."
  });
  const storageCorrections = {
    "set": {
      signature: "storage.set(key, value)",
      params: [p("key", "string", "Validated storage key."), p("value", "boolean | number | string | nil", "Persisted value; nil deletes the key.")],
      summary: "Sets or deletes one value and immediately saves the owner store.",
      returns: "true after the atomic save; false plus an error for invalid input, suspension, or persistence failure.",
      example: `local ok, err = storage.set("launches", storage.get("launches", 0) + 1)
if not ok then mod.error(err) end`
    },
    "delete": {
      summary: "Removes one key and immediately saves the owner store.",
      returns: "true when the key existed; false when it did not. A save failure returns false plus an error.",
      example: `local existed, err = storage.delete("legacy_cache")`
    },
    "set_schema": {
      summary: "Sets a non-negative schema version and immediately saves it.",
      returns: "true after the atomic save; false plus an error for a negative value, suspension, or persistence failure.",
      example: `assert(storage.set_schema(2))`
    },
    "migrate": {
      summary: "Runs a schema-migration callback with intermediate file saves suppressed, then saves the new schema once on success.",
      returns: "success, schema_or_error. On success the second value is the current/target integer schema; on failure it is an error string.",
      details: "The callback receives from_schema and target_schema and may return false, reason to reject the migration. A callback error or false result does not write the file, but mutations already made to the in-memory table are not rolled back; reload or explicitly restore them before continuing."
    }
  };
  for (const [shortName, patch] of Object.entries(storageCorrections)) {
    revise(`storage.${shortName}`, patch);
    const aliasPatch = {...patch};
    if (aliasPatch.signature) aliasPatch.signature = aliasPatch.signature.replace(/^storage/, "mod.storage");
    if (aliasPatch.example) aliasPatch.example = aliasPatch.example.replaceAll("storage.", "mod.storage.");
    revise(`mod.storage.${shortName}`, aliasPatch);
  }

  revise("mod.assets.load_spritesheet", {
    signature: "mod.assets.load_spritesheet(id, path [, options])",
    params: [
      p("id", "string", "Owner-local id: 1..63 letters, digits, _, -, ., or :."),
      p("path", "string", "Mod-relative PNG path."),
      p("options", "table", "cell_w/cell_h (default 16), padding (default 0), flags (default 1), and force.", "optional")
    ]
  });
  revise("mod.assets.begin_batch", {
    returns: "true after increasing batch depth; nil plus an error when the mod is inactive.",
    details: "Batches may nest up to depth 32. Registrations defer the atlas rebuild until the outer batch is ended."
  });
  revise("mod.assets.end_batch", {
    returns: "true after decreasing depth and flushing when the outer batch closes; nil plus an error on inactive owner or rebuild failure."
  });
  revise("mod.assets.cancel_batch", {
    returns: "true after clearing all batch depth/dirty state; nil plus an error when the mod is inactive."
  });
  revise("mod.assets.sprite_id", {
    signature: "mod.assets.sprite_id(sheet_id [, index])",
    params: [p("sheet_id", "string", "Owner-local sheet id."), p("index", "integer", "Zero-based cell; defaults to 0.", "optional")]
  });
  revise("mod.assets.info", {
    returns: "With id: its metadata table or nil when unknown. Without id: an array of all owner sheets."
  });

  revise("mod.anim.frame_strip", {
    params: [p("options", "table", "sheet/atlas, first/start, count/len, and integer step.")],
    returns: "Array of frame descriptor tables shaped as {sprite=global_id}; empty when the sheet or count is invalid.",
    example: `local frames = mod.anim.frame_strip({
  sheet="effects", first=0, count=6, step=1
})`
  });
  revise("mod.anim.frame_table", {
    params: [p("options", "table", "Either a frame array, or {frames=array,sheet=name}. Numeric frames become {sprite=id}; table fields are preserved.")],
    returns: "Normalized array of frame descriptor tables.",
    example: `local frames = mod.anim.frame_table({
  sheet="effects",
  frames={{index=0,duration=.2}, {index=1,duration=.05}}
})`
  });
  revise("mod.anim.frame", {
    returns: "Current frame descriptor table and one-based index; nil when the animation has no frames."
  });
  revise("mod.anim.update", {
    returns: "The same mutable animation instance, or nil for a non-table argument.",
    details: "Accumulates dt × speed, advances across per-frame duration or 1/fps, and honors paused, loop, direction, and ping_pong."
  });
  revise("mod.anim.draw", {
    returns: "Boolean from mod.ui.draw_sprite; false when no drawable frame is available."
  });
  revise("animation:update", {
    returns: "The same animation instance."
  });
  revise("animation:frame", {
    returns: "Current frame descriptor table and one-based index; nil when empty."
  });
  revise("animation:draw", {
    returns: "Boolean indicating whether the current sprite was drawn."
  });

  Object.assign(groups.find(group => group.id === "game"), {
    lifecycle: "Player arguments are zero-based (0 or 1). Read calls are telemetry; writes and control calls are guarded and refused while managed online play suspends gameplay-affecting mods."
  });
  for (const entry of groups.find(group => group.id === "game").entries) {
    for (const param of entry.params || []) {
      if (param.name === "player" || param.name === "ai_player") {
        param.description = `${param.description.replace(/\s*Zero-based: 0 or 1\.$/, "")} Zero-based: 0 or 1.`;
      }
    }
  }
  revise("mod.game.snapshot", {
    signature: "mod.game.snapshot([player [, include_tiles]])",
    params: [
      p("player", "integer", "Zero-based perspective, 0 or 1; defaults to 0 and is masked to one bit.", "optional"),
      p("include_tiles", "boolean", "Include current-room tile telemetry; defaults to true.", "optional")
    ],
    returns: "Always a snapshot table. When the player pointer is unavailable, the table contains an error field instead of player telemetry.",
    details: "The table includes deterministic counters, player/enemy snapshots, room/camera context, active entities and swords, countdown/leader state, and optional tile information.",
    example: `local state = mod.game.snapshot(0, false)
if not state.error then
  mod.log(("%s,%s"):format(state.player_x, state.player_y))
end`
  });
  revise("mod.game.sword_snapshot", {
    returns: "Table with p0_has_sword, p1_has_sword, and a one-based swords array."
  });
  revise("mod.game.native_tick", {
    returns: "Unsigned integer tick, or nil when the native global is unavailable."
  });
  revise("mod.game.set_native_tick", {
    returns: "true after writing; false when the native global is unavailable; false plus an error when the online mutation guard refuses the call."
  });
  revise("mod.game.rng_seed", {
    returns: "Unsigned integer seed, or nil when the native global is unavailable."
  });
  revise("mod.game.set_rng_seed", {
    returns: "true after writing; false when the native global is unavailable; false plus an error when the online mutation guard refuses the call."
  });
  for (const spelling of ["colour", "color"]) {
    revise(`mod.game.player_${spelling}`, {
      signature: `mod.game.player_${spelling}([player [, clothing]])`,
      returns: "RGBA table; nil plus an error when the native color function is unavailable.",
      example: `local skin, err = mod.game.player_${spelling}(0, false)`
    });
    revise(`mod.game.player_${spelling}_index`, {
      signature: `mod.game.player_${spelling}_index([player [, clothing]])`,
      example: `local index = mod.game.player_${spelling}_index(0, true)`
    });
    revise(`mod.game.set_player_${spelling}_index`, {
      returns: "The effective wrapped palette index.",
      example: `local effective = mod.game.set_player_${spelling}_index(0, true, 4)`
    });
  }
  revise("mod.game.set_player_render_colours", {
    example: `mod.game.set_player_render_colours(0, {1,.8,.7,1}, {.2,.6,1,1})`
  });
  revise("mod.game.set_player_render_colors", {
    example: `mod.game.set_player_render_colors(0, skin, clothes)`
  });
  revise("mod.game.set_player_body_hidden", {
    example: `mod.game.set_player_body_hidden(0, true)`
  });
  revise("mod.game.player_body_hidden", {
    signature: "mod.game.player_body_hidden([player])",
    params: [p("player", "integer", "Zero-based player index, 0 or 1; defaults to 0.", "optional")],
    example: `local hidden = mod.game.player_body_hidden(0)`
  });
  revise("mod.game.set_player_sword_idle_offset", {
    signature: "mod.game.set_player_sword_idle_offset(player [, x, y])",
    params: [
      p("player", "integer", "Zero-based player index, 0 or 1."),
      p("x, y", "number", "Pixel offsets clamped to -16..16; each defaults to 0.", "optional")
    ],
    example: `mod.game.set_player_sword_idle_offset(0, 0, -3)`
  });
  revise("mod.game.state_checksum", {
    returns: "Unsigned checksum integer; nil plus an error when deterministic state cannot be captured."
  });
  revise("mod.game.block_next_tick", {
    returns: "true after arming or clearing the hook; false plus an error when mutation is refused."
  });
  for (const name of ["mod.game.poll_cmds", "mod.game.poll_cmds_raw"]) {
    revise(name, {
      signature: `${name}([player [, mode]])`,
      params: [
        p("player", "integer", "Zero-based player index, 0 or 1; defaults to 0.", "optional"),
        p("mode", "integer", "Native command-poll mode; defaults to 1.", "optional")
      ],
      example: `local mask = ${name}(0, 1)`
    });
  }
  revise("mod.game.set_input", {
    params: [
      p("player", "integer", "Zero-based player index, 0 or 1."),
      p("mask", "integer | table", "CMD_* bitmask or supported named-command table."),
      p("ticks", "integer", "Duration in gameplay ticks; defaults to 1.", "optional"),
      p("replace", "boolean", "Replace instead of merge; defaults to true.", "optional")
    ],
    example: `mod.game.set_input(1, mod.game.CMD_LEFT | mod.game.CMD_ATTACK, 1, true)`
  });
  revise("mod.game.input_override", {
    params: [
      p("player", "integer", "Zero-based player index, 0 or 1."),
      p("mask", "integer | table", "CMD_* bitmask or supported named-command table."),
      p("polls", "integer", "Number of command polls; defaults to 1.", "optional"),
      p("replace", "boolean", "Replace instead of merge; defaults to false.", "optional")
    ],
    example: `mod.game.input_override(1, mod.game.CMD_JUMP, 2)`
  });
  revise("mod.game.input_clear", {
    example: `mod.game.input_clear(1)`
  });
  revise("mod.game.input_status", {
    signature: "mod.game.input_status([player])",
    params: [p("player", "integer", "Zero-based player index, 0 or 1; defaults to 0.", "optional")],
    returns: "Table with active, tick_active/mask/ticks/replace, poll_active/mask/frames/replace, raw_blocked, effective_now, and raw_now.",
    example: `local status = mod.game.input_status(1)`
  });
  revise("mod.game.block_raw_input", {
    example: `mod.game.block_raw_input(1, true)`
  });
  revise("mod.game.register_menu_mode", {
    params: [p("spec", "table", "id (≤31 bytes), label (≤23 bytes), on_activate(player_index), and optional RGB color.")],
    example: `mod.game.register_menu_mode({
  id="training_ai",
  label="AI TRAINING",
  color={.7,.45,1},
  on_activate=function(player)
    mod.game.arm_ai_match((player + 1) % 2, true)
    return true
  end
})`
  });
  revise("mod.game.arm_ai_match", {
    example: `mod.game.arm_ai_match(1, true)`
  });
  revise("mod.game.ai_match", {
    returns: "Table with active=true, ai_player, and training while armed/active; nil otherwise."
  });
  revise("mod.game.start_match", {
    returns: "Boolean from the native start request; false plus an error only when the online mutation guard refuses the call."
  });
  revise("mod.game.map_size", {
    returns: "Table {w=world_pixel_width,h=world_pixel_height}, or nil when native dimension functions are unavailable.",
    example: `local size = mod.game.map_size()
if size then mod.log(size.w .. "x" .. size.h) end`
  });
  revise("mod.game.room_tiles", {
    params: [p("room", "integer", "Non-negative room index; defaults to 0.", "optional")],
    returns: "Table with room, w, h, optional tile_w/tile_h/origin_x/origin_y, and a flat one-based row-major ids array; nil plus an error when unavailable.",
    example: `local room, err = mod.game.room_tiles(0)
if room then
  local tile_id = room.ids[row * room.w + column + 1] -- row/column are 0-based
end`
  });
  revise("mod.game.room_tile", {
    params: [
      p("column", "integer", "One-based tile column."),
      p("row", "integer", "One-based tile row."),
      p("room", "integer", "Non-negative room index; defaults to 0.", "optional")
    ],
    returns: "Table with exists, id, frame, arg, room_index, and optional pixel coordinates; nil when unavailable or out of range.",
    example: `local tile = mod.game.room_tile(4, 8, 0)
if tile and tile.exists then mod.log(tile.id) end`
  });

  revise("mod.audio.play_sfx", {
    params: [
      p("path_or_builtin", "string", "Mod-relative audio path, or a built-in id when the string does not look like a path."),
      p("options", "table", "loops (default 0; -1 forever), ticks (default -1), and per-play volume multiplied by owner SFX volume.", "optional")
    ]
  });
  revise("mod.audio.play_music", {
    params: [
      p("path", "string", "Existing mod-relative music file."),
      p("options", "table", "loops (default -1/forever) and per-play volume multiplied by owner music volume.", "optional")
    ]
  });
  revise("mod.audio.stop_music", {
    returns: "true when already stopped or after stopping owner-held music; false plus an error when another mod owns playback."
  });
  revise("mod.audio.set_music_volume", {
    returns: "true after storing the owner volume clamped to 0..1; false plus an error without mod context.",
    notes: ["The WinMM file fallback cannot change volume after playback starts."]
  });
  revise("mod.audio.set_sfx_volume", {
    returns: "true after storing the owner volume clamped to 0..1; false plus an error without mod context."
  });
  revise("mod.audio.stop_sfx", {
    signature: "mod.audio.stop_sfx()",
    params: [],
    summary: "Stops every mixer and generated-audio voice owned by the calling mod.",
    returns: "Number of voices/channels reported stopped.",
    example: `local stopped = mod.audio.stop_sfx()`
  });
  revise("mod.audio.status", {
    returns: "Table with backend, generated_backend, sfx_volume, music_volume, cached_chunks, generated_chunks, generated_pcm_bytes, generated_chunk_limit, and generated_pcm_limit."
  });

  window.EGGNOGG_API_GROUPS = groups;
})();
