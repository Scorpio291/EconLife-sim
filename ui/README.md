# EconLife UI

React frontend + Node.js bridge server for the EconLife economic simulation.

## Architecture

```
C++ CLI (econlife_cli --interactive)
    ↕  JSON-line protocol over stdin/stdout
Node.js Bridge Server (server/bridge.ts)
    ↕  WebSocket (/ws) + HTTP static files (port 3000)
React Frontend (src/)
    └  Zustand store manages state + WebSocket lifecycle
```

The bridge spawns the C++ CLI as a child process, reads JSON-line state from
stdout, and broadcasts it to all connected WebSocket clients. Client commands
(tick, action, quit) are forwarded to the CLI's stdin.

## Quick Start

```bash
# First time setup
npm install

# Production: build + serve on http://localhost:3000
npm start

# Development: Vite HMR + bridge server
npm run dev
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `SIM_PATH` | `../../build/simulation/cli/econlife_cli` | Path to CLI binary |
| `PORT` | `3000` | HTTP + WebSocket port |
| `SIM_SEED` | `42` | World generation seed |
| `SIM_NPCS` | `2000` | Number of significant NPCs |
| `SIM_PROVINCES` | `6` | Number of provinces |

## Project Structure

```
ui/
├── server/
│   └── bridge.ts          # Node.js bridge: HTTP + WebSocket + CLI IPC
├── src/
│   ├── main.tsx            # React entry point
│   ├── App.tsx             # Layout shell, toast/log/keyboard integration
│   ├── store.ts            # Zustand store (state, metrics history, action log)
│   ├── types.ts            # TypeScript interfaces mirroring C++ JSON output
│   ├── index.css           # Dark theme, all styles
│   ├── components/
│   │   ├── TopBar.tsx           # Date, speed controls, player stats
│   │   ├── SceneCardPanel.tsx   # NPC dialogue + choice buttons
│   │   ├── CalendarPanel.tsx    # Upcoming events, accept/decline
│   │   ├── ProvincePanel.tsx    # Province list + travel buttons
│   │   ├── MetricsDashboard.tsx # Economy metrics + sparkline charts
│   │   ├── BusinessPanel.tsx    # Player businesses + sector picker
│   │   ├── Sparkline.tsx        # SVG sparkline chart (no dependencies)
│   │   ├── Toast.tsx            # Scene card notification toasts
│   │   └── ActionHistoryPanel.tsx # Collapsible action log sidebar
│   └── hooks/
│       └── useKeyboardShortcuts.ts
├── package.json
├── vite.config.ts
├── tsconfig.json            # Frontend type checking
├── tsconfig.node.json       # Server type checking
└── tsconfig.server.json     # Server compilation (emits to server-dist/)
```

## State Management

Single zustand store with these fields:

| Field | Type | Description |
|-------|------|-------------|
| `connected` | `boolean` | WebSocket connection status |
| `state` | `SimState \| null` | Latest simulation state snapshot |
| `speed` | `Speed` | `paused`, `step`, `play`, `fast` |
| `metricsHistory` | `MetricsSnapshot[]` | Rolling 100-tick window for sparklines |
| `actionLog` | `ActionLogEntry[]` | Last 200 player actions |

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Space | Toggle play/pause |
| Right Arrow | Step one tick |
| Up Arrow | Increase speed (pause → play → fast) |
| Down Arrow | Decrease speed (fast → play → pause) |

## IPC Protocol

The CLI outputs one JSON object per line on stdout:
- `{"type":"state","state":{...}}` — full UI state after each tick
- `{"type":"ack","success":true}` — action acknowledgement
- `{"type":"error","message":"..."}` — error

The bridge sends JSON commands on stdin:
- `{"cmd":"tick"}` — advance one tick
- `{"cmd":"tick","count":N}` — advance N ticks
- `{"cmd":"action","action_type":"...","payload":{...}}` — enqueue player action
- `{"cmd":"quit"}` — clean shutdown

## Tech Stack

- React 19, Zustand 5, Vite 6, TypeScript 5.6
- No CSS framework — vanilla CSS with custom properties
- No chart library — hand-rolled SVG sparklines
- ws (WebSocket library for the bridge server)
