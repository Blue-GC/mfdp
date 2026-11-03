# Contributing to MFDP

Thank you for helping. Anyone can propose a change through a pull request. This page explains how, and what a change must meet before it is merged.

## Before you start

- **Small fixes** (typos, docs, an obvious bug): open a pull request directly.
- **Anything bigger** (new language adapter, format change, new option): open an issue first and describe what you want to do, so nobody wastes time on a change that won't fit.
- **Security problems**: do not open a public issue. Follow `SECURITY.md`.

By submitting a contribution you agree that it is licensed under the Apache License 2.0, as in section 5 of the license.

## Set up

**Blue GC members with write access** clone the repository directly:

```
git clone https://github.com/Blue-GC/mfdp.git
cd mfdp
```

**Everyone else** works from a fork. Click Fork on the [Blue-GC/mfdp](https://github.com/Blue-GC/mfdp) page, then clone your fork and add the original repository as `upstream`:

```
git clone https://github.com/<your-github-username>/mfdp.git
cd mfdp
git remote add upstream https://github.com/Blue-GC/mfdp.git
```

Then, in both cases:

```
make setup
make build
make test
```

All tests must pass before you change anything. Tests for adapters whose toolchain you don't have (Java 11+, Rust, .NET, Go, libclang) are skipped, and that is fine.

## Making a change

1. Update your copy: `git fetch upstream && git switch -c my-change upstream/main`
2. Make one focused change. One pull request should do one thing.
3. Add or update tests for it.
4. Run `make build && make test` and make sure they pass.
5. Commit with a clear message, for example `Fix Java adapter version check`.
6. Push to your fork and open a pull request against `main`.

## What every pull request must meet

A pull request is merged only when all of these are true:

- **CI is green.** The `build-and-test` check must pass.
- **Tests cover the change.** Bug fixes include a test that fails without the fix. New features include tests for normal and error cases.
- **No unrelated changes.** No reformatting of untouched code, no moved files without a reason.
- **Matches the existing style.** C++20, the same naming and error handling (`mfdp::Error` with an `ErrorCode`), and the same layout (`src/core`, `src/runtime`, `src/pipeline`).
- **No new warnings.** The build uses `-Wall -Wextra`.
- **Public API changes are deliberate.** Anything in `include/mfdp/` is a promise to users. Explain why it changes in the pull request.
- **Binary format changes bump the version** and update the "Binary format" section of the README.
- **Docs are updated.** New options, commands or environment variables are added to the README.
- **Safe by default.** Adapters never execute the source code they parse. The HTTP API stays inside `WORKSPACE_ROOT` and `ARTIFACT_DIR`. No secrets, tokens or personal data in the repo.
- **One approving review** from a maintainer, and all review comments resolved.

## Adding a language adapter

1. Write the adapter so it reads source from stdin, accepts `--language` and `--filename`, and prints the JSON described in the README. `adapters/python_adapter.py` is the smallest reference.
2. Register the language in `src/core/languages.cpp` and in `config/adapters/default.json`.
3. Add test fixtures under `tests/fixtures/` (a valid sample, a `bad.*` file with a syntax error, and a multi-file case) and tests in `tests/test_more_languages.cpp`.
4. Document the toolchain it needs in the README requirements.

## Reporting bugs

Open an issue with the command you ran, what you expected, what happened, your OS, and the output of `./build/mfdp version`.