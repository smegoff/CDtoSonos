'use strict';

// Run directly: node --test tests/test_heltec_visuals.cjs
// No browser, npm packages, network, or firmware device is needed.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const appPath = path.join(__dirname, '..', 'firmware', 'heltec-v3', 'main', 'web', 'app.js');
const source = fs.readFileSync(appPath, 'utf8');
const boundary = source.indexOf("$('wifiForm').addEventListener");
assert.ok(boundary > 0, 'app functions must precede the UI event bindings');

function meterState(overrides = {}) {
  return {
    clock_locked: true, capture_age_ms: 8, meter_updates: 1,
    rms_l: -12, rms_r: -18, peak_l: -3, peak_r: -6,
    spectrum: Array(31).fill(-20), sample_rate: 44100, channels: 2, bits: 16,
    cpu_percent: 5, cpu0_percent: 6, cpu1_percent: 4, cpu_mhz: 240,
    heap_free: 100000, captured_frames: 44100, stream_frames: 43,
    uptime_s: 1, stream_clients: 1, sonos_group_ip: '',
    stream_url: 'http://example.invalid:8000/cd.flac', ...overrides,
  };
}

function harness() {
  let now = 0;
  let timerId = 0;
  const timers = new Map();
  const elements = new Map();
  const calls = [];
  let fetchHandler = async () => ({ok: true, json: async () => meterState()});

  function canvasContext() {
    const log = [];
    const context = {log};
    for (const name of ['clearRect', 'fillRect', 'strokeRect', 'beginPath', 'moveTo',
      'lineTo', 'stroke', 'fill', 'fillText', 'save', 'restore', 'rect', 'clip', 'arc', 'drawImage']) {
      context[name] = (...args) => log.push({name, args});
    }
    context.createLinearGradient = () => ({addColorStop() {}});
    return context;
  }

  function element(id = '') {
    const context = canvasContext();
    return {
      id, width: 540, height: 280, style: {}, textContent: '', value: '',
      options: [], dataset: {}, hidden: false, handlers: {},
      addEventListener(name, handler) { this.handlers[name] = handler; },
      classList: {toggle() {}, remove() {}},
      getContext: () => context,
      getAttribute(name) { return this[name] ?? null; },
      setAttribute(name, value) { this[name] = value; },
      appendChild(child) { this.options.push(child); },
    };
  }

  const document = {
    hidden: false,
    getElementById(id) {
      if (!elements.has(id)) elements.set(id, element(id));
      return elements.get(id);
    },
    createElement: () => element(),
    querySelectorAll: () => [],
  };
  const context = vm.createContext({
    document, console, AbortController,
    localStorage: {getItem: () => null, setItem() {}},
    performance: {now: () => now},
    requestAnimationFrame() {},
    setTimeout(fn, delay) {
      const id = ++timerId;
      timers.set(id, {fn, delay});
      return id;
    },
    clearTimeout(id) { timers.delete(id); },
    fetch(url, options) {
      calls.push({url, options});
      return fetchHandler(url, options);
    },
  });
  vm.runInContext(source.slice(0, boundary), context, {filename: appPath});
  return {
    run: code => vm.runInContext(code, context),
    setClock(value) { now = value; },
    setFetch(handler) { fetchHandler = handler; },
    document, timers, calls,
    element: id => document.getElementById(id),
    pollDelay() { return this.timers.get(this.run('pollTimer'))?.delay; },
    targets(state = meterState()) {
      context.testState = state;
      this.run('updateVisualTargets(testState)');
    },
    animate(time) {
      now = time;
      this.run(`animateVisual(${time})`);
    },
  };
}

