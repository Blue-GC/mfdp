# Security Policy

## Reporting a vulnerability

Please do not report security problems in public issues.

Use GitHub's private reporting: open the Security tab of this repository and choose "Report a vulnerability". Include what you found, how to reproduce it, and which version you tested.

We will acknowledge your report as soon as we can, work on a fix, and credit you in the release notes if you wish.

## Scope

Of particular interest:

- Path traversal or sandbox escapes in the HTTP API (`WORKSPACE_ROOT`, `ARTIFACT_DIR`)
- Authentication or client allowlist bypasses
- Memory safety problems when reading `.mfdp` artifacts
- Adapters executing or leaking the source code they parse

## Supported versions

Only the latest release on `main` receives security fixes.