let csrf = null;
let settings = null;
const $ = (id) => document.getElementById(id);

async function api(url, options = {}) {
  options.headers = {...(options.headers || {})};
  if (csrf) options.headers['X-CSRF-Token'] = csrf;
  if (options.body) options.headers['Content-Type'] = 'application/json';
  const response = await fetch(url, options);
  if (!response.ok) {
    const error = await response.json().catch(() => ({detail: response.statusText}));
    throw new Error(error.detail);
  }
  return response.json();
}

async function showApp() {
  $('auth').hidden = true;
  $('app').hidden = false;
  settings = await api('/api/settings');
  fillSettings();
  discoverRooms();
  await refresh();
  setInterval(refresh, 2500);
}

async function boot() {
  const bootstrap = await api('/api/bootstrap');
  $('version').textContent = `v${bootstrap.version}`;
  if (bootstrap.authenticated) {
    csrf = bootstrap.csrf;
    await showApp();
    return;
  }
  $('authTitle').textContent = bootstrap.setup_required ? 'Create administrator' : 'CD to Sonos';
  $('authHelp').textContent = bootstrap.setup_required
    ? 'Choose a password of at least 12 characters.'
    : 'Sign in to manage the appliance.';
  $('authForm').dataset.mode = bootstrap.setup_required ? 'setup' : 'login';
}

$('authForm').addEventListener('submit', async (event) => {
  event.preventDefault();
  try {
    const result = await api(`/api/${event.currentTarget.dataset.mode}`, {
      method: 'POST',
      body: JSON.stringify({username: $('username').value, password: $('password').value})
    });
    csrf = result.csrf;
    await showApp();
  } catch (error) {
    $('authError').textContent = error.message;
  }
});

async function refresh() {
  try {
    const status = await api('/api/status');
    const audio = status.audio;
    const system = status.system;
    liveStreamDbfs = Number.isFinite(audio.dbfs) ? audio.dbfs : -90;
    liveStreamConnected = Boolean(audio.connected);
    syncStreamMonitor();
    const history = Array.isArray(audio.history) ? audio.history : [];
    const unseenLevels = history.filter((point) => Number(point.time) > liveLevelLastTimestamp);
    if (unseenLevels.length) {
      liveLevelLastTimestamp = Math.max(...unseenLevels.map((point) => Number(point.time)));
      liveLevelQueue.push(...unseenLevels.map((point) => Number(point.dbfs)).filter(Number.isFinite));
      if (liveLevelQueue.length > 35) liveLevelQueue = liveLevelQueue.slice(-35);
    }
    if (visualizerMode === 'vu' && !spectrumAnimation) drawSpectrum();
    $('headline').textContent = audio.connected ? 'CD stream active' : 'Ready for CD input';
    $('subhead').textContent = status.stereo_label;
    $('level').textContent = `${audio.dbfs.toFixed(1)} dBFS`;
    $('peak').textContent = `${audio.peak_dbfs.toFixed(1)} dBFS`;
    $('channels').textContent = status.capture.channels;
    $('meter').style.width = `${Math.max(0, Math.min(100, (audio.dbfs + 60) / 60 * 100))}%`;
    $('clip').textContent = audio.clipping ? 'CLIPPING — reduce level' : 'No clipping';
    $('clip').style.color = audio.clipping ? 'var(--danger)' : '';
    $('streamStatus').textContent = audio.connected ? 'Running' : 'Stopped';
    $('codec').textContent = audio.active_codec ? audio.active_codec.toUpperCase() : '—';
    $('fallback').textContent = audio.fallback_reason || 'None';
    $('dropouts').textContent = audio.dropouts;
    $('cpu').textContent = `${system.cpu_percent}%`;
    $('memory').textContent = `${system.memory_percent}%`;
    $('disk').textContent = `${system.disk_percent}%`;
    $('temp').textContent = system.temperature_c == null ? 'Unavailable' : `${system.temperature_c} °C`;
  } catch (error) {
    $('headline').textContent = 'Connection problem';
    $('subhead').textContent = error.message;
  }
}

