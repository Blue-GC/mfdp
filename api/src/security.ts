import * as crypto from 'node:crypto';
import * as fs from 'node:fs';
import * as path from 'node:path';

export type Scope = 'READ' | 'WRITE';

export interface Client {
  id: string;
  name?: string;
  ip: string;
  scopes: Scope[];
  enabled: boolean;
}

export class HttpError extends Error {
  status: number;
  code: string;

  constructor(status: number, code: string, message: string) {
    super(message);
    this.status = status;
    this.code = code;
  }
}

export function safeEqual(a: string, b: string): boolean {
  const ha = crypto.createHash('sha256').update(a).digest();
  const hb = crypto.createHash('sha256').update(b).digest();
  return crypto.timingSafeEqual(ha, hb);
}

export function extractSecret(headers: Record<string, string | string[] | undefined>): string | undefined {
  const auth = headers['authorization'];
  const value = Array.isArray(auth) ? auth[0] : auth;
  if (value && /^bearer\s+/i.test(value)) return value.replace(/^bearer\s+/i, '').trim();
  const custom = headers['x-mfdp-secret'];
  const c = Array.isArray(custom) ? custom[0] : custom;
  return c ? c.trim() : undefined;
}

export function normalizeIp(ip: string): string {
  let value = ip.trim();
  if (value.startsWith('::ffff:')) value = value.slice(7);
  if (value === '::1') return '127.0.0.1';
  return value;
}

export function clientAllowed(clients: Client[], ip: string, scope: Scope): boolean {
  const target = normalizeIp(ip);
  return clients.some((c) => c.enabled && normalizeIp(c.ip) === target && c.scopes.includes(scope));
}

export function parseClients(json: unknown): Client[] {
  const list = (json as { clients?: unknown }).clients;
  if (!Array.isArray(list)) throw new Error('clients.json must contain a "clients" array');
  return list.map((entry, index) => {
    const e = entry as Partial<Client>;
    if (typeof e.id !== 'string' || typeof e.ip !== 'string' || !Array.isArray(e.scopes)) {
      throw new Error('clients.json entry ' + index + ' needs id, ip and scopes');
    }
    return {
      id: e.id,
      name: e.name,
      ip: e.ip,
      scopes: e.scopes.filter((s): s is Scope => s === 'READ' || s === 'WRITE'),
      enabled: e.enabled !== false
    };
  });
}

function realOrParent(target: string): string {
  try {
    return fs.realpathSync(target);
  } catch {
    const parent = path.dirname(target);
    if (parent === target) return target;
    return path.join(realOrParent(parent), path.basename(target));
  }
}

export function isWithin(rootDir: string, target: string): boolean {
  const root = realOrParent(path.resolve(rootDir));
  const resolved = realOrParent(path.resolve(target));
  const rel = path.relative(root, resolved);
  return rel === '' || (!rel.startsWith('..') && !path.isAbsolute(rel));
}

export function resolveInside(rootDir: string, candidate: string, what: string): string {
  if (typeof candidate !== 'string' || candidate.length === 0 || candidate.includes('\0')) {
    throw new HttpError(400, 'InvalidArgument', what + ' must be a non-empty path');
  }
  const absolute = path.resolve(rootDir, candidate);
  if (!isWithin(rootDir, absolute)) {
    throw new HttpError(403, 'PathNotAllowed', what + ' is outside the allowed directory');
  }
  return absolute;
}

export function plainFileName(name: string): string {
  if (
    typeof name !== 'string' ||
    name.length === 0 ||
    name.length > 200 ||
    name.includes('/') ||
    name.includes('\\') ||
    name.includes('\0') ||
    name === '.' ||
    name === '..'
  ) {
    throw new HttpError(400, 'InvalidArgument', 'output must be a plain file name');
  }
  return name;
}
