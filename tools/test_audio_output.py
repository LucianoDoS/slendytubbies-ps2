"""Record the PCSX2 enemy test through Windows speaker loopback, then fingerprint
the source scream in the audible output (rather than only testing a PCM cursor).
Requires PyAudioWPatch and scipy; never opens a microphone.
"""
import argparse,hashlib,json,shutil,subprocess,time,wave
from datetime import datetime
from pathlib import Path
import pyaudiowpatch as pa
from check_audio_output import check
from check_switch_audio import check_switches

parser=argparse.ArgumentParser();parser.add_argument('--mode',choices=['enemy','switches'],default='enemy');args=parser.parse_args()

root=Path(__file__).resolve().parents[1]
out=root/'ps2/qa/capture-audio'/datetime.now().strftime('%Y%m%d-%H%M%S')
out.mkdir(parents=True,exist_ok=True)
(out/'elf.json').write_text(json.dumps({'Hash':hashlib.sha256((root/'dist/SLENDY.ELF').read_bytes()).hexdigest().upper()}))
frames=[]
with pa.PyAudio() as audio:
    device=audio.get_default_wasapi_loopback()
    rate=int(device['defaultSampleRate']);channels=int(device['maxInputChannels'])
    def callback(data,count,timing,status):
        frames.append(data)
        return None,pa.paContinue
    with audio.open(format=pa.paInt16,channels=channels,rate=rate,input=True,
                    input_device_index=device['index'],frames_per_buffer=1024,stream_callback=callback):
        with (out/'test.log').open('w') as log:
            test=subprocess.Popen([shutil.which('pwsh') or 'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',
                str(root/'tools/test_pcsx2.ps1'),'-Enemy' if args.mode=='enemy' else '-Audio','-Visible'],cwd=root,stdout=log,stderr=subprocess.STDOUT)
            started=time.monotonic()
            while test.poll() is None:
                if time.monotonic()-started>110:
                    test.terminate();raise RuntimeError('Enemy test timed out')
                time.sleep(.2)
        time.sleep(.5)
        if test.returncode:raise RuntimeError(f'Native enemy test failed: {out}/test.log')
raw=b''.join(frames)
with wave.open(str(out/'pcsx2-enemy.wav'),'wb') as wav:
    wav.setparams((channels,2,rate,0,'NONE','not compressed'));wav.writeframes(raw)
check(out) if args.mode=='enemy' else check_switches(out)
