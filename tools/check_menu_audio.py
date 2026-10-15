"""Check native music delivery against emulator time, catching render starvation."""
import re
import sys
from pathlib import Path


def check(path):
    log=Path(path).read_text(encoding='utf-8',errors='replace')
    starts=[];intervals=[];start=None
    for match in re.finditer(r'\[\s*([\d.]+)\] STPS MUSIC (START|STOP) position=(\d+) samples=(\d+)(?: t=([\d.]+))?',log):
        # PCSX2's prefix is host wall time: shader compilation and GS PNG dumps
        # can pause emulation. The ELF's timer shares the playback time domain.
        time=float(match[5] or match[1]);position=int(match[3]);samples=int(match[4])
        if samples<=0:raise RuntimeError('Menu music is missing from the ELF')
        if match[2]=='START':
            if start is not None:raise RuntimeError('Music restarted inside the menu')
            if position!=0:raise RuntimeError('Music did not restart on returning to the menu')
            start=time;starts.append(time)
        elif start is not None:
            elapsed=time-start;delivered=position/22050
            intervals.append((elapsed,delivered));start=None
    if len(intervals)<3:raise RuntimeError('Missing menu/game music transitions')
    elapsed,delivered=max(intervals)
    if elapsed<20 or abs(delivered-elapsed)>elapsed*.05:
        raise RuntimeError(f'Music delivery too slow/fast: {delivered:.3f}s PCM / {elapsed:.3f}s elapsed')
    print(f'MENU AUDIO PASS: {len(intervals)} transitions, {delivered:.3f}s PCM / {elapsed:.3f}s elapsed')


if __name__=='__main__':check(sys.argv[1])