test('VU level sweep stays finite, monotonic and in the upper arc from silence to full scale', () => {
  const h = harness();
  let previousX = -Infinity;
  for (let db = -96; db <= 0; db += 0.5) {
    const angle = h.run(`vuAngle(${db})`);
    assert.ok(Number.isFinite(angle));
    const x = Math.cos(angle);
    assert.ok(x >= previousX - 1e-12, `needle reversed at ${db} dBFS`);
    assert.ok(Math.sin(angle) < 0, `needle left the upper arc at ${db} dBFS`);
    previousX = x;
  }
  assert.ok(h.run('vuAngle(-48)') < h.run('vuAngle(-24)'));
  assert.ok(h.run('vuAngle(-24)') < h.run('vuAngle(0)'));
});

test('nonfinite readings become silence and finite out-of-range readings are clamped', () => {
  const h = harness();
  for (const value of ['NaN', 'Infinity', '-Infinity', 'undefined', 'null', "''", "'bad'"]) {
    assert.equal(h.run(`finiteDb(${value})`), -96);
    assert.ok(Number.isFinite(h.run(`vuAngle(${value})`)));
  }
  assert.equal(h.run('finiteDb(-120)'), -96);
  assert.equal(h.run('finiteDb(100)'), 3);
  assert.equal(h.run('vuToUnit(3)'), 1);
  assert.equal(h.run('levelToUnit(-120)'), 0);
  h.targets(meterState({rms_l: Infinity, peak_r: NaN, spectrum: [-200, Infinity, 100]}));
  assert.equal(h.run('visuals.vuTarget[0]'), -96);
  assert.equal(h.run('visuals.peakTarget[1]'), -96);
  assert.deepEqual(Array.from(h.run('visuals.spectrumTarget.slice(0, 4)')), [-96, -96, 3, -96]);
});

test('VU and spectrum settle promptly on attack and release more gently', () => {
  const h = harness();
  h.setClock(100);
  h.targets(meterState({rms_l: -6, spectrum: Array(31).fill(-6)}));
  h.run('lastFrameAt = 100; visuals.vuDisplay[0] = -48');
  h.animate(200);
  assert.ok(h.run('visuals.vuDisplay[0]') > -9, 'VU should cover over 90% of a rise in 100 ms');
  h.run('visuals.vuDisplay[0] = -6; visuals.vuTarget[0] = -48');
  h.animate(300);
  assert.ok(h.run('visuals.vuDisplay[0]') > -30, 'VU release should retain some momentum');
  h.run("visuals.mode = 'spectrum'; visuals.spectrumDisplay.fill(-48)");
  h.animate(400);
  assert.ok(h.run('visuals.spectrumDisplay[0]') > -7, 'spectrum attack should settle in about 100 ms');
});

test('VU clips the needle and hub, then draws the bezel last', () => {
  const h = harness();
  h.run("drawVu($('vuLeft'), -6)");
  const log = h.element('vuLeft').getContext('2d').log;
  const names = log.map(call => call.name);
  assert.ok(names.indexOf('drawImage') < names.indexOf('clip'));
  assert.ok(names.indexOf('clip') < names.indexOf('stroke'));
  assert.ok(names.indexOf('clip') < names.indexOf('arc'));
  assert.ok(names.indexOf('fill') < names.indexOf('restore'));
  assert.equal(names.at(-1), 'strokeRect');
  assert.ok(names.indexOf('restore') < names.length - 1);
});

test('peak meter attacks instantly, falls at a controlled rate and holds its marker', () => {
  const h = harness();
  h.setClock(100);
  h.targets(meterState({peak_l: -3}));
  h.run('lastFrameAt = 100');
  h.animate(200);
  assert.equal(h.run('visuals.peakDisplay[0]'), -3);
  assert.equal(h.run('visuals.peakHold[0]'), -3);
  h.setClock(250);
  h.targets(meterState({meter_updates: 2, peak_l: -30}));
  h.animate(300);
  assert.ok(Math.abs(h.run('visuals.peakDisplay[0]') - -6.6) < 1e-9);
  assert.equal(h.run('visuals.peakHold[0]'), -3);
  h.setClock(850);
  h.targets(meterState({meter_updates: 3, peak_l: -30}));
  h.animate(900);
  assert.ok(h.run('visuals.peakHold[0]') < -3, 'hold should decay after its hold period');
  assert.ok(h.run('visuals.peakHold[0]') > -30, 'hold should fall progressively');
});

