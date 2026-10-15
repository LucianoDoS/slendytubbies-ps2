"""Export youwonscene.unity as a small, separate 3D world for its fixed camera."""
import json
import numpy as np
from PIL import Image,ImageDraw
from font_atlas import debug_font
from export_assets import Exporter,trs,xyz
from export_player_trees import geometry,export_models

class VictoryExporter(Exporter):
    def export(self):
        self.image_texture('white',Image.new('RGBA',(64,64),'white'))
        f=self.load('Assets/youwonscene.unity')
        objects={o.path_id:o.read_typetree() for o in f[0].objects}
        types={o.path_id:o.type.name for o in f[0].objects}
        transforms={d['m_GameObject']['m_PathID']:pid for pid,d in objects.items() if types[pid]=='Transform'}
        def matrix(p):
            if not p:return np.eye(4)
            return matrix(objects[p]['m_Father']['m_PathID'])@trs(objects[p])
        for vertices,triangles,texture,color,name in geometry(self,f,world_space=True):
            self.meshes.append((vertices,triangles,texture,0,0,color,name))
        camera=next(d for pid,d in objects.items() if types[pid]=='Camera' and d['m_Enabled'])
        camera_pose=matrix(transforms[camera['m_GameObject']['m_PathID']])
        rs=next(d for pid,d in objects.items() if types[pid]=='RenderSettings')
        env=[rs['m_FogDensity']]+[rs['m_FogColor'][k] for k in ('r','g','b')]+[rs['m_AmbientLight'][k] for k in ('r','g','b')]
        self.config=[0.0]*127
        for i in range(3):self.config[56+i*7:63+i*7]=env
        component=next(d for pid,d in objects.items() if 'm_TerrainData' in d)
        obj=self.resolve(f,component['m_TerrainData']);tf=self.file_of(obj);terrain=obj.read_typetree();hm=terrain['m_Heightmap']
        self.width=self.height=257
        self.heights=np.asarray(hm['m_Heights'],dtype='<u2').reshape(513,513).T[::2,::2].copy()
        scale=hm['m_Scale'];self.config[77:80]=[scale['x']*2,scale['y']/32767,scale['z']*2]
        ground=terrain['m_SplatDatabase']['m_Splats'][0]
        self.config[80]=self.texture(self.resolve(tf,ground['texture']))
        self.splats=np.zeros((256,256,4),dtype='uint8');self.splats[:,:,0]=255
        self.trees=[]
        for tree in terrain['m_DetailDatabase']['m_TreeInstances']:
            p=xyz(tree['position'])*np.array([scale['x']*512,scale['y'],scale['z']*512])
            self.trees.append(list(p)+[tree['widthScale'],tree['heightScale'],float(tree['index'])])
        export_models(self,tf,terrain,export_viewmodel=False)
        sky=self.resolve(f,rs['m_SkyboxMaterial']);sf=self.file_of(sky)
        envs={k['name']:v for k,v in sky.read_typetree()['m_SavedProperties']['m_TexEnvs']}
        skies=[self.texture(self.resolve(sf,envs[k]['m_Texture']),128,opaque=True) for k in ('_FrontTex','_BackTex','_LeftTex','_RightTex','_UpTex','_DownTex')]
        light=next(d for pid,d in objects.items() if types[pid]=='Light' and d['m_Type']==1 and d['m_Enabled'])
        direction=-matrix(transforms[light['m_GameObject']['m_PathID']])[:3,2];direction/=np.linalg.norm(direction)
        sun=direction.tolist()+[light['m_Color'][k]*light['m_Intensity']*2 for k in ('r','g','b')]
        self.config[88]=self.image_texture('won-gui',Image.open(self.root/'Assets/youwongui.png'),512,256)
        font=Image.new('RGBA',(128,128),(0,0,0,0));draw=ImageDraw.Draw(font);ft=debug_font()
        for c in range(32,128):draw.text(((c%16)*8,(c//16-2)*16),chr(c),font=ft,fill='white')
        self.config[94]=self.image_texture('font',font,128)
        db=terrain['m_DetailDatabase'];spec=db['m_DetailPrototypes'][0]
        self.grass_texture=self.texture(self.resolve(tf,spec['prototypeTexture']))
        density=np.zeros((1024,1024),dtype='uint8')
        for i,patch in enumerate(db['m_Patches']):
            if not patch['layerIndices']:continue
            values=np.asarray(patch['numberOfObjects'],dtype='uint8').reshape(-1,8,8)
            density[(i//128)*8:(i//128)*8+8,(i%128)*8:(i%128)*8+8]=values.max(axis=0)
        density=density.reshape(256,4,256,4).max(axis=(1,3))
        self.grass_grid_size=256;self.grass_distance=45
        self.grass_index=np.zeros((32,32),dtype='<u2');raw=bytearray(64)
        for z in range(32):
            for x in range(32):
                patch=np.minimum(density[z*8:z*8+8,x*8:x*8+8],15)
                if not patch.any():continue
                self.grass_index[z,x]=len(raw)//64;raw.extend(patch.tobytes())
        self.grass_patches=bytes(raw)
        notice=Image.open(self.root/'Assets/playerhaswon.png');box=notice.getbbox()
        bounds=[box[0]/notice.width,box[1]/notice.height,(box[2]-box[0])/notice.width,(box[3]-box[1])/notice.height]
        grass=[spec[k] for k in ('minWidth','maxWidth','minHeight','maxHeight')]+[spec[c][k] for c in ('healthyColor','dryColor') for k in ('r','g','b')]
        header=['/* Original fixed-camera victory scene; geometry remains 3D. */',
                'static const float victory_camera[5]={'+','.join(f'{v:.9f}f' for v in (camera_pose[:3,3].tolist()+[float(np.arctan2(camera_pose[0,2],camera_pose[2,2])),float(np.arcsin(camera_pose[1,2]))]))+'};',
                'static const float victory_sun[6]={'+','.join(f'{v:.9f}f' for v in sun)+'};',
                'static const unsigned victory_sky[6]={'+','.join(map(str,skies))+'};',
                'static const float victory_grass[2][10]={'+','.join('{'+','.join(f'{v:.9f}f' for v in grass)+'}' for _ in range(2))+'};',
                'static const unsigned victory_grass_modes[2]={'+','.join(str(spec['renderMode']) for _ in range(2))+'};',
                'static const unsigned victory_tree_colors[]={'+','.join(str(t['color']['rgba'])+'u' for t in db['m_TreeInstances'])+'};',
                'static const float victory_notice[4]={'+','.join(f'{v:.9f}f' for v in bounds)+'};']
        (self.out/'victory_layout.h').write_text('\n'.join(header))
        (self.out/'scene.json').write_text(json.dumps({'source':'Assets/youwonscene.unity','camera':camera_pose.tolist(),'sun':sun,'trees':len(self.trees),'notice_duration':5,'detail_grid':256},indent=2))
        self.write_blob()

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--source',default='original');parser.add_argument('--output',default='ps2/generated/victory')
    args=parser.parse_args();VictoryExporter(args.source,args.output).export()
