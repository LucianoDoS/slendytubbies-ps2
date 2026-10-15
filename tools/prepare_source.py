"""Create a source-only repository archive; never include local game assets."""
import argparse
import hashlib
import json
from pathlib import Path
from datetime import datetime, timezone
import zipfile

ROOT_FILES = (
    'README.md', 'LICENSE', 'CREDITS.md', 'CONTRIBUTING.md',
    '.gitignore', '.gitattributes', 'build.ps1', 'play_pcsx2.ps1', 'Launch-ISO.cmd', 'ps2/Makefile',
)
SOURCE_DIRS = {
    'ps2/src': {'.c', '.h'}, 'ps2/tests': {'.c', '.h'},
    'tools': {'.py', '.ps1', '.sh', '.txt'},
    'docs': {'.md'}, '.github': {'.yml', '.yaml', '.md'}, 'third_party': {'.txt'},
}


def prepare(root, output):
    root = root.resolve()
    selected = [root / name for name in ROOT_FILES]
    for directory, extensions in SOURCE_DIRS.items():
        selected.extend(path for path in (root / directory).rglob('*')
                        if path.is_file() and path.suffix in extensions
                        and 'local' not in path.relative_to(root).parts
                        and '__pycache__' not in path.parts)
    files = {}
    for path in sorted(set(selected)):
        if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(root):
            raise ValueError(f'Invalid source entry: {path.name}')
        name = path.relative_to(root).as_posix()
        data = path.read_bytes()
        data.decode('utf-8-sig')
        if b'\0' in data or len(data) > 2 * 1024 * 1024:
            raise ValueError(f'Unexpected binary or oversized source entry: {name}')
        files[name] = data
    if not any(name.startswith('.github/workflows/') for name in files):
        raise ValueError('Source package is missing its CI workflow')
    manifest = {
        'schema_version': 1,
        'license': 'MIT for original port contributions; see CREDITS.md',
        'original_game_assets_included': False, 'compiled_game_included': False,
        'files': [{'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                  for name, data in files.items()],
    }
    files['SOURCE_MANIFEST.json'] = (json.dumps(manifest, indent=2) + '\n').encode('utf-8')
    output.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f')
    stage = output / 'source' / stamp / 'slendytubbies-ps2'
    stage.mkdir(parents=True)
    archive_path = output / 'SLENDY_PS2_SOURCE.zip'
    with zipfile.ZipFile(archive_path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in files.items():
            target = stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            entry = zipfile.ZipInfo('slendytubbies-ps2/' + name)
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)
    with zipfile.ZipFile(archive_path) as archive:
        assert archive.testzip() is None
        assert set(archive.namelist()) == {'slendytubbies-ps2/' + name for name in files}
        for name, data in files.items():
            assert archive.read('slendytubbies-ps2/' + name) == data
    report = dict(verified=True, files=len(files), bytes=archive_path.stat().st_size,
                  sha256=hashlib.sha256(archive_path.read_bytes()).hexdigest(),
                  archive=str(archive_path.resolve()), source_tree=str(stage.resolve()),
                  game_assets_included=False, compiled_game_included=False)
    (output / 'source-package.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    prepare(repo, args.output or repo / 'dist')
