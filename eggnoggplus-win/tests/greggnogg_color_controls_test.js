"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const Core = require("../greggnogg/editor-core.js");
const source = fs.readFileSync(path.join(__dirname,"../greggnogg/editor.js"), "utf8");
const builder = source.slice(source.indexOf("  function buildColorBank("), source.indexOf("\n  function renderAll()"));

class Element {
  constructor(tag) { this.tag = tag; this.children = []; this.listeners = {}; this.attrs = {}; this.style = {}; this.classList = {toggle() {}, remove() {}}; }
  append(...children) { this.children.push(...children); }
  appendChild(child) { this.append(child); }
  setAttribute(key, value) { this.attrs[key] = value; }
  removeAttribute(key) { delete this.attrs[key]; }
  addEventListener(event, fn) { this.listeners[event] = fn; }
  fire(event) { this.listeners[event]?.({relatedTarget:null}); }
  contains(element) { return element === this || this.children.some(c => c.contains(element)); }
  querySelectorAll() { return []; }
  focus() {}
}

function fixture(goal) {
  const state = {document:{rules:{eggnoggColor:[0.12345,0.5,0.9]}, rooms:[{appearance:{primary:{background:[0,0,0]}}}]}};
  const history = [];
  let renders = 0;
  const context = {Core, state, COLOR_KEYS:["background"], COLOR_LABELS:{background:"Background"}, FALLBACK_COLORS:{background:"#000000"},
    document:{activeElement:null, createElement:tag => new Element(tag)},
    activeRoom:() => state.document.rooms[0],
    historySnapshot:label => ({label,document:structuredClone(state.document)}),
    pushHistory:(snapshot,label) => history.push({snapshot,label}),
    setAppearancePreviewBank() {}, validate() {}, renderGrid() { renders++; }, renderRoomTabs() {}, markChanged() {}, refreshPalettePreviews() {},
  };
  vm.createContext(context);
  vm.runInContext(builder, context);
  const host = new Element("div");
  context.buildColorBank(host, goal ? {goal:state.document.rules.eggnoggColor} : state.document.rooms[0].appearance.primary, goal ? "map" : "primary", goal ? {
    keys:["goal"], labels:{goal:"Goal"}, historyLabel:"Change Eggnogg color",
    onChange(key,color) { state.document.rules.eggnoggColor = color; },
  } : undefined);
  const field = host.children[0];
  return {state, history, field, renders:() => renders};
}

test("goal RGB controls preview exact channels and group a gesture into one undo", () => {
  const f = fixture(true);
  const editor = f.field.children[4];
  const red = editor.children[0].children[1];
  red.fire("pointerdown"); red.value = "0.333"; red.fire("input");
  assert.equal(f.state.document.rules.eggnoggColor[0],0.333);
  assert.equal(f.state.document.rules.eggnoggColor[1],0.5);
  assert.equal(f.renders(),1);
  red.value = "0.777"; red.fire("input"); red.fire("pointerup");
  assert.equal(f.history.length,1);
  assert.equal(f.history[0].label,"Change Eggnogg color");
  assert.equal(f.history[0].snapshot.document.rules.eggnoggColor[0],0.12345);
  assert.deepEqual(f.state.document.rooms[0].appearance.primary.background,[0,0,0]);
  f.field.fire("focusout"); assert.equal(f.history.length,1);
});

test("invalid goal RGB or hex input preserves the document", () => {
  const f = fixture(true);
  const before = JSON.stringify(f.state.document);
  const hex = f.field.children[2], rgb = f.field.children[3];
  hex.value = "#bad"; hex.fire("input");
  assert.equal(hex.attrs["aria-invalid"],"true");
  rgb.value = "1, -0.1, 0.5"; rgb.fire("input");
  assert.equal(rgb.attrs["aria-invalid"],"true");
  assert.equal(JSON.stringify(f.state.document),before);
  rgb.value = "0.123456, 0.2, 0.3"; rgb.fire("input"); f.field.fire("focusout");
  assert.equal(f.state.document.rules.eggnoggColor[0],0.123456);
  assert.equal(f.history.length,1);
});

test("shared RGB controls retain room palette behavior", () => {
  const f = fixture(false);
  const hex = f.field.children[2];
  hex.value = "#ff0000"; hex.fire("input"); f.field.fire("focusout");
  assert.equal(JSON.stringify(f.state.document.rooms[0].appearance.primary.background),"[1,0,0]");
  assert.equal(f.state.document.rules.eggnoggColor[0],0.12345);
  assert.equal(f.history[0].label,"Change room palette");
});
