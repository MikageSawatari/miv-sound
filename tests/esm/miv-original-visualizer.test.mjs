import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import { ASPECTS, ASPECT_FILES, MAX_EFFECTS, normalizeLayout, layoutsEqual, validateLayout, paletteModesForType }
  from '../../external/effetune/js/visualizer/visualizer-model.js';
import { TEXT_LINE_HEIGHT } from '../../external/effetune/js/visualizer/visualizer-text.js';
import { lyricsFrame, lyricsOpacity } from '../../external/effetune/js/visualizer/visualizer-lyrics.js';
import { VisualizerRenderer } from '../../external/effetune/js/visualizer/visualizer-renderer.js';
import { withRecordingCanvas } from './helpers/visualizer-recording-canvas.mjs';
import { paletteColor } from '../../external/effetune/js/visualizer/visualizer-effects.js';
import { GRADIENT_PRESETS } from '../../external/effetune/js/visualizer/visualizer-palette-presets.js';

const layouts = JSON.parse(await readFile(new URL('../../ui-shim/presets/miv-original.json', import.meta.url), 'utf8'));
const close = (a, b) => assert.ok(Math.abs(a - b) < 1e-10, `${a} != ${b}`);
const captionBottoms = { '16:9': .94, '21:9': .93, '4:3': .94, '1:1': .94, '9:16': .625 };
const captionFonts = { '16:9': 31.5, '21:9': 180 / 7, '4:3': 42, '1:1': 56, '9:16': 48 };

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
      if (type === 'notes') assert.deepEqual(item.palette, source.palette);
      assert.deepEqual(item.effects.filter(e => e.type === 'backplate'),
        harmony.items.find(i => i.type === 'notes').effects);
    }
    for (const type of ['title', 'artist', 'album']) {
      const item = layout.items.find(i => i.type === type), source = now.items.find(i => i.type === type);
      close(item.style.fontSize / item.rect.h, source.style.fontSize / source.rect.h);
    }

    const ratio = aspect.split(':').map(Number), height = 1280 * ratio[1] / ratio[0];
    const [notes, artwork, title, artist, album, spectrum, lyrics] = layout.items;
    assert.ok(paletteModesForType('spectrum').includes(spectrum.palette.mode));
    assert.equal(spectrum.palette.mode, 'gradient');
    assert.equal(spectrum.palette.direction, 'frequency');
    assert.deepEqual(spectrum.palette.stops, [{ pos: 0, color: '#40dfff' },
      { pos: 1, color: GRADIENT_PRESETS.find(p => p.id === 'lavender').colors[0] }]);
    const plate = lyrics.effects.find(e => e.type === 'backplate');
    assert.ok(plate.fillOpacity >= .75);
    assert.equal(plate.fill, '#000000');
    assert.equal(lyrics.palette.color, '#ffffff');
    assert.ok(lyrics.style.shadowOpacity >= .75);
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
    close(lyrics.rect.y + lyrics.rect.h, captionBottoms[aspect]);
    for (const item of [artwork, title, artist, album]) {
      assert.equal(overlaps(lyrics.rect, item.rect), false, `caption overlaps ${item.id}`);
    }
    assert.equal(lyrics.params.lyricsView, 'line');
    assert.equal(lyrics.params.lyricsNext, true);
    assert.equal(lyrics.params.lyricsMotion, 'step');
    assert.equal(lyrics.params.lyricsStepDuration, .4);
    assert.equal(lyrics.params.lyricsWordFill, true);
    assert.equal(lyrics.params.lyricsFadeIn, 1);
    assert.equal(lyrics.params.lyricsFadeOut, 3);
    assert.equal(lyrics.style.align, 'center');
    close(lyrics.style.fontSize, captionFonts[aspect]);
    for (const item of layout.items) assert.ok(item.effects.length <= MAX_EFFECTS);
    const fades = lyrics.effects.filter(effect => effect.type === 'edge-fade');
    assert.equal(fades.length, 1);
    const fade = fades[0];
    assert.equal(fade.enabled, true);
    assert.equal(fade.amount, 1);
    assert.equal(fade.shape, 'rectangle');
    assert.equal(lyrics.params.lyricsLineSpacing, 1.25);
    close(fade.top, fade.bottom);
    assert.equal(fade.left, 0);
    assert.equal(fade.right, 0);
    const rowHeight = lyrics.style.fontSize * TEXT_LINE_HEIGHT;
    const block = rowHeight * (1 + lyrics.params.lyricsLineSpacing);
    const boxHeight = lyrics.rect.h * height, zone = boxHeight * fade.top;
    // A row centered on the box edge has half a row visible. A linear ramp
    // needs a full row of fade width to keep that entire half at <= 50% alpha.
    close(zone, rowHeight);
    assert.ok(boxHeight - block - 2 * zone >= 2 - 1e-10, 'leave at least two pixels of slack');
    assert.ok(boxHeight - block - 2 * zone < 3 + 1e-10, 'keep caption compact after rounding up');
    assert.equal(lyricsOpacity(lyrics, null, false), 0);
    const frame = lyricsFrame(lyrics, { data: { synced: true, lines: [
      { time: 0, text: 'Current' }, { time: 5, text: 'Next' }
    ] }, time: 2, duration: 10 }, false);
    assert.equal(frame.list, false);
    assert.equal(frame.current, 0);
    assert.equal(frame.next, true);
  });
}

