const $ = id => document.getElementById(id);
const frequencies = ['63', '100', '160', '250', '400', '630', '1k', '1.6k', '2.5k', '4k', '6.3k', '8k', '10k', '12.5k', '16k', '20k'];
const POLL_MS = 250;
const HIDDEN_POLL_MS = 2000;
const FRAME_MS = 1000 / 30;
const vuFaceCache = new WeakMap();
let previousStatus = null;
let pollTimer = 0;
let pollInFlight = false;
let lastFrameAt = 0;

const visuals = {
  mode: localStorage.getItem('cdts-visual') === 'spectrum' ? 'spectrum' : 'vu',
  spectrumTarget: new Array(16).fill(-96),
  spectrumDisplay: new Array(16).fill(-96),
  vuTarget: [-96, -96],
  vuDisplay: [-96, -96]
};

function finiteDb(value) {
  return Number.isFinite(Number(value)) ? Math.max(-96, Math.min(3, Number(value))) : -96;
}

function levelToUnit(db) {
  return Math.max(0, Math.min(1, (finiteDb(db) + 60) / 63));
}

function vuToUnit(db) {
  return Math.max(0, Math.min(1, (finiteDb(db) + 24) / 27));
}

function approach(current, target, elapsedMs, attackMs, releaseMs) {
  const timeConstant = target > current ? attackMs : releaseMs;
  return current + (target - current) * (1 - Math.exp(-elapsedMs / timeConstant));
}

