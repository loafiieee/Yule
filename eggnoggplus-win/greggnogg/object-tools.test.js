const test = require('node:test');
const assert = require('node:assert/strict');
const Core = require('./editor-core.js');
const Entities = require('./content-workspace/core.js');
const Objects = require('./object-tools.js');

test('room graph object painting targets and erases one placed room copy', () => {
  const graph = Core.convertMirroredToRoomGraph(Core.createDefaultDocument());
  const created = Objects.create(graph);
  const doc = created.document;
  const key = created.key;
  const room = doc.layout.nodes.find(node => node.id === 'right_1').room;

  assert.equal(Objects.paint(doc, key, room, [{row: 2, col: 3}], false, 'right_1'), true);
  assert.equal(Objects.paint(doc, key, room, [{row: 2, col: 3}], false, 'left_1'), true);
  assert.deepEqual(doc.entities.placements.map(p => [p.instance, p.room, p.side]), [
    ['right_1', room, undefined],
    ['left_1', room, undefined]
  ]);

  const expanded = Entities.expandPlacements(doc.entities, doc);
  assert.equal(expanded.length, 2);
  assert.notEqual(expanded[0].x, expanded[1].x);
  assert.equal(Objects.paint(doc, null, room, [{row: 2, col: 3}], true, 'right_1'), true);
  assert.deepEqual(doc.entities.placements.map(p => p.instance), ['left_1']);
});

test('room graph object painting requires a selected compatible instance', () => {
  const graph = Core.convertMirroredToRoomGraph(Core.createDefaultDocument());
  const created = Objects.create(graph);
  const room = created.document.layout.nodes.find(node => node.id === 'right_1').room;
  assert.throws(
    () => Objects.paint(created.document, created.key, room, [{row: 1, col: 1}], false, 'center'),
    /Choose a placed room copy/
  );
});
