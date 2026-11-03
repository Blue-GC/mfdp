const path = require('path');

function normalize(text) {
  return String(text).normalize('NFKD').replace(/[\u0300-\u036f]/g, '');
}

function slugify(text, separator = '-') {
  return normalize(text)
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, separator)
    .replace(new RegExp('^' + separator + '|' + separator + '$', 'g'), '');
}

function fileSlug(filePath) {
  const base = path.basename(filePath, path.extname(filePath));
  return slugify(base);
}

module.exports = { slugify, fileSlug };
