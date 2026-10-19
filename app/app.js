// Auleek Turntable companion app (Web Bluetooth). No build step, no dependencies.
// Protocol: see firmware/include/config.h (UUIDs) and firmware/src/api.cpp.
'use strict';

const APP_VERSION = '2.0.0';
const PROTO = 1;
const SERVICE_UUID = '6e7a0001-9b3c-4f1e-8a2d-5c1f0e7b2a10';
const COMMAND_UUID = '6e7a0002-9b3c-4f1e-8a2d-5c1f0e7b2a10';
const STATE_UUID = '6e7a0003-9b3c-4f1e-8a2d-5c1f0e7b2a10';
const EVENTS_UUID = '6e7a0004-9b3c-4f1e-8a2d-5c1f0e7b2a10';
const RECONNECT_DELAYS_MS = [1000, 2000, 5000, 10000, 10000, 20000, 30000];
const REPLY_TIMEOUT_MS = 6000;

const $ = id => document.getElementById(id);

const ble = {
  device: null,
  command: null,
  state: null,
  events: null,
  userDisconnect: false,
  reconnectTimer: null,
  reconnectAttempt: 0,
  writeChain: Promise.resolve(),
  lineBuffer: '',
  decoder: new TextDecoder(),
  nextId: 1,
  pending: new Map(),
};

const app = {
  info: null,
  wifi: null,
  myRun: null,
  scanBuffer: [],
  testId: null,
  selectedSsid: null,
};

// ---------------------------------------------------------------- UI helpers

function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') node.className = v;
    else if (k === 'text') node.textContent = v;
    else node.setAttribute(k, v);
  }
  for (const c of children) node.append(c);
  return node;
}

function show(id, visible) { $(id).hidden = !visible; }

function setText(id, text, cls) {
  const node = $(id);
  node.textContent = text || '';
  node.className = cls || '';
}

function setConn(state, text) {
  $('connDot').className = 'dot ' + state;
  $('connText').textContent = text;
  const connected = state === 'connected';
  show('connectBtn', !connected && state !== 'connecting');
  show('disconnectBtn', connected || state === 'connecting');
  show('tabs', connected);
  for (const p of ['control', 'wifi', 'device']) show('tab-' + p, connected && currentTab === p);
  for (const b of document.querySelectorAll('form button, #scanBtn, #manualBtn, #copyDiagBtn, #forgetBtn')) {
    b.disabled = !connected;
  }
}

function help(html) {
  const node = $('connHelp');
  node.innerHTML = html;  // static strings only
  node.hidden = !html;
}

let currentTab = 'control';
for (const t of document.querySelectorAll('.tab')) {
  t.addEventListener('click', () => {
    currentTab = t.dataset.tab;
    for (const o of document.querySelectorAll('.tab')) o.classList.toggle('active', o === t);
    for (const p of ['control', 'wifi', 'device']) show('tab-' + p, currentTab === p);
  });
}

// ---------------------------------------------------------------- Bluetooth

async function connect() {
  help('');
  if (!navigator.bluetooth) {
    help('This browser can\'t use Bluetooth. Open this page in <b>Chrome</b> or <b>Edge</b> on Android, Windows or Mac. ' +
         '(On iPhone/iPad, use the free <b>Bluefy</b> browser.)');
    return;
  }
  try {
    if (navigator.bluetooth.getAvailability && !(await navigator.bluetooth.getAvailability())) {
      help('Bluetooth is turned off or not available on this device. Turn it on and try again.');
      return;
    }
  } catch (_) { /* not supported everywhere; carry on */ }

  let device;
  try {
    device = await navigator.bluetooth.requestDevice({ filters: [{ services: [SERVICE_UUID] }] });
  } catch (err) {
    if (err.name === 'NotFoundError') {
      help('No turntable selected. Make sure it is powered on and within a few metres.<br>' +
           '<b>Android:</b> Location must be switched on for Bluetooth scanning.<br>' +
           '<b>Mac:</b> allow Chrome to use Bluetooth in System Settings &rarr; Privacy &amp; Security &rarr; Bluetooth.');
    } else if (err.name === 'SecurityError') {
      help('Bluetooth needs a secure page. Open the app from its https:// address.');
    } else {
      help('Could not start Bluetooth: ' + escapeHtml(err.message));
    }
    return;
  }
  if (ble.device && ble.device !== device) ble.device.removeEventListener('gattserverdisconnected', onDisconnected);
  ble.device = device;
  ble.userDisconnect = false;
  device.addEventListener('gattserverdisconnected', onDisconnected);
  await connectGatt();
}

