<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/mfdp_dark.png">
    <source media="(prefers-color-scheme: light)" srcset="assets/mfdp_light.png">
    <img alt="MFDP, Model File Definition Protocol" src="assets/mfdp_light.png" width="720">
  </picture>
</p>

# MFDP (Model File Definition Protocol)

MFDP converts source files into compact binary `.mfdp` artifacts (structured symbols, signatures, imports, dependency edges, optional source fragments and SHA-256 source hashes) so an AI model can read and edit code with far fewer tokens. On large files, selecting only the symbols you need cuts token usage by up to 90%.

The core (binary format, selection, dependency resolution, context rendering, CLI) is C++20. Parsing uses real parsers through replaceable adapters. A Fastify HTTP API and Node.js and Python bindings sit on top of one JSON entry point in the core binary.

MFDP currently targets programming languages. Support for other kinds of files and documents is planned.

## Token reduction

| Case (one 140k-token Python file) | Original | Context | Reduction |
|---|---|---|---|
| Structural overview (`--kwrds "*"`) | 140,238 | 17,730 | 87.4% |
| Three selected methods in full | 140,238 | 6,100 | 95.7% |
| One whole 13k-line class selected | 140,238 | 153,151 | -9.2% |

- Counts come from a built-in word-piece heuristic, not a model tokenizer, so treat them as approximations.
- Savings grow with file size and shrink as you select more. Selecting nearly the whole file costs more than the original, because the output adds metadata.
- Tiny files cannot reach these numbers.
- Only Python has been measured so far. Run `mfdp-bench` on your own code before relying on any figure, and please share results for other languages.

## Quick start

```
git clone https://github.com/Blue-GC/mfdp.git
cd mfdp
make setup
make build
make test
```

| Command | What it does |
|---|---|
| `make setup` | Creates `.env` with a random `SECRET`, creates `artifacts/`, runs `npm install`. Never overwrites an existing `.env`. |
| `make build` | Builds `build/mfdp` with CMake, or with a direct `g++` build when CMake is missing. |
| `make test` | Runs the C++, Node.js binding, API security and Python binding tests. |
| `make api` | Starts the HTTP API (port 3847) and the test web page (port 3848). |
| `make nodejs` | Packages the Node.js binding into `build/bindings/nodejs/`. |
| `make python` | Packages the Python binding into `build/bindings/python/`. |
| `make clean` | Removes build outputs. |

`make X` runs `node scripts/index.js X`, which detects your OS and runs the matching script from `scripts/unix/` or `scripts/windows/`. `node scripts/index.js help` lists the commands.

Try it on the bundled examples:

```
./build/mfdp write --src examples/python/inventory.py --kwrds "*" --output /tmp/inventory.mfdp
./build/mfdp extract /tmp/inventory.mfdp --kwrds "*"
./build/mfdp verify /tmp/inventory.mfdp --src examples/python/inventory.py
```

## Platform support

- **Linux:** built and tested.
- **macOS:** has `#if` guards (`pipe2`, `getrandom`, executable path lookup) that have not been compiled on a Mac. Reports welcome.
- **Windows:** the C++ core uses `fork`/`exec` and POSIX file APIs and does not build natively. Use WSL (`wsl --install`). `make setup` runs natively.

## Requirements

- A C++20 compiler (GCC 11+ or Clang 14+), zlib development headers (`zlib1g-dev`), Node.js 18+
- CMake 3.16+ is optional. Without it `make build` compiles directly with `g++`.
- `python3` 3.9+ (the Python adapter uses the standard `ast` module; 3.12 for PEP 695 syntax)
- C and C++ adapter: `pip install libclang`, or set `MFDP_LIBCLANG` to a libclang shared library. In a virtual environment, set `MFDP_PYTHON` to its interpreter.
- Go adapter: `go` 1.18+ (`MFDP_GO` overrides)
- Java adapter: a JDK 11+ (a JRE is not enough, it needs the `jdk.compiler` module), 17 recommended (`MFDP_JAVA` overrides). With an older Java the Java tests are skipped.
- Rust adapter: `cargo`. The first parse compiles `adapters/rust` and needs network for the `syn` crate (`MFDP_CARGO` overrides).
- C# adapter: .NET SDK 8+. The first parse restores Roslyn and builds `adapters/csharp` (`MFDP_DOTNET` overrides).
- TypeScript and JavaScript adapter: Node 18+ with TypeScript 5 or 6 (`npm install` at the root installs it). TypeScript 7 native builds lack the JS API. Lookup order: `MFDP_TYPESCRIPT`, `require('typescript')`, `NODE_PATH`, common global roots, `npm root -g`.

