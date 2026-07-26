#pragma once

namespace cdts::web_assets {

inline constexpr char kIndex[] = R"CDTS(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>CD to Sonos</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <header>
    <div>
      <p class="eyebrow">NAD C541i · lossless network transport</p>
      <h1>CD to Sonos</h1>
    </div>
    <div class="status-pills">
      <span id="radioState" class="pill safe">LoRa disabled</span>
      <span id="clockState" class="pill">Waiting for I²S</span>
    </div>
  </header>

  <main>
    <section class="meters panel">
      <div class="visual-heading">
        <div><p class="eyebrow">Live audio</p><h2>Digital input monitor</h2></div>
        <p id="format">44.1 kHz · 16-bit · stereo FLAC</p>
      </div>
      <canvas id="spectrum" width="1000" height="230" aria-label="Live audio spectrum"></canvas>
      <div class="vu-pair">
        <div class="vu"><canvas id="vuLeft" width="420" height="210"></canvas><span>L</span></div>
        <div class="vu"><canvas id="vuRight" width="420" height="210"></canvas><span>R</span></div>
      </div>
    </section>

    <section class="grid">
      <article class="panel">
        <p class="eyebrow">Lossless stream</p><h2>Playback</h2>
        <dl>
          <div><dt>Signal</dt><dd id="signal">Silent</dd></div>
          <div><dt>Clients</dt><dd id="clients">0</dd></div>
          <div><dt>Buffer overruns</dt><dd id="overruns">0</dd></div>
        </dl>
        <audio id="localAudio" controls preload="none"></audio>
        <a id="streamLink" class="button" href="#" target="_blank">Open FLAC stream</a>
      </article>

      <article class="panel">
        <p class="eyebrow">Sonos</p><h2>Sonos playback</h2>
        <label for="sonosPlayer">Group or room</label>
        <select id="sonosPlayer"><option value="">Select group or room</option></select>
        <div class="actions">
          <button id="discoverSonos" type="button">Discover</button>
          <button id="playSonos" type="button" disabled>Play</button>
          <button id="stopSonos" class="secondary" type="button" disabled>Stop</button>
        </div>
        <p id="sonosAuto" class="help">Automatic playback is startingâ€¦</p>
        <p id="sonosMessage" class="help">Choose a room or existing Sonos group. Bonding is never changed.</p>
      </article>

      <article class="panel">
        <p class="eyebrow">Network</p><h2>Wi-Fi setup</h2>
        <form id="wifiForm">
          <label for="ssid">Network name</label>
          <input id="ssid" name="ssid" maxlength="32" required autocomplete="off">
          <button id="scanWifi" class="secondary" type="button">Scan for networks</button>
          <div id="wifiResults" class="network-results"></div>
          <label for="password">Password</label>
          <input id="password" name="password" type="password" maxlength="64" autocomplete="new-password">
          <button type="submit">Save and restart</button>
        </form>
        <p id="networkState" class="help">Checking network…</p>
      </article>

      <article class="panel">
        <p class="eyebrow">Diagnostics</p><h2>System</h2>
        <dl>
          <div><dt>IP address</dt><dd id="ip">—</dd></div>
          <div><dt>Free heap</dt><dd id="heap">—</dd></div>
          <div><dt>CPU load</dt><dd id="cpu">—</dd></div>
          <div><dt>CPU cores</dt><dd id="cpuCores">—</dd></div>
          <div><dt>Stream delivery</dt><dd id="delivery">—</dd></div>
          <div><dt>Source LRCLK</dt><dd id="sourceRate">—</dd></div>
          <div><dt>Captured</dt><dd id="captured">—</dd></div>
          <div><dt>I²S timeouts</dt><dd id="timeouts">0</dd></div>
          <div><dt>Capture stack free</dt><dd id="captureStack">—</dd></div>
          <div><dt>Analysis stack free</dt><dd id="analysisStack">—</dd></div>
          <div><dt>Analysis updates</dt><dd id="analysisRuns">0</dd></div>
          <div><dt>Firmware</dt><dd id="version">—</dd></div>
        </dl>
      </article>
    </section>
  </main>
  <script src="/app.js"></script>
</body>
</html>
)CDTS";

