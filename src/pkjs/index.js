// WristWake - JavaScript part (runs in the Pebble app on the phone)
//
// Tasks:
// - receive the watch state and send it to Home Assistant (mobile_app integration)
// - register the device with Home Assistant once; the token is discarded afterwards
// - without network, remember the last state and send it next time
// - pass settings from the settings page on to the watch
//
// The watch is the source of truth. Only a copy of the state and the webhook addresses
// are stored here (never the token).

var LS_STATE = 'ww_state';      // last state of the watch
var LS_HA = 'ww_ha';            // {url, webhook_id, cloudhook_url, remote_ui_url, device_name}
var LS_PENDING = 'ww_pending';  // true = last state has not reached Home Assistant yet
var LS_DISABLED = 'ww_disabled';// entities disabled in Home Assistant (unique_id -> true)
var LS_NAMES = 'ww_sensor_names'; // display names registered with Home Assistant (unique_id -> name)

var NAME_SEPARATOR = '\x1f';
var DAY_KEYS = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun'];

function load(key, fallback) {
  try {
    var raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : fallback;
  } catch (e) {
    return fallback;
  }
}

function store(key, value) {
  if (value === null || value === undefined) localStorage.removeItem(key);
  else localStorage.setItem(key, JSON.stringify(value));
}

var state = load(LS_STATE, null);
var ha = load(LS_HA, null);

