#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const root = path.resolve(__dirname, '..');

const commands = {
  build: 'Build the C++ core (CMake, or a direct compiler build when CMake is missing) into build/',
  nodejs: 'Package the Node.js binding into build/bindings/nodejs/',
  python: 'Package the Python binding into build/bindings/python/',
  setup: 'Create .env from .env.example with a fresh SECRET and run npm install',
  test: 'Run the C++, Node.js binding, API and Python binding tests',
  api: 'Build and start the HTTP API and the test web page',
  clean: 'Remove build outputs'
};

function detectPlatform() {
  switch (process.platform) {
    case 'win32':
      return 'windows';
    case 'darwin':
      return 'mac';
    case 'linux':
      return 'linux';
    default:
      return 'linux';
  }
}

function usage(exitCode) {
  const lines = ['usage: node scripts/index.js <command> [args]', '', 'commands:'];
  for (const name of Object.keys(commands)) lines.push('  ' + name.padEnd(8) + commands[name]);
  lines.push('', 'also: bindings nodejs|python   (same as the nodejs and python commands)');
  (exitCode === 0 ? console.log : console.error)(lines.join('\n'));
  process.exit(exitCode);
}

function makeExecutable(dir) {
  if (!fs.existsSync(dir)) return;
  for (const entry of fs.readdirSync(dir)) {
    if (!entry.endsWith('.sh')) continue;
    const file = path.join(dir, entry);
    try {
      fs.chmodSync(file, 0o755);
    } catch (e) {
      console.error('warning: could not chmod ' + file + ': ' + e.message);
    }
  }
}

function run(command, args) {
  const result = spawnSync(command, args, { cwd: root, stdio: 'inherit' });
  if (result.error) {
    console.error('failed to run ' + command + ': ' + result.error.message);
    return 1;
  }
  return result.status === null ? 1 : result.status;
}

function main() {
  let [command, ...args] = process.argv.slice(2);
  if (!command || command === 'help' || command === '--help' || command === '-h') usage(command ? 0 : 1);
  if (command === 'bindings') {
    const target = args.shift();
    if (target !== 'nodejs' && target !== 'python') usage(1);
    command = target;
  }
  if (!Object.prototype.hasOwnProperty.call(commands, command)) {
    console.error('unknown command: ' + command + '\n');
    usage(1);
  }
  const platform = detectPlatform();
  console.log('[mfdp] platform: ' + platform + ', command: ' + command);
  if (platform === 'windows') {
    const script = path.join(root, 'scripts', 'windows', command + '.ps1');
    const shell = process.env.MFDP_POWERSHELL || 'powershell.exe';
    process.exit(run(shell, ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, ...args]));
  }
  const dir = path.join(root, 'scripts', 'unix');
  makeExecutable(dir);
  process.exit(run('bash', [path.join(dir, command + '.sh'), ...args]));
}

main();