function fillSettings() {
  $('alsa').value = settings.capture.alsa_device;
  $('channelSetting').value = settings.capture.channels;
  $('stereoVerified').checked = settings.capture.stereo_verified;
  $('threshold').value = settings.audio.threshold_dbfs;
  $('startDelay').value = settings.audio.start_delay_seconds;
  $('silence').value = settings.audio.silence_timeout_seconds;
  $('calibrated').checked = settings.audio.calibration_approved;
  $('automation').checked = settings.audio.automation_enabled;
  $('restore').checked = settings.sonos.restore_previous;
  $('preserve').checked = settings.sonos.preserve_volume;
  $('codecMode').value = settings.audio.codec_mode;
  $('clipStop').checked = settings.audio.stop_on_clipping;
}

document.querySelectorAll('[data-view]').forEach((button) => {
  button.addEventListener('click', () => {
    document.querySelectorAll('.view').forEach((view) => { view.hidden = true; });
    $(button.dataset.view).hidden = false;
  });
});

async function action(url, body) {
  try {
    await api(url, {method: 'POST', body: body ? JSON.stringify(body) : undefined});
    await refresh();
  } catch (error) {
    alert(error.message);
  }
}

$('start').addEventListener('click', () => action('/api/playback/start', {codec: 'flac'}));
$('startMp3').addEventListener('click', () => action('/api/playback/start', {codec: 'mp3'}));
$('stop').addEventListener('click', () => action('/api/playback/stop'));
$('localFlac').addEventListener('click', async () => {
  const result = await api('/api/stream/start', {method: 'POST', body: JSON.stringify({codec: 'flac'})});
  preparedLocalCodec = 'flac';
  preparedLocalUrl = result.url;
  $('localPlayer').src = `${result.url}?cache=${Date.now()}`;
  $('streamLink').href = result.direct_url;
  $('localPlayer').load();
  $('localPlay').hidden = false;
  $('localHelp').textContent = 'FLAC mount prepared. This browser may not decode live FLAC; use MP3 if Play remains silent.';
  $('spectrumState').textContent = 'Ready · press Play local audio';
  await refresh();
});
$('localMp3').addEventListener('click', async () => {
  const result = await api('/api/stream/start', {method: 'POST', body: JSON.stringify({codec: 'mp3'})});
  preparedLocalCodec = 'mp3';
  preparedLocalUrl = `${result.url}?cache=${Date.now()}`;
  $('localPlayer').removeAttribute('src');
  $('localPlayer').load();
  $('streamLink').href = result.direct_url;
  $('localPlayer').load();
  $('localPlay').hidden = false;
  $('localHelp').textContent = 'Browser-compatible MP3 is ready. Press Play local audio.';
  $('spectrumState').textContent = 'Ready · press Play local audio';
  await refresh();
});
$('localPlay').addEventListener('click', playPreparedLocalAudio);
$('localStop').addEventListener('click', stopPcmPlayback);
$('test').addEventListener('click', async () => {
  const result = await api('/api/playback/test', {method: 'POST'});
  $('testMessage').textContent = result.message;
});

async function discoverRooms() {
  $('roomMessage').textContent = 'Discovering your Sonos system…';
  try {
    const rooms = await api('/api/sonos/rooms');
    $('room').innerHTML = '<option value="">Choose a room</option>' + rooms.map((room) =>
      `<option value="${room.uid}">${room.name} — ${room.members.join(', ')}</option>`).join('');
    $('room').value = settings.sonos.coordinator_uid;
    $('roomMessage').textContent = rooms.length ? `${rooms.length} room${rooms.length === 1 ? '' : 's'} available` : 'No Sonos rooms found';
  } catch (error) {
    $('roomMessage').textContent = `Discovery failed: ${error.message}`;
  }
}

$('discover').addEventListener('click', discoverRooms);
$('room').addEventListener('change', async () => {
  if (!$('room').value) return;
  settings.sonos.coordinator_uid = $('room').value;
  try {
    await api('/api/settings', {method: 'PUT', body: JSON.stringify(settings)});
    $('roomMessage').textContent = `Saved · ${$('room').selectedOptions[0].textContent}`;
  } catch (error) {
    $('roomMessage').textContent = `Could not save: ${error.message}`;
  }
});

