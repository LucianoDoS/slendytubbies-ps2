"""Reconstruct a Unity package without trusting archive paths."""
import argparse
import json
from pathlib import Path, PurePosixPath
import tarfile
import gzip
import shutil
import tempfile

def extract(package, destination):
    root = Path(destination).resolve()
    root.mkdir(parents=True, exist_ok=True)
    manifest = []
    # Avoid repeatedly decompressing a 190 MB gzip stream for random member reads.
    with tempfile.TemporaryFile() as plain:
        with gzip.open(package, 'rb') as compressed:
            shutil.copyfileobj(compressed, plain)
        plain.seek(0)
        _extract_plain(plain, root, manifest)
    (root / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f'Reconstructed {len(manifest)} assets in {root}')

def _extract_plain(plain, root, manifest):
    with tarfile.open(fileobj=plain, mode='r:') as archive:
        members = {m.name: m for m in archive.getmembers() if m.isfile()}
        for name, member in members.items():
            if not name.endswith('/pathname'):
                continue
            guid = name.split('/')[0]
            path = archive.extractfile(member).read().decode('utf-8-sig').splitlines()[0].strip()
            relative = PurePosixPath(path.replace('\\', '/'))
            if relative.is_absolute() or '..' in relative.parts or relative.parts[0] != 'Assets' or ':' in path:
                raise ValueError(f'Unsafe path: {path}')
            target = root.joinpath(*relative.parts)
            entry = {'guid': guid, 'path': path, 'bytes': 0}
            asset = members.get(guid + '/asset')
            if asset:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.extractfile(asset).read())
                entry['bytes'] = asset.size
            else:
                target.mkdir(parents=True, exist_ok=True)
            meta = members.get(guid + '/asset.meta') or members.get(guid + '/metaData')
            if meta:
                Path(str(target) + '.meta').write_bytes(archive.extractfile(meta).read())
            manifest.append(entry)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('package')
    parser.add_argument('destination')
    args = parser.parse_args()
    extract(args.package, args.destination)
