export const ws = $state({
  // Start in 'reconnecting' so the very first render doesn't briefly flash a
  // red 'disconnected' dot before initWs() flips to the actual connecting
  // state. The first user-visible state is the orange pulsing dot.
  state: 'reconnecting',
  status: {},
  lastError: '',
  lastEvent: null,
  responses: {},
  lastMessageAt: 0,
  lastConnectedAt: 0,
  lastDisconnectAt: 0,
  hasEverDisconnected: false,
});

let socket = null;
let reconnectDelay = 1000;
let reconnectTimer = null;
let deadPeerTimer = null;
const DEAD_PEER_TIMEOUT_MS = 3000;

function getWsUrl() {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${proto}//${location.host}/ws`;
}

function connect() {
  ws.state = 'reconnecting';
  try {
    socket = new WebSocket(getWsUrl());
  } catch {
    ws.state = 'disconnected';
    scheduleReconnect();
    return;
  }

  socket.onopen = () => {
    ws.state = 'connected';
    ws.lastConnectedAt = Date.now();
    ws.lastError = '';
    reconnectDelay = 1000;
    armDeadPeerTimer();
  };

  socket.onmessage = (e) => {
    ws.lastMessageAt = Date.now();
    armDeadPeerTimer();
    try {
      const msg = JSON.parse(e.data);
      if (msg.type === 'status') {
        ws.status = msg.data;
      } else if (msg.type === 'error') {
        // Command-level error (bad target, conflict, etc.) — surface it but
        // do NOT drop the WS connection; the link itself is fine.
        ws.lastError = msg.message || 'WebSocket error';
        return;
      } else if (msg.type === 'event') {
        ws.lastEvent = msg;
        applyEventToStatus(msg);
      } else if (msg.type === 'config' || msg.type === 'config_saved' || msg.type === 'pong') {
        const key = msg.reqId || msg.type;
        ws.responses = { ...ws.responses, [key]: msg };
      }
    } catch {}
  };

  socket.onclose = () => {
    clearDeadPeerTimer();
    if (ws.state !== 'disconnected') {
      ws.state = 'disconnected';
      ws.lastDisconnectAt = Date.now();
      ws.hasEverDisconnected = true;
    }
    scheduleReconnect();
  };

  socket.onerror = () => {
    socket.close();
  };
}

function armDeadPeerTimer() {
  clearDeadPeerTimer();
  deadPeerTimer = setTimeout(() => {
    deadPeerTimer = null;
    ws.lastError = '';   // dead-peer is silent timeout, not an explicit error
    ws.state = 'disconnected';
    ws.lastDisconnectAt = Date.now();
    ws.hasEverDisconnected = true;
    if (socket) {
      try { socket.close(); } catch {}
      socket = null;
    }
    scheduleReconnect();
  }, DEAD_PEER_TIMEOUT_MS);
}

function clearDeadPeerTimer() {
  if (deadPeerTimer) { clearTimeout(deadPeerTimer); deadPeerTimer = null; }
}

function applyEventToStatus(msg) {
  if (msg.event !== 'hardware_command') return;
  const action = msg.action;
  const target = msg.target;
  if (!action || !target) return;

  const next = {
    ...ws.status,
    areas: { ...(ws.status?.areas || {}) },
    filling: { ...(ws.status?.filling || {}) }
  };

  if (target === 'Grass' || target === 'Drip') {
    const area = { ...(next.areas[target] || {}) };
    if (action === 'start') {
      area.running = true;
      area.manuallyStarted = true;
      area.scheduleActive = -1;
    } else if (action === 'stop') {
      area.running = false;
      area.manuallyStarted = false;
    } else if (action === 'zone_next' && target === 'Grass' && msg.zone != null) {
      area.zone = msg.zone;
    }
    next.areas[target] = area;
    ws.status = next;
    return;
  }

  if (target === 'Filling') {
    const filling = { ...(next.filling || {}) };
    if (action === 'start') {
      filling.running = true;
      filling.manuallyStarted = true;
      filling.scheduleActive = -1;
    } else if (action === 'stop') {
      filling.running = false;
      filling.manuallyStarted = false;
    }
    next.filling = filling;
    ws.status = next;
  }
}

function scheduleReconnect() {
  if (reconnectTimer) return;
  ws.state = 'reconnecting';
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    reconnectDelay = Math.min(reconnectDelay * 2, 30000);
    connect();
  }, reconnectDelay);
}

export function sendCommand(action, target, extra = {}) {
  if (socket?.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ type: 'command', action, target, ...extra }));
  }
}

export function initWs() {
  if (!socket && !reconnectTimer) {
    connect();
  }
}