$('save').addEventListener('click', async () => {
  settings.capture.alsa_device = $('alsa').value;
  settings.capture.channels = Number($('channelSetting').value);
  settings.capture.stereo_verified = $('stereoVerified').checked;
  settings.audio.threshold_dbfs = Number($('threshold').value);
  settings.audio.start_delay_seconds = Number($('startDelay').value);
  settings.audio.silence_timeout_seconds = Number($('silence').value);
  settings.audio.calibration_approved = $('calibrated').checked;
  settings.audio.automation_enabled = $('automation').checked;
  settings.audio.codec_mode = $('codecMode').value;
  settings.audio.stop_on_clipping = $('clipStop').checked;
  settings.sonos.coordinator_uid = $('room').value || settings.sonos.coordinator_uid;
  settings.sonos.restore_previous = $('restore').checked;
  settings.sonos.preserve_volume = $('preserve').checked;
  try {
    await api('/api/settings', {method: 'PUT', body: JSON.stringify(settings)});
    $('saveMessage').textContent = 'Settings saved atomically.';
  } catch (error) {
    $('saveMessage').textContent = error.message;
  }
});

$('refreshDiag').addEventListener('click', async () => {
  $('diag').textContent = JSON.stringify(await api('/api/diagnostics'), null, 2);
});

boot().catch((error) => {
  $('authError').textContent = `Dashboard failed to start: ${error.message}`;
});

let audioContext;
let spectrumAnalyser;
let spectrumSource;
let spectrumAnimation;
const spectrumPeaks = new Float32Array(64);
let visualizerMode = 'spectrum';
let vuNeedle = 0;
let liveStreamDbfs = -90;
let liveStreamConnected = false;
let liveLevelQueue = [];
let liveLevelLastTimestamp = 0;
let liveLevelLastStep = 0;
let preparedLocalCodec = null;
let preparedLocalUrl = null;
let mediaAbortController = null;
let pcmSocket = null;
let pcmNextTime = 0;
let monitorSocket = null;
let streamSpectrumBins = new Uint8Array(1024);

function syncStreamMonitor() {
  if (!liveStreamConnected) {
    if (monitorSocket) monitorSocket.close();
    monitorSocket = null;
    streamSpectrumBins.fill(0);
    return;
  }
  if (pcmSocket?.readyState === WebSocket.OPEN || monitorSocket) return;
  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  monitorSocket = new WebSocket(`${protocol}//${location.host}/ws/audio`);
  monitorSocket.binaryType = 'arraybuffer';
  monitorSocket.onopen = () => {
    if (!pcmSocket) $('spectrumState').textContent = 'Live · stream monitor';
    if (!spectrumAnimation) drawSpectrum();
  };
  monitorSocket.onmessage = (event) => updateStreamSpectrum(new Float32Array(event.data));
  monitorSocket.onerror = () => {};
  monitorSocket.onclose = () => { monitorSocket = null; };
}

function updateStreamSpectrum(interleaved) {
  const size = 2048;
  const frames = Math.floor(interleaved.length / 2);
  if (frames < size) return;
  const real = new Float64Array(size);
  const imag = new Float64Array(size);
  const offset = frames - size;
  for (let i = 0; i < size; i++) {
    const sample = (interleaved[(offset + i) * 2] + interleaved[(offset + i) * 2 + 1]) * .5;
    real[i] = sample * (.5 - .5 * Math.cos(2 * Math.PI * i / (size - 1)));
  }
  for (let i = 1, j = 0; i < size; i++) {
    let bit = size >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) [real[i], real[j]] = [real[j], real[i]];
  }
  for (let length = 2; length <= size; length <<= 1) {
    const angle = -2 * Math.PI / length;
    for (let start = 0; start < size; start += length) {
      for (let j = 0; j < length / 2; j++) {
        const wr = Math.cos(angle * j);
        const wi = Math.sin(angle * j);
        const even = start + j;
        const odd = even + length / 2;
        const tr = wr * real[odd] - wi * imag[odd];
        const ti = wr * imag[odd] + wi * real[odd];
        real[odd] = real[even] - tr;
        imag[odd] = imag[even] - ti;
        real[even] += tr;
        imag[even] += ti;
      }
    }
  }
  for (let i = 0; i < streamSpectrumBins.length; i++) {
    const magnitude = Math.hypot(real[i], imag[i]) / (size / 2);
    const db = 20 * Math.log10(magnitude || 1e-6);
    streamSpectrumBins[i] = Math.max(0, Math.min(255, (db + 90) / 80 * 255));
  }
}

