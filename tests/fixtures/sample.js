const fs = require('fs');
const { a, b: c } = require('./lib');
function main(x) { return a(x) + fs.readFileSync(x) + undefinedThing(); }
exports.hello = function (n) { return main(n); };
module.exports = { main };
