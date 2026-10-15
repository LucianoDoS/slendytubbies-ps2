"""Original skyboxes, directional lights and five-second startup overlays."""
import json
import numpy as np
from PIL import Image


def export_visuals(exporter,terrain):
    from export_assets import trs
    skies=[];sun=[]
    for name in ('SingleplayerMorning','SingleplayerDusk','SingleplayerNight'):
        file=exporter.load(f'Assets/{name}.unity')
        objects={o.path_id:o.read_typetree() for o in file[0].objects}
        types={o.path_id:o.type.name for o in file[0].objects}
        transforms={d['m_GameObject']['m_PathID']:pid for pid,d in objects.items() if types[pid]=='Transform'}
        def matrix(pid):
            if not pid:return np.eye(4)
            d=objects[pid];return matrix(d['m_Father']['m_PathID'])@trs(d)
        settings=next(d for pid,d in objects.items() if types[pid]=='RenderSettings')
        material=exporter.resolve(file,settings['m_SkyboxMaterial']);mf=exporter.file_of(material)
        envs={k['name']:v for k,v in material.read_typetree()['m_SavedProperties']['m_TexEnvs']}
        skies.append([exporter.texture(exporter.resolve(mf,envs[key]['m_Texture']),128,opaque=True)
                      for key in ('_FrontTex','_BackTex','_LeftTex','_RightTex','_UpTex','_DownTex')])
        light=next((d for pid,d in objects.items() if types[pid]=='Light' and d['m_Type']==1 and d['m_Enabled']),None)
        if light:
            direction=-matrix(transforms[light['m_GameObject']['m_PathID']])[:3,2]
            direction/=np.linalg.norm(direction)
            sun.append(direction.tolist()+[light['m_Color'][k]*light['m_Intensity']*2 for k in ('r','g','b')])
        else:sun.append([0]*6)
    overlays=[]
    for name in ('collectgui','helplessvictum'):
        image=Image.open(exporter.root/f'Assets/{name}.png');box=image.getbbox()
        texture=exporter.image_texture(name+'-cropped',image.crop(box),256,64 if name=='collectgui' else 32)
        overlays.append([texture,box[0]/image.width,box[1]/image.height,
                         (box[2]-box[0])/image.width,(box[3]-box[1])/image.height])
    player=exporter.load('Assets/PlayerMultiplayer.prefab')
    spotlight=next(o.read_typetree() for o in player[0].objects if o.type.name=='Light' and o.read_typetree()['m_Type']==0)
    cookie=exporter.texture(exporter.resolve(player,spotlight['m_Cookie']),64)
    header=['/* Generated from the original scenes and popup textures. */',
            f'static const unsigned scene_cookie={cookie};',
            'static const float scene_flashlight_color[3]={'+','.join(f"{spotlight['m_Color'][k]:.9f}f" for k in ('r','g','b'))+'};',
            'static const unsigned scene_sky[3][6]={'+','.join('{'+','.join(map(str,s))+'}' for s in skies)+'};',
            'static const float scene_sun[3][6]={'+','.join('{'+','.join(f'{v:.9f}f' for v in s)+'}' for s in sun)+'};',
            'static const float scene_intro[2][5]={'+','.join('{'+','.join(f'{v:.9f}f' for v in s)+'}' for s in overlays)+'};',
            'static const unsigned scene_tree_colors[]={'+','.join(str(t['color']['rgba'])+'u' for t in terrain['m_DetailDatabase']['m_TreeInstances'])+'};']
    (exporter.out/'scene_visuals.h').write_text('\n'.join(header))
    (exporter.out/'scene_visuals.json').write_text(json.dumps({'skies':skies,'sun':sun,'intro':overlays,'cookie':cookie},indent=2))