Adapters that are not installed make writes fail with `AdapterUnavailable`, and their tests are skipped. The first Rust and C# runs can take minutes, so the per-file timeout is 60 seconds normally and 900 for those two (`MFDP_ADAPTER_TIMEOUT` overrides, in seconds).

Source code is never executed by any adapter.

## Languages

Implemented with real parsers: Python (`ast`), TypeScript and JavaScript (TypeScript compiler API), C and C++ (libclang), Go (`go/parser`), Java (JDK compiler tree API), Rust (`syn`) and C# (Roslyn).

The Rust and C# adapters were written without a Rust or .NET toolchain and have not been compiled or run by the authors, so expect bugs.

Mapped but not implemented (rejected with `UnsupportedLanguage`): Ruby, PHP, Swift, Kotlin, HTML, CSS, SQL, Bash, PowerShell, JSON, YAML, TOML, XML, Vue, Svelte. The map lives in `src/core/languages.cpp`.

Per-language notes:
- **C and C++:** `.h` maps to C (use `--lang cpp` for C++ headers). Missing headers are tolerated, every `#include` is kept as a relevant import, and includes resolve names across supplied files.
- **Go:** files in the same directory form one package. Import paths are always external.
- **Java:** packages map to directories. Same-package and wildcard imports resolve across supplied files. Member calls on variables and fields are qualified by their declared types (not `var`).
- **Rust:** `use crate::...`, `self::` and `super::` resolve to supplied files. `impl` blocks attach methods to their type, and trait impls are recorded as bases.
- **C#:** all supplied files share one project scope. Member calls use declared types (not `var`).

## Configuration

Everything lives in the project root. The HTTP API and both bindings refuse to start without a `.env` file there. The C++ binary does not require it and uses defaults, so it can be embedded without one.

- **`.env`** (from `.env.example`): `APP_INFO`, `PROGRAMMING_ADAPTERS`, `API_HOST`, `API_PORT`, `WEBPAGE_PORT`, `WORKSPACE_ROOT`, `ARTIFACT_DIR`, `REQUIRE_SECRET`, `SECRET`, `CLIENTS_WHITELIST` (the older spelling `CLIENTS_WHITLIST` is also read) and `AUTHORIZED_CLIENTS`. Real environment variables override the file. Values may carry inline `# comments`.
- **`config/config.json`:** application information, shown by `mfdp version` and `GET /v1/info`.
- **`config/clients.json`:** IP allowlist for the HTTP API only. Each client has `id`, `ip`, `scopes` (`READ`, `WRITE`) and `enabled`. `WRITE` is required for `/v1/write`, `READ` for everything else. `::1` and `::ffff:127.0.0.1` count as `127.0.0.1`.
- **`config/adapters/<name>.json`:** selected by `PROGRAMMING_ADAPTERS` (default `default`). Maps a language id or file extension to an adapter path relative to that JSON file. `null` means no adapter. The extension is tried before the language id, so `"h"` can use a different adapter than `"c"`.

The adapter kind comes from the path:

| Path | Runs with |
|---|---|
| `.py` | Python |
| `.js`, `.mjs`, `.cjs` | Node |
| `.go` | `go run` |
| `.java` | the JDK source launcher |
| directory with `Cargo.toml` | cargo |
| directory with a `.csproj` | dotnet |
| any other executable file | run directly |

### Writing your own adapter

An adapter reads the source on stdin, accepts `--language <id> --filename <name>`, and prints one JSON object:

```
{"ok":true,"language":"...","imports":[...],"symbols":[...],"exports":[...]}
{"ok":false,"error":{"kind":"parse_error","message":"...","line":1,"column":1}}
```

