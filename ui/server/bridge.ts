import { spawn, type ChildProcess } from 'node:child_process';
import { createInterface } from 'node:readline';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer, type WebSocket } from 'ws';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Configuration
const SIM_PATH = process.env['SIM_PATH'] ?? resolve(__dirname, '../../build/simulation/cli/econlife_cli');
const WS_PORT = parseInt(process.env['WS_PORT'] ?? '3001', 10);
const SIM_SEED = process.env['SIM_SEED'] ?? '42';
const SIM_NPCS = process.env['SIM_NPCS'] ?? '2000';
const SIM_PROVINCES = process.env['SIM_PROVINCES'] ?? '6';

let simProcess: ChildProcess | null = null;
let latestState: string | null = null; // Raw JSON string of latest state message

// ── Spawn simulation process ────────────────────────────────────────────────

function startSim(): ChildProcess {
  console.log(`[bridge] Spawning: ${SIM_PATH} --interactive --seed ${SIM_SEED} --npcs ${SIM_NPCS} --provinces ${SIM_PROVINCES}`);

  const child = spawn(SIM_PATH, [
    '--interactive',
    '--seed', SIM_SEED,
    '--npcs', SIM_NPCS,
    '--provinces', SIM_PROVINCES,
  ], {
    stdio: ['pipe', 'pipe', 'inherit'], // stdin=pipe, stdout=pipe, stderr=inherit
  });

  child.on('error', (err) => {
    console.error(`[bridge] Failed to start simulation: ${err.message}`);
    process.exit(1);
  });

  child.on('exit', (code) => {
    console.log(`[bridge] Simulation exited with code ${code}`);
    simProcess = null;
  });

  // Read JSON lines from stdout
  const rl = createInterface({ input: child.stdout! });
  rl.on('line', (line) => {
    if (!line.trim()) return;
    try {
      const msg = JSON.parse(line);
      if (msg.type === 'state') {
        latestState = line;
        broadcast(line);
      } else if (msg.type === 'ack' || msg.type === 'error') {
        broadcast(line);
      }
    } catch {
      console.error('[bridge] Failed to parse sim output:', line.slice(0, 200));
    }
  });

  return child;
}

// ── WebSocket server ────────────────────────────────────────────────────────

const wss = new WebSocketServer({ port: WS_PORT });
const clients = new Set<WebSocket>();

function broadcast(data: string) {
  for (const client of clients) {
    if (client.readyState === client.OPEN) {
      client.send(data);
    }
  }
}

wss.on('connection', (ws) => {
  console.log('[bridge] Client connected');
  clients.add(ws);

  // Send latest state to new client
  if (latestState) {
    ws.send(latestState);
  }

  ws.on('message', (raw) => {
    const msg = raw.toString();
    if (!simProcess?.stdin?.writable) {
      ws.send(JSON.stringify({ type: 'error', message: 'Simulation not running' }));
      return;
    }

    // Forward command to simulation stdin
    simProcess.stdin.write(msg + '\n');
  });

  ws.on('close', () => {
    console.log('[bridge] Client disconnected');
    clients.delete(ws);
  });
});

// ── Start ───────────────────────────────────────────────────────────────────

console.log(`[bridge] WebSocket server listening on port ${WS_PORT}`);
simProcess = startSim();

// Graceful shutdown
process.on('SIGINT', () => {
  console.log('\n[bridge] Shutting down...');
  if (simProcess?.stdin?.writable) {
    simProcess.stdin.write('{"cmd":"quit"}\n');
  }
  wss.close();
  process.exit(0);
});