test('stale telemetry fades instead of retaining live-looking frozen levels', () => {
  const h = harness();
  h.setClock(100);
  h.targets(meterState());
  h.run('lastFrameAt = 100; visuals.vuDisplay[0] = -12');
  h.animate(200);
  h.animate(850);
  assert.ok(h.run('visuals.vuDisplay[0]') < -30);
  assert.equal(h.element('meterHealth').textContent, 'Waiting for fresh meter data');
  assert.equal(h.element('meterHealth').hidden, false);
});

test('LED peak bars use whole bounded cells and keep the hold marker inside its cell', () => {
  const h = harness();
  for (let db = -96; db <= 6; db += 0.25) {
    const cells = h.run(`peakLedCells(${db})`);
    assert.ok(Number.isInteger(cells) && cells >= 0 && cells <= 48);
  }
  assert.equal(h.run('peakLedCells(-96)'), 0);
  assert.equal(h.run('peakLedCells(3)'), 48);
  h.setClock(100);
  h.targets(meterState({peak_l: 3, peak_r: -96}));
  h.animate(200);
  assert.equal(h.element('peakLeft').style.clipPath, 'inset(0 0% 0 0)');
  assert.equal(h.element('glowLeft').style.clipPath, h.element('peakLeft').style.clipPath);
  assert.equal(h.element('holdLeft').style.left, `${47 / 48 * 100}%`);
  assert.equal(h.element('holdLeft').style.opacity, '1');
  assert.equal(h.element('holdLeft').style.color, '#ff3a23');
  assert.equal(h.element('peakRight').style.clipPath, 'inset(0 100% 0 0)');
  assert.equal(h.element('glowRight').style.clipPath, h.element('peakRight').style.clipPath);
  assert.equal(h.element('holdRight').style.opacity, '0');
});

test('held peak colours match complete green, amber and red LED cells', () => {
  const h = harness();
  for (const cell of [0, 1, 35]) assert.equal(h.run(`peakLedColour(${cell})`), '#20ed86');
  for (const cell of [36, 43]) assert.equal(h.run(`peakLedColour(${cell})`), '#ffb928');
  for (const cell of [44, 48]) assert.equal(h.run(`peakLedColour(${cell})`), '#ff3a23');
});

test('responses with a frozen meter_updates counter also fade; advancing counter recovers', () => {
  const h = harness();
  h.setClock(100);
  h.targets(meterState({meter_updates: 50}));
  h.run('lastFrameAt = 100; visuals.vuDisplay[0] = -12');
  h.setClock(750);
  h.targets(meterState({meter_updates: 50}));
  h.animate(800);
  const faded = h.run('visuals.vuDisplay[0]');
  assert.ok(faded < -30, 'fresh HTTP response must not disguise frozen device meters');
  assert.equal(h.element('meterHealth').textContent, 'Waiting for fresh meter data');
  h.setClock(850);
  h.targets(meterState({meter_updates: 51}));
  h.animate(900);
  assert.ok(h.run('visuals.vuDisplay[0]') > faded);
  assert.equal(h.element('meterHealth').textContent, '');
  assert.equal(h.element('meterHealth').hidden, true);
});