async function startSpectrumPlayback() {
  const player = $('localPlayer');
  try {
    await player.play();
    await attachAnalyser();
  } catch (error) {
    $('spectrumState').textContent = `Playback unavailable · ${error.name || 'browser error'}`;
    $('localHelp').textContent = `Browser playback failed: ${error.message || error}. The direct stream remains available for VLC.`;
  }
}

async function attachAnalyser() {
  const player = $('localPlayer');
  if (!audioContext) {
    audioContext = new (window.AudioContext || window.webkitAudioContext)();
    spectrumAnalyser = audioContext.createAnalyser();
    spectrumAnalyser.fftSize = 2048;
    spectrumAnalyser.smoothingTimeConstant = 0.78;
    spectrumAnalyser.minDecibels = -90;
    spectrumAnalyser.maxDecibels = -10;
    spectrumSource = audioContext.createMediaElementSource(player);
    spectrumSource.connect(spectrumAnalyser);
    spectrumAnalyser.connect(audioContext.destination);
  }
  await audioContext.resume();
  $('spectrumState').textContent = 'Live · browser FFT';
  drawSpectrum();
}

async function playPreparedLocalAudio() {
  if (!preparedLocalUrl) {
    $('localHelp').textContent = 'Prepare the browser MP3 stream first.';
    return;
  }
  if (preparedLocalCodec === 'mp3') {
    await startPcmPlayback();
  } else {
    await startSpectrumPlayback();
  }
}

async function startPcmPlayback() {
  stopPcmPlayback();
  if (monitorSocket) {
    monitorSocket.close();
    monitorSocket = null;
  }
  if (!audioContext) audioContext = new (window.AudioContext || window.webkitAudioContext)();
  if (!spectrumAnalyser) {
    spectrumAnalyser = audioContext.createAnalyser();
    spectrumAnalyser.fftSize = 2048;
    spectrumAnalyser.smoothingTimeConstant = .78;
    spectrumAnalyser.minDecibels = -90;
    spectrumAnalyser.maxDecibels = -10;
    spectrumAnalyser.connect(audioContext.destination);
  }
  await audioContext.resume();
  pcmNextTime = audioContext.currentTime + .12;
  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  pcmSocket = new WebSocket(`${protocol}//${location.host}/ws/audio`);
  pcmSocket.binaryType = 'arraybuffer';
  pcmSocket.onopen = () => {
    $('spectrumState').textContent = 'Buffering PCM audio…';
    $('localHelp').textContent = 'Connected directly to the Pi PCM decoder.';
    $('localStop').hidden = false;
  };
  pcmSocket.onmessage = (event) => {
    const interleaved = new Float32Array(event.data);
    const frames = Math.floor(interleaved.length / 2);
    if (!frames) return;
    const buffer = audioContext.createBuffer(2, frames, 44100);
    const left = buffer.getChannelData(0);
    const right = buffer.getChannelData(1);
    for (let i = 0; i < frames; i++) {
      left[i] = interleaved[i * 2];
      right[i] = interleaved[i * 2 + 1];
    }
    const source = audioContext.createBufferSource();
    source.buffer = buffer;
    source.connect(spectrumAnalyser);
    if (pcmNextTime < audioContext.currentTime + .04 || pcmNextTime > audioContext.currentTime + 1) {
      pcmNextTime = audioContext.currentTime + .08;
    }
    source.start(pcmNextTime);
    pcmNextTime += buffer.duration;
    $('spectrumState').textContent = 'Live · PCM audio';
  };
  pcmSocket.onerror = () => {
    $('spectrumState').textContent = 'PCM connection failed';
    $('localHelp').textContent = 'Could not connect to the Pi audio WebSocket.';
  };
  pcmSocket.onclose = () => {
    if ($('spectrumState').textContent.startsWith('Live')) $('spectrumState').textContent = 'Local audio stopped';
  };
  drawSpectrum();
}

function stopPcmPlayback() {
  if (pcmSocket) {
    pcmSocket.onclose = null;
    pcmSocket.close();
    pcmSocket = null;
  }
  $('localStop').hidden = true;
  if (audioContext) audioContext.suspend().catch(() => {});
  $('spectrumState').textContent = 'Local audio stopped';
  if (liveStreamConnected) syncStreamMonitor();
}