function log(text) {
  console.log('[WristWake] ' + text);
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

function request(method, url, headers, body, callback) {
  var xhr = new XMLHttpRequest();
  xhr.open(method, url, true);
  xhr.timeout = 15000;
  xhr.setRequestHeader('Content-Type', 'application/json');
  for (var name in headers) xhr.setRequestHeader(name, headers[name]);
  xhr.onload = function () {
    var data = null;
    try { data = JSON.parse(xhr.responseText); } catch (e) { /* empty or not JSON */ }
    callback(null, xhr.status, data);
  };
  xhr.onerror = function () { callback('network'); };
  xhr.ontimeout = function () { callback('timeout'); };
  xhr.send(body ? JSON.stringify(body) : null);
}

function normalizeUrl(url) {
  url = (url || '').replace(/^\s+|\s+$/g, '');
  if (url && !/^https?:\/\//i.test(url)) url = 'http://' + url;
  return url.replace(/\/+$/, '');
}

// Order as documented by Home Assistant: cloudhook_url, then remote_ui_url, then the entered address
function webhookUrls() {
  var urls = [];
  if (!ha) return urls;
  if (ha.cloudhook_url) urls.push(ha.cloudhook_url);
  if (ha.remote_ui_url) urls.push(ha.remote_ui_url + '/api/webhook/' + ha.webhook_id);
  if (ha.url) urls.push(ha.url + '/api/webhook/' + ha.webhook_id);
  return urls;
}

// Webhook call; on network errors try the next address
function sendWebhook(payload, callback) {
  var urls = webhookUrls();
  var index = 0;
  function attempt() {
    if (index >= urls.length) return callback('network');
    request('POST', urls[index], {}, payload, function (err, status, data) {
      if (err || status >= 500 || status === 404) {
        index++;
        return attempt();
      }
      if (status === 410) {  // registration was deleted in Home Assistant
        ha.deleted = true;
        store(LS_HA, ha);
        return callback('gone');
      }
      callback(null, status, data);
    });
  }
  attempt();
}

// ---------------------------------------------------------------------------
// Sensors (18: next alarm, alarm 1-8, alarm 1-8 enabled, status)
// ---------------------------------------------------------------------------

function pad(n) {
  return (n < 10 ? '0' : '') + n;
}

function iso(timestamp) {
  return timestamp ? new Date(timestamp * 1000).toISOString() : null;
}

function dayList(mask) {
  var days = [];
  for (var i = 0; i < 7; i++) if (mask & (1 << i)) days.push(DAY_KEYS[i]);
  return days;
}

function alarmAttributes(a) {
  if (!a.u) return { slot: a.s, used: false };
  return {
    slot: a.s, used: true, name: a.n || '', time: pad(a.h) + ':' + pad(a.m),
    days: dayList(a.d), enabled: !!a.e, sound: a.so, vibrate: !!a.vi, pattern: a.p, snooze: a.z,
    'protected': !!a.pr
  };
}

function alarmBySlot(st, slot) {
  for (var i = 0; i < st.a.length; i++) if (st.a[i].s === slot) return st.a[i];
  return null;
}

// Full sensor descriptions; updates only use state/icon/attributes
function buildSensors(st) {
  var de = st.lang === 'de';
  var id = st.id;
  var sensors = [];

  var next = null;
  st.a.forEach(function (a) {
    if (a.u && a.e && a.nx && (!next || a.nx < next.nx)) next = a;
  });
  sensors.push({
    type: 'sensor', unique_id: id + '_next', name: de ? 'Nächster Wecker' : 'Next alarm',
    device_class: 'timestamp', icon: 'mdi:alarm', state: next ? iso(next.nx) : null,
    attributes: {
      slot: next ? next.s : null, name: next ? next.n : null,
      alarms: st.a.filter(function (a) { return a.u; }).map(alarmAttributes)
    }
  });

  st.a.forEach(function (a) {
    var active = !!(a.u && a.e);
    // Display name including the alarm name, e.g. "Alarm 1 · Work"; the entity ID stays the same
    var label = (de ? 'Wecker ' : 'Alarm ') + a.s + (a.u && a.n ? ' · ' + a.n : '');
    sensors.push({
      type: 'sensor', unique_id: id + '_wecker_' + a.s, name: label,
      device_class: 'timestamp', icon: active ? 'mdi:alarm' : 'mdi:alarm-off',
      state: active && a.nx ? iso(a.nx) : null, attributes: alarmAttributes(a)
    });
    sensors.push({
      type: 'binary_sensor', unique_id: id + '_wecker_' + a.s + '_aktiv',
      name: label + (de ? ' aktiv' : ' active'),
      icon: active ? 'mdi:alarm-check' : 'mdi:alarm-off', state: active,
      attributes: { slot: a.s, name: a.u ? (a.n || '') : '' }
    });
  });

  var statusAlarm = st.ss ? alarmBySlot(st, st.ss) : null;
  sensors.push({
    type: 'sensor', unique_id: id + '_status', name: 'Status',
    icon: st.st === 'ringing' ? 'mdi:alarm-bell' : (st.st === 'snoozed' ? 'mdi:alarm-snooze' : 'mdi:alarm-note'),
    state: st.st,
    attributes: { slot: st.ss || null, name: statusAlarm && statusAlarm.u ? (statusAlarm.n || '') : null }
  });
  return sensors;
}

function watchInfo() {
  try { return Pebble.getActiveWatchInfo() || {}; } catch (e) { return {}; }
}

function firmwareVersion() {
  var fw = watchInfo().firmware;
  return fw ? fw.major + '.' + fw.minor + '.' + fw.patch + (fw.suffix ? '-' + fw.suffix : '') : 'unknown';
}

function registrationInfo(deviceName) {
  return {
    app_version: state ? state.v : '0',
    device_name: deviceName,
    manufacturer: 'Core Devices',
    model: 'Pebble Time 2',
    os_version: firmwareVersion()
  };
}

// ---------------------------------------------------------------------------
// Home Assistant: registration and updates
// ---------------------------------------------------------------------------

// Register sensors one by one (Home Assistant only accepts single ones); retry failed ones once.
// Registering an existing sensor again updates its display name.
// Registered names are remembered so that only changes are registered again.
function registerSensorList(list, callback) {
  var queue = list.slice();
  var retried = {};
  var failed = 0;
  var names = load(LS_NAMES, {});
  function nextSensor() {
    var sensor = queue.shift();
    if (!sensor) {
      store(LS_NAMES, names);
      return callback(failed ? 'sensors' : null);
    }
    sendWebhook({ type: 'register_sensor', data: sensor }, function (err, status) {
      if (err === 'gone') return callback('gone');
      if (err || (status !== 200 && status !== 201)) {
        if (!retried[sensor.unique_id]) {
          retried[sensor.unique_id] = true;
          queue.push(sensor);
        } else {
          failed++;
        }
      } else {
        names[sensor.unique_id] = sensor.name;
      }
      nextSensor();
    });
  }
  nextSensor();
}

function registerSensors(callback) {
  registerSensorList(buildSensors(state), callback);
}

// The token is only used here and discarded afterwards (never stored, never logged).
function connect(url, token, deviceName, callback) {
  if (!state) return callback('no_state');
  url = normalizeUrl(url);
  if (!url || !token) return callback('missing');
  var body = registrationInfo(deviceName || state.wn);
  body.device_id = 'wristwake_' + state.id;
  body.app_id = 'wristwake';
  body.app_name = 'WristWake';
  body.os_name = 'PebbleOS';
  body.supports_encryption = false;
  request('POST', url + '/api/mobile_app/registrations', { Authorization: 'Bearer ' + token }, body,
    function (err, status, data) {
      token = null;
      if (err) return callback(err === 'timeout' ? 'timeout' : 'network');
      if (status === 401 || status === 403) return callback('auth');
      if (status !== 200 && status !== 201 || !data || !data.webhook_id) return callback('http_' + status);
      ha = {
        url: url, webhook_id: data.webhook_id, cloudhook_url: data.cloudhook_url || null,
        remote_ui_url: data.remote_ui_url || null, device_name: body.device_name
      };
      store(LS_HA, ha);
      store(LS_DISABLED, {});
      store(LS_NAMES, {});
      log('Registered with Home Assistant');
      registerSensors(function (sensorErr) {
        if (sensorErr) return callback('sensors');
        pushState(function () { callback(null); });
      });
    });
}

function pushState(callback) {
  callback = callback || function () {};
  if (!ha || ha.deleted || !state) return callback('not_connected');
  // Has an alarm name changed? Then register these sensors with the new name first.
  var known = load(LS_NAMES, {});
  var renamed = buildSensors(state).filter(function (s) { return known[s.unique_id] !== s.name; });
  if (renamed.length) {
    return registerSensorList(renamed, function (err) {
      if (err === 'gone' || err === 'network') {
        store(LS_PENDING, true);
        return callback(err);
      }
      sendUpdates(callback);
    });
  }
  sendUpdates(callback);
}

function sendUpdates(callback) {
  var disabled = load(LS_DISABLED, {});
  var data = buildSensors(state).filter(function (s) { return !disabled[s.unique_id]; })
    .map(function (s) {
      return { type: s.type, unique_id: s.unique_id, state: s.state, icon: s.icon, attributes: s.attributes };
    });
  sendWebhook({ type: 'update_sensor_states', data: data }, function (err, status, result) {
    if (err) {
      store(LS_PENDING, true);
      log('Home Assistant not reachable, state will be sent later (' + err + ')');
      return callback(err);
    }
    store(LS_PENDING, false);
    var reregister = false;
    if (result) {
      for (var uid in result) {
        var r = result[uid] || {};
        if (r.is_disabled) disabled[uid] = true;
        if (r.error && r.error.code === 'not_registered') reregister = true;
      }
      store(LS_DISABLED, disabled);
    }
    if (reregister) return registerSensors(function () { callback(null); });
    callback(null);
  });
}

// On every start: query disabled entities again (in case they were re-enabled)
function refreshDisabled(callback) {
  sendWebhook({ type: 'get_config' }, function (err, status, config) {
    if (!err && config && config.entities) {
      var disabled = {};
      for (var uid in config.entities) {
        if (config.entities[uid] && config.entities[uid].disabled) disabled[uid] = true;
      }
      store(LS_DISABLED, disabled);
    }
    callback();
  });
}

function updateRegistration(deviceName) {
  if (!ha || ha.deleted || !deviceName || deviceName === ha.device_name) return;
  sendWebhook({ type: 'update_registration', data: registrationInfo(deviceName) }, function (err) {
    if (!err) {
      ha.device_name = deviceName;
      store(LS_HA, ha);
    }
  });
}

// ---------------------------------------------------------------------------
// Watch
// ---------------------------------------------------------------------------

function sendToWatch(dict, callback) {
  Pebble.sendAppMessage(dict, function () { callback(null); }, function () { callback('watch'); });
}

// Test alarm on the watch: rings with the sound and vibration pattern of the alarm, shows "Test"
function sendTest(t, callback) {
  sendToWatch({ TEST_SLOT: t.slot }, callback);
}

// Send settings from the settings page to the watch
function applySettings(s, callback) {
  var dict = {
    THEME: s.theme,
    THEME_COLORS: s.colors,
    NAMES: s.names.join(NAME_SEPARATOR),
    WATCH_NAME: s.watch_name,
    LEFT_HANDED: s.left_handed ? 1 : 0
  };
  sendToWatch(dict, function (err) {
    if (!err) updateRegistration(s.watch_name);
    callback(err);
  });
}

function haInfo() {
  if (!ha) return { connected: false };
  return {
    connected: !ha.deleted, deleted: !!ha.deleted, url: ha.url, cloud: !!ha.cloudhook_url,
    device_name: ha.device_name, pending: !!load(LS_PENDING, false)
  };
}

Pebble.addEventListener('ready', function () {
  log('ready');
  sendToWatch({ REQUEST_STATE: 1 }, function () {});
  if (ha && !ha.deleted) refreshDisabled(function () {});
});

Pebble.addEventListener('appmessage', function (e) {
  var raw = e.payload && e.payload.STATE;
  if (!raw) return;
  try {
    state = JSON.parse(raw);
  } catch (err) {
    log('Invalid state from the watch');
    return;
  }
  store(LS_STATE, state);
  pushState();
  if (typeof Pebble.sendConfigMessage === 'function') {
    Pebble.sendConfigMessage({ type: 'state', state: state, ha: haInfo() });
  }
});

// "Settings" in the Pebble app (older apps without configPage support): open the page.
// The current state is written into the page so it works without a direct connection.
var CONFIG_HTML = require('./config_html');

Pebble.addEventListener('showConfiguration', function () {
  log('Opening settings');
  var initial = JSON.stringify({ state: state, ha: haInfo() }).replace(/</g, '\\u003c');
  var html = CONFIG_HTML.replace('/*__WW_INITIAL__*/', 'INITIAL = ' + initial + ';');
  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
});

function notify(title, text) {
  if (typeof Pebble.showSimpleNotificationOnPebble === 'function') {
    Pebble.showSimpleNotificationOnPebble(title, text);
  }
}

// Fallback without a direct connection: the page sends everything at once when it closes.
Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var msg;
  try {
    msg = JSON.parse(decodeURIComponent(e.response));
  } catch (err) {
    return;
  }
  var de = state && state.lang === 'de';
  if (msg.test) {
    sendTest(msg.test, function () {});
    return;
  }
  if (msg.settings) {
    applySettings(msg.settings, function (err) {
      if (err) notify('WristWake', de ? 'Einstellungen konnten nicht an die Uhr gesendet werden.'
                                      : 'Settings could not be sent to the watch.');
    });
  }
  if (msg.disconnect) {
    ha = null;
    store(LS_HA, null);
    store(LS_PENDING, false);
  }
  if (msg.connect) {
    var token = msg.connect.token;
    msg.connect.token = null;
    connect(msg.connect.url, token, msg.connect.name, function (err) {
      notify('Home Assistant', err
        ? (de ? 'Verbindung fehlgeschlagen (' + err + ').' : 'Connection failed (' + err + ').')
        : (de ? 'Verbunden.' : 'Connected.'));
    });
    token = null;
  }
});

// Messages from the settings page (src/config/index.html) over the direct connection
Pebble.addEventListener('configmessage', function (e) {
  var msg = e.data || {};
  if (msg.type === 'get') {
    e.respond({ state: state, ha: haInfo() });
  } else if (msg.type === 'test') {
    sendTest(msg.test, function (err) {
      e.respond(err ? { error: err } : { ok: true });
    });
  } else if (msg.type === 'save') {
    applySettings(msg.settings, function (err) {
      e.respond(err ? { error: err } : { ok: true });
    });
  } else if (msg.type === 'connect') {
    var token = msg.token;
    msg.token = null;
    connect(msg.url, token, msg.name, function (err) {
      e.respond(err ? { error: err, ha: haInfo() } : { ok: true, ha: haInfo() });
    });
    token = null;
  } else if (msg.type === 'disconnect') {
    ha = null;
    store(LS_HA, null);
    store(LS_PENDING, false);
    e.respond({ ok: true, ha: haInfo() });
  } else {
    e.respond({ error: 'unknown' });
  }
});
