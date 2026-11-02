import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import { ASPECTS, ASPECT_FILES, normalizeLayout, layoutsEqual, validateLayout }
  from '../../external/effetune/js/visualizer/visualizer-model.js';
import { TEXT_LINE_HEIGHT } from '../../external/effetune/js/visualizer/visualizer-text.js';
import { lyricsFrame, lyricsOpacity } from '../../external/effetune/js/visualizer/visualizer-lyrics.js';
import { VisualizerRenderer } from '../../external/effetune/js/visualizer/visualizer-renderer.js';

const layouts = JSON.parse(await readFile(new URL('../../ui-shim/presets/miv-original.json', import.meta.url), 'utf8'));
const close = (a, b) => assert.ok(Math.abs(a - b) < 1e-10, `${a} != ${b}`);

test('mIV Original covers exactly the upstream aspect strings', () => {
  assert.deepEqual(Object.keys(layouts).sort(), [...ASPECTS].sort());
});

for (const aspect of ASPECTS) {
  test(`mIV Original ${aspect} is valid, normalized and preserves source styling`, async () => {
    const layout = layouts[aspect];
    assert.equal(layout.aspect, aspect);
    assert.equal(validateLayout(layout), true);
    assert.deepEqual(normalizeLayout(layout), layout);
    assert.equal(layoutsEqual(normalizeLayout(layout), layout), true);
    assert.deepEqual(layout.items.map(i => i.type), ['notes', 'artwork', 'title', 'artist', 'album', 'spectrum', 'lyrics']);
    assert.deepEqual(layout.items.map((i, n) => i.id), layout.items.map((i, n) => `${i.type}-${n + 1}`));

    const upstream = JSON.parse(await readFile(new URL(
      `../../external/effetune/presets/visualizer/${ASPECT_FILES[aspect]}`, import.meta.url), 'utf8'));
    const harmony = upstream['Harmony Lab'], now = upstream['Now Playing'];
    assert.deepEqual(layout.background, harmony.background);
    for (const type of ['notes', 'spectrum']) {
      const item = layout.items.find(i => i.type === type);
      const source = (type === 'notes' ? harmony : now).items.find(i => i.type === type);
      assert.deepEqual(item.params, source.params);
      assert.deepEqual(item.palette, source.palette);
      assert.deepEqual(item.effects.filter(e => e.type === 'backplate'),
        harmony.items.find(i => i.type === 'notes').effects);
    }
    for (const type of ['title', 'artist', 'album']) {
      const item = layout.items.find(i => i.type === type), source = now.items.find(i => i.type === type);
      close(item.style.fontSize / item.rect.h, source.style.fontSize / source.rect.h);
    }

    const ratio = aspect.split(':').map(Number), height = 1280 * ratio[1] / ratio[0];
    const [notes, artwork, title, artist, album, spectrum, lyrics] = layout.items;
    close(artwork.rect.w * 1280, artwork.rect.h * height);
    for (const item of layout.items) {
      const { x, y, w, h } = item.rect;
      assert.ok(x >= 0 && y >= 0 && x + w <= 1 && y + h <= 1, item.id);
    }
    const overlaps = (a, b) => a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
    for (const item of [artwork, title, artist, album, spectrum]) {
      assert.equal(overlaps(notes.rect, item.rect), false, `notes overlaps ${item.id}`);
    }
    for (const [a, b] of [[artwork, title], [title, artist], [artist, album], [album, spectrum]]) {
      assert.equal(overlaps(a.rect, b.rect), false, `${a.id} overlaps ${b.id}`);
    }
    assert.ok(lyrics.rect.x >= spectrum.rect.x && lyrics.rect.y >= spectrum.rect.y);
    assert.ok(lyrics.rect.x + lyrics.rect.w <= spectrum.rect.x + spectrum.rect.w);
    assert.ok(lyrics.rect.y + lyrics.rect.h <= spectrum.rect.y + spectrum.rect.h);
    assert.equal(lyrics.params.lyricsView, 'line');
    assert.equal(lyrics.params.lyricsNext, true);
    assert.equal(lyrics.params.lyricsWordFill, true);
    assert.equal(lyrics.params.lyricsFadeIn, 1);
    assert.equal(lyrics.params.lyricsFadeOut, 3);
    assert.equal(lyrics.style.align, 'center');
    // Two rows fit with space for the shadow: row + one line advance.
    assert.ok(lyrics.style.fontSize * TEXT_LINE_HEIGHT * (1 + lyrics.params.lyricsLineSpacing) < lyrics.rect.h * height);
    assert.equal(lyricsOpacity(lyrics, null, false), 0);
    const frame = lyricsFrame(lyrics, { data: { synced: true, lines: [
      { time: 0, text: 'Current' }, { time: 5, text: 'Next' }
    ] }, time: 2, duration: 10 }, false);
    assert.equal(frame.list, false);
    assert.equal(frame.current, 0);
    assert.equal(frame.next, true);
  });
}

