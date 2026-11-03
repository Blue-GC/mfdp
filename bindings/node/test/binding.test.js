'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { MFDP, MFDPError, AuthError } = require('..');

const repo = path.resolve(__dirname, '..', '..', '..');
const bin = process.env.MFDP_BIN || path.join(repo, 'build', 'mfdp');
const goodSecret = 'c2VjcmV0LXNlY3JldC1zZWNyZXQtc2VjcmV0LTEyMzQ1Ng==';

function makeRoot(envText) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mfdp-node-'));
  fs.symlinkSync(path.join(repo, 'config'), path.join(dir, 'config'));
  fs.symlinkSync(path.join(repo, 'adapters'), path.join(dir, 'adapters'));
  if (envText !== null) fs.writeFileSync(path.join(dir, '.env'), envText);
  return dir;
}

test('a missing .env refuses to start', () => {
  const root = makeRoot(null);
  assert.throws(() => new MFDP({ root, bin }), (e) => e instanceof MFDPError && e.code === 'EnvMissing');
});

test('write, extract, inspect and verify round trip', async () => {
  const root = makeRoot('REQUIRE_SECRET=false\n');
  const mfdp = new MFDP({ root, bin });
  const source = 'def f(x):\n    return x\n\ndef g():\n    return f(1)\n';
  const w = await mfdp.write({ memory: [{ filename: 'a.py', content: source }], kwrds: 'g', output_dir: root });
  assert.match(path.basename(w.output), /^[0-9a-f]{32}\.mfdp$/);
  const ctx = await mfdp.extract(w.output, { kwrds: 'g', budget: 400 });
  assert.ok(ctx.text.includes('def g()'));
  assert.ok(ctx.tokens > 0);
  const info = await mfdp.inspect(w.output);
  assert.strictEqual(info.files[0].language, 'python');
  const ok = await mfdp.verify(w.output, [], [{ path: 'a.py', content: source }]);
  assert.strictEqual(ok.ok, true);
  const stale = await mfdp.verify(w.output, [], [{ path: 'a.py', content: source + '#' }]);
  assert.strictEqual(stale.ok, false);
  assert.strictEqual(stale.entries[0].status, 'stale');
});

test('parse errors carry code, file and line', async () => {
  const root = makeRoot('');
  const mfdp = new MFDP({ root, bin });
  await assert.rejects(
    mfdp.write({ memory: [{ filename: 'bad.py', content: 'def broken(:\n    pass\n' }], output_dir: root }),
    (e) => e instanceof MFDPError && e.code === 'ParseFailed' && e.details.line === 1
  );
});

test('REQUIRE_SECRET enforces the secret in the binding', () => {
  const root = makeRoot('REQUIRE_SECRET=true\nSECRET=' + goodSecret + '\n');
  delete process.env.MFDP_SECRET;
  assert.throws(() => new MFDP({ root, bin }), AuthError);
  assert.throws(() => new MFDP({ root, bin, secret: 'wrong' }), AuthError);
  assert.doesNotThrow(() => new MFDP({ root, bin, secret: goodSecret }));
  process.env.MFDP_SECRET = goodSecret;
  assert.doesNotThrow(() => new MFDP({ root, bin }));
  delete process.env.MFDP_SECRET;
});

test('REQUIRE_SECRET with the placeholder secret fails closed', () => {
  const root = makeRoot('REQUIRE_SECRET=true\nSECRET=YOUR_BASE64_ENCODED_SECRET_HERE\n');
  assert.throws(() => new MFDP({ root, bin, secret: 'x' }), (e) => e.code === 'ConfigError');
});

test('a missing binary is reported', () => {
  const root = makeRoot('');
  assert.throws(() => new MFDP({ root, bin: '/nonexistent/mfdp' }), (e) => e.code === 'BinaryMissing');
});
