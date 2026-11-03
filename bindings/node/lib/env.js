'use strict';

const fs = require('fs');
const path = require('path');

class MFDPError extends Error {
  constructor(code, message, details) {
    super(message);
    this.name = 'MFDPError';
    this.code = code;
    this.details = details || {};
  }
}

class AuthError extends MFDPError {
  constructor(message) {
    super('AuthFailed', message);
    this.name = 'AuthError';
  }
}

function parseEnv(text) {
  const out = {};
  for (const rawLine of text.split(/\r?\n/)) {
    let line = rawLine.trim();
    if (!line || line.startsWith('#')) continue;
    if (line.startsWith('export ')) line = line.slice(7).trim();
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    const key = line.slice(0, eq).trim();
    let value = line.slice(eq + 1).trim();
    if (!key) continue;
    if (value.startsWith('"') || value.startsWith("'")) {
      const quote = value[0];
      const close = value.indexOf(quote, 1);
      value = close < 0 ? value.slice(1) : value.slice(1, close);
    } else {
      const m = value.match(/\s#/);
      if (m) value = value.slice(0, m.index).trim();
      value = value.replace(/\\+$/, '').trim();
    }
    out[key] = value;
  }
  return out;
}

function isRoot(dir) {
  return fs.existsSync(path.join(dir, 'config', 'adapters'));
}

function findRoot(start) {
  if (process.env.MFDP_ROOT) {
    const explicit = path.resolve(process.env.MFDP_ROOT);
    if (!isRoot(explicit)) {
      throw new MFDPError('RootNotFound', 'MFDP_ROOT does not contain config/adapters: ' + explicit);
    }
    return explicit;
  }
  const starts = [start || process.cwd(), path.resolve(__dirname, '..', '..', '..')];
  for (const s of starts) {
    let dir = path.resolve(s);
    for (let i = 0; i < 10; i++) {
      if (isRoot(dir)) return dir;
      const parent = path.dirname(dir);
      if (parent === dir) break;
      dir = parent;
    }
  }
  throw new MFDPError('RootNotFound', 'could not find the MFDP project root; set MFDP_ROOT');
}

function loadEnv(root) {
  const file = path.join(root, '.env');
  if (!fs.existsSync(file)) {
    throw new MFDPError(
      'EnvMissing',
      'MFDP requires a .env file in the project root (' + file + '). Run: make setup'
    );
  }
  return parseEnv(fs.readFileSync(file, 'utf8'));
}

function isTrue(value) {
  return typeof value === 'string' && ['true', '1', 'yes', 'on'].includes(value.trim().toLowerCase());
}

const PLACEHOLDER = 'YOUR_BASE64_ENCODED_SECRET_HERE';

function secretIsUsable(secret) {
  return typeof secret === 'string' && secret.length >= 16 && secret !== PLACEHOLDER;
}

module.exports = { MFDPError, AuthError, parseEnv, findRoot, loadEnv, isTrue, secretIsUsable, PLACEHOLDER };
