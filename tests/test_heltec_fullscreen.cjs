'use strict';

// node --test tests/test_heltec_fullscreen.cjs
// Exercises the real browser functions with no browser packages or device I/O.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const appPath = path.join(__dirname, '..', 'firmware', 'heltec-v3', 'main', 'web', 'app.js');
const source = fs.readFileSync(appPath, 'utf8');
const boundary = source.indexOf("$('wifiForm').addEventListener");
assert.ok(boundary > 0, 'fullscreen functions must precede application startup');

function harness(mode = 'native') {
  const elements = new Map();
  const listeners = new Map();
  const calls = {requests: [], exits: 0, fetch: 0, raf: 0, timers: 0, scroll: [], focus: []};
  let pending = null;
  let exitDenied = false;
  const document = {fullscreenElement: null, fullscreenEnabled: true, hidden: false};
  const window = {
    scrollX: 12, scrollY: 347,
    scrollTo(x, y) {
      if (typeof x === 'object') ({left: x, top: y} = x);
      this.scrollX = x;
      this.scrollY = y;
      calls.scroll.push([x, y]);
    },
  };

  function element(id) {
    const classes = new Set();
    let disabled = false;
    const result = {
      id, inert: false, isConnected: true, hidden: false, visible: true,
      style: {}, textContent: '', title: '', attributes: {},
      classList: {
        toggle(name, force) {
          const on = force === undefined ? !classes.has(name) : force;
          if (on) classes.add(name); else classes.delete(name);
          return on;
        },
        add(...names) { names.forEach(name => classes.add(name)); },
        remove(...names) { names.forEach(name => classes.delete(name)); },
        contains(name) { return classes.has(name); },
      },
      setAttribute(name, value) { this.attributes[name] = String(value); },
      getAttribute(name) { return this.attributes[name] ?? null; },
      getClientRects() { return this.visible && !this.hidden ? [{}] : []; },
      focus(options) {
        // Disabled controls cannot receive restored focus. This also catches
        // restoring focus before finally re-enables the fullscreen button.
        if (!this.disabled && !this.inert && this.isConnected) {
          document.activeElement = this;
          calls.focus.push({id, options});
        }
      },
      querySelectorAll() { return []; },
    };
    Object.defineProperty(result, 'disabled', {
      get: () => disabled,
      set(value) {
        disabled = value;
        if (value && document.activeElement === result) document.activeElement = document.body;
      },
    });
    elements.set(id, result);
    return result;
  }

  document.body = element('body');
  document.documentElement = element('html');
  const panel = element('liveAudioPanel');
  const button = element('fullscreenAudio');
  const header = element('header');
  const background = element('dashboardControls');
  background.inert = true; // Verify pre-existing state survives, rather than clearing everything.
  element('fullscreenHint');
  const middle = element('showSpectrum');
  const disabled = element('disabledControl');
  disabled.disabled = true;
  const hidden = element('hiddenControl');
  hidden.hidden = true;
  const last = element('spectrumGain');
  const outside = element('outsideControl');
  panel.querySelectorAll = () => [button, middle, disabled, hidden, last];
  panel.contains = value => value === panel || panel.querySelectorAll().includes(value);
  document.activeElement = button;
  document.getElementById = id => elements.get(id) || element(id);
  document.querySelectorAll = selector =>
    selector === 'body > header, #dashboardControls' ? [header, background] : [];
  document.addEventListener = (name, handler) => {
    if (!listeners.has(name)) listeners.set(name, []);
    listeners.get(name).push(handler);
  };
  function dispatch(name, event = {}) {
    for (const handler of listeners.get(name) || []) handler(event);
  }

  function nativeEntered() {
    document.fullscreenElement = panel;
    window.scrollX = window.scrollY = 0;
    dispatch('fullscreenchange');
  }
  if (mode !== 'unsupported') {
    panel.requestFullscreen = function requestFullscreen() {
      calls.requests.push(this.id);
      if (mode === 'denied') return Promise.reject(new Error('Permissions policy denied fullscreen'));
      if (mode === 'pending') {
        return new Promise((resolve, reject) => { pending = {resolve, reject}; });
      }
      nativeEntered();
      return Promise.resolve();
    };
  }
  document.exitFullscreen = async () => {
    calls.exits++;
    if (exitDenied) throw new Error('Exit refused');
    document.fullscreenElement = null;
    dispatch('fullscreenchange');
  };
  const context = vm.createContext({
    document, window, console, AbortController,
    localStorage: {getItem: () => null, setItem() {}},
    performance: {now: () => 0},
    requestAnimationFrame() { calls.raf++; },
    setTimeout() { calls.timers++; return calls.timers; },
    clearTimeout() {},
    fetch() { calls.fetch++; throw new Error('Fullscreen must not request network data'); },
  });
  vm.runInContext(source.slice(0, boundary), context, {filename: appPath});
  // Use the application's actual bindings without starting its audio visuals,
  // polling loop, or unrelated control handlers below the source boundary.
  for (const event of ['fullscreenchange', 'keydown']) {
    const binding = source.match(new RegExp(`^document\\.addEventListener\\('${event}',[^\\n]+\\);$`, 'm'));
    assert.ok(binding, `${event} handler must be wired in app.js`);
    vm.runInContext(binding[0], context, {filename: appPath});
  }
  return {
    run: code => vm.runInContext(code, context),
    toggle: () => vm.runInContext('toggleAudioFullscreen()', context),
    document, window, calls, panel, button, header, background, middle, last, outside,
    key(key, shiftKey = false) {
      const event = {key, shiftKey, prevented: false, preventDefault() { this.prevented = true; }};
      dispatch('keydown', event);
      return event;
    },
    browserExit() { document.fullscreenElement = null; dispatch('fullscreenchange'); },
    completeRequest(allow = true) {
      assert.ok(pending, 'a native request must be pending');
      if (allow) { nativeEntered(); pending.resolve(); }
      else pending.reject(new Error('Permissions policy denied fullscreen'));
      pending = null;
    },
    denyExit(value = true) { exitDenied = value; },
    quiet() {
      assert.equal(calls.fetch, 0, 'fullscreen must not create an audio/telemetry request');
      assert.equal(calls.raf, 0, 'fullscreen must not start another animation loop');
      assert.equal(calls.timers, 0, 'fullscreen must not start another polling timer');
      assert.equal(this.run('pollTimer'), 0);
      assert.equal(this.run('pollInFlight'), false);
    },
  };
}

