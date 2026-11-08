const $ = id => document.getElementById(id);
const frequencies = ['20', '25', '31.5', '40', '50', '63', '80', '100', '125', '160', '200', '250', '315', '400', '500', '630', '800', '1k', '1.25k', '1.6k', '2k', '2.5k', '3.15k', '4k', '5k', '6.3k', '8k', '10k', '12.5k', '16k', '20k'];
const POLL_MS = 100;
const STATUS_MS = 2000;
const HIDDEN_POLL_MS = 5000;
const FRAME_MS = 1000 / 60;
const STALE_MS = 600;
const PEAK_LED_COUNT = 48;
const vuFaceCache = new WeakMap();
const spectrumFaceCache = new WeakMap();
let previousStatus = null;
let pollTimer = 0;
let pollInFlight = false;
let lastFrameAt = 0;
let nextStatusAt = 0;
let eqDirty = false;
let eqSaving = false;
const eqSliders = [];
let lastMeterAt = -Infinity;
let lastMeterUpdate = null;
let lastMeterAdvanceAt = -Infinity;
let lastReadoutAt = 0;
const audioFullscreen = {fallback: false, busy: false, active: false, cancelRequested: false,
  returnFocus: null, scroll: [0, 0], background: []};

const visuals = {
  mode: localStorage.getItem('cdts-visual') === 'spectrum' ? 'spectrum' : 'vu',
  spectrumTarget: new Array(frequencies.length).fill(-96),
  spectrumDisplay: new Array(frequencies.length).fill(-96),
  spectrumHold: new Array(frequencies.length).fill(-96),
  spectrumHoldUntil: new Array(frequencies.length).fill(0),
  spectrumGain: 0,
  vuTarget: [-96, -96],
  vuDisplay: [-96, -96],
  peakTarget: [-96, -96],
  peakDisplay: [-96, -96],
  peakHold: [-96, -96],
  peakHoldUntil: [0, 0]
};

function isAudioFullscreen() {
  return document.fullscreenElement === $('liveAudioPanel') || audioFullscreen.fallback;
}

function syncAudioFullscreen() {
  const active = isAudioFullscreen();
  const button = $('fullscreenAudio');
  if (active && !audioFullscreen.active) {
    audioFullscreen.returnFocus ??= document.activeElement;
    audioFullscreen.background = [...document.querySelectorAll('body > header, #dashboardControls')]
      .map(element => ({element, inert: element.inert}));
    audioFullscreen.background.forEach(({element}) => { element.inert = true; });
  }
  $('liveAudioPanel').classList.toggle('is-fullscreen', active);
  document.body.classList.toggle('audio-fullscreen', active);
  button.setAttribute('aria-pressed', String(active));
  button.textContent = active ? '↙ Exit fullscreen' : '⛶ Fullscreen';
  button.title = active ? 'Return to the dashboard (Escape)' : 'Expand only the Live Audio panel';
  $('fullscreenHint').hidden = !active;
  $('fullscreenHint').textContent = audioFullscreen.fallback
    ? 'Panel-only view · Exit fullscreen or press Escape to return.'
    : 'Press Escape or Exit fullscreen to return to the dashboard.';
  if (!active && audioFullscreen.active) {
    audioFullscreen.background.forEach(({element, inert}) => { element.inert = inert; });
    audioFullscreen.background = [];
  }
  // Native fullscreenchange may arrive before exitFullscreen() resolves and
  // before the toggle button is enabled again; restore focus only afterwards.
  if (!active && !audioFullscreen.busy && audioFullscreen.returnFocus) {
    const focus = audioFullscreen.returnFocus;
    if (focus?.isConnected) focus.focus({preventScroll: true});
    audioFullscreen.returnFocus = null;
    window.scrollTo(...audioFullscreen.scroll);
  }
  audioFullscreen.active = active;
}