// Exercise the real renderer in Node with a recording Canvas 2D context. This
// checks composition and cache retirement without starting a browser/WebView.
test('lyrics backplate fades with the caption and vanishes when lyrics disappear', () => {
  const events = [];
  const canvas = () => {
    const result = { width: 1280, height: 720 };
    const stack = [];
    const ctx = {
      globalAlpha: 1,
      save() { stack.push(this.globalAlpha); },
      restore() { this.globalAlpha = stack.pop(); },
      clearRect() {}, beginPath() {}, rect() {}, clip() {}, roundRect() {},
      translate() {}, scale() {}, stroke() {}, strokeText() {},
      measureText(text) { return { width: text.length * 10 }; },
      getTransform() { return new DOMMatrix(); },
      fillRect() {},
      fill() { events.push({ kind: 'backplate', canvas: result, alpha: this.globalAlpha }); },
      fillText(text) { events.push({ kind: 'text', text, canvas: result }); },
      drawImage(image) { events.push({ kind: 'image', image, canvas: result, alpha: this.globalAlpha }); }
    };
    result.getContext = () => ctx;
    return result;
  };
  const previousDocument = globalThis.document;
  const previousMatrix = globalThis.DOMMatrix;
  // Hit-test transforms are outside this composition test.
  globalThis.DOMMatrix = class {
    translate() { return this; }
    multiply() { return this; }
    scale() { return this; }
    inverse() { return this; }
  };
  globalThis.document = { createElement: canvas };
  try {
    const stage = canvas(), renderer = new VisualizerRenderer(stage);
    const item = layouts['16:9'].items.find(i => i.type === 'lyrics');
    const layout = { ...layouts['16:9'], items: [item] };
    const sources = { getModulators: () => ({}), getFrame: () => null };
    const lyrics = { data: { synced: true, lines: [
      { time: 0, text: 'Current' }, { time: 5, text: 'Next' }
    ] }, time: .5, duration: 20 };
    renderer.draw(layout, sources, {}, 0, { lyrics, quality: 'high' });
    const composite = renderer.layers.get(item.id).lyricsLayer;
    assert.ok(events.some(e => e.kind === 'backplate' && e.canvas === composite));
    assert.deepEqual(events.filter(e => e.kind === 'text').map(e => e.text), ['Current', 'Next']);
    assert.ok(events.some(e => e.kind === 'image' && e.canvas === stage && e.image === composite && e.alpha === .5));
    for (const absent of [null, { data: { lines: [] } }, { ...lyrics, isStopped: true }]) {
      events.length = 0;
      renderer.draw(layout, sources, {}, 1, { lyrics: absent, quality: 'high' });
      assert.equal(renderer.layers.has(item.id), false);
      assert.equal(renderer.effects.states.has(item.id), false);
      assert.equal(events.some(e => e.kind === 'backplate' || e.kind === 'text'), false);
    }
    renderer.dispose();
  } finally {
    globalThis.document = previousDocument;
    globalThis.DOMMatrix = previousMatrix;
  }
});