test('spectrum peak markers hold briefly, fall naturally and attack new peaks immediately', () => {
  const h = harness();
  h.setClock(100);
  h.targets(meterState({spectrum: Array(31).fill(-12)}));
  h.run("visuals.mode = 'spectrum'; lastFrameAt = 100");
  h.animate(200);
  assert.equal(h.run('visuals.spectrumHold[0]'), -12);
  assert.equal(h.run('visuals.spectrumHoldUntil[0]'), 380);
  h.setClock(250);
  h.targets(meterState({meter_updates: 2, spectrum: Array(31).fill(-40)}));
  h.animate(300);
  assert.equal(h.run('visuals.spectrumHold[0]'), -12, 'marker should survive a short trough');
  h.animate(380);
  assert.equal(h.run('visuals.spectrumHold[0]'), -12, 'the full 180 ms hold should elapse');
  h.animate(400);
  assert.ok(Math.abs(h.run('visuals.spectrumHold[0]') - -12.64) < 1e-9);
  h.animate(500);
  assert.ok(Math.abs(h.run('visuals.spectrumHold[0]') - -15.84) < 1e-9,
    'marker should fall at 32 dB per second rather than snap to the lower bar');
  h.setClock(510);
  h.targets(meterState({meter_updates: 3, spectrum: Array(31).fill(-3)}));
  h.animate(520);
  assert.equal(h.run('visuals.spectrumHold[0]'), -3);
  h.animate(1200);
  assert.ok(h.run('visuals.spectrumHold[0]') < -3, 'stale data must also release spectrum peaks');
});

test('spectrum uses 31 bounded bands and reuses its static grid cache', () => {
  const h = harness();
  const labels = Array.from(h.run('frequencies'));
  assert.equal(labels.length, 31);
  assert.equal(labels[0], '20');
  assert.equal(labels.at(-1), '20k');
  for (const array of ['spectrumTarget', 'spectrumDisplay', 'spectrumHold', 'spectrumHoldUntil']) {
    assert.equal(h.run(`visuals.${array}.length`), 31);
  }
  h.targets(meterState({spectrum: Array(40).fill(3)}));
  assert.equal(h.run('visuals.spectrumTarget.length'), 31, 'extra incoming bands should not extend the chart');
  h.targets(meterState({spectrum: [-20, -30]}));
  assert.equal(h.run('visuals.spectrumTarget.length'), 31);
  assert.ok(Array.from(h.run('visuals.spectrumTarget.slice(2)')).every(value => value === -96));

  h.run('visuals.spectrumHold.fill(0); drawSpectrum(new Array(31).fill(0))');
  const canvas = h.element('spectrum');
  const cached = h.run("spectrumFaceCache.get($('spectrum'))");
  assert.equal(cached.rows, 30);
  const allRects = [canvas, cached.face].flatMap(item =>
    item.getContext('2d').log.filter(call => call.name === 'fillRect'));
  for (const {args: [x, y, width, height]} of allRects) {
    assert.ok([x, y, width, height].every(Number.isFinite));
    assert.ok(x >= 0 && y >= 0 && width > 0 && height > 0);
    assert.ok(x + width <= canvas.width + 1e-9 && y + height <= canvas.height + 1e-9,
      'LED segments, peak markers and cached background should stay within the canvas');
  }
  const labelsOnFace = cached.face.getContext('2d').log
    .filter(call => call.name === 'fillText').map(call => call.args[0]);
  assert.ok(labels.every(label => labelsOnFace.includes(label)));
  h.run('drawSpectrum(new Array(31).fill(-24))');
  assert.equal(h.run("spectrumFaceCache.get($('spectrum'))"), cached);
});

test('spectrum sensitivity clamps locally and changes presentation without altering audio readings', () => {
  const h = harness();
  const start = source.indexOf("$('spectrumGain').addEventListener");
  const end = source.indexOf("$('stopSonos').addEventListener", start);
  assert.ok(start >= boundary && end > start, 'load only the spectrum sensitivity event binding');
  h.run(source.slice(start, end));
  const slider = h.element('spectrumGain');
  const setGain = value => slider.handlers.input({target: {value: String(value)}});
  h.targets(meterState());
  const rawBefore = h.run('JSON.stringify([visuals.spectrumTarget, visuals.spectrumHold, visuals.vuTarget, visuals.peakTarget])');
  const vuBefore = h.run('vuToUnit(-24)');
  assert.equal(h.run('spectrumUnit(-66)'), 0);
  assert.equal(h.run('spectrumUnit(-33)'), 0.5);
  assert.equal(h.run('spectrumUnit(0)'), 1);
  setGain(12);
  assert.ok(h.run('spectrumUnit(-33)') > 0.5);
  assert.equal(h.element('spectrumGainValue').textContent, '+12 dB · visuals only');
  setGain(100);
  assert.equal(h.run('visuals.spectrumGain'), 24);
  assert.equal(h.run('spectrumUnit(-3)'), 1);
  setGain(-100);
  assert.equal(h.run('visuals.spectrumGain'), -12);
  assert.equal(h.run('spectrumUnit(-66)'), 0);
  assert.equal(h.run('vuToUnit(-24)'), vuBefore);
  assert.equal(h.run('JSON.stringify([visuals.spectrumTarget, visuals.spectrumHold, visuals.vuTarget, visuals.peakTarget])'), rawBefore);
  assert.equal(h.calls.length, 0, 'sensitivity changes must not send stream or audio control requests');
  setGain('invalid');
  assert.equal(h.run('visuals.spectrumGain'), 0);
});