`adapters/python_adapter.py` is the smallest reference. The language must also be listed in `src/core/languages.cpp` to be accepted.

## CLI

```
mfdp write --src a.py,b.py --kwrds "calculate_total,validate" --mode auto --output out.mfdp
mfdp read ARTIFACT
mfdp inspect ARTIFACT
mfdp extract ARTIFACT --kwrds calculate_total --budget 800
mfdp verify ARTIFACT --src a.py,b.py
mfdp rpc
mfdp version
mfdp-bench [--kwrds LIST] [--mode M] [--budget N] PATH...
```

Exit codes: 0 success, 1 usage, 2 error (`error [Code] ...`), 3 `verify` found stale, missing or unverifiable sources. Boolean options accept `true|false|1|0|yes|no`, and `--no-lines` style negation works.

`mfdp-bench` prints original tokens, rendered-context tokens, reduction percentage, symbols kept, fragments included and an omitted-token estimate.

## `write` options

| Option | Default | Behavior |
|---|---|---|
| `src` | required | Source file paths: a vector in the API, a comma-separated list on the CLI. |
| `memory` (API) | none | In-memory sources `{filename, content}`. |
| `kwrds` | `*` | `*` selects every symbol. Otherwise comma-separated names matched exactly against parsed or qualified names (`Class.method`). Unmatched names produce `symbol_not_found` warnings, and no match at all is a `NoMatch` error. |
| `inc` / `include` | `true` | Import declarations on or off. Aliases, conflicting values are an error. |
| `inctyps` / `include_types` | `true` | Include type definitions. Aliases. When `false`, enclosing types of selected members stay as signature-only stubs (warning `type_ancestor_stub`). |
| `import_mode` | `relevant` | `all`, `relevant` (imports whose bound names are used by stored symbols; star imports are kept) or `none`. `inc=false` implies `none`. |
| `legacy_inctyps` | `false` | Compatibility alias: `inctyps=true` means import mode `all`, `false` means `relevant`, and types are not filtered. |
| `lines` | `true` | Store line numbers. |
| `budget` | none | Token budget for rendered context, stored as the default for `extract`. Never truncates the file. In `auto` mode a full-source artifact over budget is downgraded to `symbols` (warning `auto_mode_downgraded`). |
| `lang` | `auto` | Language override. The real parser still validates, so a wrong override fails with `ParseFailed`. |
| `hash` | `true` | SHA-256 of each original source file. |
| `mode` | `auto` | `symbols`: signatures and metadata only. `full`: also stores source fragments for selected symbols. `auto`: `symbols` for `kwrds=*`, otherwise `full`, subject to `budget`. |
| `deps` | `true` | Store dependency edges and signature-only context for referenced symbols. |
| `dep_depth` | `1` (1 to 8) | Hops of resolved dependencies to include. |
| `combined` | `true` | One artifact for all inputs, or one per input. |
| `output` | random name | File path, or a list of paths for `combined=false`. A directory gets random names. |
| `output_dir` | `.` | Directory for random names. |
| `overwrite` | `false` | Existing explicit outputs are never replaced unless set (`OutputExists`). |

Comma-separated lists: a comma separates entries, `\,` is a literal comma, `\\` a backslash, and an entry can be wrapped in double quotes (`"my dir, x/a.py",b.py`). The vector API needs no escaping.

Default output name is `<32 random hex chars>.mfdp` from `getrandom(2)` (fallback `/dev/urandom`). Files are created with `O_EXCL`, so a collision is retried and never overwritten.

## `extract` options

`kwrds` (names or `*`), `budget` (overrides the stored one), `expand` (`auto|always|never` for source fragments), `deps` and `imports`.

By default `*` renders signatures only and notes how many fragments are stored. Named symbols get their source. Budget priority: file headers, selected signatures, dependency summaries, relevant imports, docs, source fragments (smallest first), then context symbols. Omissions come back as structured warnings (`budget_omitted_source`, `budget_omitted_deps`, `budget_exceeded_minimum`).

## HTTP API

