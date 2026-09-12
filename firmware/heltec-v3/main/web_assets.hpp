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
      <p class="eyebrow">NAD C541i - lossless network transport</p>
      <h1>CD to Sonos</h1>
    </div>
    <div class="status-pills">
      <span id="radioState" class="pill safe">LoRa disabled</span>
      <span id="clockState" class="pill">Waiting for I2S</span>
    </div>
  </header>

  <main>
    <section id="liveAudioPanel" class="meters panel" aria-labelledby="liveAudioTitle">
      <div class="visual-heading">
        <div>
          <p class="eyebrow">Live audio</p>
          <h2 id="liveAudioTitle">Digital input monitor</h2>
        </div>
        <p id="format">44.1 kHz - 16-bit - stereo FLAC</p>
      </div>
      <div id="spectrumView" class="visual-view" hidden>
        <div class="spectrum-rack">
          <div class="rack-legend"><span>31 BAND REAL-TIME ANALYSER</span><span>20 Hz — 20 kHz · L/R energy</span></div>
          <canvas id="spectrum" width="1100" height="290" aria-label="Live 31-band LED audio spectrum"></canvas>
        </div>
        <div class="spectrum-controls"><label for="spectrumGain">Display sensitivity</label><input id="spectrumGain" type="range" min="-12" max="24" step="3" value="0"><output id="spectrumGainValue">0 dB · visuals only</output></div>
      </div>
      <div id="vuView" class="visual-view">
        <div class="vu-pair">
          <div class="vu"><canvas id="vuLeft" width="420" height="210" aria-label="Left channel VU meter"></canvas><div class="vu-readout"><span>L RMS</span><output id="vuLeftValue">-- dBFS</output></div></div>
          <div class="vu"><canvas id="vuRight" width="420" height="210" aria-label="Right channel VU meter"></canvas><div class="vu-readout"><span>R RMS</span><output id="vuRightValue">-- dBFS</output></div></div>
        </div>
      </div>
      <div class="peak-meters" aria-label="Stereo LED peak levels">
        <div class="peak-row"><span>L peak</span><div class="peak-track"><div class="peak-glow" aria-hidden="true"><div id="glowLeft"></div></div><div id="peakLeft" class="peak-fill"></div><i id="holdLeft" class="peak-hold"></i></div><output id="peakLeftValue">-- dBFS</output></div>
        <div class="peak-row"><span>R peak</span><div class="peak-track"><div class="peak-glow" aria-hidden="true"><div id="glowRight"></div></div><div id="peakRight" class="peak-fill"></div><i id="holdRight" class="peak-hold"></i></div><output id="peakRightValue">-- dBFS</output></div>
      </div>
      <div class="visual-tools">
        <span class="faceplate-brand" aria-label="Sonos compatible DIY CD streamer">
          <span class="retro-wordmark" aria-hidden="true">SONOS</span>
          <span class="nameplate-model">CD-S1 <small>LOSSLESS NETWORK TRANSPORT</small></span>
          <span class="nameplate-diy">DIY · SONOS COMPATIBLE</span>
        </span>
        <div class="visual-toggle" role="group" aria-label="Audio visualizer">
          <button id="showVu" type="button" data-visual="vu" aria-pressed="true">VU meters</button>
          <button id="showSpectrum" class="secondary" type="button" data-visual="spectrum" aria-pressed="false">Spectrum</button>
        </div>
        <button id="fullscreenAudio" class="secondary fullscreen-button" type="button" aria-controls="liveAudioPanel" aria-pressed="false" title="Expand only the Live Audio panel">⛶ Fullscreen</button>
      </div>
      <p id="meterHealth" class="visual-note" role="status" hidden></p>
      <p id="fullscreenHint" class="fullscreen-hint" role="status" hidden></p>
    </section>

    <section id="dashboardControls" class="grid">
      <article class="panel">
        <p class="eyebrow">Lossless stream</p>
        <h2>Playback</h2>
        <dl>
          <div><dt>Signal</dt><dd id="signal">Silent</dd></div>
          <div><dt>Clients</dt><dd id="clients">0</dd></div>
          <div><dt>Buffer overruns</dt><dd id="overruns">0</dd></div>
        </dl>
        <audio id="localAudio" controls preload="none"></audio>
        <a id="streamLink" class="button" href="#" target="_blank">Open FLAC stream</a>
      </article>

      <article class="panel">
        <p class="eyebrow">Sonos</p>
        <h2>Sonos playback</h2>
        <label for="sonosPlayer">Group or room</label>
        <select id="sonosPlayer"><option value="">Select group or room</option></select>
        <div class="actions">
          <button id="discoverSonos" type="button">Discover</button>
          <button id="playSonos" type="button" disabled>Play</button>
          <button id="stopSonos" class="secondary" type="button" disabled>Stop</button>
        </div>
        <p id="sonosAuto" class="help">Automatic playback is starting...</p>
        <p id="sonosMessage" class="help">Choose a room or existing Sonos group. Bonding is never changed.</p>
      </article>

      <article class="panel">
        <p class="eyebrow">Network</p>
        <h2>Wi-Fi setup</h2>
        <form id="wifiForm">
          <label for="ssid">Network name</label>
          <input id="ssid" name="ssid" maxlength="32" required autocomplete="off">
          <button id="scanWifi" class="secondary" type="button">Scan for networks</button>
          <div id="wifiResults" class="network-results"></div>
          <label for="password">Password</label>
          <input id="password" name="password" type="password" maxlength="64" autocomplete="new-password">
          <button type="submit">Save and restart</button>
        </form>
        <p id="networkState" class="help">Checking network...</p>
      </article>

      <article class="panel">
        <p class="eyebrow">Diagnostics</p>
        <h2>System</h2>
        <dl>
          <div><dt>IP address</dt><dd id="ip">--</dd></div>
          <div><dt>Free heap</dt><dd id="heap">--</dd></div>
          <div><dt>CPU load</dt><dd id="cpu">--</dd></div>
          <div><dt>CPU cores</dt><dd id="cpuCores">--</dd></div>
          <div><dt>Stream delivery (all clients)</dt><dd id="delivery">--</dd></div>
          <div><dt>Capture delivery</dt><dd id="captureRate">--</dd></div>
          <div><dt>Source LRCLK</dt><dd id="sourceRate">--</dd></div>
          <div><dt>Captured</dt><dd id="captured">--</dd></div>
          <div><dt>Empty I2S waits</dt><dd id="timeouts">0</dd></div>
          <div><dt>DMA buffers lost</dt><dd id="dmaLoss">--</dd></div>
          <div><dt>Capture blocks lost</dt><dd id="publishLoss">--</dd></div>
          <div><dt>Capture interruptions</dt><dd id="clockLoss">--</dd></div>
          <div><dt>Last captured block</dt><dd id="captureAge">--</dd></div>
          <div><dt>Capture stack free</dt><dd id="captureStack">--</dd></div>
          <div><dt>Analysis stack free</dt><dd id="analysisStack">--</dd></div>
          <div><dt>Analysis updates</dt><dd id="analysisRuns">0</dd></div>
          <div><dt>Meter / spectrum updates</dt><dd id="meterUpdates">--</dd></div>
          <div><dt>Analysis block time / maximum</dt><dd id="analysisTime">--</dd></div>
          <div><dt>Firmware</dt><dd id="version">--</dd></div>
        </dl>
        <p class="help">Empty waits can occur when the CD clock stops; they are not a count of corrupted samples. Lost-buffer counters and capture delivery help identify interruptions. Counters reset on reboot.</p>
      </article>
    </section>
  </main>

  <script src="/app.js"></script>
