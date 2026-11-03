import os
import re

PLACEHOLDER = "YOUR_BASE64_ENCODED_SECRET_HERE"


class MFDPError(Exception):
    def __init__(self, code, message, file="", line=0, column=0):
        super().__init__(message)
        self.code = code
        self.message = message
        self.file = file
        self.line = line
        self.column = column

    def __str__(self):
        return "[%s] %s" % (self.code, self.message)


class AuthError(MFDPError):
    def __init__(self, message):
        super().__init__("AuthFailed", message)


def parse_env(text):
    values = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:].strip()
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if not key:
            continue
        if value[:1] in ('"', "'"):
            quote = value[0]
            close = value.find(quote, 1)
            value = value[1:] if close < 0 else value[1:close]
        else:
            match = re.search(r"\s#", value)
            if match:
                value = value[: match.start()].strip()
            value = value.rstrip("\\").strip()
        values[key] = value
    return values


def _is_root(directory):
    return os.path.isdir(os.path.join(directory, "config", "adapters"))


def find_root(start=None):
    explicit = os.environ.get("MFDP_ROOT")
    if explicit:
        explicit = os.path.abspath(explicit)
        if not _is_root(explicit):
            raise MFDPError("RootNotFound", "MFDP_ROOT does not contain config/adapters: " + explicit)
        return explicit
    here = os.path.dirname(os.path.abspath(__file__))
    starts = [start or os.getcwd(), os.path.abspath(os.path.join(here, "..", "..", ".."))]
    for begin in starts:
        directory = os.path.abspath(begin)
        for _ in range(10):
            if _is_root(directory):
                return directory
            parent = os.path.dirname(directory)
            if parent == directory:
                break
            directory = parent
    raise MFDPError("RootNotFound", "could not find the MFDP project root; set MFDP_ROOT")


def load_env(root):
    path = os.path.join(root, ".env")
    if not os.path.isfile(path):
        raise MFDPError(
            "EnvMissing", "MFDP requires a .env file in the project root (%s). Run: make setup" % path
        )
    with open(path, "r", encoding="utf-8") as handle:
        return parse_env(handle.read())


def is_true(value):
    return isinstance(value, str) and value.strip().lower() in ("true", "1", "yes", "on")


def secret_is_usable(secret):
    return isinstance(secret, str) and len(secret) >= 16 and secret != PLACEHOLDER
