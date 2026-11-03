import * as fs from 'node:fs';
import * as path from 'node:path';
import Fastify, { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify';
import { MFDP, MFDPError, ExtractParams, WriteParams, findRoot, loadEnv } from '@bluegc/mfdp';
import {
  Client,
  HttpError,
  Scope,
  clientAllowed,
  extractSecret,
  parseClients,
  plainFileName,
  resolveInside,
  safeEqual
} from './security';

const PLACEHOLDER = 'YOUR_BASE64_ENCODED_SECRET_HERE';

function isTrue(value: string | undefined): boolean {
  return value !== undefined && ['true', '1', 'yes', 'on'].includes(value.trim().toLowerCase());
}

const STATUS: Record<string, number> = {
  InvalidArgument: 400,
  Conflict: 400,
  PathNotAllowed: 403,
  Io: 404,
  UnsupportedLanguage: 422,
  ParseFailed: 422,
  CorruptArtifact: 422,
  TruncatedArtifact: 422,
  UnsupportedVersion: 422,
  NoMatch: 422,
  AdapterUnavailable: 503,
  AdapterFailed: 502,
  TooLarge: 413,
  OutputExists: 409,
  StaleSource: 409
};

interface Settings {
  root: string;
  host: string;
  apiPort: number;
  webPort: number;
  requireSecret: boolean;
  secret: string;
  whitelist: boolean;
  clients: Client[];
  workspace: string;
  artifactDir: string;
}

function loadSettings(): Settings {
  const root = findRoot(process.cwd());
  const env = loadEnv(root);
  const requireSecret = isTrue(env.REQUIRE_SECRET);
  const secret = env.SECRET || '';
  if (requireSecret && (secret.length < 16 || secret === PLACEHOLDER)) {
    throw new Error('REQUIRE_SECRET=true but SECRET in .env is missing or still the placeholder. Run: make setup');
  }
  const whitelist = isTrue(env.CLIENTS_WHITELIST ?? env.CLIENTS_WHITLIST ?? 'true');
  let clients: Client[] = [];
  if (whitelist) {
    const file = path.resolve(root, env.AUTHORIZED_CLIENTS || './config/clients.json');
    if (!fs.existsSync(file)) throw new Error('AUTHORIZED_CLIENTS file not found: ' + file);
    clients = parseClients(JSON.parse(fs.readFileSync(file, 'utf8')));
  }
  const workspace = path.resolve(root, env.WORKSPACE_ROOT || '.');
  const artifactDir = path.resolve(root, env.ARTIFACT_DIR || './artifacts');
  fs.mkdirSync(artifactDir, { recursive: true });
  return {
    root,
    host: env.API_HOST || '127.0.0.1',
    apiPort: Number(env.API_PORT || 3847),
    webPort: Number(env.WEBPAGE_PORT || 3848),
    requireSecret,
    secret,
    whitelist,
    clients,
    workspace,
    artifactDir
  };
}

function authorize(settings: Settings, req: FastifyRequest, scope: Scope): void {
  if (settings.whitelist && !clientAllowed(settings.clients, req.ip, scope)) {
    throw new HttpError(403, 'ClientNotAuthorized', 'this client is not authorized for ' + scope);
  }
  if (settings.requireSecret) {
    const supplied = extractSecret(req.headers);
    if (!supplied || !safeEqual(supplied, settings.secret)) {
      throw new HttpError(401, 'AuthFailed', 'missing or invalid secret');
    }
  }
}

function asObject(body: unknown): Record<string, unknown> {
  if (body === null || typeof body !== 'object' || Array.isArray(body)) {
    throw new HttpError(400, 'InvalidArgument', 'request body must be a JSON object');
  }
  return body as Record<string, unknown>;
}

function stringList(value: unknown, name: string): string[] {
  if (value === undefined) return [];
  if (!Array.isArray(value) || value.some((v) => typeof v !== 'string')) {
    throw new HttpError(400, 'InvalidArgument', name + ' must be an array of strings');
  }
  return value as string[];
}

function artifactPath(settings: Settings, value: unknown): string {
  if (typeof value !== 'string') throw new HttpError(400, 'InvalidArgument', 'artifact is required');
  return resolveInside(settings.artifactDir, value, 'artifact');
}

function buildApi(settings: Settings, mfdp: MFDP): FastifyInstance {
  const app = Fastify({ logger: false, bodyLimit: 20 * 1024 * 1024 });
  const webOrigins = new Set([
    'http://127.0.0.1:' + settings.webPort,
    'http://localhost:' + settings.webPort,
    'http://' + settings.host + ':' + settings.webPort
  ]);

  app.addHook('onRequest', async (req, reply) => {
    const origin = req.headers.origin;
    if (origin && webOrigins.has(origin)) {
      reply.header('Access-Control-Allow-Origin', origin);
      reply.header('Vary', 'Origin');
      reply.header('Access-Control-Allow-Headers', 'Authorization, Content-Type, X-MFDP-Secret');
      reply.header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    }
    if (req.method === 'OPTIONS') {
      return reply.code(204).send();
    }
  });

  app.setErrorHandler((error: unknown, _req: FastifyRequest, reply: FastifyReply) => {
    if (error instanceof HttpError) {
      reply.code(error.status).send({ ok: false, error: { code: error.code, message: error.message } });
      return;
    }
    if (error instanceof MFDPError) {
      const status = STATUS[error.code] || 500;
      reply.code(status).send({ ok: false, error: { code: error.code, message: error.message, ...error.details } });
      return;
    }
    const e = error as { statusCode?: number; message?: string };
    if (e.statusCode && e.statusCode < 500) {
      reply.code(e.statusCode).send({ ok: false, error: { code: 'BadRequest', message: e.message || 'bad request' } });
      return;
    }
    reply.code(500).send({ ok: false, error: { code: 'Internal', message: 'internal error' } });
  });

  app.get('/health', async () => ({ ok: true, name: 'MFDP' }));

  app.get('/v1/info', async (req) => {
    authorize(settings, req, 'READ');
    return { ok: true, result: await mfdp.info() };
  });

  app.post('/v1/write', async (req) => {
    authorize(settings, req, 'WRITE');
    const body = asObject(req.body);
    const params: Record<string, unknown> = { ...body };
    const src = stringList(body.src, 'src').map((p) => resolveInside(settings.workspace, p, 'src'));
    params.src = src;
    delete params.output_dir;
    params.output_dir = settings.artifactDir;
    if (body.output !== undefined) {
      const names = stringList(Array.isArray(body.output) ? body.output : [body.output], 'output');
      params.output = names.map((n) => '"' + path.join(settings.artifactDir, plainFileName(n)) + '"').join(',');
    }
    if (!src.length && !Array.isArray(body.memory)) {
      throw new HttpError(400, 'InvalidArgument', 'provide src paths or memory files');
    }
    const result = await mfdp.write(params as unknown as WriteParams);
    return { ok: true, result };
  });

  app.post('/v1/read', async (req) => {
    authorize(settings, req, 'READ');
    const body = asObject(req.body);
    return { ok: true, result: await mfdp.read(artifactPath(settings, body.artifact)) };
  });

  app.post('/v1/inspect', async (req) => {
    authorize(settings, req, 'READ');
    const body = asObject(req.body);
    return { ok: true, result: await mfdp.inspect(artifactPath(settings, body.artifact)) };
  });

  app.post('/v1/extract', async (req) => {
    authorize(settings, req, 'READ');
    const body = asObject(req.body);
    const { artifact, ...rest } = body;
    return { ok: true, result: await mfdp.extract(artifactPath(settings, artifact), rest as unknown as ExtractParams) };
  });

  app.post('/v1/verify', async (req) => {
    authorize(settings, req, 'READ');
    const body = asObject(req.body);
    const src = stringList(body.src, 'src').map((p) => resolveInside(settings.workspace, p, 'src'));
    const originals = Array.isArray(body.originals) ? (body.originals as { path: string; content: string }[]) : [];
    return { ok: true, result: await mfdp.verify(artifactPath(settings, body.artifact), src, originals) };
  });

  return app;
}

function buildWebPage(settings: Settings): FastifyInstance {
  const app = Fastify({ logger: false });
  const page = fs.readFileSync(path.resolve(__dirname, '..', '..', 'public', 'index.html'), 'utf8');
  app.get('/', async (_req, reply) => reply.type('text/html; charset=utf-8').send(page));
  app.get('/config.json', async () => ({
    apiBase: 'http://127.0.0.1:' + settings.apiPort,
    requireSecret: settings.requireSecret
  }));
  return app;
}

async function main(): Promise<void> {
  const settings = loadSettings();
  const mfdp = new MFDP({ root: settings.root, secret: settings.requireSecret ? settings.secret : undefined });
  const api = buildApi(settings, mfdp);
  const web = buildWebPage(settings);
  await api.listen({ host: settings.host, port: settings.apiPort });
  await web.listen({ host: settings.host, port: settings.webPort });
  console.log('MFDP API:  http://' + settings.host + ':' + settings.apiPort);
  console.log('Web page:  http://' + settings.host + ':' + settings.webPort);
  console.log(
    'auth: secret ' + (settings.requireSecret ? 'REQUIRED' : 'off') + ', client whitelist ' + (settings.whitelist ? 'on' : 'off')
  );
}

main().catch((error: unknown) => {
  console.error('MFDP failed to start: ' + (error instanceof Error ? error.message : String(error)));
  process.exit(1);
});
