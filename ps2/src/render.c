#include "render.h"
#include "terrain_surface.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../generated/menu/menu_layout.h"
#include "../generated/scene_visuals.h"
#include "../generated/fidelity.h"
#include "../generated/victory/victory_layout.h"
#include "../generated/player_walk.h"
#include "transform.h"

typedef struct { float x,y,z,u,v,r,g,b,f,alpha; unsigned mask; } CameraVertex;
enum { TRIANGLE_BUDGET=20000 };
static const World *world;static const Game *game;
static const World *menu_world,*victory_world;static int menu_scene,menu_baked,win_scene;
static const float *active_sun;
static const unsigned *active_tree_colors;
static const float (*active_grass)[10];
static const unsigned *active_grass_modes;
static const float *walk_frames;
int render_set_view_animation(const void *data,size_t bytes) {
    if(bytes!=VIEW_WALK_FRAMES*VIEW_WALK_VERTICES*6*sizeof(float))return 0;
    walk_frames=data;return 1;
}
static const float *menu_matrix;
void render_set_menu_world(const World *w) {menu_world=w;}
void render_set_victory_world(const World *w) {victory_world=w;}
static float sy,cy,sp,cp,far_distance,density,fog[3],ambient[3];
static int triangles;
static float draw_color[3]={1,1,1};
static unsigned draw_alpha=128;
static int cull_faces;
static float fog_table[4097];static float cached_density=-1;
static float clampf(float x,float min,float max) { return x<min?min:x>max?max:x; }
static int tree_in_view(const Tree *tree,const float bounds[6]);
static void prepare_vertex(CameraVertex *v) {
    v->mask=(v->z<.15f?1:0)|(v->x+v->z*.7698f<0?2:0)|(v->z*.7698f-v->x<0?4:0)
           |(v->y+v->z*.57735f<0?8:0)|(v->z*.57735f-v->y<0?16:0);
}
static CameraVertex camera(Vec3 p,float u,float v,float nx,float ny,float nz,const float *color) {
    float dx=p.x-game->feet.x,dy=p.y-game->feet.y-game->eye,dz=p.z-game->feet.z;
    float vz=dx*sy+dz*cy; CameraVertex out;
    out.x=dx*cy-dz*sy;out.y=dy*cp-vz*sp;out.z=dy*sp+vz*cp;out.u=u;out.v=v;
    if(menu_scene) {
        out.x=dx*menu_matrix[0]+dy*menu_matrix[4]+dz*menu_matrix[8];
        out.y=dx*menu_matrix[1]+dy*menu_matrix[5]+dz*menu_matrix[9];
        out.z=dx*menu_matrix[2]+dy*menu_matrix[6]+dz*menu_matrix[10];
    }
    float distance2=dx*dx+dy*dy+dz*dz,light=0;
    if(game->flashlight&&distance2<400) {
        float dist=sqrtf(distance2);
        float cone=out.z/fmaxf(dist,.001f);light=clampf((cone-.9063078f)*18,0,1)*clampf(1-dist/20,0,1)*2.5f;
    }
    float diffuse=clampf(ny*.5f-nx*.15f-nz*.15f+.6f,.3f,1);
    int fog_index=(int)(distance2*.125f);float visibility=fog_table[fog_index<4096?fog_index:4096];
    out.r=clampf(color[0]*(ambient[0]*diffuse+light),0,1.7f);
    out.g=clampf(color[1]*(ambient[1]*diffuse+light),0,1.7f);
    out.b=clampf(color[2]*(ambient[2]*diffuse+light),0,1.7f);
    out.f=visibility;out.alpha=1;
    if(!menu_scene) {
        const float *sun=active_sun;float lambert=fmaxf(0,nx*sun[0]+ny*sun[1]+nz*sun[2]);
        out.r=clampf(out.r+color[0]*sun[3]*lambert,0,1.99f);
        out.g=clampf(out.g+color[1]*sun[4]*lambert,0,1.99f);
        out.b=clampf(out.b+color[2]*sun[5]*lambert,0,1.99f);
    }
    if(menu_baked) {out.r=nx;out.g=ny;out.b=nz;}
    prepare_vertex(&out);return out;
}
static float (*vertex_light)[3];static unsigned *light_offsets,*tree_order;
static unsigned tree_first[15],tree_count[15],tree_cost[15];static float tree_bounds[3][6];
static size_t light_capacity,offset_capacity;static const World *lit_world;
static int lit_day=-1;
static int cache_lights(void) {
    if(lit_world==world&&lit_day==game->time_of_day)return 1;
    if(world->header->meshes>offset_capacity){unsigned *next=realloc(light_offsets,world->header->meshes*sizeof(*next));
        if(!next)return 0;
        light_offsets=next;offset_capacity=world->header->meshes;}
    if(lit_world!=world) {
        unsigned *next=realloc(tree_order,world->header->meshes*sizeof(*next));if(!next)return 0;tree_order=next;
        unsigned cursor=0;
        for(int kind=0;kind<15;kind++) {
            tree_first[kind]=cursor;tree_count[kind]=tree_cost[kind]=0;
            if(kind<3)for(int k=0;k<3;k++){tree_bounds[kind][k]=1e9f;tree_bounds[kind][k+3]=-1e9f;}
            for(unsigned i=0;i<world->header->meshes;i++) {
                const Mesh *m=world->mesh+i;if(!(m->flags&M_TREE)||m->id!=(unsigned)kind)continue;
                tree_order[cursor++]=i;tree_count[kind]++;tree_cost[kind]+=m->triangles;
                if(kind<3)for(int k=0;k<3;k++){tree_bounds[kind][k]=fminf(tree_bounds[kind][k],m->min[k]);tree_bounds[kind][k+3]=fmaxf(tree_bounds[kind][k+3],m->max[k]);}
            }
        }
    }
    unsigned total=0;
    for(unsigned i=0;i<world->header->meshes;i++){light_offsets[i]=total;total+=world->mesh[i].vertices;}
    if(total>light_capacity){void *next=realloc(vertex_light,total*sizeof(*vertex_light));if(!next)return 0;vertex_light=next;light_capacity=total;}
    const float *sun=active_sun;
    for(unsigned i=0;i<world->header->meshes;i++) {
        const Mesh *m=world->mesh+i;const Vertex *v=(const Vertex *)(world->blob+m->vertex_offset);
        for(unsigned j=0;j<m->vertices;j++) {
            float diffuse=clampf(v[j].ny*.5f-v[j].nx*.15f-v[j].nz*.15f+.6f,.3f,1);
            float lambert=fmaxf(0,v[j].nx*sun[0]+v[j].ny*sun[1]+v[j].nz*sun[2]);
            for(int k=0;k<3;k++)vertex_light[light_offsets[i]+j][k]=m->color[k]*(ambient[k]*diffuse+sun[3+k]*lambert);
        }
    }
    lit_world=world;lit_day=game->time_of_day;return 1;
}
static CameraVertex camera_view(const float *position,float u,float v) {
    CameraVertex out;out.x=position[0];out.y=position[1];out.z=position[2];out.u=u;out.v=v;
    out.r=position[3];out.g=out.b=0;out.f=1;out.alpha=1;
    prepare_vertex(&out);return out;
}
static float view_matrix[16] __attribute__((aligned(16)));
static float transformed[4096][4] __attribute__((aligned(16)));
static void camera_vertices(CameraVertex *out,const Vertex *vertices,unsigned count,const float *pose) {
    for(unsigned offset=0;offset<count;offset+=4096) {
        unsigned batch=count-offset;if(batch>4096)batch=4096;
        transform_vertices(transformed,vertices+offset,batch,pose);
        for(unsigned j=0;j<batch;j++) {
            out[offset+j]=camera_view(transformed[j],vertices[offset+j].u,vertices[offset+j].v);
            out[offset+j].mask|=(offset+j+1)<<8;
        }
    }
}
static void prepare_view_matrix(void) {
    float x=-game->feet.x,y=-game->feet.y-game->eye,z=-game->feet.z;
    const float m[16]={cy,-sy*sp,sy*cp,0,0,cp,sp,0,-sy,-cy*sp,cy*cp,0,
        x*cy-z*sy,y*cp-(x*sy+z*cy)*sp,y*sp+(x*sy+z*cy)*cp,1};
    for(int i=0;i<16;i++)view_matrix[i]=m[i];
}
static void local_view(float *out,const float *local) {
    for(int col=0;col<4;col++)for(int row=0;row<4;row++)
        out[col*4+row]=view_matrix[row]*local[col*4]+view_matrix[4+row]*local[col*4+1]+view_matrix[8+row]*local[col*4+2]+view_matrix[12+row]*local[col*4+3];
}
static void lerp(CameraVertex *c,const CameraVertex *a,const CameraVertex *b,float t) {
    float *cc=&c->x;const float *aa=&a->x,*bb=&b->x;
    for(int i=0;i<10;i++) cc[i]=aa[i]+(bb[i]-aa[i])*t;
    c->mask=0;
}
static DrawVertex project(const CameraVertex *v) {
    float q=1/v->z;
    return (DrawVertex){256+v->x*332.553f*q,224-v->y*387.98f*q,v->z,v->u,v->v,
        (uint8_t)(int)clampf(v->r*draw_color[0]*128,0,255),(uint8_t)(int)clampf(v->g*draw_color[1]*128,0,255),
        (uint8_t)(int)clampf(v->b*draw_color[2]*128,0,255),(uint8_t)(int)(draw_alpha*v->alpha),q,(unsigned)(int)(clampf(v->f,0,1)*255)<<24};
}
static CameraVertex *project_base;static unsigned project_count;
static int lazy_lighting;static unsigned shade_offset;static float shade_tint[3],shade_material[3];
static DrawVertex *project_cache;static unsigned project_capacity;
static void start_projection(CameraVertex *base,unsigned count) {
    project_base=NULL;
    if(count>project_capacity){void *next=realloc(project_cache,count*sizeof(*project_cache));if(!next)return;project_cache=next;project_capacity=count;}
    project_base=base;project_count=count;platform_cache_reset();
    for(unsigned i=0;i<count;i++)base[i].mask&=~32u;
}
static inline void shade_cached(const CameraVertex *vertex) {
    if(!lazy_lighting||!project_base||(vertex->mask&64))return;
    unsigned index=(vertex->mask>>8)-1;
    CameraVertex *v=project_base+index;float light=0,distance2=v->r;
    if(game->flashlight&&distance2<400) {
        float distance=sqrtf(distance2);
        light=clampf((v->z/fmaxf(distance,.001f)-.9063078f)*18,0,1)*clampf(1-distance/20,0,1)*2.5f;
    }
    unsigned fi=(int)(distance2*.125f);float visible=fog_table[fi<4096?fi:4096],limit=game->time_of_day==2?1.7f:1.99f;
    const float *base=vertex_light[shade_offset+index];
    v->r=clampf(base[0]*shade_tint[0]+shade_material[0]*light,0,limit);
    v->g=clampf(base[1]*shade_tint[1]+shade_material[1]*light,0,limit);
    v->b=clampf(base[2]*shade_tint[2]+shade_material[2]*light,0,limit);v->f=visible;
    v->mask|=64;
}
static inline DrawVertex project_cached(const CameraVertex *v) {
    unsigned key=v->mask>>8;
    if(project_base&&key&&key<=project_count) {
        unsigned index=key-1;
        if(!(v->mask&32)){project_cache[index]=project(v);project_cache[index].cache_key|=index+1;project_base[index].mask|=32;}
        return project_cache[index];
    }
    return project(v);
}
typedef union {struct{float x,y,z,u,v,r,g,b,f,alpha;};float values[12];} ClipVertex __attribute__((aligned(16)));
static float clip_plane(const ClipVertex *v,int side) {
    switch(side){case 0:return v->z-.15f;case 1:return v->x+v->z*.7698f;case 2:return v->z*.7698f-v->x;
        case 3:return v->y+v->z*.57735f;default:return v->z*.57735f-v->y;}
}
static void clip_lerp(ClipVertex *out,const ClipVertex *a,const ClipVertex *b,float t) {
#ifdef _EE
    const float ratio[4] __attribute__((aligned(16)))={t,t,t,t};
    __asm__ volatile(
        "lqc2 $vf13,0(%3)\n lqc2 $vf14,0(%1)\n lqc2 $vf15,0(%2)\n"
        "vsub $vf15,$vf15,$vf14\n vmulx $vf15,$vf15,$vf13\n vadd $vf14,$vf14,$vf15\n sqc2 $vf14,0(%0)\n"
        "lqc2 $vf14,16(%1)\n lqc2 $vf15,16(%2)\n"
        "vsub $vf15,$vf15,$vf14\n vmulx $vf15,$vf15,$vf13\n vadd $vf14,$vf14,$vf15\n sqc2 $vf14,16(%0)\n"
        ::"r"(out),"r"(a),"r"(b),"r"(ratio):"memory");
#else
    for(int i=0;i<8;i++)out->values[i]=a->values[i]+(b->values[i]-a->values[i])*t;
#endif
    out->f=a->f+(b->f-a->f)*t;out->alpha=a->alpha+(b->alpha-a->alpha)*t;
}
static void clipped_triangle(int tex,const CameraVertex *a,const CameraVertex *b,const CameraVertex *c,unsigned outside) {
    ClipVertex buffers[2][12];ClipVertex *p=buffers[0],*temp=buffers[1];int n=3;
    memcpy(p,a,40);memcpy(p+1,b,40);memcpy(p+2,c,40);
    for(int side=0;side<5;side++) {
        if(!(outside&(1u<<side)))continue;
        int next=0;
        for(int i=0;i<n;i++) {
            const ClipVertex *va=p+i,*vb=p+(i+1)%n;float da=clip_plane(va,side),db=clip_plane(vb,side);
            if(da>=0) temp[next++]=*va;
            if((da>=0)!=(db>=0))clip_lerp(temp+next++,va,vb,da/(da-db));
        }
        n=next;if(n<3)return;ClipVertex *swap=p;p=temp;temp=swap;
    }
    DrawVertex projected[12];
    for(int i=0;i<n;i++){CameraVertex vertex;memcpy(&vertex,p+i,40);projected[i]=project(&vertex);}
    for(int i=1;i<n-1;i++) { DrawVertex v[3]={projected[0],projected[i],projected[i+1]};platform_triangle(tex,v);triangles++; }
}
static inline void triangle_vertices(int tex,const CameraVertex *a,const CameraVertex *b,const CameraVertex *c) {
    if(triangles>=TRIANGLE_BUDGET) return;
    unsigned ca=a->mask&31,cb=b->mask&31,cc=c->mask&31;if(ca&cb&cc)return;
    if(cull_faces&&a->x*(b->y*c->z-b->z*c->y)-a->y*(b->x*c->z-b->z*c->x)+a->z*(b->x*c->y-b->y*c->x)>=0)return;
    shade_cached(a);shade_cached(b);shade_cached(c);
    if(ca|cb|cc){clipped_triangle(tex,a,b,c,ca|cb|cc);return;}
    DrawVertex v[3]={project_cached(a),project_cached(b),project_cached(c)};platform_triangle(tex,v);triangles++;
}
#define triangle(texture,a,b,c) triangle_vertices((texture),&(a),&(b),&(c))
static CameraVertex floor_light[2048];static unsigned floor_light_count;
static float light_plane(const CameraVertex *v,int side) {
    switch(side){case 0:return v->z-.15f;case 1:return 20-v->z;case 2:return v->x+v->z*.4663077f;
        case 3:return v->z*.4663077f-v->x;case 4:return v->y+v->z*.4663077f;default:return v->z*.4663077f-v->y;}
}
static void collect_floor_light(const CameraVertex *a,const CameraVertex *b,const CameraVertex *c) {
    unsigned outside=0;
    for(int side=0;side<6;side++) {
        float da=light_plane(a,side),db=light_plane(b,side),dc=light_plane(c,side);
        if(da<0&&db<0&&dc<0)return;
        if(da<0||db<0||dc<0)outside|=1u<<side;
    }
    CameraVertex buffers[2][12];CameraVertex *p=buffers[0],*temp=buffers[1];int n=3;
    p[0]=*a;p[1]=*b;p[2]=*c;
    for(int side=0;side<6;side++)if(outside&(1u<<side)) {
        int next=0;
        for(int i=0;i<n;i++) {
            const CameraVertex *va=p+i,*vb=p+(i+1)%n;float da=light_plane(va,side),db=light_plane(vb,side);
            if(da>=0)temp[next++]=*va;
            if((da>=0)!=(db>=0))lerp(temp+next++,va,vb,da/(da-db));
        }
        n=next;if(n<3)return;CameraVertex *swap=p;p=temp;temp=swap;
    }
    for(int i=0;i<n;i++) {
        p[i].u=.5f+p[i].x/(p[i].z*.9326154f);p[i].v=.5f-p[i].y/(p[i].z*.9326154f);
        float fade=clampf((20-p[i].z)/5,0,1)*.65f;
        p[i].r=scene_flashlight_color[0]*fade;p[i].g=scene_flashlight_color[1]*fade;p[i].b=scene_flashlight_color[2]*fade;
        prepare_vertex(p+i);
    }
    for(int i=1;i<n-1&&floor_light_count+3<=2048;i++) {
        floor_light[floor_light_count++]=p[0];floor_light[floor_light_count++]=p[i];floor_light[floor_light_count++]=p[i+1];
    }
}
static void terrain_layers(CameraVertex v[4],int x,int z,int step) {
    float cumulative[4]={0};int first=1,width=world->header->width-1;
    for(int ground=0;ground<4;ground++) {
        float weights[4],maximum=0;int ox[4]={0,step,step,0},oz[4]={0,0,step,step};
        for(int i=0;i<4;i++) {
            int ix=x+ox[i],iz=z+oz[i];if(ix>=width)ix=width-1;if(iz>=width)iz=width-1;
            weights[i]=world->splat[(iz*width+ix)*4+ground];maximum=fmaxf(maximum,weights[i]);
        }
        if(maximum==0)continue;
        float repeat=win_scene?5:ground==0?3:ground==1?5:ground==2?4:7;
        float uv[4][2];terrain_uv_quad(uv,x,z,step,world->config[C_SCALE],world->config[C_SCALE+2],repeat);
        if(!first)platform_blend(4);
        for(int i=0;i<4;i++) {
            cumulative[i]+=weights[i];v[i].alpha=first?1:(cumulative[i]>0?weights[i]/cumulative[i]:0);
            v[i].u=uv[i][0];v[i].v=uv[i][1];
        }
        int texture=(int)world->config[C_GROUND+ground];triangle(texture,v[0],v[2],v[1]);triangle(texture,v[0],v[3],v[2]);
        if(!first)platform_blend(0);
        first=0;
    }
    for(int i=0;i<4;i++)v[i].alpha=1;
}
static CameraVertex terrain_interpolate(const CameraVertex v[4],int u,int z,int step) {
    CameraVertex out;const CameraVertex *a,*b,*c;float wa,wb,wc;
    if(u>=z){a=v;b=v+1;c=v+2;wa=1-u/(float)step;wb=(u-z)/(float)step;wc=z/(float)step;}
    else {a=v;b=v+2;c=v+3;wa=1-z/(float)step;wb=u/(float)step;wc=(z-u)/(float)step;}
    for(int k=0;k<10;k++)(&out.x)[k]=(&a->x)[k]*wa+(&b->x)[k]*wb+(&c->x)[k]*wc;
    prepare_vertex(&out);return out;
}
/* Fully fogged tiles need one opaque pass. Retain all fine edge samples so
 * their borders meet the original near terrain without cracks or skirts. */
