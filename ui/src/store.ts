import { create } from 'zustand';
import type { SimState, SimMessage, Speed, MetricsSnapshot, ActionLogEntry } from './types';

let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let reconnectDelay = 1000;
let nextActionId = 0;

interface SimStore {
  connected: boolean;
  state: SimState | null;
  speed: Speed;
  ws: WebSocket | null;
  tickPending: boolean;
  metricsHistory: MetricsSnapshot[];
  actionLog: ActionLogEntry[];

  connect: () => void;
  disconnect: () => void;
  sendTick: () => void;
  sendAction: (actionType: string, payload: Record<string, unknown>) => void;
  setSpeed: (speed: Speed) => void;
}

export const useSimStore = create<SimStore>((set, get) => ({
  connected: false,
  state: null,
  speed: 'paused',
  ws: null,
  tickPending: false,
  metricsHistory: [],
  actionLog: [],

  connect: () => {
    const existing = get().ws;
    if (existing && existing.readyState <= WebSocket.OPEN) return;

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;
    const ws = new WebSocket(wsUrl);

    ws.onopen = () => {
      reconnectDelay = 1000;
      set({ connected: true, ws });
    };

    ws.onmessage = (event) => {
      try {
        const msg: SimMessage = JSON.parse(event.data as string);
        if (msg.type === 'state') {
          const prev = get().metricsHistory;
          const entry: MetricsSnapshot = {
            tick: msg.state.tick,
            ...msg.state.metrics,
          };
          const next = [...prev, entry];
          if (next.length > 100) next.shift();

          set({ state: msg.state, tickPending: false, metricsHistory: next });

          const { speed } = get();
          if (speed === 'play') {
            setTimeout(() => {
              get().sendTick();
            }, 500);
          } else if (speed === 'fast') {
            get().sendTick();
          }
        }
      } catch {
        // Ignore parse errors
      }
    };

    ws.onclose = () => {
      set({ connected: false, ws: null, speed: 'paused' });
      if (reconnectTimer) clearTimeout(reconnectTimer);
      reconnectTimer = setTimeout(() => {
        get().connect();
      }, reconnectDelay);
      reconnectDelay = Math.min(reconnectDelay * 2, 16000);
    };

    ws.onerror = () => {
      ws.close();
    };
  },

  disconnect: () => {
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    const { ws } = get();
    if (ws) {
      ws.close();
    }
    set({ connected: false, ws: null, speed: 'paused' });
  },

  sendTick: () => {
    const { ws, connected, tickPending } = get();
    if (!ws || !connected || tickPending) return;
    set({ tickPending: true });
    ws.send(JSON.stringify({ cmd: 'tick' }));
  },

  sendAction: (actionType, payload) => {
    const { ws, connected, state, actionLog } = get();
    if (!ws || !connected) return;
    const entry: ActionLogEntry = {
      id: nextActionId++,
      tick: state?.tick ?? 0,
      date: state?.date ?? '',
      actionType,
      payload,
      timestamp: Date.now(),
    };
    set({ actionLog: [...actionLog, entry].slice(-200) });
    ws.send(JSON.stringify({ cmd: 'action', action_type: actionType, payload }));
  },

  setSpeed: (speed) => {
    set({ speed });
    if (speed === 'step') {
      set({ speed: 'paused', tickPending: false });
      get().sendTick();
    } else if (speed === 'play' || speed === 'fast') {
      get().sendTick();
    }
  },
}));