`make api` starts the API on `API_HOST:API_PORT` and a test page on `WEBPAGE_PORT` (open `http://127.0.0.1:3848`).

| Route | Scope | Body |
|---|---|---|
| `GET /health` | none | |
| `GET /v1/info` | READ | |
| `POST /v1/write` | WRITE | `write` options with `src` (paths inside `WORKSPACE_ROOT`) and/or `memory`. `output` is a plain file name placed in `ARTIFACT_DIR`. |
| `POST /v1/read`, `/v1/inspect` | READ | `{artifact}` (name or path inside `ARTIFACT_DIR`) |
| `POST /v1/extract` | READ | `{artifact, kwrds, budget, expand, deps, imports}` |
| `POST /v1/verify` | READ | `{artifact, src: [paths], originals: [{path, content}]}` |

Responses are `{"ok": true, "result": ...}` or `{"ok": false, "error": {"code", "message", ...}}`.

| Status | Meaning |
|---|---|
| 400 | Invalid argument or conflicting options |
| 401 | Missing or invalid secret |
| 403 | Client not authorized, or path outside the allowed directory |
| 404 | File not found or unreadable |
| 409 | Output already exists, or source is stale |
| 413 | Output too large |
| 422 | Parse failure, unsupported language, corrupt or unsupported artifact, no matching symbols |
| 502 | Adapter failed |
| 503 | Adapter runtime missing |

Authentication: with `REQUIRE_SECRET=true` every `/v1` request needs `Authorization: Bearer <SECRET>` or `X-MFDP-Secret`. Comparison is constant-time, and the server refuses to start if `SECRET` is empty, shorter than 16 characters, or still the placeholder. The client allowlist (IP and scope) is checked first. The API reads and writes files only inside `WORKSPACE_ROOT` and `ARTIFACT_DIR`, and symlink escapes are rejected. Keep it on `127.0.0.1` unless it sits behind HTTPS. Browser calls from the test page are allowed through CORS for the page origin only.

```
curl -X POST http://127.0.0.1:3847/v1/write -H "Authorization: Bearer $MFDP_SECRET" -H "Content-Type: application/json" -d '{"src":["examples/typescript/cart.ts","examples/typescript/money.ts"],"kwrds":"total"}'
curl -X POST http://127.0.0.1:3847/v1/extract -H "Authorization: Bearer $MFDP_SECRET" -H "Content-Type: application/json" -d '{"artifact":"NAME.mfdp","kwrds":"total","budget":800}'
```

## Bindings

Both bindings are thin wrappers over `build/mfdp rpc` (one JSON request on stdin, one JSON response on stdout; methods `write`, `read`, `inspect`, `extract`, `verify`, `info`). With `REQUIRE_SECRET=true` they need the secret (the `secret` option or `MFDP_SECRET`) and compare it with `.env`. This is an in-process guard, not a security boundary: anyone who can read `.env` can read the secret. Outside this repository set `MFDP_ROOT` (the project root) and `MFDP_BIN` (the binary).

```ts
import { MFDP } from '@bluegc/mfdp';

const mfdp = new MFDP({ secret: process.env.MFDP_SECRET });
const { output } = await mfdp.write({ src: ['src/cart.ts'], kwrds: 'total', mode: 'auto' });
const ctx = await mfdp.extract(output, { kwrds: 'total', budget: 800 });
console.log(ctx.text);
const report = await mfdp.verify(output, ['src/cart.ts']);
if (!report.ok) throw new Error('source changed, regenerate');
```

```python
import os
from mfdp import MFDP

mfdp = MFDP(secret=os.environ.get("MFDP_SECRET"))
result = mfdp.write(src=["src/cart.ts"], kwrds="total", mode="auto")
ctx = mfdp.extract(result.output, kwrds="total", budget=800)
print(ctx.text)
assert mfdp.verify(result.output, ["src/cart.ts"]).ok
```

## C++ API

```cpp
#include "mfdp/mfdp.h"
```