test('clock loss and old capture data force both visual targets to silence', () => {
  for (const bad of [{clock_locked: false}, {capture_age_ms: 601}]) {
    const h = harness();
    h.setClock(100);
    h.targets(meterState(bad));
    assert.deepEqual(Array.from(h.run('visuals.vuTarget')), [-96, -96]);
    assert.deepEqual(Array.from(h.run('visuals.peakTarget')), [-96, -96]);
    assert.ok(Array.from(h.run('visuals.spectrumTarget')).every(value => value === -96));
  }
});

test('scheduler uses 100 ms meters, full status every 2 s and 5 s when hidden', async () => {
  const h = harness();
  await h.run('updateStatus()');
  assert.equal(h.calls.at(-1).url, '/api/status');
  assert.equal(h.pollDelay(), 100);
  h.setClock(100);
  await h.run('updateStatus()');
  assert.equal(h.calls.at(-1).url, '/api/meters');
  h.setClock(1999);
  await h.run('updateStatus()');
  assert.equal(h.calls.at(-1).url, '/api/meters');
  h.setClock(2000);
  await h.run('updateStatus()');
  assert.equal(h.calls.at(-1).url, '/api/status');
  h.document.hidden = true;
  h.setClock(2100);
  await h.run('updateStatus()');
  assert.equal(h.calls.at(-1).url, '/api/status');
  assert.equal(h.pollDelay(), 5000);
});

test('scheduler permits only one pending request and counts request time toward cadence', async () => {
  const h = harness();
  let resolve;
  h.setFetch(() => new Promise(done => { resolve = done; }));
  const pending = h.run('updateStatus()');
  assert.equal(h.run('pollInFlight'), true);
  await h.run('updateStatus()');
  assert.equal(h.calls.length, 1);
  h.setClock(40);
  resolve({ok: true, json: async () => meterState()});
  await pending;
  assert.equal(h.run('pollInFlight'), false);
  assert.equal(h.pollDelay(), 60);
  assert.equal(h.timers.size, 1, 'only the next poll timer should remain');
});

test('failed requests back off, and a 1500 ms timeout aborts and releases the request', async () => {
  const h = harness();
  h.setFetch(async () => ({ok: false, status: 503}));
  await h.run('updateStatus()');
  assert.equal(h.element('clockState').textContent, 'Dashboard disconnected');
  assert.equal(h.pollDelay(), 1000);
  assert.equal(h.run('pollInFlight'), false);

  h.setClock(1000);
  h.setFetch((url, {signal}) => new Promise((resolve, reject) => {
    signal.addEventListener('abort', () => reject(new Error('aborted')), {once: true});
  }));
  const pending = h.run('updateStatus()');
  const timeout = [...h.timers.values()].find(timer => timer.delay === 1500);
  assert.ok(timeout, 'request should have a bounded abort deadline');
  h.setClock(2500);
  timeout.fn();
  await pending;
  assert.equal(h.calls.at(-1).options.signal.aborted, true);
  assert.equal(h.run('pollInFlight'), false);
  assert.equal(h.pollDelay(), 1000, 'a timed-out request should leave the full failure backoff');
  assert.equal(h.timers.size, 1);
});