static void distant_terrain(uint8_t distant[1024],int step) {
    memset(distant,0,1024);
    if(menu_scene||win_scene||density<=0||world->header->width!=513||world->header->height!=513)return;
    const int tile=16;float sx=world->config[C_SCALE],sz=world->config[C_SCALE+2],limit=2.4f/density;
    if(limit>=far_distance)return;
    float white[3]={1,1,1};int texture=(int)world->config[C_GROUND];
    int cx=(int)(game->feet.x/(sx*tile)),cz=(int)(game->feet.z/(sz*tile));
    int radius=(int)(far_distance/(sx*tile))+2;
    for(int z=cz-radius;z<=cz+radius;z++)for(int x=cx-radius;x<=cx+radius;x++) {
        if(x<0||z<0||x>=32||z>=32)continue;
        float dx=fmaxf(fmaxf(x*tile*sx-game->feet.x,game->feet.x-(x+1)*tile*sx),0);
        float dz=fmaxf(fmaxf(z*tile*sz-game->feet.z,game->feet.z-(z+1)*tile*sz),0);
        float distance=dx*dx+dz*dz;if(distance<limit*limit)continue;
        distant[z*32+x]=1;
        if(distance>far_distance*far_distance)continue;
        float forward=((x+.5f)*tile*sx-game->feet.x)*sy+((z+.5f)*tile*sz-game->feet.z)*cy;
        if(forward<-(sx+sz)*tile*.5f)continue;
        CameraVertex edge[32],centre;int n=0,gridx=x*tile,gridz=z*tile;
        for(int side=0;side<4;side++)for(int d=0;d<tile;d+=step) {
            int ix=gridx+(side==0?d:side==1?tile:side==2?tile-d:0);
            int iz=gridz+(side==0?0:side==1?d:side==2?tile:tile-d);
            Vec3 p={ix*sx,world->height[iz*world->header->width+ix]*world->config[C_SCALE+1],iz*sz};
            float rx=p.x-game->feet.x,ry=p.y-game->feet.y-game->eye,rz=p.z-game->feet.z,vz=rx*sy+rz*cy;
            edge[n]=(CameraVertex){.x=rx*cy-rz*sy,.y=ry*cp-vz*sp,.z=ry*sp+vz*cp,.r=1,.g=1,.b=1,.alpha=1};prepare_vertex(edge+n++);
        }
        int ix=gridx+tile/2,iz=gridz+tile/2;
        centre=camera((Vec3){ix*sx,world->height[iz*world->header->width+ix]*world->config[C_SCALE+1],iz*sz},0,0,0,1,0,white);centre.f=0;
        for(int i=0;i<n;i++)triangle(texture,centre,edge[(i+1)%n],edge[i]);
    }
}
static void terrain(void) {
    cull_faces=1;
    static CameraVertex cache[96*96];static unsigned stamps[96*96],serial;
    if(++serial==0){for(int i=0;i<96*96;i++)stamps[i]=0;serial=1;}
    int step=terrain_step(game,menu_scene,win_scene);float sx=world->config[C_SCALE],sz=world->config[C_SCALE+2];
    static uint8_t distant[1024];distant_terrain(distant,step);
    int radius=(int)(far_distance/(sx*step))+1,tx=(int)(game->feet.x/sx)/step,tz=(int)(game->feet.z/sz)/step;
    float white[3]={1,1,1};
    floor_light_count=0;int light=!menu_scene&&game->flashlight;
    static uint8_t paint_detail[256*256];static const World *paint_world;static int paint_step;
    if(paint_world!=world||paint_step!=step){memset(paint_detail,0,sizeof(paint_detail));paint_world=world;paint_step=step;}
    for(int z=(tz-radius)*step;z<(tz+radius)*step;z+=step) for(int x=(tx-radius)*step;x<(tx+radius)*step;x+=step) {
        if(x<0||z<0||x+step>=(int)world->header->width||z+step>=(int)world->header->height) continue;
        if(!menu_scene&&!win_scene&&world->header->width==513&&distant[(z/16)*32+x/16])continue;
        float dx=x*sx-game->feet.x,dz=z*sz-game->feet.z;
        if(dx*dx+dz*dz>far_distance*far_distance) continue;
        if(dx*sy+dz*cy<-(float)step*sx*2) continue;
        CameraVertex v[4];int ox[4]={0,step,step,0},oz[4]={0,0,step,step};
        float heights[4],minheight=1e9f,maxheight=-1e9f;
        for(int i=0;i<4;i++){heights[i]=world->height[(z+oz[i])*world->header->width+x+ox[i]]*world->config[C_SCALE+1];minheight=fminf(minheight,heights[i]);maxheight=fmaxf(maxheight,heights[i]);}
        if(!menu_scene) {
            float side=(fabsf(cy)+fabsf(sy))*step*sx;
            float maximum_z=((sp>=0?maxheight:minheight)-game->feet.y-game->eye)*sp+(dx*sy+dz*cy+side)*cp;
            if(fabsf(dx*cy-dz*sy)-side>maximum_z*.7698f)continue;
        }
        int layer=world->header->version==1?world->splat[z*(world->header->width-1)+x]:0;if(layer>3) layer=0;
        float repeat=win_scene?5:layer==0?3:layer==1?5:layer==2?4:7;
        float uv[4][2]={{0}};
        if(world->header->version==1)terrain_uv_quad(uv,x,z,step,sx,sz,repeat);
        for(int i=0;i<4;i++) {
            int ix=x+ox[i],iz=z+oz[i];Vec3 p={ix*sx,heights[i],iz*sz};
            float nx=0,ny=1,nz=0;
            if(win_scene) {
                nx=(world_height(world,p.x-sx,p.z)-world_height(world,p.x+sx,p.z))/(2*sx);
                nz=(world_height(world,p.x,p.z-sz)-world_height(world,p.x,p.z+sz))/(2*sz);
                float norm=sqrtf(nx*nx+1+nz*nz);nx/=norm;ny/=norm;nz/=norm;
            }
            int slot=((iz-(tz-radius)*step)/step)*96+(ix-(tx-radius)*step)/step;
            if(slot>=0&&slot<96*96) {
                if(stamps[slot]!=serial){cache[slot]=camera(p,0,0,nx,ny,nz,white);stamps[slot]=serial;}
                v[i]=cache[slot];v[i].u=uv[i][0];v[i].v=uv[i][1];
            }else v[i]=camera(p,uv[i][0],uv[i][1],nx,ny,nz,white);
        }
        if(world->header->version==1) {
            int texture=(int)world->config[C_GROUND+layer];triangle(texture,v[0],v[2],v[1]);triangle(texture,v[0],v[3],v[2]);
        }else {
            unsigned detail=16;
            if(!menu_scene&&!win_scene&&dx*dx+dz*dz<48*48&&step>=2&&step<=4) {
                int slot=(z/step)*256+x/step;
                if(!paint_detail[slot]) {
                    unsigned code=16;
                    if(terrain_paint_step(world,x,z,step)<step) {
                        code=32;
                        if(step==2)code|=1;
                        else for(int q=0;q<4;q++)if(terrain_paint_step(world,x+(q&1)*2,z+(q>>1)*2,2)==1)code|=1<<q;
                    }
                    paint_detail[slot]=code;
                }
                detail=paint_detail[slot];
            }
            if(detail==16)terrain_layers(v,x,z,step);
            else {
                CameraVertex nodes[25];int stride=detail&15?1:2;
                for(int zz=0;zz<=step;zz+=stride)for(int xx=0;xx<=step;xx+=stride)nodes[zz*5+xx]=terrain_interpolate(v,xx,zz,step);
                for(int zz=0;zz<step;zz+=2)for(int xx=0;xx<step;xx+=2) {
                    int unit=detail&(1<<((zz/2)*2+xx/2)),sub=unit?1:2;
                    for(int vz=zz;vz<zz+2;vz+=sub)for(int ux=xx;ux<xx+2;ux+=sub) {
                        CameraVertex fine[4]={nodes[vz*5+ux],nodes[vz*5+ux+sub],nodes[(vz+sub)*5+ux+sub],nodes[(vz+sub)*5+ux]};
                        terrain_layers(fine,x+ux,z+vz,sub);
                    }
                }
            }
        }
        if(light&&dx*dx+dz*dz<32*32){collect_floor_light(v,v+2,v+1);collect_floor_light(v,v+3,v+2);}
    }
}
static void skybox(void) {
    cull_faces=0;
    static const float corners[6][4][3]={
        {{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},
        {{1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1}},
        {{1,-1,1},{1,-1,-1},{1,1,-1},{1,1,1}},
        {{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}},
        {{-1,1,1},{1,1,1},{1,1,-1},{-1,1,-1}},
        {{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}}};
    float white[3]={1,1,1};
    for(int face=0;face<6;face++) {
        CameraVertex v[4];
        for(int j=0;j<4;j++) {
            Vec3 p={game->feet.x+corners[face][j][0]*10000,game->feet.y+game->eye+corners[face][j][1]*10000,game->feet.z+corners[face][j][2]*10000};
            v[j]=camera(p,j==1||j==2?1:0,j>=2?0:1,0,1,0,white);v[j].r=v[j].g=v[j].b=1;v[j].f=1;prepare_vertex(v+j);
        }
        int texture=win_scene?victory_sky[face]:scene_sky[game->time_of_day][face];
        triangle(texture,v[0],v[1],v[2]);triangle(texture,v[0],v[2],v[3]);
    }
}
typedef struct {unsigned index;float center[3],radius;} DrawMesh;
static DrawMesh *draw_meshes;static size_t draw_capacity;static unsigned draw_count;
static const World *draw_world;
static int prepare_mesh_bounds(void) {
    if(draw_world==world)return 1;
    if(world->header->meshes>draw_capacity) {
        void *next=realloc(draw_meshes,world->header->meshes*sizeof(*draw_meshes));
        if(!next)return 0;
        draw_meshes=next;draw_capacity=world->header->meshes;
    }
    draw_count=0;
    for(unsigned i=0;i<world->header->meshes;i++) {
        const Mesh *m=world->mesh+i;if(m->flags&(M_TREE|M_VIEW))continue;
        DrawMesh *d=draw_meshes+draw_count++;d->index=i;
        for(int k=0;k<3;k++)d->center[k]=(m->min[k]+m->max[k])*.5f;
        float x=m->max[0]-m->min[0],y=m->max[1]-m->min[1],z=m->max[2]-m->min[2];
        d->radius=sqrtf(x*x+z*z+((m->flags&(M_ENEMY|M_HOOVER))?y*y:0))*.5f;
    }
    draw_world=world;return 1;
}
static void meshes(void) {
    static CameraVertex *cache;static size_t capacity;
    if(!prepare_mesh_bounds())return;
    float enemy_pose[16] __attribute__((aligned(16))),hoover_pose[16] __attribute__((aligned(16)));
    float enemy_local[16] __attribute__((aligned(16))),hoover_local[16] __attribute__((aligned(16)));
    if(!menu_scene) {
        game_enemy_matrix(world,game,enemy_local);local_view(enemy_pose,enemy_local);
        game_hoover_matrix(world,game,hoover_local);local_view(hoover_pose,hoover_local);
    }
    uint32_t cached_offset=UINT32_MAX,cached_actor=UINT32_MAX;float white[3]={1,1,1},cached_color[3]={-1,-1,-1};
    for(unsigned entry=0;entry<draw_count;entry++) {
        const DrawMesh *bounds=draw_meshes+entry;unsigned i=bounds->index;
        const Mesh *m=world->mesh+i;
        if((m->flags&M_ENEMY)&&(game->state==ST_DEAD||game->state==ST_WON))continue;
        if((m->flags&M_DAY_TREE)&&game->time_of_day==2)continue;
        if((m->flags&M_COLLECT)&&(game->custards&(1<<m->id))) continue;
        float x=bounds->center[0],z=bounds->center[2],radius=bounds->radius;
        if(!menu_scene&&(m->flags&(M_ENEMY|M_HOOVER))) {
            const float *pose=m->flags&M_ENEMY?enemy_local:hoover_local;
            x=pose[0]*bounds->center[0]+pose[4]*bounds->center[1]+pose[8]*bounds->center[2]+pose[12];
            z=pose[2]*bounds->center[0]+pose[6]*bounds->center[1]+pose[10]*bounds->center[2]+pose[14];
        }
        float dx=x-game->feet.x,dz=z-game->feet.z;
        float mesh_far=menu_scene||density<=0?far_distance:fminf(far_distance,2.3f/density);
        if(dx*dx+dz*dz>(mesh_far+radius)*(mesh_far+radius)||dx*sy+dz*cy < -radius-2) continue;
        if(!menu_scene&&fabsf(dx*cy-dz*sy)>(dx*sy+dz*cy)*.77f+radius*1.27f) continue;
        if(!menu_scene&&!(m->flags&(M_ENEMY|M_HOOVER|M_DOOR))&&!tree_in_view(&(Tree){0,0,0,1,1,0},(float[6]){m->min[0],m->min[1],m->min[2],m->max[0],m->max[1],m->max[2]}))continue;
        if(m->vertices>capacity) {CameraVertex *next=realloc(cache,m->vertices*sizeof(*cache));if(!next) return;cache=next;capacity=m->vertices;}
        const Vertex *vertices=(const Vertex *)(world->blob+m->vertex_offset);
        const uint16_t *indices=(const uint16_t *)(world->blob+m->index_offset);
        const float *material=menu_scene?white:m->color;
        unsigned actor=(m->flags&(M_ENEMY|M_HOOVER|M_DOOR))|(m->id<<16);
        if(cached_offset!=m->vertex_offset||cached_actor!=actor||cached_color[0]!=material[0]||cached_color[1]!=material[1]||cached_color[2]!=material[2]) {
            platform_work(0,1);
            float local[16] __attribute__((aligned(16)))={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
            float matrix[16] __attribute__((aligned(16)));const float *pose=view_matrix;
            if(!menu_scene&&(m->flags&(M_ENEMY|M_HOOVER|M_DOOR))) {
                if(m->flags&M_ENEMY)pose=enemy_pose;
                else if(m->flags&M_HOOVER)pose=hoover_pose;
                else {
                    const float *d=world->config+101+m->id*12;
                    for(int col=0;col<4;col++)for(int row=0;row<3;row++)local[col*4+row]+=(d[row*4+col]-local[col*4+row])*game->door_open;
                    local_view(matrix,local);pose=matrix;
                }
            }
            if(!menu_scene)camera_vertices(cache,vertices,m->vertices,pose);
            else for(uint32_t j=0;j<m->vertices;j++) {
                const Vertex *v=vertices+j;Vec3 p={v->x,v->y,v->z};
                cache[j]=camera(p,v->u,v->v,v->nx,v->ny,v->nz,material);
                cache[j].mask|=(j+1)<<8;
            }
            cached_offset=m->vertex_offset;cached_actor=actor;
            for(int j=0;j<3;j++)cached_color[j]=material[j];
            platform_work(0,0);
        }
        for(int j=0;j<3;j++) draw_color[j]=menu_scene?m->color[j]:1;
        cull_faces=(m->flags&M_CULL)!=0;
        platform_cutout((m->flags&M_CUTOUT)!=0);
        lazy_lighting=!menu_scene;shade_offset=light_offsets?light_offsets[i]:0;
        for(int k=0;k<3;k++){shade_tint[k]=1;shade_material[k]=material[k];}
        start_projection(cache,m->vertices);platform_work(1,1);
        for(uint32_t j=0;j<m->triangles;j++,indices+=3) triangle(m->texture,cache[indices[0]],cache[indices[1]],cache[indices[2]]);
        platform_work(1,0);project_base=NULL;lazy_lighting=0;
    }
    platform_cutout(0);
    for(int j=0;j<3;j++) draw_color[j]=1;
}
typedef struct {unsigned index;float distance;} VisibleTree;
static int nearer_tree(const void *a,const void *b) {
    float delta=((const VisibleTree *)a)->distance-((const VisibleTree *)b)->distance;
    return delta<0?-1:delta>0;
}
static int tree_in_view(const Tree *t,const float bounds[6]) {
    float hx=(bounds[3]-bounds[0])*t->width*.5f,hy=(bounds[4]-bounds[1])*t->height*.5f,hz=(bounds[5]-bounds[2])*t->width*.5f;
    float dx=t->x+(bounds[3]+bounds[0])*t->width*.5f-game->feet.x;
    float dy=t->y+(bounds[4]+bounds[1])*t->height*.5f-game->feet.y-game->eye;
    float dz=t->z+(bounds[5]+bounds[2])*t->width*.5f-game->feet.z;
    float x=dx*cy-dz*sy,y=dy*cp-(dx*sy+dz*cy)*sp,z=dy*sp+(dx*sy+dz*cy)*cp;
    if(z-.15f+fabsf(sy*cp)*hx+fabsf(sp)*hy+fabsf(cy*cp)*hz<0)return 0;
    for(int sign=-1;sign<=1;sign+=2) {
        float radius=fabsf(sign*cy+.7698f*sy*cp)*hx+fabsf(.7698f*sp)*hy+fabsf(-sign*sy+.7698f*cy*cp)*hz;
        if(sign*x+.7698f*z+radius<0)return 0;
        radius=fabsf(-sign*sy*sp+.57735f*sy*cp)*hx+fabsf(sign*cp+.57735f*sp)*hy+fabsf(-sign*cy*sp+.57735f*cy*cp)*hz;
        if(sign*y+.57735f*z+radius<0)return 0;
    }
    return 1;
}
static void trees(void) {
    static VisibleTree *visible;static size_t visible_capacity;
    static CameraVertex *cache;static size_t capacity;
    if(world->header->trees>visible_capacity) {
        VisibleTree *next=realloc(visible,world->header->trees*sizeof(*next));if(!next)return;
        visible=next;visible_capacity=world->header->trees;
    }
    unsigned count=0,tree_work=0;
    // Reserve CPU work for the original enemy mesh during an encounter.
    // The closest trees keep priority; remaining trees retain leaf-card LODs.
    float enemy_dx=game->enemy.x-game->feet.x,enemy_dz=game->enemy.z-game->feet.z;
    float enemy_d2=enemy_dx*enemy_dx+enemy_dz*enemy_dz,enemy_dy=game->enemy.y+1-game->feet.y-game->eye;
    float forward=enemy_dx*sy+enemy_dz*cy,enemy_z=enemy_dy*sp+forward*cp,enemy_y=enemy_dy*cp-forward*sp;
    // Match rendering, rather than AI line-of-sight: an enemy behind a trunk
    // still submits its mesh to the GS and consumes the same CPU time.
    int enemy_on_screen=enemy_d2<900&&enemy_z>-2&&fabsf(enemy_dx*cy-enemy_dz*sy)<enemy_z*.77f+2.5f&&fabsf(enemy_y)<enemy_z*.58f+2.5f;
    unsigned tree_budget=win_scene?7000:enemy_on_screen?(enemy_d2<36?1200:2200):4000;
    // Beyond this distance the source exponential fog hides the geometry.
    float limit=fminf(far_distance,density>0?2.3f/density:far_distance);
    for(uint32_t i=0;i<world->header->trees;i++) {
        const Tree *t=world->tree+i;float dx=t->x-game->feet.x,dz=t->z-game->feet.z;
        float dist=dx*dx+dz*dz;
        if(dist>(limit+12)*(limit+12)||dx*sy+dz*cy<-12)continue;
        if(fabsf(dx*cy-dz*sy)>(dx*sy+dz*cy)*.77f+16)continue;
        if((unsigned)t->type>=3||!tree_in_view(t,tree_bounds[(unsigned)t->type]))continue;
        visible[count++]=(VisibleTree){i,dist};
    }
    qsort(visible,count,sizeof(*visible),nearer_tree);
    for(unsigned i=0;i<count;i++) {
        const Tree *t=world->tree+visible[i].index;uint32_t cached_offset=UINT32_MAX;float cached_color[3]={-1,-1,-1};
        float matrix[16] __attribute__((aligned(16))),local[16] __attribute__((aligned(16)))={t->width,0,0,0,0,t->height,0,0,0,0,t->width,0,t->x,t->y,t->z,1};
        local_view(matrix,local);
        // Keep the original close mesh; reduce leaf groups sooner on the PS2.
        float near=win_scene?20:8,medium=win_scene?60:20;
        unsigned kind=(unsigned)t->type;
        float center_y=t->y+(tree_bounds[kind][1]+tree_bounds[kind][4])*.5f*t->height-game->feet.y-game->eye;
        float distance=visible[i].distance+(win_scene?0:center_y*center_y);
        unsigned prototype=kind+(distance>medium*medium?6:distance>near*near?3:0);
        if(distance>(win_scene?110*110:30*30))prototype=kind+9;
        while(prototype<9&&tree_work+tree_cost[prototype]>tree_budget)prototype+=3;
        if(!win_scene&&prototype>=9&&(enemy_on_screen||tree_work+tree_cost[prototype]>tree_budget))prototype=kind+12;
        tree_work+=tree_cost[prototype];
        // Preserve complete trees when the CPU's frame budget is reached.
        if(triangles+tree_cost[prototype]*2>TRIANGLE_BUDGET-(win_scene?4800:0))continue;
        for(unsigned part=0;part<tree_count[prototype];part++) {
            unsigned j=tree_order[tree_first[prototype]+part];const Mesh *m=world->mesh+j;
            if(!tree_in_view(t,(float[6]){m->min[0],m->min[1],m->min[2],m->max[0],m->max[1],m->max[2]}))continue;
            if(m->vertices>capacity) {
                CameraVertex *next=realloc(cache,m->vertices*sizeof(*next));if(!next)return;cache=next;capacity=m->vertices;
            }
            unsigned tint=active_tree_colors[visible[i].index];float material[3];
            for(int k=0;k<3;k++)material[k]=m->color[k]*((tint>>(k*8))&255)/255.f;
            if(cached_offset!=m->vertex_offset||cached_color[0]!=material[0]||cached_color[1]!=material[1]||cached_color[2]!=material[2]) {
                platform_work(0,1);
                const Vertex *vertices=(const Vertex *)(world->blob+m->vertex_offset);
                camera_vertices(cache,vertices,m->vertices,matrix);
                cached_offset=m->vertex_offset;
                for(int k=0;k<3;k++)cached_color[k]=material[k];
                platform_work(0,0);
            }
            for(int k=0;k<3;k++)draw_color[k]=1;
            cull_faces=(m->flags&M_CULL)!=0;
            platform_cutout((m->flags&M_CUTOUT)!=0);
            const uint16_t *indices=(const uint16_t *)(world->blob+m->index_offset);
            lazy_lighting=1;shade_offset=light_offsets[j];
            for(int k=0;k<3;k++){shade_tint[k]=((tint>>(k*8))&255)/255.f;shade_material[k]=material[k];}
            start_projection(cache,m->vertices);platform_work(1,1);
            for(uint32_t k=0;k<m->triangles;k++,indices+=3)triangle(m->texture,cache[indices[0]],cache[indices[1]],cache[indices[2]]);
            platform_work(1,0);project_base=NULL;lazy_lighting=0;
        }
    }
    platform_cutout(0);
    for(int j=0;j<3;j++)draw_color[j]=1;
}
static void first_person(void) {
    static CameraVertex *cache;static unsigned capacity;
    float phase=fmodf(game->time/view_walk_duration,1)*VIEW_WALK_FRAMES;
    unsigned frame=(unsigned)phase;float fraction=phase-frame;
    // The original arm and flashlight are stored relative to the player camera.
    // Render with world depth so the hand remains occluded correctly by geometry.
    for(uint32_t i=0;i<world->header->meshes;i++) {
        const Mesh *m=world->mesh+i;if(!(m->flags&M_VIEW))continue;
        unsigned outside=31;
        for(int corner=0;corner<8;corner++) {
            float x=corner&1?m->max[0]:m->min[0],y=corner&2?m->max[1]:m->min[1],z=corner&4?m->max[2]:m->min[2];
            CameraVertex bound={.x=x,.y=y*cp-z*sp,.z=y*sp+z*cp};prepare_vertex(&bound);outside&=bound.mask;
        }
        if(outside)continue;
        if(m->vertices>capacity){CameraVertex *next=realloc(cache,m->vertices*sizeof(*next));if(!next)return;cache=next;capacity=m->vertices;}
        const Vertex *vertices=(const Vertex *)(world->blob+m->vertex_offset);
        const uint16_t *indices=(const uint16_t *)(world->blob+m->index_offset);
        for(int k=0;k<3;k++)draw_color[k]=m->color[k];
        cull_faces=(m->flags&M_CULL)!=0;
        for(unsigned j=0;j<m->vertices;j++) {
                Vertex posed=vertices[j];
                if(walk_frames&&m->id<sizeof(view_walk_offsets)/sizeof(view_walk_offsets[0])) {
                    unsigned offset=view_walk_offsets[m->id]+j;
                    const float *a=walk_frames+(frame*VIEW_WALK_VERTICES+offset)*6;
                    const float *b=walk_frames+(((frame+1)%VIEW_WALK_FRAMES)*VIEW_WALK_VERTICES+offset)*6;
                    posed.x=a[0]+(b[0]-a[0])*fraction;posed.y=a[1]+(b[1]-a[1])*fraction;posed.z=a[2]+(b[2]-a[2])*fraction;
                    posed.nx=a[3]+(b[3]-a[3])*fraction;posed.ny=a[4]+(b[4]-a[4])*fraction;posed.nz=a[5]+(b[5]-a[5])*fraction;
                }
                const Vertex *v=&posed;float diffuse=clampf(.65f+v->ny*.3f-v->nz*.2f,.3f,1);
                float brightness=view_walk_parts[m->id]?(game->time_of_day==0?1.35f:game->flashlight?1.0f:.25f):game->flashlight?.65f:.25f;
                cache[j]=(CameraVertex){v->x,v->y*cp-v->z*sp,v->y*sp+v->z*cp,v->u,v->v,
                                     brightness*diffuse,brightness*diffuse,brightness*diffuse,1,1,0};
                prepare_vertex(cache+j);
        }
        for(unsigned j=0;j<m->triangles;j++,indices+=3) {
            if(view_walk_parts[m->id]!=2){triangle(m->texture,cache[indices[0]],cache[indices[1]],cache[indices[2]]);continue;}
            // Hide the upper torso in first person while retaining the original hips and legs.
            CameraVertex polygon[4];int count=0;
            for(int edge=0;edge<3;edge++) {
                const CameraVertex *a=cache+indices[edge],*b=cache+indices[(edge+1)%3];
                float da=-.98f-(a->y*cp+a->z*sp),db=-.98f-(b->y*cp+b->z*sp);
                if(da>=0)polygon[count++]=*a;
                if((da>=0)!=(db>=0)){lerp(polygon+count,a,b,da/(da-db));prepare_vertex(polygon+count++);}
            }
            for(int edge=1;edge<count-1;edge++)triangle(m->texture,polygon[0],polygon[edge],polygon[edge+1]);
        }
    }
    for(int k=0;k<3;k++)draw_color[k]=1;
}
static void flashlight_cookie(void) {
    if(!floor_light_count)return;
    // Reuse the floor's exact triangles: no independent decal can cut through it.
    // The light shares the camera origin, so its cookie is linear in screen space.
    cull_faces=0;platform_blend(3);
    for(unsigned i=0;i<floor_light_count;i++)floor_light[i].f=1;
    for(unsigned i=0;i<floor_light_count;i+=3)triangle(scene_cookie,floor_light[i],floor_light[i+1],floor_light[i+2]);
    platform_blend(0);
}
static void grass(void) {
    if(world->header->version<2||!game->draw_grass)return;
    const uint16_t *patches=(const uint16_t *)(world->blob+(unsigned)world->config[127]);
    const uint8_t *samples=world->blob+(unsigned)world->config[128];
    int grid=world->header->version==3?(int)world->config[130]:1024;
    float scale=grid/((world->header->width-1)*world->config[C_SCALE]);
    float range=win_scene?world->config[131]:16;
    int rings=(int)(range*scale)+1,budget=win_scene?1200:320;
    int cx=(int)(game->feet.x*scale),cz=(int)(game->feet.z*scale),count=0;
    cull_faces=0;
    platform_cutout(1);int step=terrain_step(game,menu_scene,win_scene);
    // Instantiate only nearby source density cells; one shared grass texture.
    for(int ring=0;ring<rings;ring++)for(int dz=-ring;dz<=ring;dz++)for(int dx=-ring;dx<=ring;dx++) {
        if(abs(dx)!=ring&&abs(dz)!=ring)continue;
        int x=cx+dx,z=cz+dz;if(x<0||z<0||x>=grid||z>=grid)continue;
        unsigned patch=patches[(z/8)*(grid/8)+x/8];if(!patch)continue;
        unsigned density=samples[patch*64+(z%8)*8+x%8];if(!density)continue;
        unsigned seed=(unsigned)x*73856093u^(unsigned)z*19349663u;
        float px=(x+.15f+(seed&255)*.0027f)/scale,pz=(z+.15f+((seed>>8)&255)*.0027f)/scale;
        float rx=px-game->feet.x,rz=pz-game->feet.z,d=rx*rx+rz*rz;
        if(d>range*range||rx*sy+rz*cy<-1||fabsf(rx*cy-rz*sy)>(rx*sy+rz*cy)*.77f+2)continue;
        unsigned kind=(density&240)?1:0;const float *spec=active_grass[kind];
        float variation=(seed&255)/255.f,height=spec[2]+(spec[3]-spec[2])*variation;
        float half=(spec[0]+(spec[1]-spec[0])*variation)*.5f;
        float dry=.5f+.45f*sinf(px*.1f)*cosf(pz*.1f),color[3];
        for(int k=0;k<3;k++)color[k]=spec[4+k]*(1-dry)+spec[7+k]*dry;
        float bend=sinf(game->time*1.5f+(seed&31))*.06f;
        int billboard=active_grass_modes[kind]==0;
        float inverse_distance=billboard?1/sqrtf(fmaxf(d,.0001f)):0;
        for(int axis=0;axis<(billboard?1:2);axis++) {
            float ax=billboard?rz*inverse_distance*half:axis?0:half;
            float az=billboard?-rx*inverse_distance*half:axis?half:0;CameraVertex v[4];
            // Each bottom corner follows the visible floor, including slopes.
            float yl=terrain_surface_height(world,step,px-ax,pz-az)-.025f;
            float yr=terrain_surface_height(world,step,px+ax,pz+az)-.025f;
            Vec3 p[4]={{px-ax,yl,pz-az},{px+ax,yr,pz+az},{px+ax+bend,yr+height,pz+az},{px-ax+bend,yl+height,pz-az}};
            for(int i=0;i<4;i++){v[i]=camera(p[i],i==1||i==2?1:0,i<2?1:0,0,1,0,color);v[i].alpha=clampf((range*range-d)/(range*5),0,1);}
            int texture=(int)world->config[129];triangle(texture,v[0],v[1],v[2]);triangle(texture,v[0],v[2],v[3]);
        }
        if(++count>=budget){platform_cutout(0);return;}
    }
    platform_cutout(0);
}
static void image(int tex,float x,float y,float width,float height,uint32_t tint) {platform_sprite(tex,x,y,width,height,0,0,1,1,tint);}
static void text(float x,float y,const char *str,uint32_t color) {
    int tex=(int)world->config[C_FONT];float start=x;
    for(;*str;str++) {unsigned char c=*str;if(c=='\n') {y+=18;x=start;continue;}if(c>=32&&c<128) platform_sprite(tex,x,y,8,16,(c%16)/16.f,(c/16-2)/8.f,1/16.f,1/8.f,color);x+=8;}
}
static void small_text(float x,float y,const char *str,uint32_t color) {
    int tex=(int)world->config[C_FONT];
    for(;*str;str++,x+=4){unsigned c=(unsigned char)*str;if(c>=32&&c<128)platform_sprite(tex,x,y,4,8,(c%16)/16.f,(c/16-2)/8.f,1/16.f,1/8.f,color);}
}
static unsigned particle_random(unsigned value) {value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;return value^(value>>16);}
static float particle_unit(unsigned value) {return (particle_random(value)&65535)/65535.f;}
static void menu_particles(void) {
    cull_faces=0;
    for(unsigned i=0;i<sizeof(menu_emitters)/sizeof(menu_emitters[0]);i++) {
        const MenuEmitter *emitter=menu_emitters+i;platform_blend(emitter->additive?1:2);
        for(unsigned j=0;j<24;j++) {
            // Gameplay time stops on menu screens; their own clock keeps emitting.
            unsigned seed=i*101+j*17+2013;float age=fmodf(game->state_time+particle_unit(seed)*emitter->life,emitter->life);
            float phase=age/emitter->life*4;int key=(int)phase;if(key>3)key=3;float blend=phase-key;
            float color[4];for(int k=0;k<4;k++)color[k]=emitter->color[key][k]*(1-blend)+emitter->color[key+1][k]*blend;
            if(color[3]<.008f)continue;
            float size=fmaxf(.05f,emitter->size[0]+particle_unit(seed+1)*(emitter->size[1]-emitter->size[0])+emitter->grow*age)*.5f;
            Vec3 center={emitter->position[0]+(particle_unit(seed+2)*2-1)*emitter->spread[0]+emitter->velocity[0]*age+emitter->force[0]*age*age*.5f,
                         emitter->position[1]+emitter->velocity[1]*age+emitter->force[1]*age*age*.5f,
                         emitter->position[2]+(particle_unit(seed+3)*2-1)*emitter->spread[2]+emitter->velocity[2]*age+emitter->force[2]*age*age*.5f};
            CameraVertex vertices[4];float white[3]={1,1,1};
            for(int k=0;k<4;k++) {
                float x=(k==1||k==2?1:-1)*size,y=(k>=2?1:-1)*size;
                Vec3 p={center.x+menu_matrix[0]*x+menu_matrix[1]*y,center.y+menu_matrix[4]*x+menu_matrix[5]*y,center.z+menu_matrix[8]*x+menu_matrix[9]*y};
                vertices[k]=camera(p,k==1||k==2?1:0,k>=2?0:1,0,1,0,white);
                vertices[k].r=color[0]*2;vertices[k].g=color[1]*2;vertices[k].b=color[2]*2;
                vertices[k].f=1; // Preserve the particle shader's emission/opacity ramp.
            }
            draw_alpha=(unsigned)clampf(color[3]*128,0,128);
            triangle(emitter->texture,vertices[0],vertices[1],vertices[2]);triangle(emitter->texture,vertices[0],vertices[2],vertices[3]);
        }
    }
    draw_alpha=128;platform_blend(0);
}
static void original_menu(const Game *g,int connected) {
    const uint32_t white=0x80808080,gray=0x80606060;
    world=menu_world;int bank=g->state==ST_HELP?1:g->state==ST_CREDITS?2:0;
    platform_world(world,bank);platform_clear(0,0,0);
    if(bank) {
        image((int)world->config[bank==1?86:87],0,0,512,448,white);
        // Only the platform-specific return hint differs from the source image.
        image(0,328,0,184,22,0x80000000);text(336,3,"X / O: BACK",white);
        platform_finish();return;
    }
    menu_scene=1;menu_matrix=menu_camera[g->menu_page];
    Game view=*g;view.feet=(Vec3){menu_matrix[3],menu_matrix[7],menu_matrix[11]};
    view.eye=0;view.flashlight=0;view.time_of_day=2;
    view.yaw=atan2f(menu_matrix[2],menu_matrix[10]);view.pitch=asinf(menu_matrix[6]);game=&view;
    sy=sinf(view.yaw);cy=cosf(view.yaw);sp=sinf(view.pitch);cp=cosf(view.pitch);
    far_distance=140;density=world->config[C_ENV];
    for(int i=0;i<3;i++){fog[i]=world->config[C_ENV+1+i];ambient[i]=world->config[C_ENV+4+i];}
    if(density!=cached_density){for(int i=0;i<4097;i++)fog_table[i]=expf(-density*density*i*8);cached_density=density;}
    platform_fog((uint8_t)(fog[0]*255),(uint8_t)(fog[1]*255),(uint8_t)(fog[2]*255));
    if(platform_static_begin(g->menu_page)) {
        terrain();platform_profile(1);menu_baked=1;meshes();menu_baked=0;platform_static_end();
    }else platform_profile(1);
    platform_profile(2);menu_particles();platform_profile(3);
    image((int)world->config[C_TITLE],world->config[95]*512,world->config[96]*448,world->config[97]*512,world->config[98]*448,white);
    if(g->menu_page==MENU_SETTINGS) {
        const char *levels[]={"Fastest","Fast","Simple","Good","Beautiful","Fantastic"};
        image(0,5,5,100,443,0x48000000);small_text(27,8,"Graphics Resolution",white);
        for(int i=0;i<6;i++) {
            image(0,10,20+i*15,40,10,i==g->menu?0x80606060:0x80303030);
            image(0,11,20+i*15,38,1,0x80505050);
            small_text(12,21+i*15,levels[i],white);
            if(i==g->quality)small_text(55,21+i*15,"*",white);
        }
    }
    int selected=g->menu_page==MENU_SETTINGS?0:g->menu;
    if(g->menu_page!=MENU_SETTINGS||g->menu==6) {
        unsigned id=menu_button_ids[g->menu_page][selected];float minx=512,miny=448,maxy=0;
        for(uint32_t i=0;i<world->header->meshes;i++) {
            const Mesh *m=world->mesh+i;if(m->id!=id)continue;
            const Vertex *v=(const Vertex *)(world->blob+m->vertex_offset);float white_color[3]={1,1,1};
            for(uint32_t j=0;j<m->vertices;j++) {
                CameraVertex c=camera((Vec3){v[j].x,v[j].y,v[j].z},0,0,0,1,0,white_color);
                if(c.z<.15f)continue;
                DrawVertex p=project(&c);
                minx=fminf(minx,p.x);miny=fminf(miny,p.y);maxy=fmaxf(maxy,p.y);
            }
        }
        text(fmaxf(2,minx-14),(miny+maxy)*.5f-8,">",white);
    }
    if(g->state==ST_NETWORK) {
        image(0,32,150,448,136,0x80101010);
        text(48,170,g->menu_page==MENU_COOP?"CO-OP":"VERSUS",white);
        text(48,205,"Network play is not available\nin this build.\n\nO: BACK",white);
    }
    if(!connected)text(40,425,"CONNECT CONTROLLER TO PORT 1",gray);
    menu_scene=0;platform_profile(4);platform_finish();platform_profile(5);
}
void render_frame(const World *w,const Game *g,int connected) {
    triangles=0;
    cull_faces=0;
    win_scene=0;
    platform_profile(0);
    if(g->state==ST_MENU||g->state==ST_HELP||g->state==ST_CREDITS||g->state==ST_NETWORK) {original_menu(g,connected);return;}
    Game fixed;
    win_scene=g->state==ST_WINSCENE;
    if(win_scene) {
        if(!victory_world)return;
        fixed=*g;fixed.feet=(Vec3){victory_camera[0],victory_camera[1],victory_camera[2]};
        fixed.yaw=victory_camera[3];fixed.pitch=victory_camera[4];fixed.eye=0;
        fixed.time_of_day=0;fixed.flashlight=fixed.map=0;
        fixed.draw_grass=1;fixed.quality=3;fixed.time=0;fixed.custards=0;
        g=&fixed;w=victory_world;
    }
    active_sun=win_scene?victory_sun:scene_sun[g->time_of_day];
    active_tree_colors=win_scene?victory_tree_colors:scene_tree_colors;
    active_grass=win_scene?victory_grass:source_grass;
    active_grass_modes=win_scene?victory_grass_modes:source_grass_modes;
    platform_world(w,g->time_of_day+1);
    world=w;game=g;triangles=0;sy=sinf(g->yaw);cy=cosf(g->yaw);sp=sinf(g->pitch);cp=cosf(g->pitch);
    const float *env=w->config+C_ENV+g->time_of_day*7;density=env[0];
    for(int i=0;i<3;i++) {fog[i]=env[1+i];ambient[i]=env[4+i];}
    if(density!=cached_density) {for(int i=0;i<4097;i++) fog_table[i]=expf(-density*density*i*8);cached_density=density;}
    if(!cache_lights())return;
    prepare_view_matrix();
    far_distance=(g->time_of_day==0?160:g->time_of_day==1?110:75)*(.7f+g->quality*.1f);
    platform_clear((uint8_t)(fog[0]*255),(uint8_t)(fog[1]*255),(uint8_t)(fog[2]*255));
    platform_fog((uint8_t)(fog[0]*255),(uint8_t)(fog[1]*255),(uint8_t)(fog[2]*255));
    const uint32_t white=0x80808080,gray=0x80606060;
    if(win_scene) {
        // Replay the fixed camera's GS geometry; this remains a depth-tested 3D scene.
        if(platform_static_begin(100)) {
            skybox();terrain();int ground=triangles;
            meshes();int models=triangles-ground;
            trees();int foliage=triangles-ground-models;
            grass();
            printf("STPS VICTORY GEOMETRY ground=%d models=%d trees=%d grass=%d total=%d\n",ground,models,foliage,triangles-ground-models-foliage,triangles);
            platform_static_end();
        }
        image((int)w->config[C_WON],0,0,512,448,white);
        image(0,0,0,400,12,0x60484848);
        small_text(128,2,"Return to the main menu",white);
        if(!connected)small_text(170,432,"CONNECT CONTROLLER TO PORT 1",white);
    } else {
        skybox();first_person();terrain();platform_profile(1);meshes();platform_profile(2);trees();grass();flashlight_cookie();platform_profile(3);
        if(g->state==ST_WON)image((int)w->config[C_WON],victory_notice[0]*512,victory_notice[1]*448,victory_notice[2]*512,victory_notice[3]*448,white);
        if(g->state!=ST_DEAD&&g->state!=ST_WON&&g->state_time<5)for(int i=0;i<2;i++)image((int)scene_intro[i][0],scene_intro[i][1]*512,scene_intro[i][2]*448,scene_intro[i][3]*512,scene_intro[i][4]*448,white);
        if(g->pickup_time>0&&g->collected>0&&g->collected<10){const float *p=source_pickups[g->collected-1];image((int)p[0],p[1]*512,p[2]*448,p[3]*512,p[4]*448,white);}
        if(g->state!=ST_WON&&g->scare_time>0&&g->scare_image>0) {int config=g->scare_image<=4?C_POPUP+g->scare_image-1:125+g->scare_image-5;image((int)w->config[config],0,0,512,448,white);}
        if(g->enemy_attacking&&g->state!=ST_DEAD&&g->state!=ST_WON)image(source_damage_border,0,0,512,448,white);
        if(g->state==ST_DEAD)image((int)w->config[C_DEAD],0,0,512,448,white);
        if(g->map) {image((int)w->config[C_MAP],32,16,448,400,white);text(48,420,"TRIANGULO: CERRAR MAPA",white);}
        if(g->state==ST_PAUSE) {image(0,80,160,352,128,0x80080808);text(168,180,"PAUSA",white);text(104,218,"START: CONTINUAR\nSELECT: MENU PRINCIPAL",gray);}
        if(!connected) text(40,390,"MANDO DESCONECTADO",white);
    }
    platform_profile(4);platform_finish();platform_profile(5);
}
