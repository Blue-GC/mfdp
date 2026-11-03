import test from 'node:test';
import assert from 'node:assert';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import {
  HttpError,
  clientAllowed,
  extractSecret,
  isWithin,
  normalizeIp,
  parseClients,
  plainFileName,
  resolveInside,
  safeEqual
} from '../src/security';

test('safeEqual compares secrets without length leaks', () => {
  assert.strictEqual(safeEqual('abc', 'abc'), true);
  assert.strictEqual(safeEqual('abc', 'abd'), false);
  assert.strictEqual(safeEqual('abc', 'abcd'), false);
  assert.strictEqual(safeEqual('', ''), true);
});

test('extractSecret reads bearer tokens and the custom header', () => {
  assert.strictEqual(extractSecret({ authorization: 'Bearer s3cret' }), 's3cret');
  assert.strictEqual(extractSecret({ authorization: 'bearer   s3cret ' }), 's3cret');
  assert.strictEqual(extractSecret({ 'x-mfdp-secret': 'abc' }), 'abc');
  assert.strictEqual(extractSecret({ authorization: 'Basic abc' }), undefined);
  assert.strictEqual(extractSecret({}), undefined);
});

test('normalizeIp maps loopback forms together', () => {
  assert.strictEqual(normalizeIp('::1'), '127.0.0.1');
  assert.strictEqual(normalizeIp('::ffff:127.0.0.1'), '127.0.0.1');
  assert.strictEqual(normalizeIp('10.0.0.5'), '10.0.0.5');
});

test('client whitelist checks ip, scope and enabled flag', () => {
  const clients = parseClients({
    clients: [
      { id: 'a', ip: '127.0.0.1', scopes: ['READ', 'WRITE'], enabled: true },
      { id: 'b', ip: '10.0.0.2', scopes: ['READ'], enabled: true },
      { id: 'c', ip: '10.0.0.3', scopes: ['READ', 'WRITE'], enabled: false }
    ]
  });
  assert.strictEqual(clientAllowed(clients, '::1', 'WRITE'), true);
  assert.strictEqual(clientAllowed(clients, '10.0.0.2', 'READ'), true);
  assert.strictEqual(clientAllowed(clients, '10.0.0.2', 'WRITE'), false);
  assert.strictEqual(clientAllowed(clients, '10.0.0.3', 'READ'), false);
  assert.strictEqual(clientAllowed(clients, '10.9.9.9', 'READ'), false);
});

test('parseClients rejects malformed entries', () => {
  assert.throws(() => parseClients({}));
  assert.throws(() => parseClients({ clients: [{ id: 'x' }] }));
});

test('path sandbox blocks traversal and symlink escapes', () => {
  const base = fs.mkdtempSync(path.join(os.tmpdir(), 'mfdp-sec-'));
  const inside = path.join(base, 'work');
  const outside = path.join(base, 'secret');
  fs.mkdirSync(inside);
  fs.mkdirSync(outside);
  fs.writeFileSync(path.join(outside, 'x.txt'), 'x');
  fs.writeFileSync(path.join(inside, 'ok.py'), 'x');
  fs.symlinkSync(outside, path.join(inside, 'link'));
  assert.strictEqual(isWithin(inside, path.join(inside, 'ok.py')), true);
  assert.strictEqual(isWithin(inside, path.join(inside, 'new', 'file.mfdp')), true);
  assert.strictEqual(isWithin(inside, outside), false);
  assert.strictEqual(isWithin(inside, path.join(inside, '..', 'secret', 'x.txt')), false);
  assert.strictEqual(isWithin(inside, path.join(inside, 'link', 'x.txt')), false);
  assert.strictEqual(isWithin(inside, path.join(base, 'work-evil')), false);
  assert.strictEqual(resolveInside(inside, 'ok.py', 'src'), path.join(inside, 'ok.py'));
  assert.throws(() => resolveInside(inside, '../secret/x.txt', 'src'), (e: unknown) => e instanceof HttpError && e.status === 403);
  assert.throws(() => resolveInside(inside, '/etc/passwd', 'src'), (e: unknown) => e instanceof HttpError && e.status === 403);
  assert.throws(() => resolveInside(inside, 'link/x.txt', 'src'), (e: unknown) => e instanceof HttpError && e.status === 403);
  assert.throws(() => resolveInside(inside, 'a\0b', 'src'), (e: unknown) => e instanceof HttpError && e.status === 400);
});

test('plainFileName only accepts bare names', () => {
  assert.strictEqual(plainFileName('abc.mfdp'), 'abc.mfdp');
  for (const bad of ['', '.', '..', 'a/b', 'a\\b', '../x', 'x\0y']) {
    assert.throws(() => plainFileName(bad), (e: unknown) => e instanceof HttpError && e.status === 400);
  }
});
