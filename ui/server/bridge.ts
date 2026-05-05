import { spawn, type ChildProcess } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';
import { createReadStream, existsSync, statSync } from 'node:fs';
import { resolve, dirname, extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { URL } from 'node:url';
import { WebSocketServer, type WebSocket } from 'ws';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Configuration
const SIM_PATH = process.env['SIM_PATH'] ?? resolve(__dirname, '../../build/simulation/cli/econlife_cli');
const PORT = parseInt(process.env['PORT'] ?? '3000', 10);
const SIM_SEED = process.env['SIM_SEED'] ?? '42';
const SIM_NPCS = process.env['SIM_NPCS'] ?? '2000';
const SIM_PROVINCES = process.env['SIM_PROVINCES'] ?? '6';
const DIST_DIR = resolve(__dirname, '../dist');

// Network / auth configuration
//
// BRIDGE_HOST: interface to bind the HTTP+WS server to. Defaults to loopback so
//   the bridge is not reachable from the LAN. Opt in to LAN access with
//   BRIDGE_HOST=0.0.0.0 (and ideally also set BRIDGE_TOKEN below).
// BRIDGE_ALLOWED_ORIGINS: comma-separated list of allowed Origin: header values
//   for browser clients. No-origin connections (curl, native test clients,
//   integration scripts) are always allowed. Defaults to localhost on PORT.
// BRIDGE_TOKEN: optional shared secret. When set, WebSocket clients must
//   connect with `?token=<value>` on the upgrade URL. Off by default.
const BRIDGE_HOST = process.env['BRIDGE_HOST'] ?? '127.0.0.1';
const ALLOWED_ORIGINS = (
  process.env['BRIDGE_ALLOWED_ORIGINS']
  ?? `http://localhost:${PORT},http://127.0.0.1:${PORT}`
).split(',').map((s) => s.trim()).filter(Boolean);
const BRIDGE_TOKEN = process.env['BRIDGE_TOKEN'] ?? '';

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

const wss = new WebSocketServer({
  server: httpServer,
  path: '/ws',
  verifyClient: ({ origin, req }, done) => {
    // Origin allow-list. Browsers send Origin; native clients (curl, test
    // scripts) typically don't, so we permit no-origin connections.
    if (origin && !ALLOWED_ORIGINS.includes(origin)) {
      console.warn(`[bridge] Rejected WS connection from disallowed origin: ${origin}`);
      done(false, 403, 'Forbidden origin');
      return;
    }

    // Optional shared-secret token check.
    if (BRIDGE_TOKEN) {
      let token: string | null = null;
      try {
        // req.url is the path+query on the upgrade request, e.g. "/ws?token=abc"
        const parsed = new URL(req.url ?? '', 'http://localhost');
        token = parsed.searchParams.get('token');
      } catch {
        token = null;
      }
      if (token !== BRIDGE_TOKEN) {
        console.warn('[bridge] Rejected WS connection: missing/invalid token');
        done(false, 401, 'Unauthorized');
        return;
      }
    }

    done(true);
  },
});
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
    const text = raw.toString();

    // Validate before forwarding to the sim's stdin. We require valid JSON
    // describing a plain object with a string `cmd` field. Anything else is
    // rejected without touching the sim.
    let parsed: unknown;
    try {
      parsed = JSON.parse(text);
    } catch {
      ws.send(JSON.stringify({ type: 'error', message: 'invalid command' }));
      return;
    }
    if (
      parsed === null
      || typeof parsed !== 'object'
      || Array.isArray(parsed)
      || typeof (parsed as { cmd?: unknown }).cmd !== 'string'
    ) {
      ws.send(JSON.stringify({ type: 'error', message: 'invalid command' }));
      return;
    }

    if (!simProcess?.stdin?.writable) {
      ws.send(JSON.stringify({ type: 'error', message: 'Simulation not running' }));
      return;
    }
    // Re-serialise the validated payload so we never forward arbitrary bytes
    // (newlines, control chars, trailing junk) to the sim.
    simProcess.stdin.write(JSON.stringify(parsed) + '\n');
  });

  ws.on('close', () => {
    console.log('[bridge] Client disconnected');
    clients.delete(ws);
  });
});

// ── Start ───────────────────────────────────────────────────────────────────

httpServer.listen(PORT, BRIDGE_HOST, () => {
  console.log(`[bridge] Server listening on http://${BRIDGE_HOST}:${PORT}`);
  if (BRIDGE_HOST === '127.0.0.1' || BRIDGE_HOST === 'localhost') {
    console.log('[bridge] Loopback only. For LAN access, set BRIDGE_HOST=0.0.0.0 (and consider BRIDGE_TOKEN).');
  }
  console.log(`[bridge] Allowed WS origins: ${ALLOWED_ORIGINS.join(', ') || '(none)'}`);
  if (BRIDGE_TOKEN) {
    console.log('[bridge] WS token auth: enabled (clients must pass ?token=...)');
  }
  simProcess = startSim();
});

// ── Graceful shutdown ───────────────────────────────────────────────────────

let shuttingDown = false;
function shutdown(signal: string) {
  if (shuttingDown) return;
  shuttingDown = true;
  console.log(`\n[bridge] Received ${signal}, shutting down...`);
  wss.close();
  httpServer.close();
  if (!simProcess) {
    process.exit(0);
    return;
  }
  if (simProcess.stdin?.writable) {
    simProcess.stdin.write('{"cmd":"quit"}\n');
  }
  const killTimer = setTimeout(() => {
    console.warn('[bridge] Sim did not exit in 2s; sending SIGKILL.');
    simProcess?.kill('SIGKILL');
  }, 2000);
  simProcess.on('exit', () => {
    clearTimeout(killTimer);
    process.exit(0);
  });
}
process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
