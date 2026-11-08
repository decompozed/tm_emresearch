from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

MANIFEST_NAME = 'update-manifest.json'
SIGNATURE_NAME = 'update-manifest.json.sig'
FORMAT = 'tb-update-manifest-1'

# Raw Ed25519 public key (64 hex chars) that a verifier ships with. Empty until the real key pair is made on the
# publishing machine; with no key a verifier refuses everything instead of trusting the manifest.
BUILTIN_PUBLIC_KEY_HEX = ''


class VerifyError(RuntimeError):
    pass


def generate_keypair(private_path: Path, public_path: Path) -> None:
    key = Ed25519PrivateKey.generate()
    private_path.write_bytes(key.private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw,
                                               serialization.NoEncryption()))
    public_path.write_text(key.public_key().public_bytes(serialization.Encoding.Raw,
                                                         serialization.PublicFormat.Raw).hex() + '\n', encoding='ascii')


def load_private_key(path: Path) -> Ed25519PrivateKey:
    return Ed25519PrivateKey.from_private_bytes(path.read_bytes())


def load_public_key_hex(text: str) -> Ed25519PublicKey:
    try:
        return Ed25519PublicKey.from_public_bytes(bytes.fromhex(text.strip()))
    except ValueError as e:
        raise VerifyError(f'bad public key: {e}') from e


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as f:
        while chunk := f.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def build_signed_manifest(files: dict[str, str], version: str) -> bytes:
    """files: relative posix path -> sha256 hex. Bytes are signed exactly as written, so there is no canonical form to get wrong."""
    body = {'format': FORMAT, 'version': version, 'files': dict(sorted(files.items()))}
    return json.dumps(body, indent=2).encode('utf-8') + b'\n'


def sign(manifest: bytes, key: Ed25519PrivateKey) -> bytes:
    return key.sign(manifest)


def _safe_relative(name: str) -> PurePosixPath:
    path = PurePosixPath(name)
    if path.is_absolute() or '\\' in name or ':' in name or any(part in ('', '.', '..') for part in path.parts):
        raise VerifyError(f'unsafe path in manifest: {name!r}')
    return path


def verify_manifest(manifest: bytes, signature: bytes, public_key: Ed25519PublicKey | None = None) -> dict:
    """Signature first, parsing second: nothing inside an unsigned or altered manifest is looked at."""
    if public_key is None:
        if not BUILTIN_PUBLIC_KEY_HEX:
            raise VerifyError('no built-in public key, refusing the manifest')
        public_key = load_public_key_hex(BUILTIN_PUBLIC_KEY_HEX)
    try:
        public_key.verify(signature, manifest)
    except InvalidSignature:
        raise VerifyError('manifest signature is invalid') from None
    try:
        body = json.loads(manifest)
    except ValueError as e:
        raise VerifyError(f'manifest is not valid JSON: {e}') from e
    if not isinstance(body, dict) or body.get('format') != FORMAT or not isinstance(body.get('files'), dict):
        raise VerifyError('manifest has the wrong format')
    return body


def verify_tree(root: Path, manifest: bytes, signature: bytes, public_key: Ed25519PublicKey | None = None) -> dict:
    """Accept only a manifest with a good signature, then check every file under root against its SHA-256."""
    body = verify_manifest(manifest, signature, public_key)
    for name, expected in body['files'].items():
        rel = _safe_relative(name)
        path = root.joinpath(*rel.parts)
        if not path.is_file():
            raise VerifyError(f'{name}: file is missing')
        if file_sha256(path) != str(expected).lower():
            raise VerifyError(f'{name}: SHA-256 does not match the signed manifest')
    return body