</body>
</html>
)CDTS";

inline constexpr char kStyle[] = R"CDTS(:root {
  color-scheme: dark;
  --bg: #151515;
  --panel: #20201f;
  --edge: #45433e;
  --text: #f3eee3;
  --muted: #b9b0a2;
  --amber: #f2a52b;
  --amber-soft: #8f591b;
  --red: #df543f;
  --green: #68b886;
}

* { box-sizing: border-box; }
body {
  margin: 0;
  background:
    repeating-linear-gradient(0deg, transparent 0 3px, #ffffff05 3px 4px),
    var(--bg);
  color: var(--text);
  font: 15px/1.45 system-ui, sans-serif;
}
header, main {
  width: min(1180px, calc(100% - 32px));
  margin-inline: auto;
}
header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 20px;
  padding: 28px 0 18px;
}
h1, h2, p { margin-top: 0; }
h1 { margin-bottom: 0; font-size: clamp(30px, 5vw, 54px); font-weight: 500; letter-spacing: -.04em; }
h2 { margin-bottom: 18px; font-weight: 500; }
.eyebrow { margin-bottom: 4px; color: var(--amber); text-transform: uppercase; letter-spacing: .13em; font-size: 12px; }
.status-pills, .actions { display: flex; gap: 8px; flex-wrap: wrap; }
.pill {
  padding: 7px 11px;
  border: 1px solid var(--edge);
  border-radius: 999px;
  color: var(--muted);
}
.pill.safe { border-color: var(--green); color: var(--green); }
.pill.live { border-color: var(--amber); color: var(--amber); }
.panel {
  border: 1px solid var(--edge);
  border-radius: 10px;
  background: linear-gradient(145deg, #ffffff08, transparent), var(--panel);
  padding: 22px;
  box-shadow: inset 0 1px #ffffff10, 0 18px 40px #0005;
}
.meters { margin-bottom: 18px; }
.visual-heading { display: flex; justify-content: space-between; gap: 20px; align-items: end; }
.visual-heading h2 { margin-bottom: 0; }
.visual-tools { display: flex; align-items: flex-end; gap: 14px; flex-wrap: wrap; justify-content: flex-end; }
.visual-tools > p { color: var(--muted); margin-bottom: 8px; }
.visual-toggle { display: flex; gap: 5px; padding: 4px; border: 1px solid var(--edge); border-radius: 8px; background: #171716; }
.visual-toggle button { margin: 0; padding: 7px 11px; border-color: transparent; background: transparent; color: var(--muted); }
.visual-toggle button[aria-pressed="true"] { background: var(--amber); color: #17110a; }
.visual-view[hidden] { display: none !important; }
.spectrum-rack { margin-top: 12px; padding: 12px 14px 7px; border: 1px solid #48504a; border-radius: 6px; background: linear-gradient(#171d19, #050706); box-shadow: inset 0 1px #8b9c8f33, inset 0 -2px #000; }
.rack-legend { display: flex; justify-content: space-between; gap: 12px; color: #b1c9be; font-size: 10px; letter-spacing: .12em; margin-bottom: 7px; }
#spectrum { width: 100%; height: auto; display: block; border: 1px solid #000; }
.spectrum-controls { display: flex; align-items: center; gap: 12px; font-size: 12px; color: var(--muted); margin: 8px 0; }
.spectrum-controls label { margin: 0; }
.spectrum-controls input { width: 140px; padding: 0; accent-color: #6fffc5; }
.spectrum-controls output { font-variant-numeric: tabular-nums; }
.vu-pair { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.vu { position: relative; }
.vu canvas { display: block; width: 100%; height: auto; }
.vu-readout { display: flex; justify-content: space-between; color: var(--muted); padding: 2px 10px 8px; font-size: 12px; font-variant-numeric: tabular-nums; }
.peak-meters { display: grid; gap: 7px; margin-top: 12px; }
.peak-row { display: grid; grid-template-columns: 44px 1fr 88px; align-items: center; gap: 10px; font-size: 12px; color: var(--muted); }
.peak-row output { text-align: right; font-variant-numeric: tabular-nums; }
.peak-track {
  --led-gap: 4px; --led-step: calc(100% / 48);
  --led-mask: repeating-linear-gradient(90deg, #000 0 calc(var(--led-step) - var(--led-gap)), transparent calc(var(--led-step) - var(--led-gap)) var(--led-step));
  --led-colours: linear-gradient(90deg, #20ed86 0 72.916667%, #ffb928 72.916667% 89.583333%, #ff3a23 89.583333% 100%);
  height: 11px; position: relative; isolation: isolate;
}
.peak-track::before, .peak-fill, .peak-glow > div {
  position: absolute; inset: 0;
  -webkit-mask-image: var(--led-mask); mask-image: var(--led-mask);
}
.peak-track::before {
  content: '';
  background: linear-gradient(180deg, #ffffff20 0 12%, transparent 25% 60%, #0009 100%), linear-gradient(90deg, #123e26 0 72.916667%, #4d350a 72.916667% 89.583333%, #4a170e 89.583333% 100%);
  box-shadow: inset 0 1px #ffffff12, inset 0 -2px #0008;
}
.peak-fill {
  background: radial-gradient(ellipse at 45% 40%, #ffffcc80, transparent 70%), linear-gradient(180deg, #fff7 0 12%, #fff1 24% 60%, #0005 85%, #0009 100%), var(--led-colours);
  background-size: var(--led-step) 100%, 100% 100%, 100% 100%;
  box-shadow: inset 0 1px #ffffcaaa, inset 0 -1px #0008;
  clip-path: inset(0 100% 0 0);
}
/* Blur just two narrow rows, not every LED or the whole panel. The blur
   wrapper is outside the segmented mask so light can spill into the gaps. */
.peak-glow { position: absolute; inset: 0; filter: blur(3px); opacity: .72; pointer-events: none; }
.peak-glow > div { background: var(--led-colours); clip-path: inset(0 100% 0 0); }
.peak-hold {
  position: absolute; top: 0; bottom: 0; width: calc(var(--led-step) - var(--led-gap)); left: 0; opacity: 0;
  color: #20ed86;
  background: radial-gradient(ellipse at 45% 40%, #ffffcc80, transparent 70%), linear-gradient(180deg, #fff7 0 12%, #fff1 24% 60%, #0005 85%, #0009 100%), currentColor;
  box-shadow: inset 0 1px #ffffcaaa, inset 0 -1px #0008, 0 0 3px currentColor;
}
.visual-note { color: var(--muted); font-size: 12px; margin: 8px 0 0; text-align: right; }
.visual-note[hidden] { display: none; }
.fullscreen-button { margin: 0; white-space: nowrap; }
.fullscreen-hint { margin: 5px 0 0; color: var(--muted); font-size: 12px; text-align: right; }
.fullscreen-hint[hidden] { display: none; }
body.audio-fullscreen { overflow: hidden; }
body.audio-fullscreen > header, body.audio-fullscreen #dashboardControls { visibility: hidden; }
#liveAudioPanel::backdrop { background: #101311; }
#liveAudioPanel.is-fullscreen {
  position: fixed; inset: 0; z-index: 1000;
  width: 100%; height: 100vh; height: 100dvh;
  max-width: none; margin: 0; border: 0; border-radius: 0;
  padding: max(16px, env(safe-area-inset-top)) max(20px, env(safe-area-inset-right)) max(12px, env(safe-area-inset-bottom)) max(20px, env(safe-area-inset-left));
  background: #111613;
  display: flex; flex-direction: column; gap: 8px; overflow: auto;
}
#liveAudioPanel.is-fullscreen .visual-heading { flex: 0 0 auto; align-items: center; }
#liveAudioPanel.is-fullscreen .visual-view { flex: 1 0 auto; display: flex; flex-direction: column; justify-content: center; min-height: 0; }
#liveAudioPanel.is-fullscreen .spectrum-rack { margin-top: 0; }
#liveAudioPanel.is-fullscreen #spectrum { max-height: calc(100dvh - 230px); min-height: 130px; object-fit: contain; }
#liveAudioPanel.is-fullscreen .vu-pair { align-items: center; }
#liveAudioPanel.is-fullscreen .vu canvas { max-height: calc(100dvh - 240px); min-height: 130px; object-fit: contain; }
#liveAudioPanel.is-fullscreen .peak-track { height: 18px; }
#liveAudioPanel.is-fullscreen .visual-note { margin-top: 0; }
#liveAudioPanel.is-fullscreen .peak-meters { flex: 0 0 auto; }
.grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 18px; padding-bottom: 32px; }
dl { margin: 0 0 18px; }
dl div { display: flex; justify-content: space-between; gap: 16px; padding: 8px 0; border-bottom: 1px solid var(--edge); }
dt { color: var(--muted); }
dd { margin: 0; font-variant-numeric: tabular-nums; text-align: right; }
label { display: block; color: var(--muted); margin: 12px 0 5px; }
input, select, button, .button {
  border: 1px solid var(--edge);
  border-radius: 6px;
  background: #171716;
  color: var(--text);
  padding: 10px 12px;
  font: inherit;
}
input, select { width: 100%; }
button, .button { cursor: pointer; display: inline-block; text-decoration: none; margin-top: 12px; }
button:not(.secondary), .button { background: var(--amber); border-color: var(--amber); color: #17110a; }
button:disabled { opacity: .42; cursor: not-allowed; }
.secondary { color: var(--muted); }
.network-results { display: grid; gap: 6px; margin-top: 8px; }
.network-results button { width: 100%; margin: 0; text-align: left; }
audio { width: 100%; margin: 4px 0 8px; }
.help { color: var(--muted); margin: 12px 0 0; }

/* A passive hi-fi faceplate: all texture and reflections are static CSS.
   Keep the existing canvas cadence and the audio/telemetry path unchanged. */
#liveAudioPanel {
  --muted: #454137;
  --faceplate: repeating-linear-gradient(0deg, #fff1 0 1px, #382b1808 1px 2px, transparent 2px 4px), linear-gradient(105deg, #b5ad9b, #e2dac7 24%, #c7bda7 65%, #ddd5c5);
  position: relative; padding: 22px 28px 20px; border-radius: 3px;
  color: #272720; border: 1px solid #9a9485; background: var(--faceplate);
  box-shadow: inset 0 2px 1px #fff9, inset 0 -3px 2px #574e3d88, 0 12px 22px #0008;
}
#liveAudioPanel .visual-heading { align-items: center; gap: 16px; margin-bottom: 17px; }
#liveAudioPanel .visual-heading > div { flex: 1; }
#liveAudioPanel .eyebrow { color: #434036; font-size: 9px; letter-spacing: .22em; }
#liveAudioPanel h2 { padding: 5px 12px; background: #252824; color: #dbd6c7; font-size: 13px; letter-spacing: .17em; text-transform: uppercase; font-weight: 400; }
#liveAudioPanel #format { color: #454137; margin: 0; font-size: 11px; text-transform: uppercase; letter-spacing: .06em; }
#liveAudioPanel .spectrum-rack, #liveAudioPanel .vu-pair {
  position: relative; margin-top: 0; padding: 17px 20px 10px; border: 5px solid #272a25; border-radius: 1px;
  background: #070c09; box-shadow: 0 0 0 1px #a8a492, 0 2px 0 2px #eee5d0, inset 0 3px 9px #000, 0 5px 9px #0006;
}
#liveAudioPanel .spectrum-rack::after, #liveAudioPanel .vu-pair::after {
  content: ''; position: absolute; inset: 0; pointer-events: none;
  background: linear-gradient(108deg, transparent 8%, #d4e6ce08 24%, transparent 25% 59%, #fff04a03 60%, transparent 77%), linear-gradient(#dce8d008, transparent 28%);
  box-shadow: inset 0 0 12px #0008;
}
#liveAudioPanel .rack-legend { color: #8c9b88; font-size: 9px; letter-spacing: .16em; }
#liveAudioPanel .vu-readout { color: #aeb7a7; }
#liveAudioPanel .spectrum-controls { margin: 15px 0 10px; font-size: 10px; text-transform: uppercase; letter-spacing: .07em; }
#liveAudioPanel .spectrum-controls input { accent-color: #353a31; height: 16px; }
#liveAudioPanel .peak-meters { padding: 13px 15px; background: #101610; border: 3px solid #363a31; box-shadow: inset 0 2px 5px #000, 0 1px #f7edda; margin-top: 14px; }
#liveAudioPanel .peak-row { color: #b7c0ab; font-size: 10px; letter-spacing: .06em; }
#liveAudioPanel .visual-tools { align-items: center; justify-content: flex-end; gap: 18px; margin-top: 20px; }
.faceplate-brand { margin-right: auto; display: grid; grid-template-columns: auto auto; align-items: center; column-gap: 15px; color: #242720; text-shadow: 0 1px #f5eed066; }
.retro-wordmark { font: 900 28px/1 Arial, Helvetica, sans-serif; letter-spacing: .12em; transform: scaleX(1.04); transform-origin: left center; }
.nameplate-model { border-left: 1px solid #55584b; padding-left: 13px; font-size: 13px; font-weight: 600; line-height: 1.3; letter-spacing: .13em; }
.faceplate-brand small { display: block; font-size: 7px; font-weight: 500; letter-spacing: .12em; }
.nameplate-diy { grid-column: 1 / -1; margin-top: 5px; font-size: 7px; letter-spacing: .17em; color: #555448; }
#liveAudioPanel .visual-toggle { padding: 0; border: 0; background: transparent; gap: 12px; }
#liveAudioPanel .visual-tools button {
  position: relative; margin: 0; padding: 8px 12px; border: 2px solid #34372f; border-radius: 1px;
  background: linear-gradient(#ece8dc 0 22%, #bab8aa 24% 77%, #85877b 79%); color: #252a21;
  box-shadow: 0 2px 0 #f6ecd2, inset 0 1px #fff8, 0 2px 4px #0005;
  font-size: 10px; letter-spacing: .07em; text-transform: uppercase;
}
#liveAudioPanel .visual-toggle button::before { content: ''; display: inline-block; width: 6px; height: 4px; margin-right: 7px; vertical-align: 2px; background: #344e32; border: 1px solid #293325; }
#liveAudioPanel .visual-toggle button[aria-pressed="true"]::before { background: #7fff7a; box-shadow: 0 0 4px #58ff69; }
#liveAudioPanel .visual-tools button:hover { filter: brightness(1.08); }
#liveAudioPanel button:focus-visible, #liveAudioPanel input:focus-visible { outline: 2px solid #205d34; outline-offset: 4px; }
#liveAudioPanel.is-fullscreen { background: var(--faceplate); }
#liveAudioPanel.is-fullscreen .visual-tools { flex: 0 0 auto; margin-top: 5px; }

@media (max-width: 720px) {
  header, .visual-heading { align-items: flex-start; flex-direction: column; }
  .grid, .vu-pair { grid-template-columns: 1fr; }
  .visual-tools { width: 100%; align-items: flex-start; justify-content: space-between; }
  .visual-note { text-align: left; }
  .spectrum-rack { padding: 8px 3px 3px; }
  .rack-legend { font-size: 8px; letter-spacing: .03em; }
  .spectrum-controls { flex-wrap: wrap; gap: 8px; }
  .peak-track { --led-gap: 1px; }
  #liveAudioPanel { padding: 16px 12px; }
  #liveAudioPanel .visual-heading { align-items: stretch; gap: 9px; }
  #liveAudioPanel .spectrum-rack, #liveAudioPanel .vu-pair { padding: 10px 4px 5px; border-width: 3px; }
  #liveAudioPanel .rack-legend { font-size: 7px; letter-spacing: .02em; }
  #liveAudioPanel .visual-tools { gap: 12px; justify-content: space-between; }
  #liveAudioPanel .faceplate-brand { flex-basis: 100%; }
  #liveAudioPanel .peak-meters { padding: 12px 7px; }
  #liveAudioPanel .peak-row { grid-template-columns: 36px 1fr 65px; gap: 6px; }
  #liveAudioPanel.is-fullscreen .visual-heading { align-items: flex-start; gap: 8px; }
  #liveAudioPanel.is-fullscreen #format { display: none; }
  #liveAudioPanel.is-fullscreen .vu canvas { max-height: calc((100dvh - 260px) / 2); }
  #liveAudioPanel.is-fullscreen .visual-note { font-size: 10px; }
}
@media (max-height: 520px) and (orientation: landscape) {
  #liveAudioPanel.is-fullscreen { padding: 8px 16px; gap: 4px; }
  #liveAudioPanel.is-fullscreen .visual-heading { flex-direction: row; gap: 8px; }
  #liveAudioPanel.is-fullscreen .visual-heading h2,
  #liveAudioPanel.is-fullscreen #format { display: none; }
  #liveAudioPanel.is-fullscreen .visual-heading { margin-bottom: 0; }
  #liveAudioPanel.is-fullscreen .faceplate-brand { flex-basis: auto; }
  #liveAudioPanel.is-fullscreen .visual-tools { margin-top: 0; }
  #liveAudioPanel.is-fullscreen .peak-meters { padding: 6px 12px; }
  #liveAudioPanel.is-fullscreen .visual-tools { width: auto; }
  #liveAudioPanel.is-fullscreen .vu-pair { grid-template-columns: 1fr 1fr; }
  #liveAudioPanel.is-fullscreen #spectrum,
  #liveAudioPanel.is-fullscreen .vu canvas { max-height: calc(100dvh - 160px); min-height: 80px; }
  #liveAudioPanel.is-fullscreen .spectrum-controls { margin: 4px 0; }
  #liveAudioPanel.is-fullscreen .peak-meters { margin-top: 4px; gap: 4px; }
}
)CDTS";

inline constexpr char kApp[] = R"CDTS(const $ = id => document.getElementById(id);
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
)CDTS";

}  // namespace cdts::web_assets
