"""Resolve gameplay sounds from serialized sources; keep complete long loops."""
import audioop,json,struct,zlib
from pathlib import Path
import numpy as np
import soundfile as sf

ULAW=0x80000000

def source_path(e,obj):
    cached=next(path for path,file in e.cache.items() if file[1] is obj.assets_file)
    return Path(cached.removesuffix('.meta'))

def pcm(path,seconds=0):
    data,rate=sf.read(path,dtype='int16',always_2d=True)
    if seconds:data=data[:int(rate*seconds)]
    raw=data.astype('<i2').tobytes()
    if data.shape[1]==2:raw=audioop.tomono(raw,2,.5,.5)
    elif data.shape[1]!=1:raise ValueError('Unsupported channel layout')
    return audioop.ratecv(raw,2,1,rate,22050,None)[0]

def export_audio(e,file,objects,types,matrix,transforms,player,ai):
    sources={objects[d['m_GameObject']['m_PathID']]['m_Name']:d
             for pid,d in objects.items() if types[pid]=='AudioSource'}
    spec=[];environment=[]
    for name in ('Flame','Teletubby house','vacum'):
        d=sources[name];obj=e.resolve(file,d['m_audioClip']);clip=obj.read_typetree()
        path=source_path(e,obj);position=matrix(transforms[d['m_GameObject']['m_PathID']])[:3,3]
        spec.append(position.tolist()+[d['m_Volume'],d['MinDistance'],d['MaxDistance'],int(clip['m_3D'])])
        environment.append(path)
    scripts=[o.read_typetree() for o in player[0].objects if o.type.name=='MonoBehaviour']
    flash=next(d for d in scripts if 'switchsound' in d and d['switchsound']['m_PathID'])
    footsteps=next(d for d in scripts if 'walkingsound' in d)
    nv=e.load('Assets/PlayerMultiplayerNVG.prefab')
    night=next(e.resolve(nv,d['switchsound']) for o in nv[0].objects if o.type.name=='MonoBehaviour'
               for d in [o.read_typetree()] if d.get('switchsound',{}).get('m_PathID')
               and e.resolve(nv,d['switchsound']).read_typetree()['m_Name']=='nightvisionsound')
    nearby=e.resolve(file,ai['nearbyaudio'])
    paths=[environment[0],source_path(e,e.resolve(player,footsteps['walkingsound'])),environment[1],environment[2],
           source_path(e,nearby),e.root/'Assets/scream2.wav',source_path(e,e.resolve(player,flash['switchsound'])),
           e.root/'Assets/nearbysound.wav',source_path(e,night)]
    report=[];golden=[]
    for i,path in enumerate(paths):
        raw=pcm(path,4 if i==1 else 0);encoded=i in (0,2)
        data=audioop.lin2ulaw(raw,2) if encoded else raw
        e.audio.append((path.stem,data))
        if encoded:e.audio_codes[path.stem]='ulaw'
        decoded=audioop.ulaw2lin(data,2) if encoded else raw
        checksum=zlib.crc32(decoded);golden.append(checksum)
        report.append(dict(id=i,source=path.relative_to(e.root).as_posix(),samples=len(raw)//2,
            seconds=len(raw)/44100,bytes=len(data),format='G.711 mu-law' if encoded else 'PCM16',decoded_crc32=checksum))
    (e.out/'audio.json').write_text(json.dumps(dict(sounds=report,environment=spec),indent=2))
    text=['/* Serialized scene AudioSources: fire, ambient house sound, vacuum. */',
          'static const float source_environment_audio[3][7]={'+','.join('{'+','.join(f'{v:.9f}f' for v in row)+'}' for row in spec)+'};',
          'static const unsigned source_audio_crc32[9]={'+','.join(f'{v}u' for v in golden)+'};']
    (e.out/'scene_audio.h').write_text('\n'.join(text))