function assertExpanded(h, fallback) {
  assert.equal(h.run('isAudioFullscreen()'), true);
  assert.equal(h.run('audioFullscreen.fallback'), fallback);
  assert.equal(h.panel.classList.contains('is-fullscreen'), true);
  assert.equal(h.document.body.classList.contains('audio-fullscreen'), true);
  assert.equal(h.button.getAttribute('aria-pressed'), 'true');
  assert.match(h.button.textContent, /Exit fullscreen/);
  assert.equal(h.header.inert, true);
  assert.equal(h.background.inert, true);
  assert.equal(h.panel.inert, false);
  assert.equal(h.document.body.inert, false);
}

function assertRestored(h) {
  assert.equal(h.run('isAudioFullscreen()'), false);
  assert.equal(h.run('audioFullscreen.active'), false);
  assert.equal(h.run('audioFullscreen.busy'), false);
  assert.equal(h.panel.classList.contains('is-fullscreen'), false);
  assert.equal(h.document.body.classList.contains('audio-fullscreen'), false);
  assert.equal(h.button.getAttribute('aria-pressed'), 'false');
  assert.equal(h.button.disabled, false);
  assert.equal(h.header.inert, false);
  assert.equal(h.background.inert, true);
  assert.equal(h.document.activeElement, h.button, 'restore focus to the original button');
  assert.equal(h.window.scrollX, 12);
  assert.equal(h.window.scrollY, 347);
  h.quiet();
}

test('native fullscreen targets only Live Audio and restores focus, scroll and inert state', async () => {
  const h = harness();
  await h.toggle();
  assert.deepEqual(h.calls.requests, ['liveAudioPanel']);
  assert.equal(h.document.fullscreenElement, h.panel);
  assertExpanded(h, false);
  h.middle.focus();
  await h.toggle();
  assert.equal(h.calls.exits, 1);
  assertRestored(h);
});

