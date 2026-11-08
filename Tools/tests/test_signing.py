import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / 'deploy' / 'updates'))

from tbupdate import signing  # noqa: E402
import updates  # noqa: E402


class SignedManifest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.root = self.dir / 'microvolts'
        (self.root / 'ENG_2.0.7.5').mkdir(parents=True)
        (self.root / 'patch.ini').write_bytes(b'[patch]\nversion=ENG_2.0.7.5\n')
        (self.root / 'ENG_2.0.7.5' / 'a.cab').write_bytes(b'cab bytes')
        self.private, self.public = self.dir / 'k.priv', self.dir / 'k.pub'
        signing.generate_keypair(self.private, self.public)
        self.key = signing.load_public_key_hex(self.public.read_text())
        local = updates.local_inventory(self.root)
        signed = updates.with_signed_manifest(local, 'ENG_2.0.7.5', self.private, self.dir)
        self.manifest = signed[signing.MANIFEST_NAME][0].read_bytes()
        self.signature = signed[signing.SIGNATURE_NAME][0].read_bytes()

    def tearDown(self):
        self.tmp.cleanup()

    def test_valid_manifest_accepted(self):
        body = signing.verify_tree(self.root, self.manifest, self.signature, self.key)
        self.assertEqual(set(body['files']), {'patch.ini', 'ENG_2.0.7.5/a.cab'})

    def test_one_changed_byte_in_manifest_refused(self):
        bad = bytearray(self.manifest)
        bad[-5] ^= 0x01
        with self.assertRaisesRegex(signing.VerifyError, 'signature is invalid'):
            signing.verify_tree(self.root, bytes(bad), self.signature, self.key)

    def test_wrong_key_refused(self):
        other_priv, other_pub = self.dir / 'o.priv', self.dir / 'o.pub'
        signing.generate_keypair(other_priv, other_pub)
        with self.assertRaisesRegex(signing.VerifyError, 'signature is invalid'):
            signing.verify_tree(self.root, self.manifest, self.signature, signing.load_public_key_hex(other_pub.read_text()))

    def test_file_not_matching_hash_refused(self):
        (self.root / 'ENG_2.0.7.5' / 'a.cab').write_bytes(b'cab byteX')
        with self.assertRaisesRegex(signing.VerifyError, 'a.cab: SHA-256 does not match'):
            signing.verify_tree(self.root, self.manifest, self.signature, self.key)

    def test_missing_file_refused(self):
        (self.root / 'ENG_2.0.7.5' / 'a.cab').unlink()
        with self.assertRaisesRegex(signing.VerifyError, 'missing'):
            signing.verify_tree(self.root, self.manifest, self.signature, self.key)

    def test_no_builtin_key_refuses(self):
        with self.assertRaisesRegex(signing.VerifyError, 'no built-in public key'):
            signing.verify_tree(self.root, self.manifest, self.signature)

    def test_path_traversal_in_signed_manifest_refused(self):
        evil = signing.build_signed_manifest({'../outside.txt': '0' * 64}, 'x')
        sig = signing.sign(evil, signing.load_private_key(self.private))
        with self.assertRaisesRegex(signing.VerifyError, 'unsafe path'):
            signing.verify_tree(self.root, evil, sig, self.key)


if __name__ == '__main__':
    unittest.main()
