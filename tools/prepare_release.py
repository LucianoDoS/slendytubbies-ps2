"""Package the tested standalone ELF as a ZIP and a PS2 ISO9660 disc image."""
import argparse
import hashlib
import io
import json
import re
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.1.0'
PREFIX = f'Slendytubbies-PS2-v{VERSION}'
BOOT = b'BOOT2 = cdrom0:\\SLENDY.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest().upper()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def check_native(elf_hash):
    qa = ROOT / 'ps2/qa'
    for name in ('walk', 'benchmark'):
        report = read_json(Path((qa / f'last-{name}-report.txt').read_text().strip()))
        assert report['elf_sha256'].upper() == elf_hash and report['target_met'], name
        if name == 'walk':
            assert report['steady_target_met'] and len(report['walk_routes']) == 8
        else:
            assert len(report['scenes']) == 34
    for name, pattern in (
        ('enemy', r'STPS SMOKE PASS enemy'),
        ('audio', r'STPS SMOKE PASS audio[^\r\n]*flashlight=2 reserved_ignored=2'),
    ):
        assert read_json(qa / f'elf-{name}.json')['Hash'].upper() == elf_hash
        assert re.search(pattern, (qa / f'pcsx2-{name}.log').read_text(encoding='utf-8-sig'))
    noonoo = Path((qa / 'last-noonoo-capture.txt').read_text().strip())
    report = read_json(noonoo / 'noonoo-check.json')
    assert report['passed'] and report['elf_sha256'].upper() == elf_hash


def verify_iso(path, elf):
    import pycdlib
    iso = pycdlib.PyCdlib()
    iso.open(str(path))
    try:
        boot, embedded = io.BytesIO(), io.BytesIO()
        iso.get_file_from_iso_fp(boot, iso_path='/SYSTEM.CNF;1')
        iso.get_file_from_iso_fp(embedded, iso_path='/SLENDY.ELF;1')
        assert boot.getvalue() == BOOT, 'Unexpected disc boot configuration'
        assert hashlib.sha256(embedded.getvalue()).hexdigest().upper() == sha(elf)
    finally:
        iso.close()


