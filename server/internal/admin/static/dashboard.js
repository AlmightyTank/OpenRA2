(() => {
  'use strict';
  // Memory-only token: never put it into URLs, cookies, storage or DOM attributes.
  let token = '';
  let rooms = [];
  let pending = false;
  const byId = (id) => document.getElementById(id);
  const fmt = (number) => new Intl.NumberFormat('en-US').format(Number(number) || 0);
  const set = (id, value) => { byId(id).textContent = String(value); };
  const errorBox = byId('dashboard-error');
  const loginError = byId('login-error');
  const loginPanel = byId('login-panel');
  const dashboard = byId('dashboard');

  function message(element, value) {
    element.textContent = value || '';
    element.hidden = !value;
  }

  async function request(path, options = {}) {
    const response = await fetch(path, {
      method: options.method || 'GET',
      headers: { 'Authorization': 'Bearer ' + token, 'Accept': 'application/json' },
      cache: 'no-store',
      credentials: 'omit'
    });
    let body;
    try { body = await response.json(); } catch (_) { body = {}; }
    if (!response.ok) {
      const e = new Error(body.error || ('HTTP ' + response.status));
      e.status = response.status;
      throw e;
    }
    return body;
  }

  function lock() {
    token = '';
    rooms = [];
    dashboard.hidden = true;
    loginPanel.hidden = false;
    byId('lock').hidden = true;
    byId('token').value = '';
    byId('token').focus();
    message(errorBox, '');
  }

  function setStatus(s) {
    set('rooms-total', fmt(s.rooms.Total));
    set('rooms-started', fmt(s.rooms.Started));
    set('rooms-waiting', fmt(s.rooms.Waiting));
    set('participants-total', fmt(s.rooms.Participants));
    set('v3-clients', fmt(s.v3_clients));
    set('v3-forwarded', fmt(s.v3_forwarded_total));
    set('v3-dropped', fmt(s.v3_dropped_total));
    set('v3-bytes', fmt(s.v3_bytes_total));
    set('connections-total', fmt(s.connections_total));
    set('joins-total', fmt(s.joins_total) + ' / ' + fmt(s.join_failures_total));
    set('relay-allocations', fmt(s.port_relay_allocations));
    const seconds = Number(s.uptime_seconds) || 0;
    set('uptime', Math.floor(seconds / 3600) + 'h ' + Math.floor((seconds % 3600) / 60) + 'm');
    set('updated', 'Updated ' + new Date().toLocaleTimeString());
    set('connection-label', 'Connected');
  }

  function cell(row, content) {
    const td = document.createElement('td');
    if (content != null) td.textContent = String(content);
    row.append(td);
    return td;
  }

  function roomRow(room) {
    const tr = document.createElement('tr');
    const name = cell(tr);
    const headline = document.createElement('strong');
    headline.textContent = room.name;
    const sub = document.createElement('div');
    sub.className = 'secondary-line';
    sub.textContent = room.code + ' · Host: ' +
      (room.participants.find((p) => p.role === 'host')?.name || 'Unknown');
    name.append(headline, sub);
    const details = document.createElement('details');
    const summary = document.createElement('summary');
    summary.textContent = 'View ' + room.connected + ' connection(s)';
    const members = document.createElement('ul');
    for (const participant of room.participants) {
      const li = document.createElement('li');
      const platforms = { 1: 'Windows', 2: 'Android', 3: 'Android x86', 4: 'iOS' };
      li.textContent = participant.name + ' (' + participant.role + ', ' +
        (platforms[participant.platform] || 'Platform ' + participant.platform) + ')';
      members.append(li);
    }
    details.append(summary, members);
    name.append(details);
    const status = cell(tr);
    const statusText = document.createElement('span');
    statusText.className = 'status' + (room.started ? ' started' : '');
    statusText.textContent = room.started ? 'IN PROGRESS' : 'WAITING';
    status.append(statusText);
    cell(tr, room.players + ' / ' + room.max);
    cell(tr, room.map || '—');
    const tagCell = cell(tr);
    const tag = document.createElement('span');
    tag.className = 'tag';
    tag.textContent = room.private ? 'PRIVATE' : 'PUBLIC';
    tagCell.append(tag);
    const action = cell(tr);
    const close = document.createElement('button');
    close.type = 'button';
    close.className = 'danger';
    close.textContent = 'Close room';
    close.disabled = room.started || pending;
    close.title = room.started
      ? 'Started matches cannot be ended from this directory dashboard.'
      : 'Remove waiting room from directory and notify connected players.';
    close.addEventListener('click', async () => {
      if (!window.confirm('Close waiting room "' + room.name + '" (' + room.code +
        ')? Connected participants will be notified.')) return;
      pending = true;
      close.disabled = true;
      try {
        await request('/admin/api/rooms/' + encodeURIComponent(room.code) + '/close', { method: 'POST' });
        message(errorBox, '');
        await refresh();
      } catch (e) {
        message(errorBox, 'Could not close room: ' + e.message);
        if (e.status === 401) lock();
      } finally {
        pending = false;
      }
    });
    action.append(close);
    return tr;
  }

  function renderRooms() {
    const filter = byId('room-filter').value;
    const selected = rooms.filter((room) =>
      filter === 'all' || (filter === 'started' && room.started) ||
      (filter === 'waiting' && !room.started) ||
      (filter === 'private' && room.private));
    const table = byId('room-rows');
    table.replaceChildren(...selected.map(roomRow));
    byId('rooms-empty').hidden = selected.length > 0;
  }

  async function refresh() {
    if (!token) return;
    try {
      const [status, directory] = await Promise.all([
        request('/admin/api/status'), request('/admin/api/rooms')
      ]);
      rooms = Array.isArray(directory.rooms) ? directory.rooms : [];
      setStatus(status);
      renderRooms();
      message(errorBox, '');
    } catch (e) {
      set('connection-label', 'Connection issue');
      message(errorBox, 'Refresh failed: ' + e.message);
      if (e.status === 401) lock();
      throw e;
    }
  }

  byId('login-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    token = byId('token').value;
    if (!token) return;
    try {
      await refresh();
      byId('token').value = '';
      loginPanel.hidden = true;
      dashboard.hidden = false;
      byId('lock').hidden = false;
      message(loginError, '');
    } catch (_) {
      message(loginError, 'Access denied or server unavailable. Verify the token and local connection.');
      token = '';
    }
  });
  byId('lock').addEventListener('click', lock);
  byId('refresh').addEventListener('click', () => { refresh().catch(() => {}); });
  byId('room-filter').addEventListener('change', renderRooms);
  window.setInterval(() => {
    if (token && !document.hidden && !pending) refresh().catch(() => {});
  }, 10000);
})();
