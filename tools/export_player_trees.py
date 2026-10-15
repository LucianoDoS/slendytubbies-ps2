"""Export shared tree prototypes and the original first-person arm/flashlight."""
import numpy as np
from UnityPy.helpers.MeshHelper import MeshHandler


TREE, VIEW = 32, 64


def foliage_lod(vertices,triangles,stride):
    """Keep whole original leaf groups; never substitute a camera-facing image."""
    if len(triangles)<400:return vertices,triangles
    parent=list(range(len(vertices)))
    def root(i):
        while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
        return i
    for a,b,c in triangles:
        parent[root(int(b))]=root(int(a));parent[root(int(c))]=root(int(a))
    groups={}
    for i,tri in enumerate(triangles):groups.setdefault(root(int(tri[0])),[]).append(i)
    if len(groups)<len(triangles)//4:return vertices,triangles
    groups=list(groups.values());order=np.random.default_rng(2013).permutation(len(groups))
    selected=[i for index in order[::stride] for i in groups[index]]
    indices=triangles[np.asarray(selected)]
    used,remapped=np.unique(indices,return_inverse=True)
    return vertices[used].copy(),remapped.reshape(-1,3).astype('<u2')

def distant_lod(vertices,triangles,leaf_budget=80,coarse=False):
    """Low-cost 3D silhouette from source positions, preserving UV islands."""
    # Whole leaf cards must retain their UV corners. Welding them like a
    # trunk collapsed small cards, leaving the distant canopy bare.
    leaves,leaf_triangles=foliage_lod(vertices,triangles,max(1,len(triangles)//leaf_budget))
    if len(leaf_triangles)<len(triangles):return leaves,leaf_triangles
    keys=np.column_stack((np.round(vertices[:,:3]/(1.0 if coarse else .65)),np.floor(vertices[:,3:5]*(1 if coarse else 2))))
    _,first,remap=np.unique(keys,axis=0,return_index=True,return_inverse=True)
    triangles=remap[triangles]
    triangles=triangles[(triangles[:,0]!=triangles[:,1])&(triangles[:,0]!=triangles[:,2])&(triangles[:,1]!=triangles[:,2])]
    used,indices=np.unique(triangles,return_inverse=True)
    return vertices[first[used]].copy(),indices.reshape(-1,3).astype('<u2')


def geometry(exporter, file, camera_name=None, world_space=False, local_transforms=None):
    from export_assets import trs
    objects={o.path_id:o.read_typetree() for o in file[0].objects}
    types={o.path_id:o.type.name for o in file[0].objects}
    transforms={d['m_GameObject']['m_PathID']:pid for pid,d in objects.items() if types[pid]=='Transform'}
    matrices={}
    def matrix(pid):
        if not pid:return np.eye(4)
        if pid not in matrices:
            d=(local_transforms or {}).get(pid,objects[pid]);matrices[pid]=matrix(d['m_Father']['m_PathID'])@trs(d)
        return matrices[pid]
    # Terrain instantiates a prototype at each TreeInstance position. Discard
    # the editor position stored on the prefab root, retaining child transforms.
    root=next(pid for pid,d in objects.items() if types[pid]=='Transform' and not d['m_Father']['m_PathID'])
    relative=np.eye(4) if world_space else np.linalg.inv(matrix(root))
    if camera_name:
        go=next(pid for pid,d in objects.items() if types[pid]=='GameObject' and d['m_Name']==camera_name)
        relative=np.linalg.inv(matrix(transforms[go]))
    filters={d['m_GameObject']['m_PathID']:d for pid,d in objects.items() if types[pid]=='MeshFilter'}
    for pid,renderer in objects.items():
        if types[pid] not in ('MeshRenderer','SkinnedMeshRenderer') or not renderer.get('m_Enabled',True):continue
        go=renderer['m_GameObject']['m_PathID'];name=objects[go]['m_Name']
        if world_space:
            p=transforms[go];enabled=True
            while p:
                enabled=enabled and objects[objects[p]['m_GameObject']['m_PathID']].get('m_IsActive',True)
                p=objects[p]['m_Father']['m_PathID']
            if not enabled:continue
        if camera_name and name not in ('rightarmmesh','leftarmmesh','rightlegmesh','leftlegmesh','default','Cylinder003','Cylinder004'):continue
        ref=renderer.get('m_Mesh') or filters.get(go,{}).get('m_Mesh')
        mesh=exporter.resolve(file,ref)
        if mesh is None:continue
        handler=MeshHandler(mesh.read());handler.process()
        vertices=np.asarray(handler.m_Vertices,dtype=float)[:,:3]
        normals=np.asarray(handler.m_Normals if handler.m_Normals else [(0,1,0)]*len(vertices),float)[:,:3]
        homogeneous=np.column_stack((vertices,np.ones(len(vertices))))
        pose=relative@matrix(transforms[go])
        positions=(pose@homogeneous.T).T[:,:3]
        nworld=(np.linalg.inv(pose[:3,:3]).T@normals.T).T
        if types[pid]=='SkinnedMeshRenderer':
            data=mesh.read_typetree();skin=data.get('m_Skin',[]);bind=data.get('m_BindPose',[])
            if skin and bind:
                bones=[relative@matrix(b['m_PathID']) for b in renderer['m_Bones']]
                bind=[np.array([[b[f'e{r}{c}'] for c in range(4)] for r in range(4)]) for b in bind]
                bone_matrices=[bone@binding for bone,binding in zip(bones,bind)]
                normal_matrices=[np.linalg.inv(m[:3,:3]).T for m in bone_matrices]
                positions=np.zeros_like(vertices);nworld=np.zeros_like(normals)
                for j,weights in enumerate(skin):
                    for k in range(4):
                        weight=weights[f'weight[{k}]'];bone=weights[f'boneIndex[{k}]']
                        if weight:
                            positions[j]+=(bone_matrices[bone]@homogeneous[j])[:3]*weight
                            nworld[j]+=(normal_matrices[bone]@normals[j])*weight
        nworld/=np.maximum(np.linalg.norm(nworld,axis=1)[:,None],1e-8)
        uv=np.asarray(handler.m_UV0 if handler.m_UV0 else [(0,0)]*len(vertices),float)[:,:2]
        for sub,triangles in enumerate(handler.get_triangles()):
            if not triangles:continue
            material=exporter.resolve(file,renderer['m_Materials'][min(sub,len(renderer['m_Materials'])-1)])
            texture,scale,offset,color=exporter.material(material)
            texcoord=uv*np.asarray(scale)+np.asarray(offset);texcoord[:,1]=1-texcoord[:,1]
            yield np.column_stack((positions,texcoord,nworld)).astype('<f4'),np.asarray(triangles,dtype='<u2'),texture,color,name


def export_models(exporter, terrain_file, terrain, export_viewmodel=True):
    report={'trees':[],'viewmodel':[]}
    for kind,prototype in enumerate(terrain['m_DetailDatabase']['m_TreePrototypes']):
        prefab=exporter.resolve(terrain_file,prototype['prefab']);file=exporter.file_of(prefab)
        name=prefab.read_typetree()['m_Name'];count=0;lod_counts=[0,0]
        for vertices,triangles,texture,color,part in geometry(exporter,file):
            exporter.meshes.append((vertices,triangles,texture,TREE,kind,color,f'tree-prototype/{name}/{part}'))
            count+=len(triangles)
            for level,stride in enumerate((3,8),1):
                v,tri=foliage_lod(vertices,triangles,stride)
                exporter.meshes.append((v,tri,texture,TREE,kind+3*level,color,f'tree-prototype/{name}/lod{level}/{part}'))
                lod_counts[level-1]+=len(tri)
            v,tri=distant_lod(vertices,triangles)
            exporter.meshes.append((v,tri,texture,TREE,kind+9,color,f'tree-prototype/{name}/distant/{part}'))
            v,tri=distant_lod(vertices,triangles,40,coarse=True)
            exporter.meshes.append((v,tri,texture,TREE,kind+12,color,f'tree-prototype/{name}/encounter-distant/{part}'))
        report['trees'].append({'index':kind,'name':name,'triangles':count,'lod_triangles':lod_counts})
    if not export_viewmodel:return report
    player=exporter.load('Assets/PlayerMultiplayer.prefab')
    for vertices,triangles,texture,color,name in geometry(exporter,player,'Camera'):
        exporter.meshes.append((vertices,triangles,texture,VIEW,0,color,f'first-person/{name}'))
        report['viewmodel'].append({'name':name,'triangles':len(triangles),
                                  'min':vertices[:,:3].min(axis=0).tolist(),'max':vertices[:,:3].max(axis=0).tolist()})
    return report