for (const aspect of ASPECTS) {
  test(`caption ${aspect} keeps synced rows clear and softly masks scrolling untimed rows`, t => {
    withRecordingCanvas(({ canvas, events }) => {
      const stage = canvas(), ratio = aspect.split(':').map(Number);
      stage.height = Math.round(stage.width * ratio[1] / ratio[0]);
      const renderer = new VisualizerRenderer(stage);
      const item = layouts[aspect].items.find(i => i.type === 'lyrics');
      const plate = item.effects.find(e => e.type === 'backplate');
      const layout = { ...layouts[aspect], items: [item] };
      const sources = { getModulators: () => ({}), getFrame: () => null };
      const synced = { data: { synced: true, lines: [
        { time: 0, text: 'Current' }, { time: 5, text: 'Next' }
      ] }, time: 2, duration: 80 };
      const draw = lyrics => {
        events.length = 0;
        renderer.draw(layout, sources, {}, lyrics?.time ?? 0, { lyrics, quality: 'high' });
        const state = renderer.layers.get(item.id);
        if (!state) return {};
        const composite = state.fadeLayer, mask = state.fadeMask;
        assert.ok(events.some(e => e.kind === 'image' && e.canvas === composite && e.image === mask && e.operation === 'destination-in'),
          'the real renderer applies its edge mask after composing text and backplate');
        assert.ok(events.some(e => e.kind === 'backplate' && e.canvas === composite && e.alpha === plate.fillOpacity));
        const alphaAt = mask.getContext('2d').alphaSampler();
        const width = mask.width, height = mask.height;
        const rows = events.filter(e => e.kind === 'text' && e.canvas === state.canvas);
        return { state, composite, width, height, rows, alpha: y => alphaAt(width / 2, y) };
      };
      const syncedFrame = draw(synced);
      assert.deepEqual(syncedFrame.rows.map(e => e.text), ['Current', 'Next']);
      const rowHeight = item.style.fontSize * TEXT_LINE_HEIGHT;
      const fade = item.effects.find(effect => effect.type === 'edge-fade');
      const zone = syncedFrame.height * fade.top;
      for (const row of syncedFrame.rows) {
        // Sample the full row rectangle, including its boundaries, after the
        // renderer has rounded the item canvas size to whole pixels.
        assert.ok(row.y - rowHeight / 2 >= zone);
        assert.ok(row.y + rowHeight / 2 <= syncedFrame.height - zone);
        for (let i = 0; i <= 32; i++) close(syncedFrame.alpha(row.y - rowHeight / 2 + i / 32 * rowHeight), 1);
      }
      // The mask adds no dimming; upstream deliberately draws the next row at 35%.
      assert.deepEqual(syncedFrame.rows.map(e => e.alpha), [1, .35]);
      close(syncedFrame.alpha(0), 0);
      close(syncedFrame.alpha(syncedFrame.height), 0);
      close(syncedFrame.alpha(zone / 2), .5);
      close(syncedFrame.alpha(syncedFrame.height - zone / 2), .5);

      const unsynced = { data: { synced: false, lines: Array.from({ length: 9 }, (_, i) => ({ text: `Row ${i}` })) },
        time: 40, duration: 80 };
      const lineAdvance = rowHeight * item.params.lyricsLineSpacing;
      const atCenter = (row, y) => ({ ...unsynced,
        time: (row + (syncedFrame.height / 2 - y) / lineAdvance) / 8 * unsynced.duration });
      const edgeRowMaxima = [];
      for (const edge of [0, syncedFrame.height]) {
        // Place a row's center exactly on the clipping boundary, then measure
        // every point of the half-row that remains visible inside the box.
        const edgeFrame = draw(atCenter(4, edge));
        const edgeRow = edgeFrame.rows.find(e => e.text === 'Row 4');
        assert.ok(edgeRow);
        close(edgeRow.y, edge);
        const visibleTop = Math.max(0, edgeRow.y - rowHeight / 2);
        const visibleBottom = Math.min(edgeFrame.height, edgeRow.y + rowHeight / 2);
        const alphas = Array.from({ length: 65 }, (_, i) => edgeRow.alpha *
          edgeFrame.alpha(visibleTop + i / 64 * (visibleBottom - visibleTop)));
        const maximum = Math.max(...alphas);
        assert.ok(maximum <= .5 + 1e-10, `edge-centered row maximum alpha is ${maximum}`);
        edgeRowMaxima.push(maximum);

        const center = edge === 0 ? zone / 2 : syncedFrame.height - zone / 2;
        const lyrics = atCenter(4, center);
        const frame = lyricsFrame(item, lyrics, false);
        assert.equal(frame.list, true);
        assert.equal(frame.horizontal, false);
        const rendered = draw(lyrics);
        const entering = rendered.rows.find(e => e.text === 'Row 4');
        assert.ok(entering);
        close(entering.y, center);
        close(entering.alpha * rendered.alpha(entering.y), .5);
        const central = rendered.rows.find(e => e.y > zone && e.y < rendered.height - zone);
        assert.ok(central);
        close(central.alpha * rendered.alpha(central.y), 1);
        // Entering/leaving glyphs meet a continuous ramp ending at zero.
        const inward = edge === 0 ? zone / 10 : rendered.height - zone / 10;
        close(rendered.alpha(edge), 0);
        close(rendered.alpha(inward), .1);
        const later = draw({ ...lyrics, time: lyrics.time + .01 });
        const moved = later.rows.find(e => e.text === 'Row 4');
        close(moved.y - entering.y, -.01 / unsynced.duration * 8 * lineAdvance);
      }

      // The same composite mask softens the dark backplate, including during
      // temporal playback fades. No detached dark strip remains at the edge.
      const fading = draw({ ...synced, time: .5 });
      assert.ok(events.some(e => e.kind === 'image' && e.canvas === stage && e.image === fading.composite && e.alpha === .5));
      for (const absent of [null, { data: { lines: [] } }, { ...synced, isStopped: true }]) {
        draw(absent);
        assert.equal(renderer.layers.has(item.id), false);
        assert.equal(renderer.effects.states.has(item.id), false);
        assert.equal(events.some(e => e.kind === 'backplate' || e.kind === 'text'), false);
      }
      t.diagnostic(JSON.stringify({ aspect, fontSize: item.style.fontSize, rowHeight,
        lineAdvance, block: rowHeight + lineAdvance, boxHeight: syncedFrame.height, zone,
        fraction: fade.top, syncedRowAlpha: [1, 1], edgeCenteredRowMax: edgeRowMaxima,
        halfZoneRowCenterAlpha: [.5, .5], edgeAlpha: 0,
        backplateAlpha: { center: plate.fillOpacity, halfZone: plate.fillOpacity / 2, edge: 0 } }));
      // Rough worst-case contrast in the opaque middle band over a full-height
      // bar. Use the renderer's actual palette sampling and normal sRGB blending.
      const hex = value => [1, 3, 5].map(i => parseInt(value.slice(i, i + 2), 16));
      const over = (fg, bg, alpha) => fg.map((v, i) => v * alpha + bg[i] * (1 - alpha));
      const luminance = rgb => rgb.map(v => {
        const c = v / 255;
        return c <= .04045 ? c / 12.92 : ((c + .055) / 1.055) ** 2.4;
      }).reduce((sum, v, i) => sum + v * [.2126, .7152, .0722][i], 0);
      const spectrum = layouts[aspect].items.find(i => i.type === 'spectrum');
      const spectrumPlate = spectrum.effects.find(e => e.type === 'backplate');
      const spectrumOpacity = spectrum.effects.find(e => e.type === 'opacity').amount;
      const base = over(hex(spectrumPlate.fill), hex(layout.background.color), spectrumPlate.fillOpacity);
      const bandLuminance = Math.max(...Array.from({ length: 257 }, (_, i) => {
        const rgb = paletteColor(spectrum.palette, i / 256).match(/[\d.]+/g).map(Number);
        return luminance(over(hex(plate.fill), over(rgb, base, spectrumOpacity), plate.fillOpacity));
      }));
      const textLuminance = luminance(hex(item.palette.color));
      const contrast = (textLuminance + .05) / (bandLuminance + .05);
      assert.ok(bandLuminance < .05 && contrast > 10);
      t.diagnostic(JSON.stringify({ aspect, textLuminance, brightestBandLuminance: bandLuminance, contrast }));
      renderer.dispose();
    });
  });
}
