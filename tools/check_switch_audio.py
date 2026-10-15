"""Verify actual switch sounds and continuous source ambience in loopback audio."""
import audioop,json,math,struct,wave
from pathlib import Path
import numpy as np
from scipy import signal

def check_switches(directory):
    root=Path(__file__).resolve().parents[1];directory=Path(directory)
    with wave.open(str(directory/'pcsx2-enemy.wav')) as wav:
        rate=wav.getframerate();channels=wav.getnchannels();raw=wav.readframes(wav.getnframes())
    recorded=np.frombuffer(raw,dtype='<i2').reshape(-1,channels).mean(1)
    divisor=math.gcd(rate,22050);recorded=signal.resample_poly(recorded,22050//divisor,rate//divisor)
    b=(root/'ps2/generated/world.bin').read_bytes();h=struct.unpack_from('<20I',b)
    def source(id):
        offset,count=struct.unpack_from('<2I',b,h[16]+id*8);samples=count&0x7fffffff
        raw=b[offset:offset+samples*(1 if count&0x80000000 else 2)]
        if count&0x80000000:raw=audioop.ulaw2lin(raw,2)
        return np.frombuffer(raw,dtype='<i2').astype(float)
    def correlation(template):
        cross=signal.correlate(recorded,template,mode='valid',method='fft')
        energy=signal.convolve(recorded**2,np.ones(len(template)),mode='valid',method='fft')
        return cross/np.sqrt(np.maximum(energy,1)*np.dot(template,template))
    # Anchor the recording to two distinct flashlight clicks, rather than the
    # very quiet opening of the background track or unrelated desktop audio.
    clip=source(6);length=2205;offset=int(np.convolve(clip**2,np.ones(length),'valid').argmax())
    score=correlation(clip[offset:offset+length]);peaks,_=signal.find_peaks(score,height=.55,distance=22050)
    candidates=[(float(score[a]+score[c]),a,c) for a in peaks for c in peaks
                if abs((c-a)/22050-3)<.12]
    assert candidates,'Both flashlight clicks must match the original source'
    _,first,second=max(candidates);origin=(first/22050-3-offset/22050+second/22050-6-offset/22050)*.5
    # Most source energy is infrasonic. Speaker processing changes that band;
    # fingerprint audible detail above 600 Hz. The near-house segment also
    # contains the original positional fire/vacuum sources, so use the two
    # far segments on opposite sides of that visit to verify uninterrupted play.
    filt=signal.butter(3,600,fs=22050,btype='highpass',output='sos')
    ambient=signal.sosfilt(filt,source(2));filtered=signal.sosfilt(filt,recorded);matches=[]
    for start in (4,19):
        template=ambient[start*22050:start*22050+5500]
        cross=signal.correlate(filtered,template,mode='valid',method='fft')
        energy=signal.convolve(filtered**2,np.ones(len(template)),mode='valid',method='fft')
        scores=cross/np.sqrt(np.maximum(energy,1)*np.dot(template,template))
        expected=int((origin+start)*22050);lo=max(0,expected-5500);hi=min(len(scores),expected+5500)
        index=lo+int(np.argmax(scores[lo:hi]));matches.append(dict(source_seconds=start,output_seconds=index/22050,correlation=float(scores[index])))
        assert scores[index]>.55,'Audible source ambience does not match before/after the house visit'
    assert abs(matches[1]['output_seconds']-matches[0]['output_seconds']-15)<.12
    switches={}
    for id,times,name in ((6,(3,6),'flashlight'),):
        clip=source(id);length=2205
        energy=np.convolve(clip**2,np.ones(length),'valid');offset=int(energy.argmax())
        scores=correlation(clip[offset:offset+length]);found=[]
        for time in times:
            expected=int((origin+time+offset/22050)*22050);lo=max(0,expected-5500);hi=min(len(scores),expected+5500)
            index=lo+int(np.argmax(scores[lo:hi]));found.append(dict(seconds=index/22050,correlation=float(scores[index])))
            assert scores[index]>.55,f'{name} does not match its original clip'
        switches[name]=found
    log=(directory/'test.log').read_text(encoding='utf-8',errors='replace')
    assert 'STPS SMOKE PASS audio' in log and 'flashlight=2 reserved_ignored=2' in log
    report=dict(passed=True,ambient_source='Assets/teletubbyhousebackground.wav',ambient_matches=matches,
                switch_source='Assets/switch.mp3',switches=switches,elf_sha256=json.loads((directory/'elf.json').read_text())['Hash'])
    (directory/'switch-audio-check.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    (root/'ps2/qa/last-switch-audio-capture.txt').write_text(str(directory))
    return report