inline constexpr char kStyle[] = R"CDTS(:root {
  color-scheme: dark;
  --bg: #151515; --panel: #20201f; --edge: #45433e;
  --text: #f3eee3; --muted: #b9b0a2; --amber: #f2a52b;
  --red: #df543f; --green: #68b886;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  background: repeating-linear-gradient(0deg, transparent 0 3px, #ffffff05 3px 4px), var(--bg);
  color: var(--text);
  font: 15px/1.45 system-ui, sans-serif;
}
header, main { width: min(1180px, calc(100% - 32px)); margin-inline: auto; }
header { display: flex; justify-content: space-between; align-items: center; gap: 20px; padding: 28px 0 18px; }
h1, h2, p { margin-top: 0; }
h1 { margin-bottom: 0; font-size: clamp(30px, 5vw, 54px); font-weight: 500; letter-spacing: -.04em; }
h2 { margin-bottom: 18px; font-weight: 500; }
.eyebrow { margin-bottom: 4px; color: var(--amber); text-transform: uppercase; letter-spacing: .13em; font-size: 12px; }
.status-pills, .actions { display: flex; gap: 8px; flex-wrap: wrap; }
.pill { padding: 7px 11px; border: 1px solid var(--edge); border-radius: 999px; color: var(--muted); }
.pill.safe { border-color: var(--green); color: var(--green); }
.pill.live { border-color: var(--amber); color: var(--amber); }
.panel {
  border: 1px solid var(--edge); border-radius: 10px;
  background: linear-gradient(145deg, #ffffff08, transparent), var(--panel);
  padding: 22px; box-shadow: inset 0 1px #ffffff10, 0 18px 40px #0005;
}
.meters { margin-bottom: 18px; }
.visual-heading { display: flex; justify-content: space-between; gap: 20px; align-items: end; }
.visual-heading h2 { margin-bottom: 0; }
.visual-heading > p { color: var(--muted); }
#spectrum { width: 100%; height: 170px; display: block; margin: 12px 0 4px; }
.vu-pair { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.vu { position: relative; }
.vu canvas { display: block; width: 100%; height: auto; }
.vu span { position: absolute; bottom: 8px; left: 50%; color: var(--muted); }
.grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 18px; padding-bottom: 32px; }
dl { margin: 0 0 18px; }
dl div { display: flex; justify-content: space-between; gap: 16px; padding: 8px 0; border-bottom: 1px solid var(--edge); }
dt { color: var(--muted); }
dd { margin: 0; font-variant-numeric: tabular-nums; text-align: right; }
label { display: block; color: var(--muted); margin: 12px 0 5px; }
input, select, button, .button {
  border: 1px solid var(--edge); border-radius: 6px; background: #171716;
  color: var(--text); padding: 10px 12px; font: inherit;
}
input, select { width: 100%; }
button, .button { cursor: pointer; display: inline-block; text-decoration: none; margin-top: 12px; }
button:not(.secondary), .button { background: var(--amber); border-color: var(--amber); color: #17110a; }
button:disabled { opacity: .42; cursor: not-allowed; }
.secondary, .help { color: var(--muted); }
.network-results { display: grid; gap: 6px; margin-top: 8px; }
.network-results button { width: 100%; margin: 0; text-align: left; }
audio { width: 100%; margin: 4px 0 8px; }
.help { margin: 12px 0 0; }
@media (max-width: 720px) {
  header, .visual-heading { align-items: flex-start; flex-direction: column; }
  .grid, .vu-pair { grid-template-columns: 1fr; }
  .visual-heading > p { margin-bottom: 0; }
}
)CDTS";

inline constexpr char kApp[] = R"CDTS(const $ = id => document.getElementById(id);
const frequencies = ['63','100','160','250','400','630','1k','1.6k','2.5k','4k','6.3k','8k','10k','12.5k','16k','20k'];
const levelToUnit = db => Math.max(0, Math.min(1, (db + 60) / 63));
let previousStatus = null;

function drawSpectrum(values) {
  const canvas = $('spectrum'), context = canvas.getContext('2d');
  const width = canvas.width, height = canvas.height, gap = 8;
  const barWidth = (width - gap * (values.length - 1)) / values.length;
  context.clearRect(0, 0, width, height);
  const gradient = context.createLinearGradient(0, height, 0, 0);
  gradient.addColorStop(0, '#d88420'); gradient.addColorStop(.7, '#f2bd45'); gradient.addColorStop(1, '#e3513c');
  values.forEach((db, index) => {
    const barHeight = levelToUnit(db) * (height - 34), x = index * (barWidth + gap);
    context.fillStyle = gradient; context.fillRect(x, height - 26 - barHeight, barWidth, barHeight);
    context.fillStyle = '#b9b0a2'; context.textAlign = 'center'; context.font = '16px system-ui';
    context.fillText(frequencies[index], x + barWidth / 2, height - 6);
  });
}

function drawVu(canvas, db) {
  const context = canvas.getContext('2d'), width = canvas.width, height = canvas.height;
  const cx = width / 2, cy = height * .9, radius = Math.min(width * .43, height * .78);
  context.clearRect(0, 0, width, height);
  const face = context.createLinearGradient(0, 0, 0, height);
  face.addColorStop(0, '#f8c965'); face.addColorStop(1, '#b86a19');
  context.fillStyle = face; context.strokeStyle = '#080808'; context.lineWidth = 12;
  context.fillRect(8, 8, width - 16, height - 16); context.strokeRect(8, 8, width - 16, height - 16);
  for (let index = 0; index <= 10; index++) {
    const angle = Math.PI * (.75 + index * .15), inner = radius - (index % 5 === 0 ? 22 : 14);
    context.beginPath(); context.moveTo(cx + Math.cos(angle) * inner, cy + Math.sin(angle) * inner);
    context.lineTo(cx + Math.cos(angle) * radius, cy + Math.sin(angle) * radius);
    context.strokeStyle = index > 7 ? '#a82418' : '#292016'; context.lineWidth = 2; context.stroke();
  }
  const needleAngle = Math.PI * (.75 + levelToUnit(db) * 1.5);
  context.beginPath(); context.moveTo(cx, cy);
  context.lineTo(cx + Math.cos(needleAngle) * (radius - 8), cy + Math.sin(needleAngle) * (radius - 8));
  context.strokeStyle = '#17100b'; context.lineWidth = 3; context.stroke();
  context.beginPath(); context.arc(cx, cy, 9, 0, Math.PI * 2); context.fillStyle = '#17100b'; context.fill();
  context.fillStyle = '#292016'; context.font = '17px system-ui'; context.textAlign = 'center';
  context.fillText(`${db.toFixed(1)} dBFS`, cx, height - 18);
}

async function updateStatus() {
  try {
    const response = await fetch('/api/status', {cache: 'no-store'}), state = await response.json();
    $('clockState').textContent = state.clock_locked ? 'I²S locked' : 'Waiting for I²S';
    $('clockState').classList.toggle('live', state.clock_locked);
    $('signal').textContent = state.signal_present ? 'Audio present' : 'Silent';
    $('clients').textContent = state.stream_clients; $('overruns').textContent = state.overruns;
    $('ip').textContent = state.ip; $('heap').textContent = `${Math.round(state.heap_free / 1024)} KB`;
    $('cpu').textContent = `${state.cpu_percent.toFixed(1)}% at ${state.cpu_mhz} MHz`;
    $('cpuCores').textContent = `${state.cpu0_percent.toFixed(1)}% / ${state.cpu1_percent.toFixed(1)}%`;
    $('captureStack').textContent = `${state.capture_stack_free} bytes`;
    $('analysisStack').textContent = `${state.analysis_stack_free} bytes`;
    $('analysisRuns').textContent = state.analysis_runs;
    $('sourceRate').textContent = `${state.source_rate_hz.toLocaleString()} Hz`;
    $('captured').textContent = `${Math.round(state.captured_frames / state.sample_rate)} s`;
    $('timeouts').textContent = state.read_timeouts; $('version').textContent = state.version;
    $('networkState').textContent = state.wifi_connected
      ? `Connected at ${state.ip} · ${state.wifi_rssi} dBm · power save off`
      : 'Setup access point active';
    $('streamLink').href = state.stream_url;
    $('sonosAuto').textContent = state.sonos_group_name
      ? `Automatic Â· ${state.sonos_group_name} Â· ${state.sonos_auto_state}`
      : `Automatic Â· ${state.sonos_auto_state}`;
    if (state.sonos_group_ip) {
      const select = $('sonosPlayer');
      let option = [...select.options].find(item => item.value === state.sonos_group_ip);
      if (!option) {
        option = document.createElement('option'); option.value = state.sonos_group_ip;
        option.textContent = state.sonos_group_name || state.sonos_group_ip;
        select.appendChild(option);
      }
      if (!select.value) select.value = state.sonos_group_ip;
      $('playSonos').disabled = false; $('stopSonos').disabled = false;
    }
    const audio = $('localAudio');
    if (audio.getAttribute('src') !== state.stream_url) audio.src = state.stream_url;
    $('format').textContent = `${state.sample_rate / 1000} kHz · ${state.bits}-bit · ${state.channels === 2 ? 'stereo' : 'mono'} FLAC`;
    const now = performance.now();
    if (previousStatus && state.stream_frames >= previousStatus.streamFrames) {
      const elapsed = (now - previousStatus.at) / 1000;
      const delivered = ((state.stream_frames - previousStatus.streamFrames) * 1024) / elapsed;
      $('delivery').textContent = `${Math.round(delivered).toLocaleString()} /s`;
    }
    previousStatus = {at: now, streamFrames: state.stream_frames};
    drawSpectrum(state.spectrum); drawVu($('vuLeft'), state.rms_l); drawVu($('vuRight'), state.rms_r);
  } catch (_) { $('clockState').textContent = 'Dashboard disconnected'; }
}

$('wifiForm').addEventListener('submit', async event => {
  event.preventDefault(); $('networkState').textContent = 'Saving securely and restarting…';
  const response = await fetch('/api/wifi', {method: 'POST', body: new URLSearchParams(new FormData(event.currentTarget))});
  if (!response.ok) $('networkState').textContent = 'Unable to save Wi-Fi settings';
});
$('scanWifi').addEventListener('click', async () => {
  const button = $('scanWifi'), results = $('wifiResults');
  button.disabled = true; button.textContent = 'Scanning…'; results.innerHTML = '';
  try {
    const data = await (await fetch('/api/wifi/scan', {cache: 'no-store'})).json();
    data.networks.forEach(network => {
      const choice = document.createElement('button');
      choice.type = 'button'; choice.className = 'secondary';
      choice.textContent = `${network.ssid} · ${network.rssi} dBm${network.secure ? ' · secured' : ' · open'}`;
      choice.addEventListener('click', () => { $('ssid').value = network.ssid; $('password').focus(); });
      results.appendChild(choice);
    });
    if (!data.networks.length) results.textContent = 'No networks found';
  } catch (_) { results.textContent = 'Scan failed'; }
  finally { button.disabled = false; button.textContent = 'Scan for networks'; }
});
$('discoverSonos').addEventListener('click', async () => {
  $('sonosMessage').textContent = 'Discovering Sonos rooms and groups…';
  const result = await (await fetch('/api/sonos/discover')).json(), select = $('sonosPlayer');
  select.innerHTML = '<option value="">Select group or room</option>';
  result.groups.forEach(group => {
    const option = document.createElement('option'); option.value = group.ip;
    option.dataset.uuid = group.uuid; option.textContent = group.name; select.appendChild(option);
  });
  if (result.selected_ip) select.value = result.selected_ip;
  $('playSonos').disabled = !select.value; $('stopSonos').disabled = !select.value;
  $('sonosMessage').textContent = result.groups.length
    ? `${result.groups.length} room/group option(s) found`
    : 'No Sonos rooms or groups found';
});
$('sonosPlayer').addEventListener('change', async event => {
  $('playSonos').disabled = !event.target.value; $('stopSonos').disabled = !event.target.value;
  const option = event.target.selectedOptions[0];
  if (event.target.value) {
    await fetch('/api/sonos/select', {method: 'POST', body: new URLSearchParams({
      ip: event.target.value, uuid: option.dataset.uuid || '', name: option.textContent
    })});
    $('sonosMessage').textContent = `${option.textContent} saved as the automatic default`;
  }
});
async function sonosAction(action) {
  const ip = $('sonosPlayer').value; if (!ip) return;
  $('sonosMessage').textContent = `${action === 'play' ? 'Starting' : 'Stopping'} Sonos…`;
  const response = await fetch(`/api/sonos/${action}`, {method: 'POST', body: new URLSearchParams({ip})});
  $('sonosMessage').textContent = response.ok ? `Sonos ${action} command accepted` : `Sonos ${action} failed`;
}
$('playSonos').addEventListener('click', () => sonosAction('play'));
$('stopSonos').addEventListener('click', () => sonosAction('stop'));
drawSpectrum(new Array(16).fill(-96)); drawVu($('vuLeft'), -96); drawVu($('vuRight'), -96);
updateStatus(); setInterval(updateStatus, 250);
)CDTS";

}  // namespace cdts::web_assets
