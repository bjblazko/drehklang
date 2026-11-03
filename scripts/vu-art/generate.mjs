// Renders the VU meter art for the full-screen visualization (ADR 0034)
// and writes it as LVGL images: lib/visualizer-art/VuMeterArt.{h,c}.
//
// The drawing functions are the approved preview's
// (docs/superpowers/specs/2026-11-03-vu-meter-preview.html), ported
// verbatim except for two things: a seeded PRNG replaces Math.random()
// so every run writes the same bytes, and the canvas is 360 px at 1x, the
// device's own resolution. Opaque images are dithered (4x4 Bayer) on the
// way to RGB565, or the lamp light's gradients band on the panel.
//
//   cd scripts/vu-art && npm ci && node generate.mjs [--preview <dir>]
//
// --preview also writes PNGs of every image and two composites.

import { createCanvas, GlobalFonts } from '@napi-rs/canvas';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT_DIR = join(HERE, '..', '..', 'lib', 'visualizer-art');
GlobalFonts.registerFromPath(join(HERE, 'fonts', 'ArchivoNarrow[wght].ttf'), 'Archivo Narrow');
GlobalFonts.registerFromPath(join(HERE, 'fonts', 'ShareTechMono-Regular.ttf'), 'Share Tech Mono');

// mulberry32, seed 1
let seed = 1;
function random() {
  seed |= 0; seed = (seed + 0x6d2b79f5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}

/* ---------- From the preview, verbatim but for random() ---------- */
const S = 360, C = 180;
const VU_MAX = Math.pow(10, 3 / 20);
const circle = (x, r) => { x.beginPath(); x.arc(C, C, r, 0, Math.PI * 2); };

const PIV = { x: 180, y: 318 }, RS = 156, SPAN = 48 * Math.PI / 180;
const posOfVu = vu => Math.pow(10, vu / 20) / VU_MAX;
const ang = p => -SPAN + 2 * SPAN * p;
const polar = (a, r) => [PIV.x + r * Math.sin(a), PIV.y - r * Math.cos(a)];
const FACE = 174;

function analogBg(x) {
  circle(x, 180); x.fillStyle = '#0a0a0b'; x.fill();
  x.save(); circle(x, FACE); x.clip();
  let g = x.createRadialGradient(180, 330, 10, 180, 240, 270);
  g.addColorStop(0, '#fff7dc'); g.addColorStop(.33, '#f9df9c');
  g.addColorStop(.68, '#dca952'); g.addColorStop(1, '#6e4917');
  x.fillStyle = g; x.fillRect(0, 0, S, S);
  x.globalCompositeOperation = 'screen';
  for (const lx of [112, 248]) {
    const h = x.createRadialGradient(lx, 312, 0, lx, 312, 125);
    h.addColorStop(0, 'rgba(255,238,185,.75)'); h.addColorStop(.5, 'rgba(255,214,130,.25)'); h.addColorStop(1, 'rgba(255,200,110,0)');
    x.fillStyle = h; x.fillRect(0, 0, S, S);
  }
  x.globalCompositeOperation = 'source-over';
  for (let i = 0; i < 1400; i++) {
    x.fillStyle = `rgba(90,60,20,${random() * .05})`;
    x.fillRect(random() * S, random() * S, 1, 1);
  }
  drawScale(x);
  x.beginPath(); x.moveTo(0, 304); x.quadraticCurveTo(180, 290, 360, 304); x.lineTo(360, 360); x.lineTo(0, 360); x.closePath();
  g = x.createLinearGradient(0, 292, 0, 360); g.addColorStop(0, '#2d2a26'); g.addColorStop(.25, '#151412'); g.addColorStop(1, '#060606');
  x.fillStyle = g; x.fill();
  x.strokeStyle = 'rgba(255,240,210,.18)'; x.lineWidth = 1;
  x.beginPath(); x.moveTo(0, 304); x.quadraticCurveTo(180, 290, 360, 304); x.stroke();
  g = x.createLinearGradient(0, 280, 0, 300); g.addColorStop(0, 'rgba(60,35,0,0)'); g.addColorStop(1, 'rgba(60,35,0,.22)');
  x.fillStyle = g; x.fillRect(0, 278, S, 20);
  g = x.createRadialGradient(180, 180, FACE * .78, 180, 180, FACE);
  g.addColorStop(0, 'rgba(0,0,0,0)'); g.addColorStop(1, 'rgba(25,12,0,.6)');
  x.fillStyle = g; x.fillRect(0, 0, S, S);
  x.restore();
  circle(x, FACE + 1.5); x.lineWidth = 3;
  g = x.createLinearGradient(0, 0, 0, S); g.addColorStop(0, '#5a5a5e'); g.addColorStop(.5, '#1d1d20'); g.addColorStop(1, '#46464a');
  x.strokeStyle = g; x.stroke();
}

function drawScale(x) {
  const ink = '#1d1712', red = '#c22a1c';
  const arc = (r, a0, a1, w, col) => {
    x.beginPath(); x.arc(PIV.x, PIV.y, r, -Math.PI / 2 + a0, -Math.PI / 2 + a1);
    x.lineWidth = w; x.strokeStyle = col; x.stroke();
  };
  const a0 = ang(posOfVu(-20)), aZero = ang(posOfVu(0)), aMax = ang(1);
  arc(RS, a0, aZero, 1.6, ink);
  arc(RS, aZero, aMax, 1.6, red);
  arc(RS + 5, aZero, aMax, 8, red);
  const major = [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3];
  const minor = [-15, -6, -4, -0.5, 0.5, 1.5, 2.5];
  x.lineCap = 'butt';
  const tick = (vu, len, w) => {
    const a = ang(posOfVu(vu)); const [x1, y1] = polar(a, RS); const [x2, y2] = polar(a, RS + len);
    x.beginPath(); x.moveTo(x1, y1); x.lineTo(x2, y2); x.lineWidth = w; x.strokeStyle = vu > 0 ? red : ink; x.stroke();
  };
  minor.forEach(v => tick(v, 7, 1)); major.forEach(v => tick(v, 14, 1.8));
  x.textAlign = 'center'; x.textBaseline = 'middle';
  x.font = '600 17px "Archivo Narrow", sans-serif';
  major.forEach(v => {
    const [tx, ty] = polar(ang(posOfVu(v)), RS + 27);
    x.fillStyle = v > 0 ? red : ink; x.fillText(String(Math.abs(v)), tx, ty);
  });
  x.font = '700 22px "Archivo Narrow", sans-serif';
  let [tx, ty] = polar(a0 - .09, RS + 22); x.fillStyle = ink; x.fillText('−', tx, ty);
  [tx, ty] = polar(aMax + .08, RS + 22); x.fillStyle = red; x.fillText('+', tx, ty);
  x.font = '500 10px "Archivo Narrow", sans-serif'; x.fillStyle = ink;
  for (const pct of [0, 20, 40, 60, 80, 100]) {
    const a = ang(pct / 100 / VU_MAX);
    const [x1, y1] = polar(a, RS - 2); const [x2, y2] = polar(a, RS - 9);
    x.beginPath(); x.moveTo(x1, y1); x.lineTo(x2, y2); x.lineWidth = 1; x.strokeStyle = ink; x.stroke();
    const [lx, ly] = polar(a, RS - 19); x.fillText(String(pct), lx, ly);
  }
  x.font = '700 38px "Archivo Narrow", sans-serif'; x.fillStyle = ink;
  x.fillText('VU', 180, 254);
  x.font = '600 11px "Archivo Narrow", sans-serif'; x.fillStyle = 'rgba(29,23,18,.75)';
  if ('letterSpacing' in x) x.letterSpacing = '2px';
  x.fillText('L + R', 180, 282);
  x.fillText('PEAK', 268, 282);
  if ('letterSpacing' in x) x.letterSpacing = '0px';
  const lg = x.createRadialGradient(266, 262, 1, 268, 264, 7);
  lg.addColorStop(0, '#7a2a20'); lg.addColorStop(1, '#2a0d09');
  x.beginPath(); x.arc(268, 264, 6, 0, Math.PI * 2); x.fillStyle = lg; x.fill();
  x.lineWidth = 1.5; x.strokeStyle = '#2a2420'; x.stroke();
}

function analogGlass(x) {
  x.save(); circle(x, FACE); x.clip();
  const g = x.createLinearGradient(40, 20, 210, 220);
  g.addColorStop(0, 'rgba(255,255,255,.30)'); g.addColorStop(.5, 'rgba(255,255,255,.06)'); g.addColorStop(1, 'rgba(255,255,255,0)');
  x.beginPath(); x.ellipse(120, 70, 190, 110, -0.45, 0, Math.PI * 2); x.fillStyle = g; x.fill();
  x.restore();
}

function drawNeedle(x, p) {
  const a = ang(p);
  const [tx, ty] = polar(a, RS + 12);
  const nx = Math.cos(a), ny = Math.sin(a);
  x.save(); circle(x, FACE); x.clip();
  x.shadowColor = 'rgba(70,40,0,.35)'; x.shadowBlur = 3; x.shadowOffsetX = 3; x.shadowOffsetY = 5;
  x.beginPath();
  x.moveTo(PIV.x - nx * 1.8, PIV.y - ny * 1.8);
  x.lineTo(tx - nx * .5, ty - ny * .5); x.lineTo(tx + nx * .5, ty + ny * .5);
  x.lineTo(PIV.x + nx * 1.8, PIV.y + ny * 1.8); x.closePath();
  x.fillStyle = '#141210'; x.fill();
  x.restore();
}

function drawPeakLamp(x, on) {
  if (on <= 0) return;
  x.save(); x.globalAlpha = Math.min(1, on);
  const g = x.createRadialGradient(268, 264, 0, 268, 264, 22);
  g.addColorStop(0, 'rgba(255,90,60,.9)'); g.addColorStop(.3, 'rgba(255,60,30,.35)'); g.addColorStop(1, 'rgba(255,40,20,0)');
  x.fillStyle = g; x.fillRect(240, 236, 56, 56);
  x.beginPath(); x.arc(268, 264, 5, 0, Math.PI * 2); x.fillStyle = '#ff6a48'; x.fill();
  x.restore();
}

const N = 32, X0 = 52, X1 = 316, GAP = 2.4, SW = (X1 - X0 - GAP * (N - 1)) / N;
const BARS = [{ y: 126, h: 34, name: 'L' }, { y: 212, h: 34, name: 'R' }];
const SCALE = [[-50, 0], [-30, .25], [-20, .42], [-10, .64], [-6, .75], [-3, .85], [0, 1]];
const posOfDb = db => {
  if (db <= SCALE[0][0]) return 0;
  for (let i = 1; i < SCALE.length; i++) {
    const [d1, p1] = SCALE[i], [d0, p0] = SCALE[i - 1];
    if (db <= d1) return p0 + (p1 - p0) * (db - d0) / (d1 - d0);
  }
  return 1;
};
const RED_FROM = Math.floor(posOfDb(-3) * N);
const segX = i => X0 + i * (SW + GAP);
const BLUE = '#8ae9ff', BLUE_GLOW = 'rgba(40,190,255,.9)', REDC = '#ff7556', RED_GLOW = 'rgba(255,60,30,.9)';
const DIM = '#59c6dd';

function digitalBg(x) {
  circle(x, 180); x.fillStyle = '#000'; x.fill();
  let g = x.createRadialGradient(180, 170, 20, 180, 180, 178);
  g.addColorStop(0, '#0b171c'); g.addColorStop(.75, '#060c0f'); g.addColorStop(1, '#010203');
  circle(x, 176); x.fillStyle = g; x.fill();
  for (const b of BARS) for (let i = 0; i < N; i++) {
    x.fillStyle = i >= RED_FROM ? '#24120f' : '#0d232a';
    x.fillRect(segX(i), b.y, SW, b.h);
  }
  x.save();
  x.shadowColor = 'rgba(40,190,255,.7)'; x.shadowBlur = 6;
  x.fillStyle = DIM; x.textAlign = 'center'; x.textBaseline = 'middle';
  x.font = '20px "Share Tech Mono", monospace';
  BARS.forEach(b => x.fillText(b.name, 36, b.y + b.h / 2 + 1));
  x.font = '12px "Share Tech Mono", monospace';
  const labels = [[-50, '50'], [-30, '30'], [-20, '20'], [-10, '10'], [-6, '6'], [-3, '3'], [0, '0']];
  labels.forEach(([db, t]) => {
    const i = Math.min(N - 1, Math.round(posOfDb(db) * N - 0.5));
    const cx = segX(i) + SW / 2;
    x.fillStyle = db >= -3 ? REDC : DIM;
    x.shadowColor = db >= -3 ? 'rgba(255,60,30,.7)' : 'rgba(40,190,255,.7)';
    x.fillText(t, cx, 186);
    x.fillRect(cx - .5, 166, 1, 4); x.fillRect(cx - .5, 202, 1, 4);
  });
  x.fillStyle = DIM; x.shadowColor = 'rgba(40,190,255,.7)';
  x.fillText('dB', 36, 186);
  if ('letterSpacing' in x) x.letterSpacing = '3px';
  x.font = '13px "Share Tech Mono", monospace';
  x.fillText('PEAK LEVEL', 180, 92);
  x.restore();
  x.save();
  x.strokeStyle = DIM; x.lineWidth = 1; x.shadowColor = 'rgba(40,190,255,.7)'; x.shadowBlur = 5;
  x.strokeRect(134, 268, 92, 20);
  x.fillStyle = DIM; x.font = '11px "Share Tech Mono", monospace'; x.textAlign = 'center'; x.textBaseline = 'middle';
  if ('letterSpacing' in x) x.letterSpacing = '2px';
  x.fillText('PEAK HOLD', 181, 279);
  x.restore();
}

function digitalMesh(x) {
  x.save(); circle(x, 176); x.clip();
  x.fillStyle = 'rgba(0,0,0,.38)';
  for (let i = 0; i < S; i += 3) { x.fillRect(i, 0, 1, S); x.fillRect(0, i, S, 1); }
  const g = x.createLinearGradient(30, 0, 230, 200);
  g.addColorStop(0, 'rgba(180,220,255,.10)'); g.addColorStop(.55, 'rgba(180,220,255,.02)'); g.addColorStop(1, 'rgba(180,220,255,0)');
  x.beginPath(); x.ellipse(130, 70, 200, 95, -0.35, 0, Math.PI * 2); x.fillStyle = g; x.fill();
  x.restore();
}

function drawBar(x, b, levelPos, holdPos) {
  const lit = Math.round(levelPos * N);
  const hold = Math.min(N - 1, Math.round(holdPos * N) - 1);
  x.save(); x.shadowBlur = 9;
  for (let i = 0; i < N; i++) {
    if (i >= lit && i !== hold) continue;
    const red = i >= RED_FROM;
    x.shadowColor = red ? RED_GLOW : BLUE_GLOW; x.fillStyle = red ? REDC : BLUE;
    x.fillRect(segX(i), b.y, SW, b.h);
  }
  x.restore();
}

/* ---------- Images ---------- */
const GLOW_MARGIN = 10;
const BAND_H = BARS[0].h + 2 * GLOW_MARGIN;

function render(draw, w = S, h = S) {
  const cv = createCanvas(w, h);
  const ctx = cv.getContext('2d');
  draw(ctx);
  return cv;
}

const face = render(analogBg);
const glassFull = render(analogGlass);
const lampFull = render(x => drawPeakLamp(x, 1));
const barsDark = render(x => { digitalBg(x); digitalMesh(x); });
const barsLitFull = render(x => {
  digitalBg(x);
  BARS.forEach(b => drawBar(x, b, 1, 0));
  digitalMesh(x);
});

function crop(cv, x, y, w, h) {
  return render(c => c.drawImage(cv, x, y, w, h, 0, 0, w, h), w, h);
}

function alphaBounds(cv) {
  const { data, width, height } = cv.getContext('2d').getImageData(0, 0, cv.width, cv.height);
  let x1 = width, y1 = height, x2 = -1, y2 = -1;
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    if (data[(y * width + x) * 4 + 3] > 0) {
      x1 = Math.min(x1, x); x2 = Math.max(x2, x); y1 = Math.min(y1, y); y2 = Math.max(y2, y);
    }
  }
  return { x: x1, y: y1, w: x2 - x1 + 1, h: y2 - y1 + 1 };
}

