'use strict';

const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');
const { MFDPError, AuthError, findRoot, loadEnv, isTrue, secretIsUsable } = require('./lib/env');
const { safeEqual } = require('./lib/secret');

class MFDP {
  constructor(options = {}) {
    this.root = options.root ? path.resolve(options.root) : findRoot(process.cwd());
    this.env = loadEnv(this.root);
    this.timeoutMs = options.timeoutMs || 15 * 60 * 1000;
    this.requireSecret = isTrue(this.env.REQUIRE_SECRET);
    if (this.requireSecret) {
      if (!secretIsUsable(this.env.SECRET)) {
        throw new MFDPError(
          'ConfigError',
          'REQUIRE_SECRET=true but SECRET in .env is missing or still the placeholder. Run: make setup'
        );
      }
      const supplied = options.secret !== undefined ? options.secret : process.env.MFDP_SECRET;
      if (typeof supplied !== 'string' || supplied.length === 0) {
        throw new AuthError('REQUIRE_SECRET=true: pass { secret } or set MFDP_SECRET');
      }
      if (!safeEqual(supplied, this.env.SECRET)) throw new AuthError('invalid secret');
    }
    const exe = process.platform === 'win32' ? 'mfdp.exe' : 'mfdp';
    this.bin = options.bin || process.env.MFDP_BIN || path.join(this.root, 'build', exe);
    if (!fs.existsSync(this.bin)) {
      throw new MFDPError('BinaryMissing', 'MFDP binary not found at ' + this.bin + '. Run: make build');
    }
  }

  rpc(method, params) {
    return new Promise((resolve, reject) => {
      const child = spawn(this.bin, ['rpc'], {
        env: { ...process.env, MFDP_ROOT: this.root },
        stdio: ['pipe', 'pipe', 'pipe']
      });
      const out = [];
      const err = [];
      let settled = false;
      const timer = setTimeout(() => {
        if (settled) return;
        settled = true;
        child.kill('SIGKILL');
        reject(new MFDPError('Timeout', 'MFDP call timed out after ' + this.timeoutMs + 'ms'));
      }, this.timeoutMs);
      child.stdout.on('data', (d) => out.push(d));
      child.stderr.on('data', (d) => err.push(d));
      child.on('error', (e) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        reject(new MFDPError('SpawnFailed', 'could not start ' + this.bin + ': ' + e.message));
      });
      child.on('close', () => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        const text = Buffer.concat(out).toString('utf8').trim();
        let reply;
        try {
          reply = JSON.parse(text);
        } catch (e) {
          reject(new MFDPError('BadResponse', 'MFDP returned invalid output: ' + Buffer.concat(err).toString('utf8').slice(0, 500)));
          return;
        }
        if (!reply.ok) {
          const e = reply.error || {};
          reject(new MFDPError(e.code || 'Internal', e.message || 'unknown error', { file: e.file, line: e.line, column: e.column }));
          return;
        }
        resolve(reply.result);
      });
      child.stdin.on('error', () => {});
      child.stdin.end(JSON.stringify({ method, params: params || {} }));
    });
  }

  async write(params) {
    const result = await this.rpc('write', params);
    result.output = result.outputs[0];
    return result;
  }

  read(artifact) {
    return this.rpc('read', { artifact });
  }

  inspect(artifact) {
    return this.rpc('inspect', { artifact });
  }

  extract(artifact, params) {
    return this.rpc('extract', { ...(params || {}), artifact });
  }

  verify(artifact, src, originals) {
    const params = { artifact };
    if (src && src.length) params.src = src;
    if (originals && originals.length) params.originals = originals;
    return this.rpc('verify', params);
  }

  info() {
    return this.rpc('info', {});
  }
}

module.exports = { MFDP, MFDPError, AuthError, findRoot, loadEnv };