async function toggleAudioFullscreen() {
  if (audioFullscreen.busy) return;
  audioFullscreen.busy = true;
  const panel = $('liveAudioPanel');
  const button = $('fullscreenAudio');
  const entering = !isAudioFullscreen();
  if (entering) {
    audioFullscreen.cancelRequested = false;
    audioFullscreen.returnFocus = document.activeElement;
    audioFullscreen.scroll = [window.scrollX, window.scrollY];
  }
  button.disabled = true;
  try {
    if (document.fullscreenElement === panel) {
      await document.exitFullscreen();
    } else if (audioFullscreen.fallback) {
      audioFullscreen.fallback = false;
    } else {
      // Request only this element. Embedded/mobile browsers may deny native
      // fullscreen, so retain an in-page panel-only mode with the same exit.
      try {
        if (!panel.requestFullscreen || document.fullscreenEnabled === false) throw new Error('Unavailable');
        await panel.requestFullscreen();
        if (audioFullscreen.cancelRequested && document.fullscreenElement === panel) await document.exitFullscreen();
      } catch (_) {
        if (!audioFullscreen.cancelRequested) audioFullscreen.fallback = true;
      }
    }
  } catch (_) {
    // If native exit is refused, leave the button usable and let Escape exit.
  } finally {
    audioFullscreen.busy = false;
    button.disabled = false;
    syncAudioFullscreen();
    if (entering && isAudioFullscreen()) button.focus({preventScroll: true});
  }
}

function audioFullscreenKeydown(event) {
  if (event.key === 'Escape' && audioFullscreen.busy) audioFullscreen.cancelRequested = true;
  if (!isAudioFullscreen()) return;
  if (event.key === 'Escape' && audioFullscreen.fallback) {
    event.preventDefault();
    audioFullscreen.fallback = false;
    syncAudioFullscreen();
  } else if (event.key === 'Tab' && audioFullscreen.fallback) {
    const controls = [...$('liveAudioPanel').querySelectorAll('button, input, [tabindex="0"]')]
      .filter(element => !element.disabled && element.getClientRects().length);
    const first = controls[0], last = controls[controls.length - 1];
    if (first && event.shiftKey && (document.activeElement === first || !controls.includes(document.activeElement))) {
      event.preventDefault(); last.focus();
    } else if (last && !event.shiftKey && (document.activeElement === last || !controls.includes(document.activeElement))) {
      event.preventDefault(); first.focus();
    }
  }
}

function finiteDb(value) {
  if (value == null || value === '') return -96;
  return Number.isFinite(Number(value)) ? Math.max(-96, Math.min(3, Number(value))) : -96;
}

function levelToUnit(db) {
  return Math.max(0, Math.min(1, (finiteDb(db) + 60) / 63));
}

function peakLedCells(db) {
  return Math.ceil(levelToUnit(db) * PEAK_LED_COUNT);
}

function peakLedColour(cell) {
  return cell <= 35 ? '#20ed86' : cell <= 43 ? '#ffb928' : '#ff3a23';
}

function vuToUnit(db) {
  return Math.max(0, Math.min(1, (finiteDb(db) + 48) / 48));
}

function vuAngle(db) {
  // Canvas Y points down: keep the whole sweep in the upper half of the dial.
  return Math.PI * (1.25 + vuToUnit(db) * .5);
}

function approach(current, target, elapsedMs, attackMs, releaseMs) {
  const timeConstant = target > current ? attackMs : releaseMs;
  return current + (target - current) * (1 - Math.exp(-elapsedMs / timeConstant));
}

function spectrumUnit(db) {
  return Math.max(0, Math.min(1, (finiteDb(db) + visuals.spectrumGain + 66) / 66));
}

