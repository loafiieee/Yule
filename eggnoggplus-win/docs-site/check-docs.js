"use strict";

const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const siteRoot = __dirname;
const projectRoot = path.resolve(siteRoot, "..");
const errors = [];

function fail(message) {
  errors.push(message);
}

function walk(dir, extension) {
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap(entry => {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) return walk(full, extension);
    return entry.name.endsWith(extension) ? [full] : [];
  });
}

function loadApiGroups() {
  const context = { window: {} };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(path.join(siteRoot, "api-data.js"), "utf8"), context);
  return context.window.EGGNOGG_API_GROUPS;
}

const groups = loadApiGroups();
const entries = groups.flatMap(group => group.entries.map(entry => ({ group, entry })));
const names = new Set();
const ids = new Set();

for (const { group, entry } of entries) {
  if (names.has(entry.name)) fail(`duplicate API name: ${entry.name}`);
  if (ids.has(entry.id)) fail(`duplicate API id: ${entry.id}`);
  names.add(entry.name);
  ids.add(entry.id);
  for (const field of ["name", "signature", "summary", "returns", "example"]) {
    if (typeof entry[field] !== "string" || !entry[field].trim()) {
      fail(`${entry.name}: missing ${field}`);
    }
  }
  if (!Array.isArray(entry.params)) fail(`${entry.name}: params must be an array`);
  for (const param of entry.params || []) {
    if (!Array.isArray(param) || param.length < 3 || param.slice(0, 3).some(value => !String(value).trim())) {
      fail(`${entry.name}: malformed parameter row`);
    }
  }
  if (!group.page) fail(`${group.id}: missing page`);
}

const luaSource = fs.readFileSync(path.join(projectRoot, "lua_manager.c"), "utf8");
const modFsSource = fs.readFileSync(path.join(projectRoot, "mod_fs.c"), "utf8");
const modHttpSource = fs.readFileSync(path.join(projectRoot, "mod_http.c"), "utf8");

function pushFunctionFields(functionName) {
  if (functionName === "fs") {
    const marker = "void mod_fs_lua_push_api";
    const start = modFsSource.indexOf(marker);
    if (start < 0) throw new Error(`missing ${marker}`);
    const body = modFsSource.slice(start);
    const fields = Array.from(
      body.matchAll(
        /fs_register_owner_function\(\s*L,\s*owner_enabled,\s*[A-Za-z0-9_]+,\s*"([^"]+)"\s*\)/g
      ),
      match => match[1]
    );
    const direct = body.match(
      /lua_pushcfunction\(L,\s*lua_fs_find_file\);\s*lua_setfield\(L,\s*-2,\s*"([^"]+)"\)/
    );
    if (direct) fields.push(direct[1]);
    return fields;
  }
  if (functionName === "http") {
    const marker = "void mod_http_lua_push_api";
    const start = modHttpSource.indexOf(marker);
    if (start < 0) throw new Error(`missing ${marker}`);
    const body = modHttpSource.slice(start);
    const fields = Array.from(
      body.matchAll(
        /http_register_owner_function\(\s*L,\s*owner,\s*[A-Za-z0-9_]+,\s*"([^"]+)"\s*\)/g
      ),
      match => match[1]
    );
    const get = body.match(
      /lua_pushcclosure\(L,\s*lua_http_get,\s*2\);\s*lua_setfield\(L,\s*-2,\s*"([^"]+)"\)/
    );
    if (get) fields.push(get[1]);
    return fields;
  }

  const marker = `static void push_${functionName}_api_table`;
  const start = luaSource.indexOf(marker);
  if (start < 0) throw new Error(`missing ${marker}`);
  const lines = luaSource.slice(start).split(/\r?\n/);
  const fields = [];
  for (const line of lines.slice(1)) {
    if (line.trim() === "}") break;
    if (!/lua_push(?:cfunction|cclosure)/.test(line)) continue;
    const match = line.match(/lua_setfield\([^,]+,\s*-2,\s*"([^"]+)"\)/);
    if (match) fields.push(match[1]);
  }
  return fields;
}

const namespaces = {
  assets: "mod.assets",
  content: "mod.content",
  config: "config",
  storage: "storage",
  interop: "interop",
  input: "mod.input",
  audio: "mod.audio",
  ui: "mod.ui",
  game: "mod.game",
  online: "mod.online",
  fs: "mod.fs",
  font: "mod.font",
  texture: "mod.texture",
  net: "mod.net",
  http: "mod.http",
  api: "mod.api",
  json: "mod.json",
  mod: "mod"
};

const sourceCallables = new Set();
for (const [sourceNamespace, documentedNamespace] of Object.entries(namespaces)) {
  for (const field of pushFunctionFields(sourceNamespace)) {
    sourceCallables.add(`${documentedNamespace}.${field}`);
  }
}

