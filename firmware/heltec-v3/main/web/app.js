const $ = id => document.getElementById(id);
const frequencies = ['63', '100', '160', '250', '400', '630', '1k', '1.6k', '2.5k', '4k', '6.3k', '8k', '10k', '12.5k', '16k', '20k'];
let lastStatus = null;

function levelToUnit(db) {
  return Math.max(0, Math.min(1, (db + 60) / 63));
}

function drawSpectrum(values) {
  const canvas = $('spectrum');
  const context = canvas.getContext('2d');
  const width = canvas.width;
  const height = canvas.height;
  const gap = 8;
  const barWidth = (width - gap * (values.length - 1)) / values.length;
  context.clearRect(0, 0, width, height);
  const gradient = context.createLinearGradient(0, height, 0, 0);
  gradient.addColorStop(0, '#d88420');
  gradient.addColorStop(.7, '#f2bd45');
  gradient.addColorStop(1, '#e3513c');
  values.forEach((db, index) => {
    const barHeight = levelToUnit(db) * (height - 34);
    const x = index * (barWidth + gap);
    context.fillStyle = gradient;
    context.fillRect(x, height - 26 - barHeight, barWidth, barHeight);
    context.fillStyle = '#b9b0a2';
    context.textAlign = 'center';
    context.font = '16px system-ui';
    context.fillText(frequencies[index], x + barWidth / 2, height - 6);
  });
}

function drawVu(canvas, db) {
  const context = canvas.getContext('2d');
  const width = canvas.width;
  const height = canvas.height;
  const cx = width / 2;
  const cy = height * .9;
  const radius = Math.min(width * .43, height * .78);
  context.clearRect(0, 0, width, height);
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
  const needleAngle = Math.PI * (.75 + levelToUnit(db) * 1.5);
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
  context.fillText(`${db.toFixed(1)} dBFS`, cx, height - 18);
}

async function updateStatus() {
  try {
    const response = await fetch('/api/status', {cache: 'no-store'});
    const state = await response.json();
    lastStatus = state;
    $('clockState').textContent = state.clock_locked ? 'I²S locked' : 'Waiting for I²S';
    $('clockState').classList.toggle('live', state.clock_locked);
    $('signal').textContent = state.signal_present ? 'Audio present' : 'Silent';
    $('clients').textContent = state.stream_clients;
    $('overruns').textContent = state.overruns;
    $('ip').textContent = state.ip;
    $('heap').textContent = `${Math.round(state.heap_free / 1024)} KB`;
    $('captured').textContent = `${Math.round(state.captured_frames / state.sample_rate)} s`;
    $('timeouts').textContent = state.read_timeouts;
    $('version').textContent = state.version;
    $('networkState').textContent = state.wifi_connected ? `Connected at ${state.ip}` : 'Setup access point active';
    $('streamLink').href = state.stream_url;
    $('sonosAuto').textContent = state.sonos_group_name
      ? `Automatic · ${state.sonos_group_name} · ${state.sonos_auto_state}`
      : `Automatic · ${state.sonos_auto_state}`;
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
    if (audio.getAttribute('src') !== state.stream_url) {
      audio.src = state.stream_url;
    }
    $('format').textContent = `${state.sample_rate / 1000} kHz · ${state.bits}-bit · ${state.channels === 2 ? 'stereo' : 'mono'} FLAC`;
    drawSpectrum(state.spectrum);
    drawVu($('vuLeft'), state.rms_l);
    drawVu($('vuRight'), state.rms_r);
  } catch (error) {
    $('clockState').textContent = 'Dashboard disconnected';
  }
}

$('wifiForm').addEventListener('submit', async event => {
  event.preventDefault();
  $('networkState').textContent = 'Saving securely and restarting…';
  const body = new URLSearchParams(new FormData(event.currentTarget));
  const response = await fetch('/api/wifi', {method: 'POST', body});
  if (!response.ok) $('networkState').textContent = 'Unable to save Wi‑Fi settings';
});

$('scanWifi').addEventListener('click', async () => {
  const button = $('scanWifi');
  const results = $('wifiResults');
  button.disabled = true;
  button.textContent = 'Scanning…';
  results.innerHTML = '';
  try {
    const data = await (await fetch('/api/wifi/scan', {cache: 'no-store'})).json();
    data.networks.forEach(network => {
      const choice = document.createElement('button');
      choice.type = 'button';
      choice.className = 'secondary';
      choice.textContent = `${network.ssid} · ${network.rssi} dBm${network.secure ? ' · secured' : ' · open'}`;
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
  $('sonosMessage').textContent = 'Discovering Sonos rooms and groups…';
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
    $('sonosMessage').textContent =
      `${option.textContent} saved as the automatic default`;
  }
});

async function sonosAction(action) {
  const ip = $('sonosPlayer').value;
  if (!ip) return;
  $('sonosMessage').textContent = `${action === 'play' ? 'Starting' : 'Stopping'} Sonos…`;
  const response = await fetch(`/api/sonos/${action}`, {
    method: 'POST',
    body: new URLSearchParams({ip})
  });
  $('sonosMessage').textContent = response.ok ? `Sonos ${action} command accepted` : `Sonos ${action} failed`;
}

$('playSonos').addEventListener('click', () => sonosAction('play'));
$('stopSonos').addEventListener('click', () => sonosAction('stop'));

drawSpectrum(new Array(16).fill(-96));
drawVu($('vuLeft'), -96);
drawVu($('vuRight'), -96);
updateStatus();
setInterval(updateStatus, 250);