function createSpectrumFace(canvas) {
  const face = document.createElement('canvas');
  face.width = canvas.width;
  face.height = canvas.height;
  const context = face.getContext('2d');
  const left = 42, top = 16, bottom = face.height - 30;
  const step = (face.width - left - 14) / frequencies.length;
  const rows = 30, rowStep = (bottom - top) / rows;
  const cellWidth = Math.max(2, step - 5), cellHeight = Math.max(2, rowStep - 3);
  context.fillStyle = '#050a09';
  context.fillRect(0, 0, face.width, face.height);
  context.font = '10px system-ui';
  context.textAlign = 'right';
  for (let db = -60; db <= 0; db += 12) {
    const y = bottom - ((db + 66) / 66) * (bottom - top);
    context.fillStyle = '#7e9a91';
    context.fillText(String(db), left - 10, y + 4);
  }
  for (let band = 0; band < frequencies.length; band++) {
    const x = left + band * step;
    context.fillStyle = '#10231d';
    for (let row = 0; row < rows; row++) {
      context.fillRect(x, bottom - (row + 1) * rowStep, cellWidth, cellHeight);
    }
    context.fillStyle = '#82978f';
    context.textAlign = 'center';
    context.fillText(frequencies[band], x + cellWidth / 2, face.height - 10);
  }
  const cached = {face, left, bottom, step, rows, rowStep, cellWidth, cellHeight};
  spectrumFaceCache.set(canvas, cached);
  return cached;
}

function drawSpectrum(values) {
  const canvas = $('spectrum');
  const context = canvas.getContext('2d');
  const grid = spectrumFaceCache.get(canvas) || createSpectrumFace(canvas);
  context.drawImage(grid.face, 0, 0);
  values.forEach((db, index) => {
    const lit = Math.round(spectrumUnit(db) * grid.rows);
    const x = grid.left + index * grid.step;
    for (let row = 0; row < lit; row++) {
      context.fillStyle = row >= grid.rows - 3 ? '#f6bd66' : '#6fffc5';
      context.fillRect(x, grid.bottom - (row + 1) * grid.rowStep, grid.cellWidth, grid.cellHeight);
    }
    const peak = Math.round(spectrumUnit(visuals.spectrumHold[index]) * grid.rows);
    if (peak > 0) {
      context.fillStyle = '#ff8954';
      context.fillRect(x, grid.bottom - peak * grid.rowStep, grid.cellWidth, 2);
    }
  });
}

function createVuFace(canvas) {
  const faceCanvas = document.createElement('canvas');
  faceCanvas.width = canvas.width;
  faceCanvas.height = canvas.height;
  const context = faceCanvas.getContext('2d');
  const width = faceCanvas.width;
  const height = faceCanvas.height;
  const cx = width / 2;
  const cy = height * .98;
  const radius = Math.min(width * .43, height * .84);
  const face = context.createLinearGradient(0, 0, 0, height);
  face.addColorStop(0, '#f8c965');
  face.addColorStop(1, '#b86a19');
  context.fillStyle = face;
  context.strokeStyle = '#080808';
  context.lineWidth = 12;
  context.fillRect(8, 8, width - 16, height - 16);
  context.strokeRect(8, 8, width - 16, height - 16);
  for (let db = -48; db <= 0; db += 3) {
    const angle = vuAngle(db);
    const labelled = [-48, -36, -24, -12, -6, 0].includes(db);
    const inner = radius - (labelled ? 16 : 9);
    context.beginPath();
    context.moveTo(cx + Math.cos(angle) * inner, cy + Math.sin(angle) * inner);
    context.lineTo(cx + Math.cos(angle) * radius, cy + Math.sin(angle) * radius);
    context.strokeStyle = db >= -6 ? '#a82418' : '#292016';
    context.lineWidth = 2;
    context.stroke();
    if (labelled) {
      context.fillStyle = db >= -6 ? '#a82418' : '#292016';
      context.font = '12px system-ui';
      context.textAlign = 'center';
      context.fillText(String(db), cx + Math.cos(angle) * (radius - 31),
        cy + Math.sin(angle) * (radius - 31) + 4);
    }
  }
  context.fillStyle = '#292016';
  context.font = 'bold 16px system-ui';
  context.textAlign = 'right';
  context.fillText('VU', width - 22, height - 22);
  context.font = '12px system-ui';
  context.textAlign = 'center';
  context.fillText('RMS / dBFS', cx, height * .67);
  vuFaceCache.set(canvas, faceCanvas);
  return faceCanvas;
}