async function connectGatt() {
  clearTimeout(ble.reconnectTimer);
  setConn('connecting', 'Connecting to ' + (ble.device.name || 'turntable') + '...');
  try {
    const server = await ble.device.gatt.connect();
    const service = await server.getPrimaryService(SERVICE_UUID);
    ble.command = await service.getCharacteristic(COMMAND_UUID);
    ble.state = await service.getCharacteristic(STATE_UUID);
    ble.events = await service.getCharacteristic(EVENTS_UUID);
    ble.lineBuffer = '';
    ble.events.addEventListener('characteristicvaluechanged', onEventChunk);
    await ble.events.startNotifications();
    ble.reconnectAttempt = 0;
    setConn('connected', 'Connected to ' + (ble.device.name || 'turntable'));
    await readState();
    const reply = await sendCommand({ cmd: 'info' });
    onInfo(reply.info);
  } catch (err) {
    console.warn('connect failed', err);
    if (!ble.userDisconnect) scheduleReconnect('Connection failed: ' + err.message);
  }
}

function onDisconnected() {
  failPending('Disconnected');
  if (ble.userDisconnect) {
    setConn('', 'Not connected');
    return;
  }
  scheduleReconnect('Connection lost');
}

function scheduleReconnect(reason) {
  if (ble.reconnectAttempt >= RECONNECT_DELAYS_MS.length) {
    setConn('', reason + '. Press Connect to try again.');
    return;
  }
  const delay = RECONNECT_DELAYS_MS[ble.reconnectAttempt++];
  setConn('connecting', reason + '. Reconnecting in ' + Math.round(delay / 1000) + ' s...');
  clearTimeout(ble.reconnectTimer);
  ble.reconnectTimer = setTimeout(() => { if (!ble.userDisconnect) connectGatt(); }, delay);
}

function disconnect() {
  ble.userDisconnect = true;
  clearTimeout(ble.reconnectTimer);
  if (ble.device && ble.device.gatt.connected) ble.device.gatt.disconnect();
  setConn('', 'Not connected');
}

// Web Bluetooth allows one GATT operation at a time: serialize writes.
function writeJson(obj) {
  const data = new TextEncoder().encode(JSON.stringify(obj));
  const run = () => ble.command.writeValueWithResponse
    ? ble.command.writeValueWithResponse(data)
    : ble.command.writeValue(data);
  ble.writeChain = ble.writeChain.catch(() => {}).then(run);
  return ble.writeChain;
}

function sendCommand(obj) {
  const id = ble.nextId++;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      ble.pending.delete(id);
      reject(new Error('No reply from the turntable'));
    }, REPLY_TIMEOUT_MS);
    ble.pending.set(id, { resolve, reject, timer });
    writeJson({ ...obj, id }).catch(err => {
      clearTimeout(timer);
      ble.pending.delete(id);
      reject(err);
    });
  });
}

function failPending(message) {
  for (const [id, p] of ble.pending) {
    clearTimeout(p.timer);
    p.reject(new Error(message));
    ble.pending.delete(id);
  }
}

async function readState() {
  const value = await ble.state.readValue();
  const s = JSON.parse(new TextDecoder().decode(value));
  if (s.proto !== PROTO) protoMismatch(s.proto);
  onMotion(s.motion);
  onWifi(s.wifi, s.test);
}

// Events arrive as newline-delimited JSON split into MTU-sized chunks.
function onEventChunk(event) {
  ble.lineBuffer += ble.decoder.decode(event.target.value, { stream: true });
  let nl;
  while ((nl = ble.lineBuffer.indexOf('\n')) >= 0) {
    const line = ble.lineBuffer.slice(0, nl);
    ble.lineBuffer = ble.lineBuffer.slice(nl + 1);
    if (!line) continue;
    let msg;
    try { msg = JSON.parse(line); } catch (_) { continue; }  // a chunk was lost: skip this line
    handleEvent(msg);
  }
  if (ble.lineBuffer.length > 8192) ble.lineBuffer = '';
}

