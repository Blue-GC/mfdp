import os
import re
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(HERE, ".."))

from mfdp import AuthError, MFDP, MFDPError

BIN = os.environ.get("MFDP_BIN") or os.path.join(REPO, "build", "mfdp")
GOOD_SECRET = "c2VjcmV0LXNlY3JldC1zZWNyZXQtc2VjcmV0LTEyMzQ1Ng=="


def make_root(env_text):
    directory = tempfile.mkdtemp(prefix="mfdp-py-")
    os.symlink(os.path.join(REPO, "config"), os.path.join(directory, "config"))
    os.symlink(os.path.join(REPO, "adapters"), os.path.join(directory, "adapters"))
    if env_text is not None:
        with open(os.path.join(directory, ".env"), "w") as handle:
            handle.write(env_text)
    return directory


class BindingTests(unittest.TestCase):
    def tearDown(self):
        os.environ.pop("MFDP_SECRET", None)

    def test_missing_env_refuses_to_start(self):
        root = make_root(None)
        with self.assertRaises(MFDPError) as ctx:
            MFDP(root=root, bin=BIN)
        self.assertEqual(ctx.exception.code, "EnvMissing")
        shutil.rmtree(root)

    def test_round_trip(self):
        root = make_root("REQUIRE_SECRET=false\n")
        mfdp = MFDP(root=root, bin=BIN)
        source = "def f(x):\n    return x\n\ndef g():\n    return f(1)\n"
        result = mfdp.write(memory=[{"filename": "a.py", "content": source}], kwrds="g", output_dir=root)
        self.assertTrue(re.match(r"^[0-9a-f]{32}\.mfdp$", os.path.basename(result.output)))
        ctx = mfdp.extract(result.output, kwrds="g", budget=400)
        self.assertIn("def g()", ctx.text)
        self.assertGreater(ctx.tokens, 0)
        self.assertEqual(mfdp.inspect(result.output).files[0].language, "python")
        ok = mfdp.verify(result.output, originals=[{"path": "a.py", "content": source}])
        self.assertTrue(ok.ok)
        stale = mfdp.verify(result.output, originals=[{"path": "a.py", "content": source + "#"}])
        self.assertFalse(stale.ok)
        self.assertEqual(stale.entries[0].status, "stale")
        shutil.rmtree(root)

    def test_parse_error_details(self):
        root = make_root("")
        mfdp = MFDP(root=root, bin=BIN)
        with self.assertRaises(MFDPError) as ctx:
            mfdp.write(memory=[{"filename": "bad.py", "content": "def broken(:\n    pass\n"}], output_dir=root)
        self.assertEqual(ctx.exception.code, "ParseFailed")
        self.assertEqual(ctx.exception.line, 1)
        shutil.rmtree(root)

    def test_secret_enforcement(self):
        root = make_root("REQUIRE_SECRET=true\nSECRET=" + GOOD_SECRET + "\n")
        with self.assertRaises(AuthError):
            MFDP(root=root, bin=BIN)
        with self.assertRaises(AuthError):
            MFDP(root=root, bin=BIN, secret="wrong")
        MFDP(root=root, bin=BIN, secret=GOOD_SECRET)
        os.environ["MFDP_SECRET"] = GOOD_SECRET
        MFDP(root=root, bin=BIN)
        shutil.rmtree(root)

    def test_placeholder_secret_fails_closed(self):
        root = make_root("REQUIRE_SECRET=true\nSECRET=YOUR_BASE64_ENCODED_SECRET_HERE\n")
        with self.assertRaises(MFDPError) as ctx:
            MFDP(root=root, bin=BIN, secret="x")
        self.assertEqual(ctx.exception.code, "ConfigError")
        shutil.rmtree(root)

    def test_missing_binary(self):
        root = make_root("")
        with self.assertRaises(MFDPError) as ctx:
            MFDP(root=root, bin="/nonexistent/mfdp")
        self.assertEqual(ctx.exception.code, "BinaryMissing")
        shutil.rmtree(root)


if __name__ == "__main__":
    unittest.main()