function startMediaSourceMp3(url) {
  const player = $('localPlayer');
  if (mediaAbortController) mediaAbortController.abort();
  mediaAbortController = new AbortController();
  const mediaSource = new MediaSource();
  const objectUrl = URL.createObjectURL(mediaSource);
  player.src = objectUrl;
  const playPromise = player.play();
  $('spectrumState').textContent = 'Buffering browser audio…';
  mediaSource.addEventListener('sourceopen', async () => {
    const sourceBuffer = mediaSource.addSourceBuffer('audio/mpeg');
    const queue = [];
    let readingDone = false;
    const appendNext = () => {
      if (sourceBuffer.updating || !queue.length) return;
      sourceBuffer.appendBuffer(queue.shift());
    };
    sourceBuffer.addEventListener('updateend', appendNext);
    try {
      const response = await fetch(url, {cache: 'no-store', signal: mediaAbortController.signal});
      if (!response.ok || !response.body) throw new Error(`Stream returned HTTP ${response.status}`);
      const reader = response.body.getReader();
      while (true) {
        const {done, value} = await reader.read();
        if (done) break;
        if (value?.byteLength) {
          queue.push(value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength));
          appendNext();
        }
      }
      readingDone = true;
    } catch (error) {
      if (error.name !== 'AbortError') {
        $('localHelp').textContent = `Stream reader failed: ${error.message}`;
        $('spectrumState').textContent = 'Local stream error';
      }
    }
  }, {once: true});
  playPromise.then(attachAnalyser).catch((error) => {
    $('localHelp').textContent = `Playback failed: ${error.message}`;
    $('spectrumState').textContent = `Playback unavailable · ${error.name}`;
  });
}

function drawSpectrum() {
  if (spectrumAnimation) cancelAnimationFrame(spectrumAnimation);
  const canvas = $('spectrumCanvas');
  const context = canvas.getContext('2d');
  const render = () => {
    const ratio = window.devicePixelRatio || 1;
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    if (canvas.width !== Math.round(width * ratio) || canvas.height !== Math.round(height * ratio)) {
      canvas.width = Math.round(width * ratio);
      canvas.height = Math.round(height * ratio);
    }
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    context.clearRect(0, 0, width, height);
    if (visualizerMode === 'vu') {
      drawVuMeters(context, width, height);
      spectrumAnimation = requestAnimationFrame(render);
      return;
    }
    const player = $('localPlayer');
    const browserAudioActive = spectrumAnalyser && audioContext?.state === 'running' && (
      pcmSocket?.readyState === WebSocket.OPEN || (!player.paused && !player.ended)
    );
    const bins = browserAudioActive
      ? new Uint8Array(spectrumAnalyser.frequencyBinCount)
      : streamSpectrumBins;
    if (!browserAudioActive && !liveStreamConnected) {
      spectrumAnimation = null;
      return;
    }
    if (browserAudioActive) spectrumAnalyser.getByteFrequencyData(bins);
    const bars = 64;
    const gap = Math.max(2, width / 320);
    const barWidth = (width - gap * (bars - 1)) / bars;
    const gradient = context.createLinearGradient(0, height, 0, 0);
    gradient.addColorStop(0, '#00c8ff');
    gradient.addColorStop(.28, '#42e3b4');
    gradient.addColorStop(.52, '#8a68ff');
    gradient.addColorStop(.76, '#ff4fa3');
    gradient.addColorStop(1, '#ffb547');
    context.fillStyle = gradient;
    for (let i = 0; i < bars; i++) {
      const low = Math.floor(Math.pow(i / bars, 2.15) * (bins.length - 1));
      const high = Math.max(low + 1, Math.floor(Math.pow((i + 1) / bars, 2.15) * bins.length));
      let total = 0;
      for (let j = low; j < high; j++) total += bins[j];
      const value = total / (high - low) / 255;
      const barHeight = Math.max(2, value * height * .94);
      const x = i * (barWidth + gap);
      context.fillRect(x, height - barHeight, barWidth, barHeight);
      spectrumPeaks[i] = Math.max(barHeight, spectrumPeaks[i] - 1.5);
      context.fillStyle = 'rgba(237,244,242,.75)';
      context.fillRect(x, height - spectrumPeaks[i] - 2, barWidth, 2);
      context.fillStyle = gradient;
    }
    spectrumAnimation = requestAnimationFrame(render);
  };
  render();
}