| Call | Purpose |
|---|---|
| `write(WriteOptions)` | Parse, build and store artifacts. Returns paths, summaries and warnings. |
| `buildArtifacts(WriteOptions)` | Same without touching disk. |
| `read(src)` / `deserialize(bytes)` | Load and fully validate an artifact. |
| `inspect(src)` | Version, flags, languages, filenames, hashes and counts. |
| `extract(src, kwrds)` / `extract(src, ExtractOptions)` | AI-readable context. |
| `verify(src, original_paths)` | Compare stored hashes to current files. |
| `requireFresh(report)` | Throws `StaleSource`. Call before applying any edit. |

Errors are `mfdp::Error` with a code, file, line and column. See `examples/api_usage.cpp` for a complete program.

## Dependency analysis and limitations

Resolved: same-file names (lexical scopes, `this.member`, base-class members, `Class.member`), imports resolved to supplied files (relative JS and TS paths with extension and index probing, Python relative and dotted modules where ambiguity is reported unresolved), namespace and default imports, and CommonJS `require`.

Unknown stays unknown: unresolved names are flagged, imports from non-supplied modules are `external`, member calls on arbitrary objects (`items.append`, `obj.method`) are not recorded, re-exports are not followed, and function-local Python imports are not tracked. Builtins and JS globals are filtered. Local shadowing is approximated by treating every parameter and local binding in a symbol as local.

MFDP does not apply edits itself. A tool that does must call `verify` and `requireFresh` first and reject stale or hashless artifacts.

## Binary format (version 1.0)

Little-endian header, 72 bytes: magic `89 4D 46 44 50 0D 0A 1A`, u16 major, u16 minor, u32 feature flags (hashes, lines, imports, types, deps, combined, fragments), u8 compression (0 none, 1 zlib) plus 3 reserved zero bytes, u64 stored length, u64 payload length, SHA-256 of the uncompressed payload, and CRC32 of the preceding 68 header bytes. The payload follows, then nothing else.

The payload uses LEB128 varints and length-prefixed UTF-8 strings: metadata (modes, kwrds, budget, dependency depth, warnings), then per file: path, filename, language, size, optional hash, counts of parsed symbols and imports, exports, imports, symbols (kind, parent, exported tri-state, flags, name, qualified name, signature, return type, doc, params, decorators, bases, optional lines, optional fragment) and dependency edges (kind, status `same-file|cross-file|external|unresolved`, name, module, target file and symbol). Serialization is deterministic for the same inputs and zlib version.

The reader rejects bad magic, header checksum failures, unknown versions or flags, truncation, trailing data, payloads over `ReadLimits` (default 256 MiB), implausible compression ratios, payload checksum mismatches, out-of-range counts and indices, and malformed enums. Major version must equal 1, and minor must not exceed what the reader knows.

## Project layout

| Path | Contents |
|---|---|
| `include/mfdp/` | Public C++ headers |
| `src/core/` | Types, errors, languages, tokens, SHA-256, JSON |
| `src/runtime/` | Adapter launching, configuration, RPC |
| `src/pipeline/` | Parsing, resolving, building, formatting, context rendering |
| `apps/cli/`, `apps/bench/` | The `mfdp` CLI and `mfdp-bench` |
| `adapters/` | Parser adapters |
| `config/` | Application, client and adapter configuration |
| `api/` | Fastify HTTP API and test web page |
| `bindings/node/`, `bindings/python/` | Language bindings |
| `scripts/` | OS-aware build scripts (`scripts/unix/build-direct.sh` is the direct build) |
| `tests/` | Unit and integration tests |
| `examples/` | Sample sources and `api_usage.cpp` |

## Contributing

Run `make build && make test` before opening a pull request. Tests for adapters whose toolchain is missing are skipped, so say which toolchains you ran. See `CONTRIBUTING.md` and `SECURITY.md`.

## License

Apache License 2.0. See `LICENSE` and `NOTICE`. Copyright 2026 Blue GC. If you redistribute MFDP or a derivative, you must keep the license and the `NOTICE` file, which credit Blue GC and the MFDP project.

<p align="center">
  <a href="https://bluegc.net">bluegc.net</a>
  &nbsp;·&nbsp;
  <a href="https://mfdp.bluegc.net">Learn more about MFDP</a>
</p>