function handleEvent(msg) {
  switch (msg.type) {
    case 'reply': {
      const p = ble.pending.get(msg.id);
      if (p) {
        clearTimeout(p.timer);
        ble.pending.delete(msg.id);
        p.resolve(msg);
      }
      break;
    }
    case 'motion': onMotion(msg); break;
    case 'wifi': onWifi(msg.wifi, msg.test); break;
    case 'net': app.scanBuffer.push(msg); break;
    case 'scan_done': onScanDone(msg); break;
  }
}

function protoMismatch(deviceProto) {
  const w = $('protoWarn');
  w.textContent = 'This app (protocol ' + PROTO + ') and the turntable firmware (protocol ' + deviceProto +
                  ') do not match. Some features may not work. Please update the app or the firmware.';
  w.hidden = false;
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

// ---------------------------------------------------------------- Control

function readNumber(id, fallback) {
  const v = $(id).value.trim();
  return v === '' ? fallback : Number(v);
}

// Predicts what the firmware will do (same model as AccelStepper: one rate for speeding up
// and slowing down; each rep is cut off at 2x its time, as in v1).
function estimateMove(p, lim) {
  const revs = Math.abs(p.angle) / 360;
  if (!(revs > 0) || !(p.time > 0)) return null;
  let rpm = revs / (p.time / 60000);
  const clamped = rpm > lim.maxSpeedRpm;
  if (clamped) rpm = lim.maxSpeedRpm;
  const accel = Math.min(p.accel > 0 ? p.accel : lim.defaultAccelRpmS, lim.maxAccelRpmS);
  const v = rpm / 60, a = accel / 60;  // rev/s, rev/s^2
  const moveS = revs >= v * v / a ? revs / v + v / a : 2 * Math.sqrt(revs / a);
  const cutoffS = 2 * p.time / 1000;
  const perRepS = Math.min(moveS, cutoffS);
  return { rpm, clamped, moveS: perRepS, cutOff: moveS > cutoffS, totalS: perRepS * p.reps + Math.max(0, p.reps - 1) * p.delay / 1000 };
}

function formatSeconds(s) {
  return s < 60 ? s.toFixed(1) + ' s' : Math.floor(s / 60) + ' min ' + Math.round(s % 60) + ' s';
}

function updateSummary() {
  const lim = (app.info && app.info.limits) ||
    { maxSpeedRpm: 60, maxAccelRpmS: 30, defaultAccelRpmS: 10 };
  const est = estimateMove({
    angle: readNumber('angle', NaN), time: readNumber('time', NaN), reps: Math.max(1, readNumber('repetitions', 1)),
    delay: Math.max(0, readNumber('repDelay', 0)), accel: readNumber('acceleration', NaN),
  }, lim);
  const box = $('moveSummary');
  if (!est) { box.replaceChildren(); return; }
  const parts = [el('span', { text: '≈ ' + est.rpm.toFixed(1) + ' RPM · each move ≈ ' + formatSeconds(est.moveS) +
                                    ' · whole sequence ≈ ' + formatSeconds(est.totalS) })];
  if (est.clamped) {
    parts.push(el('span', { class: 'warn', text: 'Faster than the ' + lim.maxSpeedRpm + ' RPM limit, so it will run at ' +
                                                 lim.maxSpeedRpm + ' RPM instead.' }));
  }
  if (est.cutOff) {
    parts.push(el('span', { class: 'warn', text: 'Too short to finish at this acceleration: the move will stop before the full angle. ' +
                                                 'Use a longer time or a higher acceleration.' }));
  }
  box.replaceChildren(...parts);
}

for (const id of ['repetitions', 'angle', 'time', 'repDelay', 'acceleration']) {
  $(id).addEventListener('input', updateSummary);
}

$('rotateForm').addEventListener('submit', async e => {
  e.preventDefault();
  const cmd = {
    cmd: 'rotate',
    repetitions: readNumber('repetitions', 1),
    angle: readNumber('angle', NaN),
    time: readNumber('time', NaN),
    repDelay: readNumber('repDelay', 0),
    direction: document.querySelector('input[name="direction"]:checked').value,
  };
  const accel = readNumber('acceleration', null);
  const decel = readNumber('deceleration', null);
  if (accel !== null) cmd.acceleration = accel;
  if (decel !== null) cmd.deceleration = decel;

  const problems = [];
  if (!Number.isInteger(cmd.angle) || cmd.angle < 0) problems.push('Angle must be a whole number of degrees.');
  if (!Number.isInteger(cmd.time) || cmd.time < 100) problems.push('Movement time must be at least 100 ms.');
  if (!Number.isInteger(cmd.repetitions) || cmd.repetitions < 1) problems.push('Repetitions must be 1 or more.');
  if (!Number.isInteger(cmd.repDelay) || cmd.repDelay < 0) problems.push('Delay must be 0 or more.');
  if (problems.length) {
    showReportMessage(problems.join(' '), 'warn');
    return;
  }

  try {
    const reply = await sendCommand(cmd);
    if (reply.ok) {
      app.myRun = reply.run;
      show('reportCard', false);
    } else {
      showReportMessage(reply.message || reply.error, 'warn');
    }
  } catch (err) {
    showReportMessage(err.message, 'err');
  }
});

$('stopBtn').addEventListener('click', () => {
  // Stop is idempotent on the device, so pressing it repeatedly is safe.
  sendCommand({ cmd: 'stop' }).catch(err => showReportMessage('Stop failed: ' + err.message, 'err'));
});

function showReportMessage(text, cls) {
  const card = $('reportCard');
  card.replaceChildren(el('div', { class: cls || '', text }));
  card.hidden = false;
}

function onMotion(m) {
  if (!m) return;
  const active = m.state === 'moving' || m.state === 'delay';
  show('progressCard', active);
  $('rotateBtn').disabled = active;
  if (active) {
    $('progressText').textContent = 'Rep ' + m.rep + ' of ' + m.reps + (m.state === 'delay' ? ' (pause)' : '') +
                                    ' · ' + (m.elapsed / 1000).toFixed(1) + ' s';
    const bar = $('progressBar');
    bar.max = m.reps;
    bar.value = Math.max(0, m.rep - (m.state === 'moving' ? 0.5 : 0));
  } else if ((m.state === 'done' || m.state === 'stopped') && m.run === app.myRun) {
    renderReport(m);
  }
}

function renderReport(m) {
  const totalTime = m.elapsed > 10000 ? (m.elapsed / 1000).toFixed(1) + 's' : m.elapsed + 'ms';
  const dir = m.direction === 'reverse' ? 'Reverse' : 'Forward';
  const card = $('reportCard');
  const block = (title, items) => [el('h4', { text: title }), el('ul', {}, ...items.map(t => el('li', { text: t })))];
  card.replaceChildren(
    el('h4', { class: m.state === 'stopped' ? 'warn' : 'ok', text: m.state === 'stopped' ? 'Sequence Stopped' : 'Sequence Complete!' }),
    ...(m.warnings ? [el('div', { class: 'warn', text: m.warnings })] : []),
    ...block('Movement', [m.reps + ' repetitions', m.angle + '° per rep (' + dir + ')', 'Total angle: ' + (m.angle * m.reps).toFixed(1) + '°']),
    ...block('Timing', ['Movement time per rep: ' + m.time + 'ms', 'Delay between reps: ' + m.repDelay + 'ms', 'Total sequence time: ' + totalTime]),
    ...block('Motion Profile', ['Target speed: ' + m.speedRpm + ' RPM', 'Acceleration: ' + m.accel + ' RPM/s', 'Deceleration: ' + m.decel + ' RPM/s']),
    ...block('Technical', ['Steps moved: ' + m.steps, 'Actual angle: ' + m.actualAngle + '°']),
  );
  card.hidden = false;
}

// ---------------------------------------------------------------- Wi-Fi

function onWifi(w, test) {
  if (!w) return;
  app.wifi = w;
  const box = $('wifiStatus');
  const lines = [];
  if (w.state === 'connected') {
    lines.push(el('div', { class: 'ok', text: 'Connected to ' + w.ssid }));
    const link = el('a', { href: 'http://' + w.ip + '/', target: '_blank', rel: 'noopener', text: 'http://' + w.ip });
    lines.push(el('div', {}, 'Web control panel: ', link));
    lines.push(el('div', { class: 'hint', text: 'Also at http://' + w.host + ' on most computers. Signal ' + w.rssi + ' dBm, channel ' + w.channel + '.' }));
  } else if (w.state === 'not_configured') {
    lines.push(el('div', { class: 'warn', text: 'No Wi-Fi network set up yet.' }));
  } else if (w.state === 'testing') {
    lines.push(el('div', { text: 'Trying the new network...' }));
  } else {
    lines.push(el('div', { class: 'warn', text: 'Not connected to ' + (w.ssid || 'Wi-Fi') + (w.state === 'connecting' ? ' (connecting...)' : '') }));
    if (w.reasonText) lines.push(el('div', { class: 'hint', text: 'Last problem: ' + w.reasonText }));
  }
  if (w.hotspot) {
    lines.push(el('div', { class: 'hint', text: 'Setup hotspot "' + w.hotspotSsid + '" is on (' + w.hotspotIp + ').' }));
  }
  box.replaceChildren(...lines);

  const locked = $('wifiLocked');
  locked.hidden = w.setupAllowed;
  locked.textContent = 'Wi-Fi settings are locked while the turntable is connected. ' +
                       'To change network, power-cycle the turntable, then make the change within 10 minutes.';

  if (test && app.testId !== null && test.id === app.testId) onTestResult(test);
  renderDeviceInfo();
}

function onTestResult(t) {
  if (t.state === 'pending') return;
  $('wifiConnectBtn').disabled = false;
  if (t.state === 'ok') {
    const link = el('a', { href: 'http://' + t.ip + '/', target: '_blank', rel: 'noopener', text: 'http://' + t.ip });
    $('wifiResult').replaceChildren(el('div', { class: 'ok' }, 'Connected to ' + t.ssid + '. Saved. Web control panel: ', link));
  } else {
    $('wifiResult').replaceChildren(el('div', { class: 'err', text: 'Could not connect to ' + t.ssid + ': ' + t.reasonText +
      '. The previous network settings were kept.' }));
  }
  app.testId = null;
}

$('scanBtn').addEventListener('click', async () => {
  app.scanBuffer = [];
  $('scanBtn').disabled = true;
  $('scanBtn').textContent = 'Scanning...';
  try {
    await sendCommand({ cmd: 'wifi_scan' });
  } catch (err) {
    resetScanButton();
    setText('wifiResult', err.message, 'err');
  }
  setTimeout(resetScanButton, 20000);  // safety net if scan_done never arrives
});

function resetScanButton() {
  $('scanBtn').disabled = false;
  $('scanBtn').textContent = 'Scan for networks';
}

function onScanDone(msg) {
  resetScanButton();
  const nets = app.scanBuffer;
  app.scanBuffer = [];
  const list = $('netList');
  if (!nets.length) {
    list.replaceChildren(el('li', { class: 'hint', text: 'No networks found. 5 GHz-only networks are not shown.' }));
    return;
  }
  list.replaceChildren(...nets.map(n => {
    const bars = n.rssi >= -55 ? '▂▄▆█' : n.rssi >= -67 ? '▂▄▆' : n.rssi >= -78 ? '▂▄' : '▂';
    const item = el('li', {},
      el('span', { text: (n.secure ? '🔒 ' : '') + n.ssid }),
      el('span', { class: 'meta', text: bars + '  ch ' + n.channel }));
    item.addEventListener('click', () => selectNetwork(n.ssid, item));
    return item;
  }));
  if (msg.count > nets.length) {
    list.append(el('li', { class: 'hint', text: 'Some networks may be missing. Scan again if yours is not listed.' }));
  }
}

function selectNetwork(ssid, item) {
  for (const li of document.querySelectorAll('#netList li')) li.classList.toggle('selected', li === item);
  $('ssid').value = ssid;
  show('wifiForm', true);
  $('pass').value = '';
  $('pass').focus();
}

$('manualBtn').addEventListener('click', () => {
  show('wifiForm', true);
  $('ssid').value = '';
  $('ssid').focus();
});

$('showPass').addEventListener('change', e => { $('pass').type = e.target.checked ? 'text' : 'password'; });

$('wifiForm').addEventListener('submit', async e => {
  e.preventDefault();
  const ssid = $('ssid').value;
  const pass = $('pass').value;
  if (!ssid.trim()) { setText('wifiResult', 'Enter the network name.', 'warn'); return; }
  if (pass.length > 0 && pass.length < 8) {
    setText('wifiResult', 'Wi-Fi passwords are normally at least 8 characters. Check the password.', 'warn');
  }
  $('wifiConnectBtn').disabled = true;
  setText('wifiResult', 'Connecting the turntable to ' + ssid + '... this can take up to 30 seconds.');
  try {
    const reply = await sendCommand({ cmd: 'wifi_set', ssid, pass });
    if (reply.ok) {
      app.testId = reply.test;
    } else {
      $('wifiConnectBtn').disabled = false;
      setText('wifiResult', reply.message || reply.error, 'err');
    }
  } catch (err) {
    $('wifiConnectBtn').disabled = false;
    setText('wifiResult', err.message, 'err');
  }
});

// ---------------------------------------------------------------- Device

function onInfo(info) {
  if (!info) return;
  app.info = info;
  if (info.proto !== PROTO) protoMismatch(info.proto);
  const p = info.limits;
  $('acceleration').placeholder = p.defaultAccelRpmS.toFixed(1) + ' (default)';
  $('deceleration').placeholder = p.defaultDecelRpmS.toFixed(1) + ' (default)';
  if (info.wifi) onWifi(info.wifi);
  renderDeviceInfo();
  updateSummary();
}

function renderDeviceInfo() {
  const info = app.info;
  if (!info) return;
  const w = app.wifi || info.wifi || {};
  const rows = [
    ['Device', info.device],
    ['Firmware', info.fw + ' (protocol ' + info.proto + ')'],
    ['Max speed', info.limits.maxSpeedRpm + ' RPM'],
    ['Max acceleration', info.limits.maxAccelRpmS + ' RPM/s'],
    ['Wi-Fi', w.state === 'connected' ? w.ssid + ' (' + w.ip + ')' : (w.state || '-').replace('_', ' ')],
    ['Last restart', info.resetReason.replace('_', ' ')],
    ['Uptime at connect', formatUptime(info.uptime)],
  ];
  if (info.lastFault) rows.push(['Last problem', 'Restarted itself: ' + info.lastFault]);
  $('deviceInfo').replaceChildren(...rows.flatMap(([k, v]) => [el('dt', { text: k }), el('dd', { text: String(v) })]));
}

function formatUptime(sec) {
  const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60);
  return h ? h + ' h ' + m + ' min' : m + ' min';
}

