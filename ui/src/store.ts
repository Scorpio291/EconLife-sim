import { create } from 'zustand';
import type { SimState, SimMessage, Speed } from './types';

interface SimStore {
  connected: boolean;
  state: SimState | null;
  speed: Speed;
  ws: WebSocket | null;
  tickPending: boolean;

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

  connect: () => {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.hostname}:3001`;
    const ws = new WebSocket(wsUrl);

    ws.onopen = () => {
      set({ connected: true, ws });
    };

    ws.onmessage = (event) => {
      try {
        const msg: SimMessage = JSON.parse(event.data as string);
        if (msg.type === 'state') {
          set({ state: msg.state, tickPending: false });

          // Auto-tick in play/fast mode
          const { speed } = get();
          if (speed === 'play') {
            setTimeout(() => {
              get().sendTick();
            }, 500);
          } else if (speed === 'fast') {
            // Send next tick immediately
            get().sendTick();
          }
        }
      } catch {
        // Ignore parse errors
      }
    };

    ws.onclose = () => {
      set({ connected: false, ws: null, speed: 'paused' });
    };

    ws.onerror = () => {
      ws.close();
    };
  },

  disconnect: () => {
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
    const { ws, connected } = get();
    if (!ws || !connected) return;
    ws.send(JSON.stringify({ cmd: 'action', action_type: actionType, payload }));
  },

  setSpeed: (speed) => {
    set({ speed });
    if (speed === 'step') {
      // Single step: send one tick then go back to paused
      get().sendTick();
      set({ speed: 'paused' });
    } else if (speed === 'play' || speed === 'fast') {
      // Start the tick loop
      get().sendTick();
    }
    // 'paused' — do nothing, the auto-tick callbacks check speed
  },
}));