function drawVuMeters(context, width, height) {
  const player = $('localPlayer');
  const browserAudioActive = spectrumAnalyser && audioContext?.state === 'running' && (
    pcmSocket?.readyState === WebSocket.OPEN || (!player.paused && !player.ended)
  );
  let db = -40;
  if (browserAudioActive) {
    const samples = new Uint8Array(spectrumAnalyser.fftSize);
    spectrumAnalyser.getByteTimeDomainData(samples);
    let energy = 0;
    for (const sample of samples) {
      const normalized = (sample - 128) / 128;
      energy += normalized * normalized;
    }
    const rms = Math.sqrt(energy / samples.length);
    db = Math.max(-40, 20 * Math.log10(rms || .0001));
  } else if (liveStreamConnected) {
    const now = performance.now();
    if (liveLevelQueue.length && now - liveLevelLastStep >= 90) {
      liveStreamDbfs = liveLevelQueue.shift();
      liveLevelLastStep = now;
    }
    db = Math.max(-40, liveStreamDbfs);
  }
  const target = Math.max(0, Math.min(1, (db + 40) / 40));
  vuNeedle += (target - vuNeedle) * (target > vuNeedle ? .22 : .07);
  context.clearRect(0, 0, width, height);
  const chassis = context.createLinearGradient(0, 0, 0, height);
  chassis.addColorStop(0, '#deded9'); chassis.addColorStop(.2, '#aaa9a4');
  chassis.addColorStop(.52, '#e5e4df'); chassis.addColorStop(1, '#a6a5a0');
  context.fillStyle = chassis;
  context.beginPath(); context.roundRect(2, 2, width - 4, height - 4, 10); context.fill();
  context.strokeStyle = '#777772'; context.lineWidth = 1.5; context.stroke();
  context.save(); context.globalAlpha = .1;
  for (let row = 7; row < height - 5; row += 3) {
    context.strokeStyle = row % 2 ? '#fff' : '#444';
    context.beginPath(); context.moveTo(8, row); context.lineTo(width - 8, row); context.stroke();
  }
  context.restore();
  drawScrew(context, 14, 15); drawScrew(context, width - 14, 15);
  drawScrew(context, 14, height - 15); drawScrew(context, width - 14, height - 15);
  const side = Math.max(24, width * .065);
  const gap = Math.max(20, width * .075);
  const meterWidth = (width - side * 2 - gap) / 2;
  const meterHeight = height * .68;
  drawMeter(context, side, 12, meterWidth, meterHeight, vuNeedle, 'L METER');
  drawMeter(context, side + meterWidth + gap, 12, meterWidth, meterHeight, vuNeedle, 'R METER');
  context.fillStyle = '#171715'; context.textAlign = 'center';
  context.font = `700 ${Math.max(8, Math.min(12, width / 65))}px system-ui`;
  context.fillText('ANALOG VU METER', width * .72, height - 9);
  context.beginPath(); context.arc(width / 2, height - 20, 3, 0, Math.PI * 2); context.fill();
  context.font = `600 ${Math.max(6, Math.min(9, width / 85))}px system-ui`;
  context.fillText('MIC', width / 2, height - 8);
}