def prepare(output):
    import pycdlib
    elf = ROOT / 'dist/SLENDY.ELF'
    elf_hash = sha(elf)
    assert elf.read_bytes()[:4] == b'\x7fELF', 'Not an ELF executable'
    check_native(elf_hash)
    output.mkdir(parents=True, exist_ok=True)
    readme = f'''SLENDYTUBBIES V1 FOR PLAYSTATION 2 - v{VERSION}

Unofficial experimental single-player fan port. Assets and audio are embedded.
Source and releases: https://github.com/LucianoDoS/slendytubbies-ps2

ELF / FREE MCBOOT
Extract the SLENDY folder to an MBR/FAT32 USB drive. Start wLaunchELF/uLaunchELF
from Free McBoot, then run mass:/SLENDY/SLENDY.ELF. Initial loading takes time.

ISO / PCSX2
Open the .iso as a disc image in PCSX2 and use Fast Boot. Supply your own BIOS.
The ISO is an ISO9660 data disc with SYSTEM.CNF pointing to the same SLENDY.ELF.
The image does not include a BIOS or a retail disc authentication mechanism.
On physical PS2, use the ELF instructions above. ISO loading with OPL or a
physical disc setup has not been validated for this release.

DUALSHOCK 2
D-pad: menus. Cross: confirm/jump. Circle: back/crouch.
Left stick: move. Right stick: look. L1/L3: run. R1: flashlight.
Triangle: map. Start: pause. Select while paused: menu. R2: unassigned.

VIDEO AND VALIDATION
512x448, NTSC 60 Hz; nominal 30 FPS game target. Controlled PCSX2 views and
walking routes passed. Previous builds booted on real PS2; this exact revision
still needs a hardware retest, particularly ground textures and Noo-Noo motion.
Multiplayer is unavailable. See the repository for known approximations.

LICENSE SCOPE
MIT covers original port code only. Game assets, characters, music and other
third-party content retain their owners and terms. Read CREDITS.md and the
included third-party notices before redistributing. No affiliation with
ZeoWorks or the owners of Teletubbies is claimed.

ELF SHA-256: {elf_hash}
'''.replace('\n', '\r\n').encode('utf-8')
    files = {
        'SLENDY.ELF': elf.read_bytes(), 'README.txt': readme,
        'LICENSE.txt': (ROOT / 'LICENSE').read_bytes(),
        'CREDITS.md': (ROOT / 'CREDITS.md').read_bytes(),
        'THIRD_PARTY.txt': (ROOT / 'third_party/NOTICES.txt').read_bytes(),
        'AFL-2.0.txt': (ROOT / 'third_party/AFL-2.0.txt').read_bytes(),
        'SHA256.txt': f'{elf_hash}  SLENDY.ELF\n'.encode('ascii'),
    }
    archive = output / f'{PREFIX}-ELF.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in files.items():
            entry = zipfile.ZipInfo('SLENDY/' + name)
            entry.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(entry, data)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for name, data in files.items():
            assert z.read('SLENDY/' + name) == data
    iso_path = output / f'{PREFIX}.iso'
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1, sys_ident='PLAYSTATION', vol_ident='SLENDY_PS2',
            app_ident_str='SLENDYTUBBIES PS2 HOMEBREW')
    buffers = []
    disc = {'SYSTEM.CNF': BOOT, 'SLENDY.ELF': files['SLENDY.ELF'],
            'README.TXT': readme, 'LICENSE.TXT': files['LICENSE.txt'],
            'CREDITS.TXT': files['CREDITS.md'], 'NOTICES.TXT': files['THIRD_PARTY.txt'],
            'AFL20.TXT': files['AFL-2.0.txt']}
    try:
        for name, data in disc.items():
            stream = io.BytesIO(data)
            buffers.append(stream)
            iso.add_fp(stream, len(data), iso_path=f'/{name};1')
        iso.write(str(iso_path))
    finally:
        iso.close()
    verify_iso(iso_path, elf)
    report = {'version': VERSION, 'elf_sha256': elf_hash, 'elf_bytes': elf.stat().st_size,
              'iso_sha256': sha(iso_path), 'iso_bytes': iso_path.stat().st_size,
              'zip_sha256': sha(archive), 'zip_bytes': archive.stat().st_size,
              'iso': str(iso_path.resolve()), 'elf_zip': str(archive.resolve()),
              'iso_contents_verified': True, 'bios_included': False,
              'real_ps2_current_revision_tested': False}
    (output / 'package.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


def finalize(output):
    report = read_json(output / 'package.json')
    assert sha(ROOT / 'dist/SLENDY.ELF') == report['elf_sha256']
    for test in ('menus', 'game'):
        result = read_json(ROOT / f'ps2/qa/iso-{test}.json')
        assert result['passed'] and result['iso_sha256'] == report['iso_sha256']
        assert result['elf_sha256'] == report['elf_sha256']
    source = read_json(ROOT / 'dist/source-package.json')
    assert source['verified'] and not source['game_assets_included']
    shutil.copyfile(source['archive'], output / f'{PREFIX}-Source.zip')
    assets = [output / f'{PREFIX}-ELF.zip', output / f'{PREFIX}.iso',
              output / f'{PREFIX}-Source.zip']
    assert sha(assets[0]) == report['zip_sha256'] and sha(assets[1]) == report['iso_sha256']
    sums = '\n'.join(f'{sha(path)}  {path.name}' for path in assets) + '\n'
    (output / 'SHA256SUMS.txt').write_text(sums, encoding='ascii')
    return {'ready_to_publish': True, 'version': VERSION, 'files': [p.name for p in assets],
            'checksums': sums}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / f'dist/release/v{VERSION}')
    parser.add_argument('--finalize', action='store_true', help='Require ISO QA and add source ZIP/checksums')
    args = parser.parse_args()
    print(json.dumps(finalize(args.output) if args.finalize else prepare(args.output), indent=2))