test('browser fullscreenchange exit performs cleanup without another exit request', async () => {
  const h = harness();
  await h.toggle();
  h.browserExit();
  assert.equal(h.calls.exits, 0);
  assertRestored(h);
});

for (const mode of ['denied', 'unsupported', 'disabled']) {
  test(`${mode} native fullscreen uses panel-only fallback and Escape restores the dashboard`, async () => {
    const h = harness(mode);
    if (mode === 'disabled') h.document.fullscreenEnabled = false;
    await h.toggle();
    assertExpanded(h, true);
    assert.equal(h.document.fullscreenElement, null);
    assert.equal(h.calls.requests.length, mode === 'denied' ? 1 : 0);
    h.window.scrollX = h.window.scrollY = 0;
    assert.equal(h.key('Escape').prevented, true);
    assert.equal(h.calls.exits, 0);
    assertRestored(h);
  });
}

test('fallback Tab navigation wraps visible enabled controls and recovers escaped focus', async () => {
  const h = harness('unsupported');
  await h.toggle();
  h.last.focus();
  assert.equal(h.key('Tab').prevented, true);
  assert.equal(h.document.activeElement, h.button);
  assert.equal(h.key('Tab', true).prevented, true);
  assert.equal(h.document.activeElement, h.last);
  h.outside.focus();
  h.key('Tab');
  assert.equal(h.document.activeElement, h.button);
  h.outside.focus();
  h.key('Tab', true);
  assert.equal(h.document.activeElement, h.last);
  h.middle.focus();
  assert.equal(h.key('Tab').prevented, false, 'ordinary navigation within the panel stays native');
  await h.toggle();
  assertRestored(h);
});

test('double-click during a pending native request does not launch a second transition', async () => {
  const h = harness('pending');
  const first = h.toggle();
  assert.equal(h.run('audioFullscreen.busy'), true);
  await h.toggle();
  assert.deepEqual(h.calls.requests, ['liveAudioPanel']);
  h.completeRequest();
  await first;
  assertExpanded(h, false);
  await h.toggle();
  assertRestored(h);
});

for (const allow of [true, false]) {
  test(`Escape cancels a pending native request even when it later ${allow ? 'succeeds' : 'rejects'}`, async () => {
    const h = harness('pending');
    const entering = h.toggle();
    h.key('Escape'); // Native browser Escape may retain its default behavior.
    h.completeRequest(allow);
    await entering;
    assert.equal(h.run('audioFullscreen.fallback'), false, 'cancelled requests must not enter fallback later');
    assertRestored(h);
  });
}

test('refused native exit leaves the panel active and its exit button usable', async () => {
  const h = harness();
  await h.toggle();
  h.denyExit();
  await h.toggle();
  assertExpanded(h, false);
  assert.equal(h.button.disabled, false);
  assert.equal(h.run('audioFullscreen.busy'), false);
  h.denyExit(false);
  h.browserExit();
  assertRestored(h);
});

test('repeated fallback entry and exit does not retain stale background or focus state', async () => {
  const h = harness('unsupported');
  for (let iteration = 0; iteration < 3; ++iteration) {
    await h.toggle();
    assertExpanded(h, true);
    await h.toggle();
    assertRestored(h);
    assert.equal(h.run('audioFullscreen.background.length'), 0);
  }
});

test('keyboard handling leaves ordinary navigation and another fullscreen element alone', async () => {
  const h = harness();
  assert.equal(h.key('Escape').prevented, false);
  assert.equal(h.key('Tab').prevented, false);
  h.document.fullscreenElement = h.outside;
  assert.equal(h.key('Escape').prevented, false);
  assert.equal(h.calls.exits, 0);
  h.document.fullscreenElement = null;
  await h.toggle();
  h.last.focus();
  assert.equal(h.key('Tab').prevented, false, 'native fullscreen keeps native focus handling');
  h.browserExit();
  assertRestored(h);
});