for (const match of luaSource.matchAll(/"function ui\.([A-Za-z0-9_]+)/g)) {
  sourceCallables.add(`mod.ui.${match[1]}`);
}
if (luaSource.includes('"ui.segmented = ui.tabs')) sourceCallables.add("mod.ui.segmented");

for (const match of luaSource.matchAll(/"function anim\.([A-Za-z0-9_]+)\(/g)) {
  sourceCallables.add(`mod.anim.${match[1]}`);
}
for (const match of luaSource.matchAll(/"function anim\.instance:([A-Za-z0-9_]+)/g)) {
  sourceCallables.add(`animation:${match[1]}`);
}

for (const method of ["register_tile", "commit", "abort"]) {
  sourceCallables.add(`transaction:${method}`);
}
sourceCallables.add("os.exit");

const mapSource = fs.readFileSync(path.join(projectRoot, "map_script.c"), "utf8");
for (const field of ["on_contact", "on_enter", "on_leave", "on_tick", "sensor", "random", "tick"]) {
  if (!mapSource.includes(`lua_setfield(L, -2, "${field}")`)) fail(`map source registration missing: map.${field}`);
  sourceCallables.add(`map.${field}`);
}
for (const method of ["set_velocity", "add_velocity", "set_velocity_limits", "clear_velocity_limits"]) {
  if (!mapSource.includes(`strcmp(key, "${method}")`)) fail(`map object method missing: ${method}`);
  sourceCallables.add(`object:${method}`);
}
for (const method of ["set_sprite", "reset_sprite"]) {
  if (!mapSource.includes(`strcmp(key, "${method}")`)) fail(`map tile method missing: ${method}`);
  sourceCallables.add(`tile:${method}`);
}

for (const callable of sourceCallables) {
  if (!names.has(callable)) fail(`undocumented source callable: ${callable}`);
}

const dynamicIdsByPage = new Map();
for (const group of groups) {
  const key = path.normalize(group.page);
  if (!dynamicIdsByPage.has(key)) dynamicIdsByPage.set(key, new Set());
  const set = dynamicIdsByPage.get(key);
  set.add(`namespace-${group.id.replace(/[^a-z0-9_.:-]+/g, "-")}`);
  group.entries.forEach(entry => set.add(entry.id));
}

function idsInHtml(text) {
  const result = new Set([...text.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]));
  for (const match of text.matchAll(/<h[23][^>]*>([\s\S]*?)<\/h[23]>/g)) {
    const plain = match[1].replace(/<[^>]+>/g, "").replace(/&amp;/g, "&").trim();
    const generated = plain.toLowerCase().replace(/[^a-z0-9_.:-]+/g, "-").replace(/^-+|-+$/g, "");
    if (generated) result.add(generated);
  }
  return result;
}

const htmlFiles = walk(siteRoot, ".html");
for (const file of htmlFiles) {
  const text = fs.readFileSync(file, "utf8");
  const relative = path.relative(siteRoot, file);
  for (const [tag, pattern] of [
    ["title", /<title>[\s\S]*?<\/title>/g],
    ["h1", /<h1(?:\s|>)[\s\S]*?<\/h1>/g],
    ["main", /<main(?:\s|>)[\s\S]*?<\/main>/g]
  ]) {
    const count = [...text.matchAll(pattern)].length;
    if (count !== 1) fail(`${relative}: expected one ${tag}, found ${count}`);
  }
  const localIds = idsInHtml(text);
  if (localIds.size !== [...text.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]).length +
      [...text.matchAll(/<h[23][^>]*>([\s\S]*?)<\/h[23]>/g)].filter(match => !/\bid=/.test(match[0])).length) {
    // Dynamic heading ids can legitimately equal an explicit id; duplicate explicit ids are checked below.
  }
  const explicitIds = [...text.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]);
  if (new Set(explicitIds).size !== explicitIds.length) fail(`${relative}: duplicate explicit id`);

  for (const match of text.matchAll(/\bhref="([^"]+)"/g)) {
    const href = match[1];
    if (/^(?:https?:|mailto:|yule:|javascript:)/.test(href)) continue;
    const [rawTarget, fragment = ""] = href.split("#", 2);
    const target = rawTarget ? path.resolve(path.dirname(file), rawTarget) : file;
    if (!fs.existsSync(target)) {
      fail(`${relative}: missing link target ${href}`);
      continue;
    }
    if (!fragment || !target.endsWith(".html")) continue;
    const targetText = fs.readFileSync(target, "utf8");
    const targetIds = idsInHtml(targetText);
    const targetRelative = path.normalize(path.relative(siteRoot, target));
    for (const id of dynamicIdsByPage.get(targetRelative) || []) targetIds.add(id);
    if (!targetIds.has(fragment)) fail(`${relative}: missing fragment ${href}`);
  }
}

for (const file of ["app.js", "api-data.js", "styles.css", "index.html", "README.md"]) {
  if (!fs.existsSync(path.join(siteRoot, file))) fail(`missing site asset: ${file}`);
}

const styleSource = fs.readFileSync(path.join(siteRoot, "styles.css"), "utf8");
const appSource = fs.readFileSync(path.join(siteRoot, "app.js"), "utf8");
const homeSource = fs.readFileSync(path.join(siteRoot, "index.html"), "utf8");
for (const token of [
  "--bg: #0e0a0b",
  "--panel: #1a1214",
  "--ember: #ff5340",
  '"Press Start 2P"',
  '"Space Grotesk"',
  ".docs-hero",
  ".hero-terminal",
]) {
  if (!styleSource.includes(token)) fail(`Yule documentation design token missing: ${token}`);
}
if (/(--teal|--cyan|--blue|--gold)\s*:/.test(styleSource)) {
  fail("legacy blue/gold documentation palette token remains");
}
if (appSource.includes("theme-toggle") || appSource.includes("eggnogg-docs-theme")) {
  fail("legacy generic theme switch remains in the Yule documentation shell");
}
for (const marker of ["docs-hero", "hero-terminal", "pixel-button", "section-chip"]) {
  if (!homeSource.includes(marker)) fail(`task-first documentation home marker missing: ${marker}`);
}

if (errors.length) {
  console.error(errors.map(error => `- ${error}`).join("\n"));
  process.exit(1);
}

console.log(`docs audit: ${htmlFiles.length} pages, ${groups.length} API namespaces, ${entries.length} documented members, source coverage and links OK`);
