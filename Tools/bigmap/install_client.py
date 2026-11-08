"""Copies packaged archives (<out>/data/: map.dat, cgd.dip, ui.dat, audio/sfx/ambient.dat, those present) into a
client COPY, keeping the replaced files in data/_bigmap_backup/. --restore puts the backup back."""
import argparse
import os
import shutil
import sys

FILES = ('map.dat', 'cgd.dip', 'ui.dat', os.path.join('audio', 'sfx', 'ambient.dat'))
PROTECTED = (r'c:\nostalgiavolts2.0', r'c:\nvcopy', r'c:\program files')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out', help='folder holding data/ from package_client.py')
    ap.add_argument('client', help='a copy of the client (the folder with Bin/ and data/)')
    ap.add_argument('--restore', action='store_true')
    a = ap.parse_args()
    client = os.path.abspath(a.client)
    if any(client.lower().startswith(p) for p in PROTECTED):
        sys.exit('refusing to touch %s: use a copy of the client' % client)
    data = os.path.join(client, 'data')
    backup = os.path.join(data, '_bigmap_backup')
    if not os.path.isfile(os.path.join(client, 'Bin', 'MicroVolts.exe')):
        sys.exit('%s does not look like a client folder' % client)
    for rel in FILES:
        src = os.path.join(a.out, 'data', rel)
        dst = os.path.join(data, rel)
        keep = os.path.join(backup, rel)
        if a.restore:
            if os.path.exists(keep):
                shutil.copyfile(keep, dst)
                print('restored', rel)
            continue
        if not os.path.exists(src):
            continue
        if not os.path.exists(keep):
            os.makedirs(os.path.dirname(keep), exist_ok=True)
            shutil.copyfile(dst, keep)
        shutil.copyfile(src, dst)
        print('installed', rel)
    return 0


if __name__ == '__main__':
    sys.exit(main())
