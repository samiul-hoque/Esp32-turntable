// Control panel served at "/". Same look as v1; {{...}} placeholders are filled in web_ui.cpp.
#pragma once

static const char WEB_PAGE[] = R"rawliteral(<!DOCTYPE html>
<html>
<head>
  <title>Auleek Turntable Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 1024 1024'><path fill='%23fff' d='M256 256h512v96H256zM192 768l208-352h224l208 352H704L512 448 320 768z'/></svg>">
  <style>
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Roboto', 'Helvetica Neue', Arial, sans-serif;
      background-color: #000000;
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
      margin: 0;
      padding: 20px;
      box-sizing: border-box;
    }
    .container {
      background: #1a1a1a;
      padding: 30px;
      border-radius: 10px;
      box-shadow: 0 0 25px rgba(255,255,255,0.1);
      width: 400px;
      max-width: 90vw;
      border: 1px solid #333;
    }
    .header {
      display: flex;
      align-items: center;
      justify-content: center;
      margin-bottom: 25px;
      gap: 15px;
    }
    .logo { width: 32px; height: 32px; flex-shrink: 0; }
    h2 { color: #ffffff; font-weight: 700; margin: 0; }
    .limits-info {
      background-color: #2a2a2a;
      border-left: 4px solid #ffffff;
      padding: 15px;
      margin-bottom: 20px;
      font-size: 12px;
      border-radius: 4px;
      color: #ffffff;
    }
    .limits-info h4 { margin: 0 0 8px 0; color: #ffffff; font-weight: 600; }
    label { margin: 10px 0 5px; display: block; font-weight: 600; color: #ffffff; }
    input[type="number"], input[type="text"], input[type="password"], select, input[type="submit"], button {
      width: 100%;
      padding: 10px;
      margin-bottom: 10px;
      border-radius: 4px;
      border: 1px solid #555;
      box-sizing: border-box;
      background-color: #2a2a2a;
      color: #ffffff;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Roboto', 'Helvetica Neue', Arial, sans-serif;
    }
    input:focus, select:focus { outline: none; border-color: #ffffff; background-color: #333; }
    input::placeholder { color: #888; }
    .section {
      background-color: #2a2a2a;
      padding: 15px;
      margin: 15px 0;
      border-radius: 5px;
      border: 1px solid #444;
    }
    .section h3 {
      margin: 0 0 15px 0;
      color: #ffffff;
      font-size: 14px;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }
    .section input, .section select { background-color: #1a1a1a; }
    .direction-group { margin-bottom: 10px; }
    .direction-group input[type="radio"] { width: auto; margin-right: 8px; margin-bottom: 5px; accent-color: #ffffff; }
    .direction-group label { display: inline; margin: 0; font-weight: 400; color: #ffffff; }
    .radio-option { margin-bottom: 8px; }
    input[type="submit"], button {
      background-color: #ffffff;
      color: #000000;
      border: none;
      cursor: pointer;
      font-size: 16px;
      font-weight: 700;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      transition: all 0.3s ease;
    }
    input[type="submit"]:hover, button:hover { background-color: #e0e0e0; transform: translateY(-1px); }
    button.stop { background-color: transparent; color: #ff6b6b; border: 2px solid #ff6b6b; }
    button.stop:hover { background-color: #ff6b6b; color: #000; }
    button.secondary { background-color: transparent; color: #fff; border: 1px solid #555; font-size: 13px; }
    .output {
      margin-top: 20px;
      padding: 15px;
      background-color: #2a2a2a;
      border-radius: 5px;
      font-size: 14px;
      border: 1px solid #444;
      color: #ffffff;
    }
    .output:empty { display: none; }
    .hint { font-size: 11px; color: #aaa; margin-top: -8px; margin-bottom: 10px; }
    .device { font-size: 11px; color: #aaa; text-align: center; margin-top: 15px; }
    .warn { color: orange; }
    .err { color: #ff6b6b; }
    .ok { color: #7bd88f; }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <svg class="logo" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024">
        <path fill="#fff" d="M256 256h512v96H256zM192 768l208-352h224l208 352H704L512 448 320 768z"/>
      </svg>
      <h2>Auleek Turntable Control</h2>
    </div>

    <div class="limits-info">
      <h4>System Limits</h4>
      <div>Max Speed: {{MAX_SPEED}} RPM</div>
      <div>Max Acceleration: {{MAX_ACCEL}} RPM/s</div>
      <div style="margin-top: 5px; font-style: italic;">Limits can only be changed in firmware</div>
    </div>

    <div class="section" id="wifiSection" hidden>
      <h3>Wi-Fi Setup</h3>
      <label for="ssidSelect">Network:</label>
      <select id="ssidSelect"></select>
      <input type="text" id="ssidManual" placeholder="Network name (hidden network)" hidden>
      <button type="button" class="secondary" id="rescanBtn">Rescan</button>
      <label for="wifiPass">Password:</label>
      <input type="password" id="wifiPass" placeholder="Leave empty for open networks">
      <button type="button" id="wifiSaveBtn">Connect</button>
      <div class="hint">The turntable works with 2.4 GHz networks only.</div>
      <div id="wifiStatus"></div>
    </div>

    <form id="controlForm">
      <div class="section">
        <h3>Movement Parameters</h3>

        <label for="repetitions">Repetitions:</label>
        <input type="number" name="repetitions" min="1" value="1" required>

        <label for="angle">Angle per Rep (degrees):</label>
        <input type="number" name="angle" min="0" required>

        <label for="time">Movement Time per Rep (ms):</label>
        <input type="number" name="time" min="100" required>

        <label for="repDelay">Delay Between Reps (ms):</label>
        <input type="number" name="repDelay" min="0" value="0">
      </div>

      <div class="section">
        <h3>Direction</h3>
        <div class="direction-group">
          <div class="radio-option">
            <input type="radio" id="forward" name="direction" value="forward" checked>
            <label for="forward">Forward</label>
          </div>
          <div class="radio-option">
            <input type="radio" id="reverse" name="direction" value="reverse">
            <label for="reverse">Reverse</label>
          </div>
        </div>
      </div>

      <div class="section">
        <h3>Motion Profile (Optional)</h3>

        <label for="acceleration">Acceleration (RPM/s):</label>
        <input type="number" name="acceleration" min="0.1" step="0.1" placeholder="{{DEF_ACCEL}} (default)">
        <div class="hint">How quickly to reach target speed</div>

        <label for="deceleration">Deceleration (RPM/s):</label>
        <input type="number" name="deceleration" min="0.1" step="0.1" placeholder="{{DEF_DECEL}} (default)">
        <div class="hint">How quickly to slow down and stop</div>
      </div>

      <input type="submit" value="Rotate">
      <button type="button" class="stop" id="stopBtn">Stop</button>
    </form>

    <div class="output" id="statusBox"></div>
    <div class="device" id="deviceInfo"></div>
  </div>

  <script>
    const $ = id => document.getElementById(id);
    const form = $('controlForm');
    const statusBox = $('statusBox');
    let pollTimer = null;

    function setStatus(text, cls) {
      statusBox.innerHTML = '';
      const span = document.createElement('span');
      if (cls) span.className = cls;
      span.textContent = text;
      statusBox.appendChild(span);
    }

    function pollMotion(showDone = true) {
      clearTimeout(pollTimer);
      fetch('/api/status').then(r => r.json()).then(s => {
        const m = s.motion;
        if (m.state === 'moving' || m.state === 'delay') {
          setStatus('Running... rep ' + m.rep + ' of ' + m.reps + (m.state === 'delay' ? ' (pause)' : ''));
          pollTimer = setTimeout(() => pollMotion(true), 300);
        } else if (showDone && (m.state === 'done' || m.state === 'stopped')) {
          fetch('/api/report').then(r => r.text()).then(html => { statusBox.innerHTML = html; });
        }
      }).catch(() => { pollTimer = setTimeout(() => pollMotion(showDone), 1000); });
    }

    form.addEventListener('submit', function(e) {
      e.preventDefault();
      const params = new URLSearchParams(new FormData(form)).toString();
      setStatus('Running...');
      fetch('/api/rotate?' + params).then(r => r.json()).then(j => {
        if (j.ok) pollMotion();
        else setStatus(j.message || j.error, 'warn');
      }).catch(err => setStatus('Error: ' + err, 'err'));
    });

    $('stopBtn').addEventListener('click', () => {
      fetch('/api/stop', { method: 'POST' }).then(() => pollMotion());
    });

    // ---- Device info / Wi-Fi setup (hotspot mode) ----
    function loadInfo() {
      fetch('/api/info').then(r => r.json()).then(info => {
        const w = info.wifi;
        let line = 'Firmware ' + info.fw;
        if (w.ip) line += ' · Wi-Fi: ' + w.ssid + ' · ' + w.ip + ' · ' + w.rssi + ' dBm';
        else if (w.hotspot) line += ' · Setup hotspot: ' + w.hotspotSsid;
        $('deviceInfo').textContent = line;
        if (w.hotspot) {
          $('wifiSection').hidden = false;
          loadScan();
        }
      }).catch(() => {});
    }

    function loadScan() {
      fetch('/api/scan').then(r => r.json()).then(s => {
        const sel = $('ssidSelect');
        const prev = sel.value;
        sel.innerHTML = '';
        s.networks.forEach(n => {
          const o = document.createElement('option');
          o.value = n.ssid;
          o.textContent = n.ssid + '  (' + n.rssi + ' dBm, ch ' + n.channel + (n.secure ? ', secured' : ', open') + ')';
          sel.appendChild(o);
        });
        const other = document.createElement('option');
        other.value = '__manual__';
        other.textContent = s.inProgress ? 'Scanning...' : 'Other / hidden network...';
        sel.appendChild(other);
        if (prev) sel.value = prev;
        $('ssidManual').hidden = sel.value !== '__manual__';
        if (s.inProgress) setTimeout(loadScan, 1500);
      }).catch(() => setTimeout(loadScan, 3000));
    }

    $('ssidSelect').addEventListener('change', () => {
      $('ssidManual').hidden = $('ssidSelect').value !== '__manual__';
    });
    $('rescanBtn').addEventListener('click', () => {
      fetch('/api/scan', { method: 'POST' }).then(() => setTimeout(loadScan, 500));
    });

    function wifiStatus(text, cls) {
      const el = $('wifiStatus');
      el.className = cls || '';
      el.textContent = text;
    }

    function pollTest(id, tries) {
      fetch('/api/status').then(r => r.json()).then(s => {
        const t = s.test;
        if (t.id !== id || t.state === 'pending') {
          if (tries > 0) setTimeout(() => pollTest(id, tries - 1), 1000);
          return;
        }
        if (t.state === 'ok') {
          wifiStatus('Connected to ' + t.ssid + '. The turntable is now at http://' + t.ip +
                     ' (or http://turntable.local). Reconnect this device to your normal Wi-Fi and open that address.', 'ok');
        } else {
          wifiStatus('Could not connect: ' + t.reasonText, 'err');
        }
      }).catch(() => {
        // The hotspot may briefly drop while the turntable switches channel.
        if (tries > 0) setTimeout(() => pollTest(id, tries - 1), 2000);
        else wifiStatus('Lost contact with the turntable. If it joined your Wi-Fi, find it at http://turntable.local or reconnect to the setup hotspot.', 'warn');
      });
    }

    $('wifiSaveBtn').addEventListener('click', () => {
      const sel = $('ssidSelect').value;
      const ssid = sel === '__manual__' ? $('ssidManual').value.trim() : sel;
      if (!ssid) { wifiStatus('Choose a network first.', 'warn'); return; }
      wifiStatus('Connecting to ' + ssid + '... this can take up to 30 seconds.');
      fetch('/api/wifi', { method: 'POST', body: new URLSearchParams({ ssid: ssid, pass: $('wifiPass').value }) })
        .then(r => r.json()).then(j => {
          if (j.ok) pollTest(j.test, 40);
          else wifiStatus(j.message || j.error, 'err');
        }).catch(err => wifiStatus('Error: ' + err, 'err'));
    });

    loadInfo();
    pollMotion(false);  // resume progress display if a move is already running
  </script>
</body>
</html>
)rawliteral";
