'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const root = path.resolve(__dirname, '..', '..');
const example = path.join(root, '.env.example');
const target = path.join(root, '.env');
const placeholder = 'SECRET=YOUR_BASE64_ENCODED_SECRET_HERE';

if (fs.existsSync(target)) {
  console.log('[mfdp] .env already exists; leaving it unchanged');
  process.exit(0);
}
if (!fs.existsSync(example)) {
  console.error('[mfdp] .env.example is missing; cannot create .env');
  process.exit(1);
}
const secret = crypto.randomBytes(32).toString('base64');
let text = fs.readFileSync(example, 'utf8');
if (!text.includes(placeholder)) {
  console.error('[mfdp] .env.example has no SECRET placeholder; copying it unchanged');
} else {
  text = text.replace(placeholder, 'SECRET=' + secret);
}
fs.writeFileSync(target, text, { mode: 0o600 });
console.log('[mfdp] created .env with a newly generated SECRET (REQUIRE_SECRET is still false)');