const glassBox = alphaBounds(glassFull);
const glass = crop(glassFull, glassBox.x, glassBox.y, glassBox.w, glassBox.h);
const LAMP = { x: 240, y: 236, w: 56, h: 56 };
const lamp = crop(lampFull, LAMP.x, LAMP.y, LAMP.w, LAMP.h);
const bandY = BARS.map(b => b.y - GLOW_MARGIN);
const litBands = bandY.map(y => crop(barsLitFull, 0, y, S, BAND_H));

/* ---------- RGB565 ---------- */
const BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];

function pack565(r, g, b) {
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

// One channel, dithered down to `bits`.
function dither(v, bits, x, y) {
  const step = 256 >> bits;
  const t = (BAYER[(y & 3) * 4 + (x & 3)] + 0.5) / 16 - 0.5;
  return Math.max(0, Math.min(255, Math.round(v + t * step)));
}

function toBytes(cv, withAlpha) {
  const { data, width, height } = cv.getContext('2d').getImageData(0, 0, cv.width, cv.height);
  const out = Buffer.alloc(width * height * (withAlpha ? 3 : 2));
  let o = 0;
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    const i = (y * width + x) * 4;
    let r = data[i], g = data[i + 1], b = data[i + 2];
    if (!withAlpha) { r = dither(r, 5, x, y); g = dither(g, 6, x, y); b = dither(b, 5, x, y); }
    const c = pack565(r, g, b);
    out[o++] = c & 0xff; out[o++] = c >> 8;
    if (withAlpha) out[o++] = data[i + 3];
  }
  return out;
}