function drawVu(canvas, db) {
  const context = canvas.getContext('2d');
  const width = canvas.width;
  const height = canvas.height;
  const cx = width / 2;
  const cy = height * .98;
  const radius = Math.min(width * .43, height * .84);
  const face = vuFaceCache.get(canvas) || createVuFace(canvas);
  context.clearRect(0, 0, width, height);
  context.drawImage(face, 0, 0);
  const needleAngle = vuAngle(db);
  context.save();
  context.beginPath();
  context.rect(14, 14, width - 28, height - 28);
  context.clip();
  context.beginPath();
  context.moveTo(cx, cy);
  context.lineTo(cx + Math.cos(needleAngle) * (radius - 8), cy + Math.sin(needleAngle) * (radius - 8));
  context.strokeStyle = '#17100b';
  context.lineWidth = 3;
  context.stroke();
  context.beginPath();
  context.arc(cx, cy, 9, 0, Math.PI * 2);
  context.fillStyle = '#17100b';
  context.fill();
  context.restore();
  // The bezel is the last layer, so neither needle nor hub can cross it.
  context.strokeStyle = '#080808';
  context.lineWidth = 12;
  context.strokeRect(8, 8, width - 16, height - 16);
}

function setVisualMode(mode) {
  visuals.mode = mode === 'spectrum' ? 'spectrum' : 'vu';
  $('spectrumView').hidden = visuals.mode !== 'spectrum';
  $('vuView').hidden = visuals.mode !== 'vu';
  document.querySelectorAll('[data-visual]').forEach(button => {
    const selected = button.dataset.visual === visuals.mode;
    button.setAttribute('aria-pressed', String(selected));
    button.classList.toggle('secondary', !selected);
  });
  localStorage.setItem('cdts-visual', visuals.mode);
}

function animateVisual(now) {
  const elapsed = Math.min(100, Math.max(1, now - (lastFrameAt || now - FRAME_MS)));
  if (!document.hidden && now - lastFrameAt >= FRAME_MS - 1) {
    lastFrameAt = now;
    const stale = now - lastMeterAt > STALE_MS || now - lastMeterAdvanceAt > STALE_MS;
    if (visuals.mode === 'spectrum') {
      visuals.spectrumDisplay = visuals.spectrumDisplay.map((current, index) =>
        approach(current, stale ? -96 : visuals.spectrumTarget[index], elapsed, 25, 130));
      visuals.spectrumHold = visuals.spectrumHold.map((peak, index) => {
        const target = stale ? -96 : visuals.spectrumTarget[index];
        if (target > peak) {
          visuals.spectrumHoldUntil[index] = now + 180;
          return target;
        }
        return now > visuals.spectrumHoldUntil[index] ? Math.max(target, peak - elapsed * .032) : peak;
      });
      drawSpectrum(visuals.spectrumDisplay);
    } else {
      visuals.vuDisplay = visuals.vuDisplay.map((current, index) =>
        approach(current, stale ? -96 : visuals.vuTarget[index], elapsed, 35, 150));
      drawVu($('vuLeft'), visuals.vuDisplay[0]);
      drawVu($('vuRight'), visuals.vuDisplay[1]);
    }
    for (let channel = 0; channel < 2; channel++) {
      const target = stale ? -96 : visuals.peakTarget[channel];
      // Instant attack on a measured peak; a controlled dB-per-second fall.
      visuals.peakDisplay[channel] = Math.max(target, visuals.peakDisplay[channel] - elapsed * .036);
      if (target > visuals.peakHold[channel]) {
        visuals.peakHold[channel] = target;
        visuals.peakHoldUntil[channel] = now + 650;
      } else if (now > visuals.peakHoldUntil[channel]) {
        visuals.peakHold[channel] = Math.max(target, visuals.peakHold[channel] - elapsed * .024);
      }
      const side = channel === 0 ? 'Left' : 'Right';
      const litCells = peakLedCells(visuals.peakDisplay[channel]);
      const heldCell = peakLedCells(visuals.peakHold[channel]);
      const ledClip = `inset(0 ${(1 - litCells / PEAK_LED_COUNT) * 100}% 0 0)`;
      $("peak" + side).style.clipPath = ledClip;
      $("glow" + side).style.clipPath = ledClip;
      $("hold" + side).style.left = `${Math.max(0, heldCell - 1) / PEAK_LED_COUNT * 100}%`;
      $("hold" + side).style.opacity = heldCell > 0 ? '1' : '0';
      $("hold" + side).style.color = peakLedColour(heldCell);
    }
    if (now - lastReadoutAt >= 100) {
      lastReadoutAt = now;
      $('meterHealth').hidden = !stale;
      $('meterHealth').textContent = stale ? 'Waiting for fresh meter data' : '';
      ['Left', 'Right'].forEach((side, channel) => {
        $('vu' + side + 'Value').textContent = `${finiteDb(stale ? -96 : visuals.vuTarget[channel]).toFixed(1)} dBFS`;
        $('peak' + side + 'Value').textContent = `${finiteDb(stale ? -96 : visuals.peakTarget[channel]).toFixed(1)} dBFS`;
      });
    }
  }
  requestAnimationFrame(animateVisual);
}