$('copyDiagBtn').addEventListener('click', async () => {
  try {
    const reply = await sendCommand({ cmd: 'info' });
    onInfo(reply.info);
    const text = JSON.stringify({ app: APP_VERSION, userAgent: navigator.userAgent, ...reply.info }, null, 2);
    await navigator.clipboard.writeText(text);
    setText('copyDiagResult', 'Copied. Paste it into an email or message to your support contact.', 'ok');
  } catch (err) {
    setText('copyDiagResult', 'Could not copy: ' + err.message, 'err');
  }
});

let forgetArmed = false;
$('forgetBtn').addEventListener('click', async () => {
  if (!forgetArmed) {
    forgetArmed = true;
    $('forgetBtn').textContent = 'Tap again to forget Wi-Fi';
    setTimeout(() => { forgetArmed = false; $('forgetBtn').textContent = 'Forget Wi-Fi network'; }, 4000);
    return;
  }
  forgetArmed = false;
  $('forgetBtn').textContent = 'Forget Wi-Fi network';
  try {
    const reply = await sendCommand({ cmd: 'wifi_forget' });
    setText('forgetResult', reply.ok ? 'Wi-Fi forgotten. The setup hotspot is now on.' : (reply.message || reply.error),
            reply.ok ? 'ok' : 'err');
  } catch (err) {
    setText('forgetResult', err.message, 'err');
  }
});

// ---------------------------------------------------------------- Start

$('connectBtn').addEventListener('click', connect);
$('disconnectBtn').addEventListener('click', disconnect);
$('appVersion').textContent = APP_VERSION;
setConn('', 'Not connected');
updateSummary();
if (!navigator.bluetooth) {
  help('This browser can\'t use Bluetooth. Open this page in <b>Chrome</b> or <b>Edge</b> on Android, Windows or Mac. ' +
       '(On iPhone/iPad, use the free <b>Bluefy</b> browser.)');
}
