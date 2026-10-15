"""Export mainmeny.unity, including its six cameras and original 3D buttons."""
import json
import audioop
import hashlib
from pathlib import Path
import numpy as np
import soundfile as sf
from PIL import Image, ImageDraw
from font_atlas import debug_font
from UnityPy.helpers.MeshHelper import MeshHandler
from export_assets import Exporter, trs, xyz
from export_menu_particles import export_particles

CAMERAS = ['CameraMenu','CameraSettings','CameraSingleplayer','CameraMultiplayer','CameraCoop','CameraVerses']
# GameObject ids from the source scene, in controller navigation order.
BUTTONS = [[339,338,337,336,5],[331],[282,281,280,6],[10,11,9],[15,16,17,8],[14,13,12,7]]

class MenuExporter(Exporter):
    def export(self):
        self.baked_colors=True
        self.image_texture('white',Image.new('RGBA',(64,64),'white'))
        f=self.load('Assets/mainmeny.unity')
        objects={o.path_id:o.read_typetree() for o in f[0].objects}
        types={o.path_id:o.type.name for o in f[0].objects}
        transforms={d['m_GameObject']['m_PathID']:pid for pid,d in objects.items() if types[pid]=='Transform'}
        matrices={}
        def matrix(pid):
            if not pid:return np.eye(4)
            if pid not in matrices:
                d=objects[pid];matrices[pid]=matrix(d['m_Father']['m_PathID'])@trs(d)
            return matrices[pid]
        def active(go):
            while go:
                if not objects[go].get('m_IsActive',1):return False
                parent=objects[transforms[go]]['m_Father']['m_PathID']
                go=objects[parent]['m_GameObject']['m_PathID'] if parent else 0
            return True
        def pose(go):return matrix(transforms[go])
        cameras=[]
        for name in CAMERAS:
            go=next(pid for pid,d in objects.items() if types[pid]=='GameObject' and d['m_Name']==name)
            cameras.append(pose(go))
        lights=[]
        for pid,d in objects.items():
            if types[pid]=='Light' and d['m_Enabled'] and active(d['m_GameObject']['m_PathID']):
                lights.append((pose(d['m_GameObject']['m_PathID'])[:3,3],xyz_rgb(d['m_Color'])*d['m_Intensity'],d['m_Range']))
        filters={d['m_GameObject']['m_PathID']:d for pid,d in objects.items() if types[pid]=='MeshFilter'}
        buttons={go for page in BUTTONS for go in page}
        for pid,rd in objects.items():
            if types[pid] not in ('MeshRenderer','SkinnedMeshRenderer') or not rd['m_Enabled']:continue
            go=rd['m_GameObject']['m_PathID']
            if not active(go):continue
            ref=rd.get('m_Mesh') or filters.get(go,{}).get('m_Mesh')
            if not ref or not ref['m_PathID']:continue
            if ref['m_PathID']==10209: # Unity's built-in 10x10 XZ plane.
                v=np.array([[-5,0,-5],[5,0,-5],[5,0,5],[-5,0,5]],float)
                normals=np.tile([0,1,0],(4,1));uv=np.array([[1,1],[0,1],[0,0],[1,0]],float)
                submeshes=[[(0,2,1),(0,3,2)]]
            else:
                mesh=self.resolve(f,ref)
                if mesh is None:continue
                h=MeshHandler(mesh.read());h.process()
                v=np.asarray(h.m_Vertices,dtype=float)[:,:3]
                normals=np.asarray(h.m_Normals if h.m_Normals else [(0,1,0)]*len(v),float)[:,:3]
                uv=np.asarray(h.m_UV0 if h.m_UV0 else [(0,0)]*len(v),float)[:,:2]
                submeshes=h.get_triangles()
            homogeneous=np.column_stack((v,np.ones(len(v))))
            world=(pose(go)@homogeneous.T).T[:,:3]
            normals=(np.linalg.inv(pose(go)[:3,:3]).T@normals.T).T
            if types[pid]=='SkinnedMeshRenderer':
                md=mesh.read_typetree();skin=md.get('m_Skin',[]);bind=md.get('m_BindPose',[])
                if skin and bind:
                    bones=[matrix(b['m_PathID']) for b in rd['m_Bones']]
                    bind=[np.array([[b[f'e{r}{c}'] for c in range(4)] for r in range(4)]) for b in bind]
                    world=np.zeros_like(v)
                    for j,s in enumerate(skin):
                        for k in range(4):
                            weight=s[f'weight[{k}]'];bi=s[f'boneIndex[{k}]']
                            if weight:world[j]+=(bones[bi]@bind[bi]@homogeneous[j])[:3]*weight
            normals/=np.maximum(np.linalg.norm(normals,axis=1)[:,None],1e-8)
            lighting=np.full_like(world,.261194)
            for p,color,radius in lights:
                direction=p-world;distance=np.linalg.norm(direction,axis=1)
                diffuse=np.maximum(0,(normals*direction/np.maximum(distance[:,None],.001)).sum(axis=1))
                lighting+=(np.maximum(0,1-distance/radius)**2*diffuse)[:,None]*color*2
            if go in buttons:lighting[:]=1 # Original self-illuminated button materials.
            for sub,tri in enumerate(submeshes):
                if not tri:continue
                material=self.resolve(f,rd['m_Materials'][min(sub,len(rd['m_Materials'])-1)]) if rd['m_Materials'] else None
                tex,scale,offset,color=self.material(material)
                if go in buttons and material is not None:
                    mf=self.file_of(material)
                    for k,val in material.read_typetree()['m_SavedProperties']['m_TexEnvs']:
                        if k['name']=='_MainTex':tex=self.texture(self.resolve(mf,val['m_Texture']),128)
                tuv=uv*np.array(scale)+np.array(offset);tuv[:,1]=1-tuv[:,1]
                verts=np.column_stack((world,tuv,lighting)).astype('<f4')
                self.meshes.append((verts,np.asarray(tri,dtype='<u2'),tex,0,go,color,objects[go]['m_Name']))
        self.config=[0.0]*127
        rs=next(d for pid,d in objects.items() if types[pid]=='RenderSettings')
        env=[rs['m_FogDensity']]+list(xyz_rgb(rs['m_FogColor']))+list(xyz_rgb(rs['m_AmbientLight']))
        for i in range(3):self.config[56+i*7:63+i*7]=env
        terrain_component=next(d for pid,d in objects.items() if 'm_TerrainData' in d)
        td_obj=self.resolve(f,terrain_component['m_TerrainData']);td=td_obj.read_typetree();tf=self.file_of(td_obj)
        hm=td['m_Heightmap'];self.width=hm['m_Width'];self.height=hm['m_Height']
        self.heights=np.asarray(hm['m_Heights'],dtype='<u2').reshape(self.width,self.height).T.copy()
        self.config[77:80]=[hm['m_Scale']['x'],hm['m_Scale']['y']/32767,hm['m_Scale']['z']]
        ground=[self.texture(self.resolve(tf,s['texture'])) for s in td['m_SplatDatabase']['m_Splats']]
        self.config[80:84]=(ground+[0]*4)[:4]
        alpha=self.resolve(tf,td['m_SplatDatabase']['m_AlphaTextures'][0]).read().image
        self.splats=np.flipud(np.argmax(np.asarray(alpha.resize((self.width-1,self.height-1)))[:,:,:len(ground)],axis=2)).astype('uint8')
        self.trees=[]
        terrain_origin=pose(terrain_component['m_GameObject']['m_PathID'])[:3,3]
        # Translate all menu geometry and cameras into this terrain's coordinate frame.
        self.meshes=[(v.copy(),tri,tex,flags,identifier,color,name) for v,tri,tex,flags,identifier,color,name in self.meshes]
        for v,*_ in self.meshes:v[:,:3]-=terrain_origin
        for c in cameras:c[:3,3]-=terrain_origin
        # Preserve the transparent margins of the original screen-wide title.
        title=Image.open(self.root/'Assets/slendytubbiestitle.png');box=title.getbbox()
        self.config[85]=self.image_texture('original-title',title.crop(box),256)
        self.config[95:99]=[box[0]/title.width,box[1]/title.height,(box[2]-box[0])/title.width,(box[3]-box[1])/title.height]
        self.config[86]=self.image_texture('original-help',Image.open(self.root/'Assets/helpscreen.png'),512)
        self.config[87]=self.image_texture('original-credits',Image.open(self.root/'Assets/creditsroll.png'),512)
        font=Image.new('RGBA',(128,128),(0,0,0,0));draw=ImageDraw.Draw(font)
        ft=debug_font()
        for c in range(32,128):draw.text(((c%16)*8,(c//16-2)*16),chr(c),font=ft,fill='white')
        self.config[94]=self.image_texture('font',font,128)
        # Follow the actual AudioSource reference, rather than choosing another track.
        source=next(d for pid,d in objects.items() if types[pid]=='AudioSource'
                    and objects[d['m_GameObject']['m_PathID']]['m_Name']=='Terrain')
        clip=self.resolve(f,source['m_audioClip'])
        imported=next(path for path,loaded in self.cache.items() if loaded[1] is clip.assets_file)
        music_path=Path(imported.removesuffix('.meta'))
        pcm,rate=sf.read(music_path,dtype='int16',always_2d=True)
        if pcm.shape[1]!=2:raise ValueError('Expected original stereo menu theme')
        mono=audioop.tomono(pcm.astype('<i2').tobytes(),2,.5,.5)
        mono,_=audioop.ratecv(mono,2,1,rate,22050,None)
        self.audio.append(('menu-theme',audioop.lin2ulaw(mono,2)))
        self.audio_codes['menu-theme']='ulaw'
        self.config[99]=source['m_Volume']
        self.config[100]=int(source['Loop'])
        (self.out/'music.json').write_text(json.dumps({
            'source':music_path.relative_to(self.root).as_posix(),
            'source_sha256':hashlib.sha256(music_path.read_bytes()).hexdigest(),
            'duration_seconds':len(mono)/44100,'samples':len(mono)//2,
            'format':'G.711 mu-law, 22050 Hz, mono',
            'volume':source['m_Volume'],'loop':source['Loop'],
            'pcm_sha256':hashlib.sha256(mono).hexdigest()},indent=2))
        particle_header=export_particles(self,f,objects,types,pose,terrain_origin)
        self.write_blob()
        layout={'source':'Assets/mainmeny.unity','cameras':[c.tolist() for c in cameras],'pages':[]}
        header=['/* Generated from mainmeny.unity; do not edit. */','static const float menu_camera[6][16]={']
        for c in cameras:header.append('{'+','.join(f'{float(v):.9f}f' for v in c.flatten())+'},')
        header.append('};\nstatic const unsigned menu_button_ids[6][5]={')
        for ids in BUTTONS:header.append('{'+','.join(map(str,ids+[0]*(5-len(ids))))+'},')
        header.append('};\nstatic const float menu_button_positions[6][5][3]={')
        for ids in BUTTONS:
            points=[(pose(go)[:3,3]-terrain_origin).tolist() for go in ids]
            layout['pages'].append([{'id':go,'name':objects[go]['m_Name'],'position':point} for go,point in zip(ids,points)])
            points += [[0,0,0]]*(5-len(points))
            header.append('{'+','.join('{'+','.join(f'{v:.9f}f' for v in p)+'}' for p in points)+'},')
        header.append('};')
        header.append(particle_header)
        (self.out/'menu_layout.h').write_text('\n'.join(header))
        (self.out/'menu_layout.json').write_text(json.dumps(layout,indent=2))

def xyz_rgb(d):return np.array([d[k] for k in ('r','g','b')])

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--source',default='original');parser.add_argument('--output',default='ps2/generated/menu')
    args=parser.parse_args();MenuExporter(args.source,args.output).export()