function drawMeter(context, x, y, width, height, value, label) {
  const inset = Math.max(7, width * .035);
  const radius = Math.min(width * .64, height * 1.18);
  const cx = x + width / 2;
  const cy = y + height * 1.18;
  const start = Math.PI * 1.16;
  const end = Math.PI * 1.84;
  context.save();
  context.shadowColor = 'rgba(0,0,0,.7)'; context.shadowBlur = 7; context.shadowOffsetY = 2;
  context.fillStyle = '#151615'; context.beginPath(); context.roundRect(x, y, width, height, 4); context.fill();
  context.shadowColor = 'transparent';
  const face = context.createRadialGradient(cx, y + height * .55, 2, cx, y + height * .55, width * .65);
  face.addColorStop(0, '#ffd36d'); face.addColorStop(.55, '#e99a35'); face.addColorStop(1, '#98491a');
  context.fillStyle = face; context.fillRect(x + inset, y + inset, width - inset * 2, height - inset * 2);
  context.strokeStyle = '#050505'; context.lineWidth = 3;
  context.strokeRect(x + inset, y + inset, width - inset * 2, height - inset * 2);
  const marks = ['20', '10', '7', '5', '3', '2', '1', '0', '1', '2', '3'];
  context.font = `${Math.max(7, Math.min(10, width / 31))}px Arial`;
  context.textAlign = 'center';
  context.textBaseline = 'middle';
  marks.forEach((mark, index) => {
    const position = index / (marks.length - 1);
    const angle = start + position * (end - start);
    const inner = radius * .76;
    const outer = radius * .84;
    context.strokeStyle = index >= 7 ? '#b42a20' : '#2d251c';
    context.lineWidth = index === 7 ? 2 : 1;
    context.beginPath();
    context.moveTo(cx + Math.cos(angle) * inner, cy + Math.sin(angle) * inner);
    context.lineTo(cx + Math.cos(angle) * outer, cy + Math.sin(angle) * outer);
    context.stroke();
    context.fillStyle = index >= 7 ? '#a2251d' : '#30261c';
    context.fillText(mark, cx + Math.cos(angle) * radius * .64, cy + Math.sin(angle) * radius * .64);
  });
  context.strokeStyle = '#a7271f'; context.lineWidth = 2;
  context.beginPath(); context.arc(cx, cy, radius * .88, start + (end-start)*.7, end); context.stroke();
  context.fillStyle = '#241b14'; context.font = `700 ${Math.max(9, Math.min(13, width / 25))}px Arial`;
  context.textAlign = 'right'; context.fillText('vu', x + width - inset - 9, y + height - inset - 9);
  const angle = start + value * (end - start);
  context.save();
  context.beginPath();
  context.rect(x + inset + 2, y + inset + 2, width - inset * 2 - 4, height - inset * 2 - 4);
  context.clip();
  context.strokeStyle = value > .88 ? '#c83129' : '#171713';
  context.lineWidth = 2;
  context.beginPath();
  context.moveTo(cx, cy);
  context.lineTo(cx + Math.cos(angle) * radius * .8, cy + Math.sin(angle) * radius * .8);
  context.stroke();
  context.fillStyle = '#171713';
  context.beginPath();
  context.arc(cx, cy, 5, 0, Math.PI * 2);
  context.fill();
  context.restore();
  context.strokeStyle = '#050505'; context.lineWidth = Math.max(5, inset);
  context.strokeRect(x + inset / 2, y + inset / 2, width - inset, height - inset);
  context.textAlign = 'center'; context.fillStyle = '#242421';
  context.font = `700 ${Math.max(7, Math.min(10, width / 36))}px system-ui`;
  context.fillText(label, cx, y + height + 13);
  context.restore();
}

function drawScrew(context, x, y) {
  const metal = context.createRadialGradient(x - 2, y - 2, 1, x, y, 7);
  metal.addColorStop(0, '#f4f4f0'); metal.addColorStop(.55, '#8b8b87'); metal.addColorStop(1, '#292a28');
  context.fillStyle = metal; context.beginPath(); context.arc(x, y, 6, 0, Math.PI * 2); context.fill();
  context.strokeStyle = '#393936'; context.lineWidth = 1.3;
  context.beginPath(); context.moveTo(x - 3, y - 2); context.lineTo(x + 3, y + 2); context.stroke();
}

function setVisualizerMode(mode) {
  visualizerMode = mode;
  $('spectrumMode').classList.toggle('active', mode === 'spectrum');
  $('vuMode').classList.toggle('active', mode === 'vu');
  $('frequencyAxis').hidden = mode === 'vu';
  $('visualizerTitle').textContent = mode === 'vu' ? 'Analogue VU meters' : 'Frequency spectrum';
  if (spectrumAnalyser || liveStreamConnected || mode === 'vu') drawSpectrum();
}

$('spectrumMode').addEventListener('click', () => setVisualizerMode('spectrum'));
$('vuMode').addEventListener('click', () => setVisualizerMode('vu'));

$('localPlayer').addEventListener('playing', () => attachAnalyser().catch(() => {}));
$('localPlayer').addEventListener('pause', () => {
  $('spectrumState').textContent = 'Paused';
});
