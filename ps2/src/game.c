#include "game.h"
#include "audio_voice.h"
#include "../generated/fidelity.h"
#include "../generated/noonoo.h"
#include <math.h>
#include <string.h>

static float clamp(float x,float a,float b) { return x<a?a:x>b?b:x; }
static float length2(float x,float z) { return sqrtf(x*x+z*z); }
static Vec3 vec(const float *p) { Vec3 v={p[0],p[1],p[2]}; return v; }
static int span(size_t size,uint32_t off,size_t count,size_t stride) { return off<=size && count<=(size-off)/stride; }
int world_open(World *w,const void *blob,size_t length) {
    const WorldHeader *h=(const WorldHeader *)blob;
    if(length<sizeof(*h)||h->magic!=0x53505453||h->version<1||h->version>3||h->bytes!=length) return 0;
    if(h->width<2||h->height<2||h->width>1025||h->height>1025||h->textures>128) return 0;
    if(!span(length,h->mesh_offset,h->meshes,sizeof(Mesh))||!span(length,h->texture_offset,h->textures,sizeof(Texture))||
       !span(length,h->box_offset,h->boxes,sizeof(Box))||!span(length,h->tree_offset,h->trees,sizeof(Tree))||
       !span(length,h->height_offset,(size_t)h->width*h->height,2)||!span(length,h->splat_offset,(size_t)(h->width-1)*(h->height-1),h->version>=2?4:1)||
       !span(length,h->config_offset,h->version==3?132:h->version==2?130:127,4)||!span(length,h->sound_offset,h->sounds,sizeof(Sound))||
       !span(length,h->collision_offset,h->collision_meshes,sizeof(CollisionMesh))) return 0;
    memset(w,0,sizeof(*w)); w->blob=blob; w->header=h;
    w->mesh=(const Mesh *)(w->blob+h->mesh_offset); w->texture=(const Texture *)(w->blob+h->texture_offset);
    w->box=(const Box *)(w->blob+h->box_offset); w->tree=(const Tree *)(w->blob+h->tree_offset);
    w->height=(const uint16_t *)(w->blob+h->height_offset); w->splat=w->blob+h->splat_offset;
    w->config=(const float *)(w->blob+h->config_offset); w->sound=(const Sound *)(w->blob+h->sound_offset);
    w->collision=(const CollisionMesh *)(w->blob+h->collision_offset);
    for(unsigned i=0;i<h->trees;i++)if(w->tree[i].type<0||w->tree[i].type>=3||!isfinite(w->tree[i].type)||
        !isfinite(w->tree[i].x)||!isfinite(w->tree[i].z)||w->tree[i].width<=0||w->tree[i].width>2)return 0;
    if(h->version>=2) {
        unsigned index=(unsigned)w->config[127],samples=(unsigned)w->config[128];
        unsigned grid=h->version==3?(unsigned)w->config[130]:1024;
        if(grid<8||grid>1024||grid%8||!span(length,index,(grid/8)*(grid/8),2)||w->config[129]<0||w->config[129]>=h->textures)return 0;
        const uint16_t *patches=(const uint16_t *)(w->blob+index);
        for(unsigned i=0;i<(grid/8)*(grid/8);i++)if(!span(length,samples,(size_t)patches[i]+1,64))return 0;
    }
    for(uint32_t i=0;i<h->meshes;i++) {
        const Mesh *m=w->mesh+i;
        if(m->vertices>65535||m->texture>=h->textures||!span(length,m->vertex_offset,m->vertices,sizeof(Vertex))||!span(length,m->index_offset,m->triangles*3,2)) return 0;
        const uint16_t *ix=(const uint16_t *)(w->blob+m->index_offset);
        for(uint32_t j=0;j<m->triangles*3;j++) if(ix[j]>=m->vertices) return 0;
    }
    for(uint32_t i=0;i<h->textures;i++) if(!span(length,w->texture[i].offset,w->texture[i].bytes,1)) return 0;
    for(uint32_t i=0;i<h->sounds;i++) if(!span(length,w->sound[i].offset,w->sound[i].samples&AUDIO_COUNT,w->sound[i].samples&AUDIO_ULAW?1:2)) return 0;
    return 1;
}
float world_height(const World *w,float x,float z) {
    x=clamp(x/w->config[C_SCALE],0,(float)w->header->width-1.001f);
    z=clamp(z/w->config[C_SCALE+2],0,(float)w->header->height-1.001f);
    int ix=(int)x,iz=(int)z; float tx=x-ix,tz=z-iz;
    const uint16_t *p=w->height+iz*w->header->width+ix;
    return ((p[0]*(1-tx)+p[1]*tx)*(1-tz)+(p[w->header->width]*(1-tx)+p[w->header->width+1]*tx)*tz)*w->config[C_SCALE+1];
}
void game_enemy_matrix(const World *w,const Game *g,float matrix[16]) {
    float dx=g->feet.x-g->enemy.x,dy=g->feet.y-g->enemy.y,dz=g->feet.z-g->enemy.z;
    float yaw=atan2f(dx,dz),pitch=atan2f(dy,fmaxf(.00001f,length2(dx,dz)));
    float sy=sinf(yaw),cy=cosf(yaw),sp=sinf(pitch),cp=cosf(pitch);
    const float rotation[16]={cy,0,-sy,0,-sy*sp,cp,-cy*sp,0,sy*cp,sp,cy*cp,0,0,0,0,1};
    memcpy(matrix,rotation,sizeof(rotation));
    const float *origin=w->config+C_ENEMY;
    for(int row=0;row<3;row++)matrix[12+row]=(&g->enemy.x)[row]-matrix[row]*origin[0]-matrix[4+row]*origin[1]-matrix[8+row]*origin[2];
}
void game_hoover_matrix(const World *w,const Game *g,float matrix[16]) {
    steering_matrix(matrix,&g->hoover,w->config+C_HOOVER,source_hoover_rotation);
}
Vec3 game_mesh_vertex(const World *w,const Game *g,const Mesh *m,const Vertex *v) {
    Vec3 p={v->x,v->y,v->z};
    if(m->flags&(M_ENEMY|M_HOOVER)) {
        float matrix[16];if(m->flags&M_ENEMY)game_enemy_matrix(w,g,matrix);else game_hoover_matrix(w,g,matrix);
        p=(Vec3){matrix[0]*v->x+matrix[4]*v->y+matrix[8]*v->z+matrix[12],
                 matrix[1]*v->x+matrix[5]*v->y+matrix[9]*v->z+matrix[13],
                 matrix[2]*v->x+matrix[6]*v->y+matrix[10]*v->z+matrix[14]};
    }
    if(m->flags&M_DOOR) {
        const float *d=w->config+101+m->id*12;
        Vec3 end={d[0]*p.x+d[1]*p.y+d[2]*p.z+d[3],d[4]*p.x+d[5]*p.y+d[6]*p.z+d[7],d[8]*p.x+d[9]*p.y+d[10]*p.z+d[11]};
        p.x+=(end.x-p.x)*g->door_open;p.y+=(end.y-p.y)*g->door_open;p.z+=(end.z-p.z)*g->door_open;
    }
    return p;
}
static Vec3 subtract(Vec3 a,Vec3 b) { Vec3 r={a.x-b.x,a.y-b.y,a.z-b.z}; return r; }
static Vec3 cross(Vec3 a,Vec3 b) { Vec3 r={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; return r; }
static float dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static void enemy_capsule(const World *w,const Game *g,Vec3 *a,Vec3 *b) {
    float matrix[16];game_enemy_matrix(w,g,matrix);
    float half=source_enemy_capsule[4]*.5f-source_enemy_capsule[3];
    float center[3];for(int k=0;k<3;k++)center[k]=(&g->enemy.x)[k]+matrix[k]*source_enemy_capsule[0]+matrix[4+k]*source_enemy_capsule[1]+matrix[8+k]*source_enemy_capsule[2];
    for(int k=0;k<3;k++){(&a->x)[k]=center[k]-matrix[4+k]*half;(&b->x)[k]=center[k]+matrix[4+k]*half;}
}
static float segment_distance2(Vec3 p,Vec3 q,Vec3 a,Vec3 b) {
    Vec3 u=subtract(q,p),v=subtract(b,a),r=subtract(p,a);
    float uu=dot(u,u),vv=dot(v,v),uv=dot(u,v),ur=dot(u,r),vr=dot(v,r);
    float den=uu*vv-uv*uv,s=den>.000001f?clamp((uv*vr-vv*ur)/den,0,1):0;
    float t=(uv*s+vr)/fmaxf(vv,.000001f);
    if(t<0){t=0;s=clamp(-ur/fmaxf(uu,.000001f),0,1);}else if(t>1){t=1;s=clamp((uv-ur)/fmaxf(uu,.000001f),0,1);}
    Vec3 d={r.x+s*u.x-t*v.x,r.y+s*u.y-t*v.y,r.z+s*u.z-t*v.z};return dot(d,d);
}
static float enemy_support(const World *w,const Game *g,float x,float z,float oldy) {
    Vec3 a,b;enemy_capsule(w,g,&a,&b);float radius=source_enemy_capsule[3],top=-1e9f;
    for(int i=0;i<2;i++){Vec3 p=i?b:a;float rr=radius*radius-(x-p.x)*(x-p.x)-(z-p.z)*(z-p.z);
        if(rr>=0)top=fmaxf(top,p.y+sqrtf(rr));}
    Vec3 d=subtract(b,a);float dx=x-a.x,dz=z-a.z,l2=dot(d,d),horizontal=d.x*d.x+d.z*d.z,k=dx*d.x+dz*d.z;
    float constant=l2*(dx*dx+dz*dz-radius*radius)-k*k,disc=k*k*d.y*d.y-horizontal*constant;
    if(horizontal>.000001f&&disc>=0){float y=(k*d.y+sqrtf(disc))/horizontal,t=(k+y*d.y)/l2;
        if(t>=0&&t<=1)top=fmaxf(top,a.y+y);}
    return top<=oldy+.4f?top:-1e9f;
}
static int segment_triangle(Vec3 a,Vec3 b,Vec3 v0,Vec3 v1,Vec3 v2) {
    Vec3 dir=subtract(b,a),e1=subtract(v1,v0),e2=subtract(v2,v0),p=cross(dir,e2);
    float det=dot(e1,p); if(fabsf(det)<1e-6f) return 0;
    float inv=1/det; Vec3 t=subtract(a,v0); float u=dot(t,p)*inv;
    if(u<0||u>1) return 0;
    Vec3 q=cross(t,e1); float v=dot(dir,q)*inv;
    if(v<0||u+v>1) return 0;
    float hit=dot(e2,q)*inv;
    return hit>.0001f&&hit<.9999f;
}
static int near_bounds(float x,float z,const float *min,const float *max,float margin) {
    return x>=min[0]-margin&&x<=max[0]+margin&&z>=min[2]-margin&&z<=max[2]+margin;
}
/* Static broad phase. Queries retain the original triangle tests; unsupported
 * sizes fall back to the complete scan rather than dropping collision. */
static const uint16_t *collision_candidates(const World *w,float minx,float minz,float maxx,float maxz,unsigned *count) {
    *count=w->header->meshes;
#ifdef STPS_LINEAR_COLLISION
    (void)minx;(void)minz;(void)maxx;(void)maxz;return NULL;
#else
    static const Mesh *indexed;static const unsigned char *indexed_blob;static unsigned indexed_count;
    static uint16_t heads[1024],visited[2048],candidates[2048],serial;
    static struct {uint16_t mesh,next;} links[4096];static int valid;
    if(indexed!=w->mesh||indexed_blob!=w->blob||indexed_count!=w->header->meshes) {
        indexed=w->mesh;indexed_blob=w->blob;indexed_count=w->header->meshes;valid=0;
        if(indexed_count>2048)return NULL;
        memset(heads,255,sizeof(heads));unsigned used=0;
        for(unsigned i=0;i<indexed_count;i++) {
            const Mesh *m=w->mesh+i;if(!(m->flags&M_COLLIDE)||(m->flags&(M_ENEMY|M_HOOVER)))continue;
            int x0=(int)clamp(floorf(m->min[0]/32),0,31),x1=(int)clamp(floorf(m->max[0]/32),0,31);
            int z0=(int)clamp(floorf(m->min[2]/32),0,31),z1=(int)clamp(floorf(m->max[2]/32),0,31);
            for(int z=z0;z<=z1;z++)for(int x=x0;x<=x1;x++) {
                if(used>=4096)return NULL;
                unsigned cell=z*32+x;links[used].mesh=i;links[used].next=heads[cell];heads[cell]=used++;
            }
        }
        valid=1;
    }
    if(!valid)return NULL;
    if(++serial==0){memset(visited,0,sizeof(visited));serial=1;}
    int x0=(int)clamp(floorf(minx/32),0,31),x1=(int)clamp(floorf(maxx/32),0,31);
    int z0=(int)clamp(floorf(minz/32),0,31),z1=(int)clamp(floorf(maxz/32),0,31);*count=0;
    for(int z=z0;z<=z1;z++)for(int x=x0;x<=x1;x++)for(unsigned link=heads[z*32+x];link!=65535;link=links[link].next) {
        unsigned i=links[link].mesh;if(visited[i]==serial)continue;
        visited[i]=serial;candidates[(*count)++]=i;
    }
    return candidates;
#endif
}
static float enemy_ground(const World *w,float x,float z) {
    static const Mesh *meshes;static const float *config;static float offset;
    if(meshes!=w->mesh||config!=w->config) {
        float bottom=1e10f;for(uint32_t i=0;i<w->header->meshes;i++)if(w->mesh[i].flags&M_ENEMY)bottom=fminf(bottom,w->mesh[i].min[1]);
        meshes=w->mesh;config=w->config;offset=bottom<1e9f?w->config[C_ENEMY+1]-bottom:0;
    }
    return world_height(w,x,z)+offset;
}
static int box_segment(const Box *box,Vec3 a,Vec3 b) {
    float start=0,end=1; const float *aa=&a.x,*bb=&b.x;
    for(int i=0;i<3;i++) {
        float d=bb[i]-aa[i];
        if(fabsf(d)<1e-6f) { if(aa[i]<box->min[i]||aa[i]>box->max[i]) return 0; }
        else { float t1=(box->min[i]-aa[i])/d,t2=(box->max[i]-aa[i])/d;
            if(t1>t2) { float t=t1;t1=t2;t2=t; } if(t1>start) start=t1; if(t2<end) end=t2; if(start>end) return 0; }
    }
    return end>.001f&&start<.999f;
}
static int tree_collision(const World *w,Vec3 a,Vec3 b,float radius,float eye) {
    static const Tree *indexed;static uint16_t heads[64*64],next[4096];
    if(w->header->trees>4096)return 0;
    if(indexed!=w->tree) {
        memset(heads,255,sizeof(heads));
        for(unsigned i=0;i<w->header->trees;i++) {
            const Tree *t=w->tree+i;const float *c=source_tree_capsules[(int)t->type];
            int x=(int)clamp((t->x+c[0]*t->width)/16,0,63),z=(int)clamp((t->z+c[2]*t->width)/16,0,63);
            next[i]=heads[z*64+x];heads[z*64+x]=i;
        }
        indexed=w->tree;
    }
    int minx=(int)clamp((fminf(a.x,b.x)-3)/16,0,63),maxx=(int)clamp((fmaxf(a.x,b.x)+3)/16,0,63);
    int minz=(int)clamp((fminf(a.z,b.z)-3)/16,0,63),maxz=(int)clamp((fmaxf(a.z,b.z)+3)/16,0,63);
    float vx=b.x-a.x,vz=b.z-a.z,length=vx*vx+vz*vz;
    for(int z=minz;z<=maxz;z++)for(int x=minx;x<=maxx;x++)for(unsigned i=heads[z*64+x];i!=65535;i=next[i]) {
        const Tree *t=w->tree+i;const float *c=source_tree_capsules[(int)t->type];
        float tx=t->x+c[0]*t->width,tz=t->z+c[2]*t->width,r=c[3]*t->width+radius;
        float fraction=radius>0?1:clamp(((tx-a.x)*vx+(tz-a.z)*vz)/fmaxf(length,.0001f),0,1);
        float dx=a.x+vx*fraction-tx,dz=a.z+vz*fraction-tz;
        if(dx*dx+dz*dz>=r*r)continue;
        float y=a.y+(b.y-a.y)*fraction,bottom=t->y+(c[1]-c[4]*.5f)*t->height,top=bottom+c[4]*t->height;
        if(y+eye<bottom||y+radius>top)continue;
        float ox=a.x-tx,oz=a.z-tz;
        if(radius==0||dx*dx+dz*dz<ox*ox+oz*oz)return 1;
    }
    return 0;
}
int world_occluded(const World *w,const Game *g,Vec3 a,Vec3 b) {
    if(tree_collision(w,a,b,0,0))return 1;
    for(uint32_t i=0;i<w->header->boxes;i++) if(box_segment(w->box+i,a,b)) return 1;
    Box ray={{fminf(a.x,b.x),fminf(a.y,b.y),fminf(a.z,b.z)},{fmaxf(a.x,b.x),fmaxf(a.y,b.y),fmaxf(a.z,b.z)}};
    unsigned count;const uint16_t *candidates=collision_candidates(w,ray.min[0],ray.min[2],ray.max[0],ray.max[2],&count);
    for(unsigned candidate=0;candidate<count;candidate++) {unsigned i=candidates?candidates[candidate]:candidate;
        const Mesh *m=w->mesh+i; if(!(m->flags&M_COLLIDE)||(m->flags&(M_ENEMY|M_HOOVER))||((m->flags&M_DAY_TREE)&&g->time_of_day==2)||((m->flags&M_DOOR)&&g->door_open>.7f)) continue;
        if(m->max[0]<ray.min[0]||m->min[0]>ray.max[0]||m->max[2]<ray.min[2]||m->min[2]>ray.max[2]||m->max[1]<ray.min[1]||m->min[1]>ray.max[1]) continue;
        const Vertex *v=(const Vertex *)(w->blob+m->vertex_offset); const uint16_t *ix=(const uint16_t *)(w->blob+m->index_offset);
        for(uint32_t j=0;j<m->triangles;j++,ix+=3) {
            Vec3 p0={v[ix[0]].x,v[ix[0]].y,v[ix[0]].z},p1={v[ix[1]].x,v[ix[1]].y,v[ix[1]].z},p2={v[ix[2]].x,v[ix[2]].y,v[ix[2]].z};
            if(segment_triangle(a,b,p0,p1,p2)) return 1;
        }
    }
    return 0;
}
static float ground(const World *w,const Game *g,float x,float z,float oldy) {
    float result=world_height(w,x,z);
    if(g->state==ST_PLAY&&(x-g->enemy.x)*(x-g->enemy.x)+(z-g->enemy.z)*(z-g->enemy.z)<16)result=fmaxf(result,enemy_support(w,g,x,z,oldy));
    // Close the upper opening using the original rim's centre, radius and height.
    // It supports arrivals from the roof without interfering with the room below.
    float roof_x=x-source_roof_support[0],roof_z=z-source_roof_support[1];
    if(roof_x*roof_x+roof_z*roof_z<source_roof_support[2]*source_roof_support[2]&&source_roof_support[3]<=oldy+.4f)
        result=fmaxf(result,source_roof_support[3]);
    for(unsigned i=0;i<w->header->boxes;i++) {
        const Box *b=w->box+i;
        if(near_bounds(x,z,b->min,b->max,0)&&b->max[1]>result&&b->max[1]<=oldy+.4f)result=b->max[1];
    }
    unsigned count;const uint16_t *candidates=collision_candidates(w,x-.4f,z-.4f,x+.4f,z+.4f,&count);
    for(unsigned candidate=0;candidate<count;candidate++) {unsigned i=candidates?candidates[candidate]:candidate;
        const Mesh *m=w->mesh+i; if(!(m->flags&M_COLLIDE)||(m->flags&(M_ENEMY|M_HOOVER))||((m->flags&M_DAY_TREE)&&g->time_of_day==2)||!near_bounds(x,z,m->min,m->max,.4f)) continue;
        const Vertex *v=(const Vertex *)(w->blob+m->vertex_offset); const uint16_t *ix=(const uint16_t *)(w->blob+m->index_offset);
        for(uint32_t j=0;j<m->triangles;j++,ix+=3) {
            const Vertex *a=v+ix[0],*b=v+ix[1],*c=v+ix[2];
            Vec3 e1={b->x-a->x,b->y-a->y,b->z-a->z},e2={c->x-a->x,c->y-a->y,c->z-a->z}; Vec3 n=cross(e1,e2);
            float length=sqrtf(dot(n,n));if(length<.000001f||fabsf(n.y)<length*.001f)continue;
            float contact_x=x,contact_z=z;
            float den=(b->z-c->z)*(a->x-c->x)+(c->x-b->x)*(a->z-c->z); if(fabsf(den)<.00001f) continue;
            float u=((b->z-c->z)*(contact_x-c->x)+(c->x-b->x)*(contact_z-c->z))/den;
            float t=((c->z-a->z)*(contact_x-c->x)+(a->x-c->x)*(contact_z-c->z))/den;
            // World coordinates are large; shared edges need a small barycentric tolerance.
            if(u<-.0001f||t<-.0001f||u+t>1.0001f) continue;
            float y=a->y*u+b->y*t+c->y*(1-u-t);
            // A steep roof still collides with a descending controller. Slope
            // limits must never remove the triangle from vertical collision.
            if(y>result&&y<=oldy+.4f) result=y;
        }
    }
    (void)g; return result;
}
static int blocked(const World *w,const Game *g,Vec3 a,Vec3 b) {
    if((g->state==ST_PLAY||g->state==ST_PAUSE)&&(b.x-g->enemy.x)*(b.x-g->enemy.x)+(b.z-g->enemy.z)*(b.z-g->enemy.z)<16) {
        Vec3 ea,eb;enemy_capsule(w,g,&ea,&eb);float r=.4f+source_enemy_capsule[3];
        Vec3 bottom=b,top=b;bottom.y+=.4f;top.y+=g->eye-.1f;
        float next=segment_distance2(bottom,top,ea,eb);bottom=a;top=a;bottom.y+=.4f;top.y+=g->eye-.1f;
        if(next<r*r&&next<segment_distance2(bottom,top,ea,eb))return 1;
    }
    if(tree_collision(w,a,b,.4f,g->eye))return 1;
    for(uint32_t i=0;i<w->header->boxes;i++) {
        const Box *box=w->box+i;
        if(near_bounds(b.x,b.z,box->min,box->max,.4f)&&b.y+g->eye>box->min[1]&&b.y+.4f<box->max[1]) return 1;
    }
    for(int j=0;j<3;j++) {
        Vec3 from=a,to=b; from.y+=.45f+j*g->eye*.4f; to.y=from.y;
        float dx=b.x-a.x,dz=b.z-a.z,d=length2(dx,dz);
        if(d>.00001f) { to.x+=dx/d*.4f;to.z+=dz/d*.4f; }
        if(world_occluded(w,g,from,to)) return 1;
    }
    return world_height(w,b.x,b.z)>a.y+.4f;
}
void game_init(Game *g) { memset(g,0,sizeof(*g));g->state=ST_MENU;g->time_of_day=2;g->flashlight=1;g->quality=3;g->draw_grass=1; }
static uint32_t random_number(Game *g) {g->random=g->random*1664525u+1013904223u;return g->random;}
void game_start(Game *g,const World *w) {
    int day=g->time_of_day,quality=g->quality;uint32_t seed=0x53545053u^(uint32_t)(g->state_time*1000000);
    memset(g,0,sizeof(*g)); g->time_of_day=day;g->quality=quality;g->random=seed;
    g->state=ST_PLAY;g->flashlight=1;g->health=100;g->eye=1.7f;g->draw_grass=1;
    g->feet=vec(w->config+C_SPAWN);g->enemy=vec(w->config+C_ENEMY);
    steering_init(&g->hoover,w->config+C_HOOVER,source_hoover_rotation);
    g->feet.y=world_height(w,g->feet.x,g->feet.z);g->enemy.y=enemy_ground(w,g->enemy.x,g->enemy.z);
    g->enemy_speed=w->config[C_CHASE];g->grounded=1;g->next_scare=32+random_number(g)%170;
}
static void update_menu(Game *g,const World *w,const Input *in) {
    const int count[6]={5,7,4,3,4,4};int n=count[g->menu_page];
    if(in->pressed&(B_UP|B_LEFT)) g->menu=(g->menu+n-1)%n;
    if(in->pressed&(B_DOWN|B_RIGHT)) g->menu=(g->menu+1)%n;
    if(in->pressed&B_BACK) {g->menu_page=MENU_MAIN;g->menu=0;return;}
    if(in->pressed&B_CONFIRM) {
        if(g->menu_page==MENU_MAIN) {
            if(g->menu==0)g->menu_page=MENU_SINGLE;
            else if(g->menu==1)g->menu_page=MENU_MULTI;
            else if(g->menu==2)g->menu_page=MENU_SETTINGS;
            else if(g->menu==3)g->state=ST_CREDITS;
            else g->state=ST_HELP;
            g->menu=0;
        } else if(g->menu_page==MENU_SETTINGS) {
            if(g->menu<6)g->quality=g->menu;
            else {g->menu_page=MENU_MAIN;g->menu=0;}
        } else if(g->menu_page==MENU_MULTI) {
            g->menu_page=g->menu==0?MENU_COOP:g->menu==1?MENU_VERSUS:MENU_MAIN;g->menu=0;
        } else if(g->menu==3) {g->menu_page=MENU_MAIN;g->menu=0;}
        else {
            g->time_of_day=g->menu;
            if(g->menu_page==MENU_SINGLE)game_start(g,w);
            else g->state=ST_NETWORK;
        }
    }
}
static void player_motion(Game *g,const World *w,const Input *in,float dt) {
    g->eye+=((g->crouching?.85f:1.7f)-g->eye)*fminf(1,dt*5);
    g->yaw+=in->look_x*2.3f*dt;g->pitch=clamp(g->pitch-in->look_y*1.8f*dt,-1.0472f,1.0472f);
    float speed=w->config[g->crouching?C_CROUCH:((in->held&B_RUN)?C_RUN:C_WALK)];
    float mx=in->move_x,my=in->move_y,mag=length2(mx,my);if(mag>1){mx/=mag;my/=mag;}
    g->moving=mag>.1f;
    float dx=(mx*cosf(g->yaw)+my*sinf(g->yaw))*speed*dt,dz=(-mx*sinf(g->yaw)+my*cosf(g->yaw))*speed*dt;
    float old_ground=g->feet.y;int standing=g->grounded&&!g->moving;Vec3 next=g->feet;
    if(fabsf(dx)>.00001f){next.x+=dx;if(!blocked(w,g,g->feet,next))g->feet.x=next.x;}
    if(fabsf(dz)>.00001f){next=g->feet;next.z+=dz;if(!blocked(w,g,g->feet,next))g->feet.z=next.z;}
    g->feet.x=clamp(g->feet.x,.4f,(w->header->width-1)*w->config[C_SCALE]-.4f);
    g->feet.z=clamp(g->feet.z,.4f,(w->header->height-1)*w->config[C_SCALE+2]-.4f);
    if((in->pressed&B_JUMP)&&g->grounded&&!g->crouching){g->vy=sqrtf(30);g->grounded=0;}
    g->vy=fmaxf(-20,g->vy-15*dt);g->feet.y+=g->vy*dt;
    // Test from the pre-integration height, so fast descent cannot tunnel past a floor.
    float gy=standing&&length2(g->feet.x-g->enemy.x,g->feet.z-g->enemy.z)>4?old_ground:ground(w,g,g->feet.x,g->feet.z,old_ground);
    if(g->feet.y<=gy){g->feet.y=gy;g->vy=0;g->grounded=1;}else g->grounded=0;
}
static int enemy_side_spot(const World *w,const Game *g,Vec3 feet) {
    if(feet.x<1||feet.z<1||feet.x>(w->header->width-1)*w->config[C_SCALE]-1||feet.z>(w->header->height-1)*w->config[C_SCALE+2]-1)return 0;
    if(fabsf(feet.y-g->feet.y)>1)return 0;
    Vec3 approach=feet;approach.x+=3;
    if(tree_collision(w,approach,feet,.55f,2.3f))return 0;
    for(unsigned i=0;i<w->header->boxes;i++) {
        const Box *box=w->box+i;
        if(near_bounds(feet.x,feet.z,box->min,box->max,.55f)&&feet.y+2.3f>box->min[1]&&feet.y+.35f<box->max[1])return 0;
    }
    Vec3 view=g->feet;view.y+=g->eye;Vec3 target=feet;target.y+=1.2f;
    if(world_occluded(w,g,view,target))return 0;
    Vec3 a=target,b=target;a.x-=.55f;b.x+=.55f;
    if(world_occluded(w,g,a,b))return 0;
    a=target;b=target;a.z-=.55f;b.z+=.55f;
    return !world_occluded(w,g,a,b);
}
static int enemy_shift_side(Game *g,const World *w) {
    float side=(g->enemy.x-g->feet.x)*cosf(g->yaw)-(g->enemy.z-g->feet.z)*sinf(g->yaw)>0?-1:1;
    for(int attempt=0;attempt<4;attempt++) {
        float sign=attempt&1?-side:side,angle=g->yaw+sign*.55f;
        float radius=w->config[C_FOLLOW]+.35f+(attempt/2)*.75f;
        Vec3 spot={g->feet.x+sinf(angle)*radius,0,g->feet.z+cosf(angle)*radius};
        spot.y=world_height(w,spot.x,spot.z);
        if(!enemy_side_spot(w,g,spot))continue;
        spot.y=enemy_ground(w,spot.x,spot.z);g->enemy=spot;g->next_enemy_shift=g->time+.25f;
        g->events|=EV_ENEMY_SHIFT;return 1;
    }
    g->next_enemy_shift=g->time+.1f;return 0;
}
void game_update(Game *g,const World *w,const Input *in,float dt) {
    g->events=0;dt=clamp(dt,0,.05f);g->state_time+=dt;
    if(g->state==ST_MENU) { update_menu(g,w,in); return; }
    if(g->state==ST_HELP||g->state==ST_CREDITS) { if(in->pressed&(B_CONFIRM|B_BACK)) g->state=ST_MENU; return; }
    if(g->state==ST_NETWORK) {if(in->pressed&B_BACK)g->state=ST_MENU;return;}
    if(g->state==ST_WINSCENE) {
        if(in->pressed&(B_CONFIRM|B_BACK)){g->state=ST_MENU;g->menu_page=MENU_MAIN;g->menu=0;g->state_time=0;}
        return;
    }
    if(g->state==ST_WON) {
        player_motion(g,w,in,dt);
        if(g->state_time>=5){g->state=ST_WINSCENE;g->state_time=0;}
        return;
    }
    if(g->state==ST_DEAD) {
        if(g->state_time>5||((in->pressed&B_CONFIRM)&&g->state_time>1)) {g->state=ST_MENU;g->state_time=0;return;}
        if(g->state==ST_DEAD)player_motion(g,w,in,dt);
        return;
    }
    if(in->pressed&B_PAUSE) { g->state=g->state==ST_PLAY?ST_PAUSE:ST_PLAY;g->moving=0;return; }
    if(g->state==ST_PAUSE) { if(in->pressed&B_BACK) { g->state=ST_MENU;g->state_time=0; } return; }
    g->time+=dt;g->scare_time=fmaxf(0,g->scare_time-dt);g->pickup_time=fmaxf(0,g->pickup_time-dt);
    if(in->pressed&B_LIGHT) { g->flashlight=!g->flashlight;g->events|=EV_SWITCH; }
    if(in->pressed&B_MAP) g->map=!g->map;
    if(in->pressed&B_CROUCH) g->crouching=!g->crouching;
    player_motion(g,w,in,dt);
    for(int i=0;i<10;i++) {
        const float *p=w->config+C_CUSTARD+i*3;
        if(!(g->custards&(1<<i))&&length2(g->feet.x-p[0],g->feet.z-p[2])<1.35f&&fabsf(g->feet.y-p[1])<2.5f) {
            g->custards|=1<<i;g->collected++;g->events|=EV_PICKUP;g->pickup_time=3;
            if(10-g->collected==5) g->enemy_speed=w->config[C_FAST];
            if(10-g->collected==2) g->enemy_speed=w->config[C_SPRINT];
            if(g->collected==10) {g->state=ST_WON;g->state_time=0;return;}
        }
    }
    float ex=g->enemy.x-g->feet.x,ez=g->enemy.z-g->feet.z,ey=g->enemy.y-g->feet.y;
    float distance=sqrtf(ex*ex+ez*ez+ey*ey);
    Vec3 view=g->feet;view.y+=g->eye;Vec3 target=g->enemy;target.y+=1;
    Vec3 sight=subtract(target,view);float sight_length=sqrtf(dot(sight,sight));
    float product=sight_length>.001f?(cosf(g->pitch)*(sinf(g->yaw)*sight.x+cosf(g->yaw)*sight.z)+sinf(g->pitch)*sight.y)/sight_length:1;
    int visible=product>w->config[C_VISIBLE]&&distance<fmaxf(30,w->config[C_RANGE])&&!world_occluded(w,g,view,target);
    // User reference: damage and lateral reappearances belong to a close
    // encounter. A distant sighting must not teleport into damage range.
    float attack_range=fminf(w->config[C_RANGE],5);
    int close=distance<attack_range,above=g->feet.y>g->enemy.y+source_enemy_capsule[3]*.75f;
    g->enemy_attacking=visible&&close;
    if(visible) {
        if(close)g->health=fmaxf(0,g->health-w->config[C_DAMAGE]*dt);
        if(!g->enemy_visible)g->events|=EV_ENEMY_SEEN;
        if(g->health<=0) {g->state=ST_DEAD;g->state_time=0;g->events|=EV_DEATH;return;}
        if(close&&!above&&g->time>=g->next_enemy_shift)enemy_shift_side(g,w);
    } else if(g->enemy_visible&&product<w->config[C_OFFSCREEN]&&close&&!above&&g->time>=g->next_enemy_shift&&enemy_shift_side(g,w)) {
        // Recreate the lateral reappearance seen in the reference video.
    }
    if(!(g->events&EV_ENEMY_SHIFT)&&((visible&&!close)||(!visible&&(product<w->config[C_OFFSCREEN]||distance>=attack_range)))) {
        if(distance>w->config[C_FOLLOW]) {
            float step=fminf(g->enemy_speed*dt,distance-w->config[C_FOLLOW]);
            g->enemy.x-=ex/distance*step;g->enemy.z-=ez/distance*step;
            g->enemy.y=enemy_ground(w,g->enemy.x,g->enemy.z);
        }
    }
    g->enemy_visible=visible;
    if(length2(g->feet.x-w->config[C_DOOR],g->feet.z-w->config[C_DOOR+2])<6) g->door_open=fminf(1,g->door_open+dt*1.5f);
    steering_step(&g->hoover,w->config+C_WAY,source_hoover_settings[0],source_hoover_settings[1],source_hoover_settings[2],dt);
    if(g->time>g->next_scare) {g->scare_time=.1f;g->scare_image=1+random_number(g)%6;g->events|=EV_SCARE;g->next_scare+=33+random_number(g)%170;}
}
// Integrate slow display frames without slowing the game clock. Button edges
// fire only once, and audio events survive all physics steps in this frame.
void game_advance(Game *g,const World *w,const Input *in,float elapsed) {
    float remaining=clamp(elapsed,0,.25f); Input step=*in;int events=0;
    do {
        float dt=fminf(remaining,1/60.f);
        game_update(g,w,&step,dt);events|=g->events;step.pressed=0;
        remaining-=dt;
    } while(remaining>.000001f);
    g->events=events;
}