const IMAGES = [
  { name: 'drehklang_vu_analog_face', cv: face, alpha: false },
  { name: 'drehklang_vu_analog_glass', cv: glass, alpha: true },
  { name: 'drehklang_vu_peak_lamp', cv: lamp, alpha: true },
  { name: 'drehklang_vu_bars_dark', cv: barsDark, alpha: false },
  { name: 'drehklang_vu_bars_lit_l', cv: litBands[0], alpha: false },
  { name: 'drehklang_vu_bars_lit_r', cv: litBands[1], alpha: false },
];

function cArray(bytes) {
  const lines = [];
  for (let i = 0; i < bytes.length; i += 32) {
    lines.push('  ' + Array.from(bytes.subarray(i, i + 32)).join(',') + ',');
  }
  return lines.join('\n');
}

function writeC() {
  mkdirSync(OUT_DIR, { recursive: true });
  const banner = '// Generated by scripts/vu-art/generate.mjs (ADR 0034). Do not edit.\n';
  const h = [banner, '#pragma once', '', '#include <lvgl.h>', '', '#ifdef __cplusplus', 'extern "C" {', '#endif', ''];
  h.push(`#define DREHKLANG_VU_GLASS_X ${glassBox.x}`, `#define DREHKLANG_VU_GLASS_Y ${glassBox.y}`);
  h.push(`#define DREHKLANG_VU_LAMP_X ${LAMP.x}`, `#define DREHKLANG_VU_LAMP_Y ${LAMP.y}`);
  h.push(`// Bars: segment i spans x = X0 + i * (W + GAP) .. + W, in hundredths of a pixel.`);
  h.push(`#define DREHKLANG_VU_BAR_SEGMENTS ${N}`);
  h.push(`#define DREHKLANG_VU_BAR_X0 ${X0}`);
  h.push(`#define DREHKLANG_VU_BAR_W_X100 ${Math.round(SW * 100)}`);
  h.push(`#define DREHKLANG_VU_BAR_GAP_X100 ${Math.round(GAP * 100)}`);
  h.push(`#define DREHKLANG_VU_BAR_L_Y ${BARS[0].y}`, `#define DREHKLANG_VU_BAR_R_Y ${BARS[1].y}`);
  h.push(`#define DREHKLANG_VU_BAR_H ${BARS[0].h}`, `#define DREHKLANG_VU_GLOW_MARGIN ${GLOW_MARGIN}`);
  h.push(`// The lit bands start GLOW_MARGIN above their bar.`, '');
  for (const img of IMAGES) h.push(`extern const lv_img_dsc_t ${img.name};`);
  h.push('', '#ifdef __cplusplus', '}', '#endif', '');
  writeFileSync(join(OUT_DIR, 'VuMeterArt.h'), h.join('\n'));

  const c = [banner, '#include "VuMeterArt.h"', ''];
  for (const img of IMAGES) {
    const bytes = toBytes(img.cv, img.alpha);
    c.push(`static const uint8_t ${img.name}_data[${bytes.length}] = {`, cArray(bytes), '};', '');
    c.push(`const lv_img_dsc_t ${img.name} = {`);
    c.push(`    .header = {.cf = ${img.alpha ? 'LV_IMG_CF_TRUE_COLOR_ALPHA' : 'LV_IMG_CF_TRUE_COLOR'}, .always_zero = 0, .reserved = 0, .w = ${img.cv.width}, .h = ${img.cv.height}},`);
    c.push(`    .data_size = ${bytes.length},`, `    .data = ${img.name}_data,`, '};', '');
  }
  writeFileSync(join(OUT_DIR, 'VuMeterArt.c'), c.join('\n'));
}

