"""Collapse redundant subdivisions of planar, continuous textured surfaces."""
import numpy as np


def planar_subdivisions(vertices,triangles):
    faces=triangles.copy()
    for _ in range(5):
        incident={};edges={}
        for index,face in enumerate(faces):
            for vertex in face:incident.setdefault(int(vertex),[]).append(index)
            for a,b in ((face[0],face[1]),(face[1],face[2]),(face[2],face[0])):
                edge=tuple(sorted((int(a),int(b))));edges[edge]=edges.get(edge,0)+1
        replacement=np.arange(len(vertices));touched=set();changes=0
        for vertex,indices in incident.items():
            if vertex in touched or len(indices)<3:continue
            local=faces[indices];ring=np.unique(local)
            neighbours=ring[ring!=vertex]
            if any(edges.get(tuple(sorted((vertex,int(n)))),0)!=2 for n in neighbours):continue
            if any(int(n) in touched for n in ring):continue
            points=vertices[local,:3].astype(float)
            normals=np.cross(points[:,1]-points[:,0],points[:,2]-points[:,0])
            lengths=np.linalg.norm(normals,axis=1)
            if np.any(lengths<1e-8):continue
            normals/=lengths[:,None]
            normal=normals[0]
            if np.min(normals@normal)<.999999:continue
            if np.max(np.abs((vertices[ring,:3]-vertices[vertex,:3])@normal))>.0001:continue
            if np.max(np.abs(vertices[ring,5:8]-vertices[vertex,5:8]))>.0001:continue
            axes=[i for i in range(3) if i!=np.abs(normal).argmax()]
            coords=np.column_stack((vertices[ring][:,axes]-vertices[vertex,axes],np.ones(len(ring))))
            fit=np.linalg.lstsq(coords,vertices[ring,3:5],rcond=None)[0]
            if np.max(np.abs(coords@fit-vertices[ring,3:5]))>.0001:continue
            for target in neighbours:
                changed=local.copy();changed[changed==vertex]=target
                keep=(changed[:,0]!=changed[:,1])&(changed[:,1]!=changed[:,2])&(changed[:,2]!=changed[:,0])
                p=vertices[changed[keep],:3].astype(float)
                cross=np.cross(p[:,1]-p[:,0],p[:,2]-p[:,0])
                if np.any(cross@normal<=1e-8):continue
                replacement[vertex]=target;touched.update(map(int,ring));changes+=1;break
        if not changes:break
        faces=replacement[faces]
        faces=faces[(faces[:,0]!=faces[:,1])&(faces[:,1]!=faces[:,2])&(faces[:,2]!=faces[:,0])]
    return faces.astype('<u2')
