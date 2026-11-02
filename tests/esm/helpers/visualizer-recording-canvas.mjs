// Record the real renderer's Canvas 2D commands. Evaluate its mask paint with
// ordinary gradient interpolation/compositing; glyph rasterization and hit-test
// transforms are outside these tests. No browser or WebView is created.
export function withRecordingCanvas(run) {
  const events = [];
  const canvas = () => {
    const result = { width: 1280, height: 720 }, stack = [];
    let paints = [];
    const alphaOf = color => color.startsWith('rgba(') ? Number(color.slice(5, -1).split(',')[3]) : 1;
    const ctx = {
      globalAlpha: 1, globalCompositeOperation: 'source-over', fillStyle: '#ffffff',
      font: '', textAlign: 'left',
      save() {
        stack.push(Object.fromEntries(['globalAlpha', 'globalCompositeOperation', 'fillStyle', 'font', 'textAlign']
          .map(key => [key, this[key]])));
      },
      restore() { Object.assign(this, stack.pop()); },
      clearRect() { paints = []; },
      beginPath() {}, rect() {}, clip() {}, roundRect() {},
      translate() {}, scale() {}, stroke() {}, strokeText() {},
      measureText(text) { return { width: text.length * 10 }; },
      getTransform() { return new DOMMatrix(); },
      createLinearGradient(x0, y0, x1, y1) {
        const stops = [];
        return {
          addColorStop(position, color) { stops.push([position, alphaOf(color)]); stops.sort((a, b) => a[0] - b[0]); },
          alphaAt(x, y) {
            const dx = x1 - x0, dy = y1 - y0;
            const t = ((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy);
            if (t <= stops[0][0]) return stops[0][1];
            for (let i = 1; i < stops.length; i++) {
              if (t <= stops[i][0]) {
                const [a, av] = stops[i - 1], [b, bv] = stops[i];
                return av + (bv - av) * (t - a) / (b - a);
              }
            }
            return stops.at(-1)[1];
          }
        };
      },
      fillRect(x, y, w, h) {
        const style = this.fillStyle;
        paints.push({ x, y, w, h, alpha: this.globalAlpha, operation: this.globalCompositeOperation,
          sample: typeof style === 'string' ? () => alphaOf(style) : (sx, sy) => style.alphaAt(sx, sy) });
      },
      alphaSampler() {
        const snapshot = [...paints];
        return (x, y) => snapshot.reduce((alpha, paint) => {
          if (x < paint.x || x > paint.x + paint.w || y < paint.y || y > paint.y + paint.h) return alpha;
          const source = paint.alpha * paint.sample(x, y);
          return paint.operation === 'destination-in' ? alpha * source : source + alpha * (1 - source);
        }, 0);
      },
      fill() { events.push({ kind: 'backplate', canvas: result, alpha: this.globalAlpha }); },
      fillText(text, x, y, maxWidth) {
        events.push({ kind: 'text', text, x, y, maxWidth, canvas: result, alpha: this.globalAlpha });
      },
      drawImage(image, ...args) {
        events.push({ kind: 'image', image, args, canvas: result, alpha: this.globalAlpha,
          operation: this.globalCompositeOperation });
      }
    };
    result.getContext = () => ctx;
    return result;
  };
  const previousDocument = globalThis.document, previousMatrix = globalThis.DOMMatrix;
  globalThis.DOMMatrix = class {
    translate() { return this; }
    multiply() { return this; }
    scale() { return this; }
    inverse() { return this; }
  };
  globalThis.document = { createElement: canvas };
  try { return run({ canvas, events }); }
  finally {
    globalThis.document = previousDocument;
    globalThis.DOMMatrix = previousMatrix;
  }
}