function updateVisualTargets(state) {
  const now = performance.now();
  lastMeterAt = now;
  if (state.meter_updates == null || state.meter_updates !== lastMeterUpdate) {
    lastMeterAdvanceAt = now;
    lastMeterUpdate = state.meter_updates;
  }
  const staleCapture = !state.clock_locked || state.capture_age_ms > STALE_MS;
  if (Array.isArray(state.spectrum)) {
    visuals.spectrumTarget = frequencies.map((_, index) => staleCapture ? -96 : finiteDb(state.spectrum[index]));
  }
  visuals.vuTarget[0] = staleCapture ? -96 : finiteDb(state.rms_l);
  visuals.vuTarget[1] = staleCapture ? -96 : finiteDb(state.rms_r);
  visuals.peakTarget[0] = staleCapture ? -96 : finiteDb(state.peak_l);
  visuals.peakTarget[1] = staleCapture ? -96 : finiteDb(state.peak_r);
}

function updateDashboard(state) {
  updateEq(state);
  $('clockState').textContent = state.clock_locked ? 'I2S locked' : 'Waiting for I2S';
  $('clockState').classList.toggle('live', state.clock_locked);
  $('signal').textContent = state.signal_present ? 'Audio present' : 'Silent';
  $('clients').textContent = state.stream_clients;
  $('sonosClients').textContent = state.sonos_stream_clients ?? '--';
  $('otherClients').textContent = state.other_stream_clients ?? '--';
  $('overruns').textContent = state.overruns;
  $('ip').textContent = state.ip;
  $('heap').textContent = `${Math.round(state.heap_free / 1024)} KB`;
  $('cpu').textContent = `${Number(state.cpu_percent).toFixed(1)}% at ${state.cpu_mhz} MHz`;
  $('cpuCores').textContent = `${Number(state.cpu0_percent).toFixed(1)}% / ${Number(state.cpu1_percent).toFixed(1)}%`;
  $('captureStack').textContent = `${state.capture_stack_free} bytes`;
  $('analysisStack').textContent = `${state.analysis_stack_free} bytes`;
  $('analysisRuns').textContent = state.analysis_runs;
  $('meterUpdates').textContent = `${state.meter_updates ?? '--'} / ${state.spectrum_updates ?? '--'}`;
  $('analysisTime').textContent = state.analysis_dsp_us == null ? '--'
    : `${(state.analysis_dsp_us / 1000).toFixed(2)} / ${(state.analysis_dsp_max_us / 1000).toFixed(2)} ms`;
  $('sourceRate').textContent = `${Number(state.source_rate_hz).toLocaleString()} Hz`;
  $('captured').textContent = `${Math.round(state.captured_frames / state.sample_rate)} s`;
  $('timeouts').textContent = state.read_timeouts;
  $('dmaLoss').textContent = state.dma_queue_overflows ?? '--';
  $('publishLoss').textContent = state.capture_publish_drops ?? '--';
  $('clockLoss').textContent = state.clock_loss_events ?? '--';
  $('captureAge').textContent = state.capture_age_ms == null ? '--'
    : state.capture_age_ms === 4294967295 ? 'Waiting for capture'
    : `${state.capture_age_ms} ms ago`;
  $('version').textContent = state.version;
  $('networkState').textContent = state.wifi_connected
    ? `Connected at ${state.ip} - ${state.wifi_rssi} dBm - power save off`
    : state.setup_ap ? 'Setup access point active' : 'Connecting to Wi-Fi...';
  $('wifiBssid').textContent = state.wifi_bssid || '--';
  $('wifiChannel').textContent = state.wifi_channel || '--';
  $('wifiReconnects').textContent = state.wifi_reconnects ?? '--';
  $('streamLink').href = state.stream_url;
  const sonosState = state.sonos_group_name
    ? `Automatic - ${state.sonos_group_name} - ${state.sonos_auto_state}`
    : `Automatic - ${state.sonos_auto_state}`;
  $('sonosAuto').textContent = state.sonos_recovery_attempts == null ? sonosState
    : `${sonosState} · Stream restart attempts: ${state.sonos_recovery_attempts}`;
  if (state.sonos_group_ip) {
    const select = $('sonosPlayer');
    let option = [...select.options].find(item => item.value === state.sonos_group_ip);
    if (!option) {
      option = document.createElement('option');
      option.value = state.sonos_group_ip;
      option.textContent = state.sonos_group_name || state.sonos_group_ip;
      select.appendChild(option);
    }
    if (!select.value) select.value = state.sonos_group_ip;
    $('playSonos').disabled = false;
    $('stopSonos').disabled = false;
  }
  const audio = $('localAudio');
  if (audio.getAttribute('src') !== state.stream_url) audio.src = state.stream_url;
  $('format').textContent = `${state.sample_rate / 1000} kHz - ${state.bits}-bit - ${state.channels === 2 ? 'stereo' : 'mono'} FLAC`;
  const now = performance.now();
  if (previousStatus && state.uptime_s >= previousStatus.uptime &&
      state.stream_frames >= previousStatus.streamFrames &&
      state.captured_frames >= previousStatus.capturedFrames) {
    const elapsed = (now - previousStatus.at) / 1000;
    const delivered = ((state.stream_frames - previousStatus.streamFrames) * 1024) / elapsed;
    $('delivery').textContent = `${Math.round(delivered).toLocaleString()} /s`;
    const captured = (state.captured_frames - previousStatus.capturedFrames) / elapsed;
    $('captureRate').textContent = `${Math.round(captured).toLocaleString()} frames/s`;
  } else {
    $('delivery').textContent = '--';
    $('captureRate').textContent = '--';
  }
  previousStatus = {at: now, uptime: state.uptime_s,
    streamFrames: state.stream_frames, capturedFrames: state.captured_frames};
  updateVisualTargets(state);
}

