"""Fingerprint three source scream fragments in actual Windows loopback output.
Short windows tolerate PCSX2 timestretch clock drift without weakening the
waveform match. Their spacing must agree with the source clip timeline.
"""
import argparse,json,math,re,struct,wave
from pathlib import Path
import numpy as np
from scipy import signal

def check(directory):
    root=Path(__file__).resolve().parents[1];directory=Path(directory)
    with wave.open(str(directory/'pcsx2-enemy.wav')) as wav:
        rate=wav.getframerate();channels=wav.getnchannels()
        raw=wav.readframes(wav.getnframes())
    recorded=np.frombuffer(raw,dtype='<i2').reshape(-1,channels).mean(axis=1)
    divisor=math.gcd(rate,22050);recorded=signal.resample_poly(recorded,22050//divisor,rate//divisor)
    blob=(root/'ps2/generated/world.bin').read_bytes();header=struct.unpack_from('<20I',blob)
    offset,count=struct.unpack_from('<2I',blob,header[16]+4*8)
    source=np.frombuffer(blob,dtype='<i2',count=count,offset=offset).astype(float)
    fragments=[]
    for start in (0,2,4):
        template=source[start*22050:start*22050+5500]
        cross=signal.correlate(recorded,template,mode='valid',method='fft')
        energy=signal.convolve(recorded**2,np.ones(len(template)),mode='valid',method='fft')
        correlation=cross/np.sqrt(np.maximum(energy,1)*np.dot(template,template))
        peaks,_=signal.find_peaks(correlation,height=.70,distance=9*22050)
        fragments.append([dict(seconds=float(p/22050),correlation=float(correlation[p])) for p in peaks])
    assert all(len(f)==2 for f in fragments),'Two copies of each source fragment must be audible'
    for encounter in range(2):
        for fragment in (1,2):
            assert abs(fragments[fragment][encounter]['seconds']-fragments[0][encounter]['seconds']-fragment*2)<.12
    log=(directory/'test.log').read_text(encoding='utf-8',errors='replace')
    health=re.search(r'STPS ENEMY SUMMARY .*health=([\d.]+)',log)
    assert 'STPS SMOKE PASS enemy' in log and health and float(health[1])>0
    report=dict(passed=True,source_clip='scream2.wav',source_seconds=count/22050,
        recording_seconds=len(recorded)/22050,fragments=fragments,health=float(health[1]),
        rms=float(np.sqrt(np.mean(recorded**2))),elf_sha256=json.loads((directory/'elf.json').read_text())['Hash'])
    (directory/'audio-check.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    (root/'ps2/qa/last-audio-capture.txt').write_text(str(directory))
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);check(p.parse_args().directory)
