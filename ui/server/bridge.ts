import { spawn, type ChildProcess } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';
import { createReadStream, existsSync, statSync } from 'node:fs';
import { resolve, dirname, extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer, type WebSocket } from 'ws';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Configuration
const SIM_PATH = process.env['SIM_PATH'] ?? resolve(__dirname, '../../build/simulation/cli/econlife_cli');
const PORT = parseInt(process.env['PORT'] ?? '3000', 10);
const SIM_SEED = process.env['SIM_SEED'] ?? '42';
const SIM_NPCS = process.env['SIM_NPCS'] ?? '2000';
const SIM_PROVINCES = process.env['SIM_PROVINCES'] ?? '6';
const DIST_DIR = resolve(__dirname, '../dist');

let simProcess: ChildProcess | null = null;
let latestState: string | null = null;

// ── MIME types ──────────────────────────────────────────────────────────────

const MIME: Record<string, string> = {
  '.html': 'text/html',
  '.js': 'application/javascript',
  '.css': 'text/css',
  '.json': 'application/json',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
};

// ── Spawn simulation process ────────────────────────────────────────────────

function startSim(): ChildProcess {
  console.log(`[bridge] Spawning: ${SIM_PATH} --interactive --seed ${SIM_SEED} --npcs ${SIM_NPCS} --provinces ${SIM_PROVINCES}`);

  const child = spawn(SIM_PATH, [
    '--interactive',
    '--seed', SIM_SEED,
    '--npcs', SIM_NPCS,
    '--provinces', SIM_PROVINCES,
  ], {
    stdio: ['pipe', 'pipe', 'inherit'],
  });

  child.on('error', (err) => {
    console.error(`[bridge] Failed to start simulation: ${err.message}`);
    process.exit(1);
  });

  child.on('exit', (code) => {
    console.log(`[bridge] Simulation exited with code ${code}`);
    simProcess = null;
  });

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

// ── HTTP server (serves built React app) ────────────────────────────────────

function serveStatic(req: IncomingMessage, res: ServerResponse) {
  const url = req.url ?? '/';
  let filePath = resolve(DIST_DIR, url === '/' ? 'index.html' : '.' + url);

  if (!filePath.startsWith(DIST_DIR)) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }

  // SPA fallback: if file doesn't exist, serve index.html
  if (!existsSync(filePath) || statSync(filePath).isDirectory()) {
    filePath = join(DIST_DIR, 'index.html');
  }

  if (!existsSync(filePath)) {
    res.writeHead(404);
    res.end('Not found');
    return;
  }

  const ext = extname(filePath);
  const contentType = MIME[ext] ?? 'application/octet-stream';
  res.writeHead(200, { 'Content-Type': contentType });
  const stream = createReadStream(filePath);
  stream.on('error', () => {
    if (!res.headersSent) res.writeHead(500);
    res.end('Internal server error');
  });
  stream.pipe(res);
}

const httpServer = createServer(serveStatic);

// ── WebSocket server (shares HTTP server) ───────────────────────────────────

const wss = new WebSocketServer({ server: httpServer, path: '/ws' });
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

  if (latestState) {
    ws.send(latestState);
  }

  ws.on('message', (raw) => {
    const msg = raw.toString();
    if (!simProcess?.stdin?.writable) {
      ws.send(JSON.stringify({ type: 'error', message: 'Simulation not running' }));
      return;
    }
    simProcess.stdin.write(msg + '\n');
  });

  ws.on('close', () => {
    console.log('[bridge] Client disconnected');
    clients.delete(ws);
  });
});

// ── Start ───────────────────────────────────────────────────────────────────

httpServer.listen(PORT, () => {
  console.log(`[bridge] Server listening on http://localhost:${PORT}`);
  simProcess = startSim();
});

process.on('SIGINT', () => {
  console.log('\n[bridge] Shutting down...');
  if (simProcess?.stdin?.writable) {
    simProcess.stdin.write('{"cmd":"quit"}\n');
  }
  wss.close();
  httpServer.close();
  process.exit(0);
});