function renderEqBand({slider, output, leds}, gain) {
  slider.value = gain;
  const value = Number(slider.value);
  output.textContent = `${value > 0 ? '+' : ''}${value} dB`;
  leds.forEach((led, index) => {
    const level = 6 - index;
    led.classList.toggle('lit', level >= Math.min(0, value) && level <= Math.max(0, value));
    led.classList.toggle('position', level === value);
  });
}

function updateEq(state) {
  if (!Array.isArray(state.eq_gains) || state.eq_gains.length !== eqSliders.length) return;
  $('eqControls').disabled = eqSaving;
  if (!eqDirty && !eqSaving) {
    $('eqPreset').value = state.eq_preset;
    eqSliders.forEach((band, i) => {
      if (Number(band.slider.value) !== state.eq_gains[i]) renderEqBand(band, state.eq_gains[i]);
    });
  }
  const mode = !state.eq_applied ? 'Waiting for audio to apply EQ'
    : state.eq_preset === 'flat' ? 'Flat bypass active'
    : `${state.eq_preset.replaceAll('-', ' ')} active · ${state.eq_headroom_db.toFixed(1)} dB headroom`;
  $('eqSummary').textContent = `${mode} · Clipped samples since boot: ${state.eq_clipped_samples}`;
}

function scheduleStatus(delay = document.hidden ? HIDDEN_POLL_MS : POLL_MS) {
  clearTimeout(pollTimer);
  pollTimer = setTimeout(updateStatus, delay);
}

