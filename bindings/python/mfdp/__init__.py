import hashlib
import hmac
import json
import os
import subprocess
import sys

from ._env import (
    AuthError,
    MFDPError,
    find_root,
    is_true,
    load_env,
    secret_is_usable,
)

__all__ = ["MFDP", "MFDPError", "AuthError", "find_root", "load_env", "Result"]
__version__ = "1.0.0"


class Result(dict):
    def __getattr__(self, name):
        try:
            return self[name]
        except KeyError:
            raise AttributeError(name)


def _wrap(value):
    if isinstance(value, dict):
        return Result({k: _wrap(v) for k, v in value.items()})
    if isinstance(value, list):
        return [_wrap(v) for v in value]
    return value


def _safe_equal(a, b):
    ha = hashlib.sha256(str(a).encode("utf-8")).digest()
    hb = hashlib.sha256(str(b).encode("utf-8")).digest()
    return hmac.compare_digest(ha, hb)


class MFDP:
    def __init__(self, root=None, secret=None, bin=None, timeout=900):
        self.root = os.path.abspath(root) if root else find_root()
        self.env = load_env(self.root)
        self.timeout = timeout
        self.require_secret = is_true(self.env.get("REQUIRE_SECRET"))
        if self.require_secret:
            if not secret_is_usable(self.env.get("SECRET")):
                raise MFDPError(
                    "ConfigError",
                    "REQUIRE_SECRET=true but SECRET in .env is missing or still the placeholder. Run: make setup",
                )
            supplied = secret if secret is not None else os.environ.get("MFDP_SECRET")
            if not supplied:
                raise AuthError("REQUIRE_SECRET=true: pass secret=... or set MFDP_SECRET")
            if not _safe_equal(supplied, self.env["SECRET"]):
                raise AuthError("invalid secret")
        exe = "mfdp.exe" if sys.platform == "win32" else "mfdp"
        self.bin = bin or os.environ.get("MFDP_BIN") or os.path.join(self.root, "build", exe)
        if not os.path.exists(self.bin):
            raise MFDPError("BinaryMissing", "MFDP binary not found at %s. Run: make build" % self.bin)

    def rpc(self, method, params=None):
        environment = dict(os.environ)
        environment["MFDP_ROOT"] = self.root
        try:
            completed = subprocess.run(
                [self.bin, "rpc"],
                input=json.dumps({"method": method, "params": params or {}}).encode("utf-8"),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=environment,
                timeout=self.timeout,
            )
        except subprocess.TimeoutExpired:
            raise MFDPError("Timeout", "MFDP call timed out after %s seconds" % self.timeout)
        except OSError as error:
            raise MFDPError("SpawnFailed", "could not start %s: %s" % (self.bin, error))
        try:
            reply = json.loads(completed.stdout.decode("utf-8"))
        except ValueError:
            raise MFDPError(
                "BadResponse", "MFDP returned invalid output: " + completed.stderr.decode("utf-8", "replace")[:500]
            )
        if not reply.get("ok"):
            error = reply.get("error") or {}
            raise MFDPError(
                error.get("code", "Internal"),
                error.get("message", "unknown error"),
                error.get("file", ""),
                error.get("line", 0),
                error.get("column", 0),
            )
        return _wrap(reply["result"])

    def write(self, **params):
        result = self.rpc("write", params)
        result["output"] = result["outputs"][0]
        return result

    def read(self, artifact):
        return self.rpc("read", {"artifact": artifact})

    def inspect(self, artifact):
        return self.rpc("inspect", {"artifact": artifact})

    def extract(self, artifact, **params):
        params["artifact"] = artifact
        return self.rpc("extract", params)

    def verify(self, artifact, src=None, originals=None):
        params = {"artifact": artifact}
        if src:
            params["src"] = list(src)
        if originals:
            params["originals"] = list(originals)
        return self.rpc("verify", params)

    def info(self):
        return self.rpc("info", {})