/* ---------- Previews ---------- */
function writePreviews(dir) {
  mkdirSync(dir, { recursive: true });
  for (const img of IMAGES) writeFileSync(join(dir, `${img.name}.png`), img.cv.toBuffer('image/png'));
  // As the device composes them: face, needle at 0 VU, lamp, glass.
  const analog = render(x => {
    x.drawImage(face, 0, 0);
    drawNeedle(x, 1 / VU_MAX);
    x.drawImage(lamp, LAMP.x, LAMP.y);
    x.drawImage(glass, glassBox.x, glassBox.y);
  });
  writeFileSync(join(dir, 'composite-analog.png'), analog.toBuffer('image/png'));
  // Bars from clipped lit bands: L at -6 dB, R at -2 dB with a hold at 0.
  const clipX = segments => segments <= 0 ? 0
    : Math.min(S, Math.ceil(segX(segments - 1) + SW + GAP / 2));
  const bars = render(x => {
    x.drawImage(barsDark, 0, 0);
    [[posOfDb(-6), -1], [posOfDb(-2), N - 1]].forEach(([p, hold], ch) => {
      const lit = Math.round(p * N);
      const left = X0 - GLOW_MARGIN, right = clipX(lit);
      if (right > left) x.drawImage(litBands[ch], left, 0, right - left, BAND_H, left, bandY[ch], right - left, BAND_H);
      if (hold >= 0) {
        const hx = Math.floor(segX(hold) - GAP / 2), hw = Math.ceil(SW + GAP);
        x.drawImage(litBands[ch], hx, 0, hw, BAND_H, hx, bandY[ch], hw, BAND_H);
      }
    });
  });
  writeFileSync(join(dir, 'composite-bars.png'), bars.toBuffer('image/png'));
}

writeC();
const at = process.argv.indexOf('--preview');
if (at > 0 && process.argv[at + 1]) writePreviews(process.argv[at + 1]);
console.log(`glass ${glassBox.w}x${glassBox.h} at ${glassBox.x},${glassBox.y}`);
for (const img of IMAGES) console.log(`${img.name} ${img.cv.width}x${img.cv.height}`);