async function updateStatus() {
  if (pollInFlight) {
    scheduleStatus();
    return;
  }
  pollInFlight = true;
  const started = performance.now();
  const fullStatus = document.hidden || started >= nextStatusAt;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 1500);
  let failed = false;
  try {
    const response = await fetch(fullStatus ? '/api/status' : '/api/meters',
      {cache: 'no-store', signal: controller.signal});
    if (!response.ok) throw new Error(`Status ${response.status}`);
    const state = await response.json();
    if (fullStatus) {
      updateDashboard(state);
      nextStatusAt = performance.now() + STATUS_MS;
    } else {
      updateVisualTargets(state);
    }
  } catch (error) {
    failed = true;
    $('clockState').textContent = 'Dashboard disconnected';
    $('clockState').classList.remove('live');
  } finally {
    clearTimeout(timeout);
    pollInFlight = false;
    const cadence = document.hidden ? HIDDEN_POLL_MS : failed ? 1000 : POLL_MS;
    // One request in flight; use a start-to-start cadence, never catch-up bursts.
    scheduleStatus(failed ? cadence : Math.max(20, cadence - (performance.now() - started)));
  }
}

$('wifiForm').addEventListener('submit', async event => {
  event.preventDefault();
  $('networkState').textContent = 'Saving securely and restarting...';
  const body = new URLSearchParams(new FormData(event.currentTarget));
  const response = await fetch('/api/wifi', {method: 'POST', body});
  if (!response.ok) $('networkState').textContent = 'Unable to save Wi-Fi settings';
});

['31.5 Hz', '63 Hz', '125 Hz', '250 Hz', '500 Hz', '1 kHz', '2 kHz', '4 kHz', '8 kHz', '16 kHz']
  .forEach((frequency, index) => {
    const label = document.createElement('label');
    label.className = 'eq-band';
    label.textContent = frequency;
    const slider = document.createElement('input');
    slider.type = 'range';
    slider.name = `gain${index}`;
    slider.min = -6;
    slider.max = 6;
    slider.value = 0;
    slider.setAttribute('aria-label', `${frequency} gain in dB`);
    const track = document.createElement('span');
    track.className = 'eq-track';
    const ladder = document.createElement('span');
    ladder.className = 'eq-ladder';
    ladder.setAttribute('aria-hidden', 'true');
    const leds = Array.from({length: 13}, () => document.createElement('i'));
    leds[6].classList.add('zero');
    ladder.append(...leds);
    track.append(ladder, slider);
    const output = document.createElement('output');
    label.append(track, output);
    $('eqBands').appendChild(label);
    const band = {slider, output, leds};
    eqSliders.push(band);
    renderEqBand(band, 0);
    slider.addEventListener('input', () => {
      $('eqPreset').value = 'custom';
      renderEqBand(band, slider.value);
      eqDirty = true;
    });
  });
$('eqPreset').addEventListener('change', () => { eqDirty = true; });
$('eqForm').addEventListener('submit', async event => {
  event.preventDefault();
  if (eqSaving) return;
  const body = new URLSearchParams(new FormData(event.currentTarget));
  eqSaving = true;
  $('eqControls').disabled = true;
  const message = $('eqMessage');
  message.hidden = false;
  message.textContent = 'Applying EQ…';
  try {
    const response = await fetch('/api/eq', {method: 'POST', body,
      signal: AbortSignal.timeout(4000)});
    if (!response.ok) throw new Error(`EQ request failed: ${response.status}`);
    eqDirty = false;
    nextStatusAt = 0;
    message.hidden = true;
  } catch (error) {
    message.textContent = 'Could not apply EQ. Try again.';
  } finally {
    eqSaving = false;
    $('eqControls').disabled = false;
  }
});