function drawSpectrum(values) {
  const canvas = $('spectrum');
  const context = canvas.getContext('2d');
  const width = canvas.width;
  const height = canvas.height;
  const gap = 8;
  const barWidth = (width - gap * (values.length - 1)) / values.length;
  context.clearRect(0, 0, width, height);
  const background = context.createLinearGradient(0, 0, 0, height);
  background.addColorStop(0, '#24211d');
  background.addColorStop(1, '#121211');
  context.fillStyle = background;
  context.fillRect(0, 0, width, height);
  context.strokeStyle = '#ffffff12';
  context.lineWidth = 1;
  for (let line = 0; line <= 4; line++) {
    const y = 8 + line * ((height - 42) / 4);
    context.beginPath();
    context.moveTo(0, y);
    context.lineTo(width, y);
    context.stroke();
  }
  const gradient = context.createLinearGradient(0, height, 0, 0);
  gradient.addColorStop(0, '#35a77a');
  gradient.addColorStop(.48, '#f2bd45');
  gradient.addColorStop(.82, '#ef7d2a');
  gradient.addColorStop(1, '#e3513c');
  values.forEach((db, index) => {
    const barHeight = levelToUnit(db) * (height - 42);
    const x = index * (barWidth + gap);
    context.fillStyle = gradient;
    context.fillRect(x, height - 28 - barHeight, barWidth, barHeight);
    context.fillStyle = '#b9b0a2';
    context.textAlign = 'center';
    context.font = '16px system-ui';
    context.fillText(frequencies[index], x + barWidth / 2, height - 7);
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
  const cy = height * .9;
  const radius = Math.min(width * .43, height * .78);
  const face = context.createLinearGradient(0, 0, 0, height);
  face.addColorStop(0, '#f8c965');
  face.addColorStop(1, '#b86a19');
  context.fillStyle = face;
  context.strokeStyle = '#080808';
  context.lineWidth = 12;
  context.fillRect(8, 8, width - 16, height - 16);
  context.strokeRect(8, 8, width - 16, height - 16);
  for (let index = 0; index <= 10; index++) {
    const angle = Math.PI * (.75 + index * .15);
    const inner = radius - (index % 5 === 0 ? 22 : 14);
    context.beginPath();
    context.moveTo(cx + Math.cos(angle) * inner, cy + Math.sin(angle) * inner);
    context.lineTo(cx + Math.cos(angle) * radius, cy + Math.sin(angle) * radius);
    context.strokeStyle = index > 7 ? '#a82418' : '#292016';
    context.lineWidth = 2;
    context.stroke();
  }
  context.fillStyle = '#292016';
  context.font = 'bold 16px system-ui';
  context.textAlign = 'right';
  context.fillText('VU', width - 22, height - 22);
  vuFaceCache.set(canvas, faceCanvas);
  return faceCanvas;
}

function drawVu(canvas, db) {
  const context = canvas.getContext('2d');
  const width = canvas.width;
  const height = canvas.height;
  const cx = width / 2;
  const cy = height * .9;
  const radius = Math.min(width * .43, height * .78);
  const face = vuFaceCache.get(canvas) || createVuFace(canvas);
  context.clearRect(0, 0, width, height);
  context.drawImage(face, 0, 0);
  const needleAngle = Math.PI * (.75 + vuToUnit(db) * 1.5);
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
  context.fillStyle = '#292016';
  context.font = '17px system-ui';
  context.textAlign = 'center';
  context.fillText(`${finiteDb(db).toFixed(1)} dBFS`, cx, height - 18);
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
  if (!document.hidden && now - lastFrameAt >= FRAME_MS) {
    lastFrameAt = now;
    if (visuals.mode === 'spectrum') {
      visuals.spectrumDisplay = visuals.spectrumDisplay.map((current, index) =>
        approach(current, visuals.spectrumTarget[index], elapsed, 55, 190));
      drawSpectrum(visuals.spectrumDisplay);
    } else {
      visuals.vuDisplay = visuals.vuDisplay.map((current, index) =>
        approach(current, visuals.vuTarget[index], elapsed, 55, 260));
      drawVu($('vuLeft'), visuals.vuDisplay[0]);
      drawVu($('vuRight'), visuals.vuDisplay[1]);
    }
  }
  requestAnimationFrame(animateVisual);
}

function updateVisualTargets(state) {
  if (Array.isArray(state.spectrum)) {
    visuals.spectrumTarget = frequencies.map((_, index) => finiteDb(state.spectrum[index]));
  }
  visuals.vuTarget[0] = finiteDb(state.rms_l);
  visuals.vuTarget[1] = finiteDb(state.rms_r);
}

function updateDashboard(state) {
  $('clockState').textContent = state.clock_locked ? 'I2S locked' : 'Waiting for I2S';
  $('clockState').classList.toggle('live', state.clock_locked);
  $('signal').textContent = state.signal_present ? 'Audio present' : 'Silent';
  $('clients').textContent = state.stream_clients;
  $('overruns').textContent = state.overruns;
  $('ip').textContent = state.ip;
  $('heap').textContent = `${Math.round(state.heap_free / 1024)} KB`;
  $('cpu').textContent = `${Number(state.cpu_percent).toFixed(1)}% at ${state.cpu_mhz} MHz`;
  $('cpuCores').textContent = `${Number(state.cpu0_percent).toFixed(1)}% / ${Number(state.cpu1_percent).toFixed(1)}%`;
  $('captureStack').textContent = `${state.capture_stack_free} bytes`;
  $('analysisStack').textContent = `${state.analysis_stack_free} bytes`;
  $('analysisRuns').textContent = state.analysis_runs;
  $('sourceRate').textContent = `${Number(state.source_rate_hz).toLocaleString()} Hz`;
  $('captured').textContent = `${Math.round(state.captured_frames / state.sample_rate)} s`;
  $('timeouts').textContent = state.read_timeouts;
  $('version').textContent = state.version;
  $('networkState').textContent = state.wifi_connected
    ? `Connected at ${state.ip} - ${state.wifi_rssi} dBm - power save off`
    : 'Setup access point active';
  $('streamLink').href = state.stream_url;
  $('sonosAuto').textContent = state.sonos_group_name
    ? `Automatic - ${state.sonos_group_name} - ${state.sonos_auto_state}`
    : `Automatic - ${state.sonos_auto_state}`;
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
  if (previousStatus && state.stream_frames >= previousStatus.streamFrames) {
    const elapsed = (now - previousStatus.at) / 1000;
    const delivered = ((state.stream_frames - previousStatus.streamFrames) * 1024) / elapsed;
    $('delivery').textContent = `${Math.round(delivered).toLocaleString()} /s`;
  }
  previousStatus = {at: now, streamFrames: state.stream_frames};
  updateVisualTargets(state);
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
  try {
    const response = await fetch('/api/status', {cache: 'no-store'});
    if (!response.ok) throw new Error(`Status ${response.status}`);
    updateDashboard(await response.json());
  } catch (error) {
    $('clockState').textContent = 'Dashboard disconnected';
    $('clockState').classList.remove('live');
  } finally {
    pollInFlight = false;
    scheduleStatus();
  }
}

$('wifiForm').addEventListener('submit', async event => {
  event.preventDefault();
  $('networkState').textContent = 'Saving securely and restarting...';
  const body = new URLSearchParams(new FormData(event.currentTarget));
  const response = await fetch('/api/wifi', {method: 'POST', body});
  if (!response.ok) $('networkState').textContent = 'Unable to save Wi-Fi settings';
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
$('stopSonos').addEventListener('click', () => sonosAction('stop'));
document.addEventListener('visibilitychange', () => scheduleStatus(0));

setVisualMode(visuals.mode);
drawSpectrum(visuals.spectrumDisplay);
drawVu($('vuLeft'), visuals.vuDisplay[0]);
drawVu($('vuRight'), visuals.vuDisplay[1]);
requestAnimationFrame(animateVisual);
updateStatus();