$('scanWifi').addEventListener('click', async () => {
  const button = $('scanWifi');
  const results = $('wifiResults');
  button.disabled = true;
  button.textContent = 'Scanning...';
  results.innerHTML = '';
  try {
    const data = await (await fetch('/api/wifi/scan', {cache: 'no-store'})).json();
    data.networks.forEach(network => {
      const choice = document.createElement('button');
      choice.type = 'button';
      choice.className = 'secondary';
      choice.textContent = `${network.ssid} - ${network.rssi} dBm - ${network.secure ? 'secured' : 'open'}`;
      choice.addEventListener('click', () => {
        $('ssid').value = network.ssid;
        $('password').focus();
      });
      results.appendChild(choice);
    });
    if (!data.networks.length) results.textContent = 'No networks found';
  } catch (error) {
    results.textContent = 'Scan failed';
  } finally {
    button.disabled = false;
    button.textContent = 'Scan for networks';
  }
});

$('discoverSonos').addEventListener('click', async () => {
  $('sonosMessage').textContent = 'Discovering Sonos rooms and groups...';
  const response = await fetch('/api/sonos/discover');
  const result = await response.json();
  const select = $('sonosPlayer');
  select.innerHTML = '<option value="">Select group or room</option>';
  result.groups.forEach(group => {
    const option = document.createElement('option');
    option.value = group.ip;
    option.dataset.uuid = group.uuid;
    option.textContent = group.name;
    select.appendChild(option);
  });
  if (result.selected_ip) select.value = result.selected_ip;
  $('playSonos').disabled = !select.value;
  $('stopSonos').disabled = !select.value;
  $('sonosMessage').textContent = result.groups.length
    ? `${result.groups.length} room/group option(s) found`
    : 'No Sonos rooms or groups found';
});

$('sonosPlayer').addEventListener('change', async event => {
  $('playSonos').disabled = !event.target.value;
  $('stopSonos').disabled = !event.target.value;
  const option = event.target.selectedOptions[0];
  if (event.target.value) {
    await fetch('/api/sonos/select', {
      method: 'POST',
      body: new URLSearchParams({
        ip: event.target.value,
        uuid: option.dataset.uuid || '',
        name: option.textContent
      })
    });
    $('sonosMessage').textContent = `${option.textContent} saved as the automatic default`;
  }
});

async function sonosAction(action) {
  const ip = $('sonosPlayer').value;
  if (!ip) return;
  $('sonosMessage').textContent = `${action === 'play' ? 'Starting' : 'Stopping'} Sonos...`;
  const response = await fetch(`/api/sonos/${action}`, {
    method: 'POST',
    body: new URLSearchParams({ip})
  });
  $('sonosMessage').textContent = response.ok ? `Sonos ${action} command accepted` : `Sonos ${action} failed`;
}

document.querySelectorAll('[data-visual]').forEach(button => {
  button.addEventListener('click', () => setVisualMode(button.dataset.visual));
});
$('playSonos').addEventListener('click', () => sonosAction('play'));
$('spectrumGain').addEventListener('input', event => {
  visuals.spectrumGain = Math.max(-12, Math.min(24, Number(event.target.value) || 0));
  $('spectrumGainValue').textContent = `${visuals.spectrumGain > 0 ? '+' : ''}${visuals.spectrumGain} dB · visuals only`;
});
$('stopSonos').addEventListener('click', () => sonosAction('stop'));
$('fullscreenAudio').addEventListener('click', toggleAudioFullscreen);
document.addEventListener('fullscreenchange', syncAudioFullscreen);
document.addEventListener('keydown', audioFullscreenKeydown);
document.addEventListener('visibilitychange', () => scheduleStatus(0));

setVisualMode(visuals.mode);
drawSpectrum(visuals.spectrumDisplay);
drawVu($('vuLeft'), visuals.vuDisplay[0]);
drawVu($('vuRight'), visuals.vuDisplay[1]);
requestAnimationFrame(animateVisual);
updateStatus();
