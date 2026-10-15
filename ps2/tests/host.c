/* Host harness: exercises the same gameplay, projection, clipping and asset blob.
 * This validates logic and renders reference frames; it does not emulate the GS.
 */
#include "game.h"
#include "render.h"
#include "audio_voice.h"
#include "terrain_surface.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "menu_route.h"
#include "../generated/fidelity.h"
#include "../generated/scene_audio.h"
static World w;
void platform_profile(int phase) {(void)phase;}
static int additive,blend_mode,cutout;
static float fog_rgb[3];
void platform_fog(uint8_t r,uint8_t g,uint8_t b){fog_rgb[0]=r;fog_rgb[1]=g;fog_rgb[2]=b;}
void platform_blend(int mode) {additive=mode==1||mode==3;blend_mode=mode;}
void platform_cutout(int enabled) {cutout=enabled;}
int platform_static_begin(int key) {(void)key;return 1;}
void platform_static_end(void) {}
void platform_cache_reset(void) {}
void platform_work(int kind,int begin) {(void)kind;(void)begin;}
static const World *texture_world;
void platform_world(const World *world,int bank) {texture_world=world;(void)bank;}
static unsigned char pixels[448][512][3];
static float depth[448][512];
static unsigned drawn;
static const Game *anchor_game;static unsigned grass_roots;static float grass_root_gap;
void platform_clear(uint8_t r,uint8_t g,uint8_t b) {
    drawn=0;for(int y=0;y<448;y++)for(int x=0;x<512;x++){pixels[y][x][0]=r;pixels[y][x][1]=g;pixels[y][x][2]=b;depth[y][x]=1e20f;}
}
static const unsigned char *texel(int texture,float u,float v) {
    const Texture *t=texture_world->texture+texture;int x=((int)floorf(u*t->width)%(int)t->width+(int)t->width)%t->width;
    int y=((int)floorf(v*t->height)%(int)t->height+(int)t->height)%t->height;return texture_world->blob+t->offset+(y*t->width+x)*4;
}
static void paint(int x,int y,const unsigned char *p,float r,float g,float b,float alpha,float visibility) {
    float c[3]={r,g,b};for(int k=0;k<3;k++){float value=fminf(255,p[k]*c[k])*visibility+fog_rgb[k]*(1-visibility);float mixed=value*alpha+pixels[y][x][k]*(additive?1:1-alpha);pixels[y][x][k]=(unsigned char)fminf(255,mixed);}
}
void platform_triangle(int texture,const DrawVertex *p) {
    if(anchor_game&&texture_world==&w&&texture==(int)w.config[129])for(int i=0;i<3;i++) {
        if(fabsf(p[i].v-1)>.0001f||(fabsf(p[i].u)>.0001f&&fabsf(p[i].u-1)>.0001f))continue;
        float x=(p[i].x-256)*p[i].z/332.553f,y=(224-p[i].y)*p[i].z/387.98f;
        float sy=sinf(anchor_game->yaw),cy=cosf(anchor_game->yaw),sp=sinf(anchor_game->pitch),cp=cosf(anchor_game->pitch);
        float forward=-y*sp+p[i].z*cp;
        float wx=anchor_game->feet.x+x*cy+forward*sy,wz=anchor_game->feet.z-x*sy+forward*cy;
        float wy=anchor_game->feet.y+anchor_game->eye+y*cp+p[i].z*sp;
        float gap=wy-terrain_surface_height(&w,terrain_step(anchor_game,0,0),wx,wz);
        grass_root_gap=fmaxf(grass_root_gap,fabsf(gap+.025f));grass_roots++;
    }
    drawn++;int minx=(int)fmaxf(0,fminf(p[0].x,fminf(p[1].x,p[2].x))),maxx=(int)fminf(511,ceilf(fmaxf(p[0].x,fmaxf(p[1].x,p[2].x))));
    int miny=(int)fmaxf(0,fminf(p[0].y,fminf(p[1].y,p[2].y))),maxy=(int)fminf(447,ceilf(fmaxf(p[0].y,fmaxf(p[1].y,p[2].y))));
    float den=(p[1].y-p[2].y)*(p[0].x-p[2].x)+(p[2].x-p[1].x)*(p[0].y-p[2].y);if(fabsf(den)<1e-5f)return;
    for(int y=miny;y<=maxy;y++)for(int x=minx;x<=maxx;x++) {
        float a=((p[1].y-p[2].y)*(x+.5f-p[2].x)+(p[2].x-p[1].x)*(y+.5f-p[2].y))/den;
        float b=((p[2].y-p[0].y)*(x+.5f-p[2].x)+(p[0].x-p[2].x)*(y+.5f-p[2].y))/den,c=1-a-b;
        if(a<0||b<0||c<0)continue;
        float q=a/p[0].z+b/p[1].z+c/p[2].z,z=1/q;
        if(z>depth[y][x]+(blend_mode==3||blend_mode==4?.0002f:0))continue;
        float u=(a*p[0].u/p[0].z+b*p[1].u/p[1].z+c*p[2].u/p[2].z)/q,v=(a*p[0].v/p[0].z+b*p[1].v/p[1].z+c*p[2].v/p[2].z)/q;
        if(blend_mode==3){u=a*p[0].u+b*p[1].u+c*p[2].u;v=a*p[0].v+b*p[1].v+c*p[2].v;}
        const unsigned char *tex=texel(texture,u,v);float alpha=tex[3]/128.f*(a*p[0].a+b*p[1].a+c*p[2].a)/128.f;
        if(alpha<(cutout?.5f:1/128.f))continue;
        paint(x,y,tex,(a*p[0].r+b*p[1].r+c*p[2].r)/128,(a*p[0].g+b*p[1].g+c*p[2].g)/128,(a*p[0].b+b*p[1].b+c*p[2].b)/128,cutout?1:alpha,(a*(p[0].cache_key>>24)+b*(p[1].cache_key>>24)+c*(p[2].cache_key>>24))/255);
        if(!blend_mode)depth[y][x]=z;
    }
}
void platform_sprite(int texture,float x,float y,float width,float height,float u,float v,float uw,float vh,uint32_t color) {
    for(int yy=(int)y;yy<y+height&&yy<448;yy++)for(int xx=(int)x;xx<x+width&&xx<512;xx++) {
        if(xx<0||yy<0)continue;
        const unsigned char *tex=texel(texture,u+(xx-x)/width*uw,v+(yy-y)/height*vh);
        paint(xx,yy,tex,(color&255)/128.f,((color>>8)&255)/128.f,((color>>16)&255)/128.f,tex[3]/128.f*((color>>24)&255)/128.f,1);
    }
}
void platform_finish(void) {}
static void save(const char *path,const Game *g) {
    anchor_game=g;render_frame(&w,g,1);anchor_game=NULL;FILE *out=fopen(path,"wb");assert(out);fprintf(out,"P6\n512 448\n255\n");fwrite(pixels,1,sizeof(pixels),out);fclose(out);printf("Rendered %s: %u triangles\n",path,drawn);
}
static Input press(unsigned bit) {Input input={0};input.connected=1;input.pressed=bit;input.held=bit;return input;}
static void tick(Game *g,Input input,float seconds) {for(int i=0;i<(int)(seconds*60);i++){game_update(g,&w,&input,1/60.f);input.pressed=0;}}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    assert(argc>=2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);unsigned char *blob=malloc(n);assert(fread(blob,1,n,f)==n);fclose(f);
    assert(world_open(&w,blob,n));assert(!world_open(&(World){0},blob,n-1));
    if(argc>=3&&!strcmp(argv[2],"--collision-digest")) {
        Game sample;game_init(&sample);game_start(&sample,&w);uint32_t seed=2013,hash=2166136261u;unsigned rays=0;
        for(unsigned i=0;i<4096;i++) {
            seed=seed*1664525u+1013904223u;const Mesh *m=w.mesh+seed%w.header->meshes;
            if(!(m->flags&M_COLLIDE))continue;
            float x=(m->min[0]+m->max[0])*.5f,z=(m->min[2]+m->max[2])*.5f,y=(m->min[1]+m->max[1])*.5f;
            Vec3 a={x-5,y,z+(int)(seed%9)-4},b={x+5,y+.2f,z-(int)(seed%9)+4};
            hash=(hash^world_occluded(&w,&sample,a,b))*16777619u;rays++;
        }
        for(unsigned route=0;route<10;route++) {
            game_start(&sample,&w);const float *p=w.config+C_CUSTARD+route*3;
            sample.feet=(Vec3){p[0]-4,world_height(&w,p[0]-4,p[2]-5),p[2]-5};sample.next_scare=10000;
            for(int frame=0;frame<480;frame++) {
                Input in={.connected=1,.move_y=.85f,.move_x=.3f*sinf(frame*.03f),.look_x=.2f,.pressed=frame%120==0?B_JUMP:0};
                game_update(&sample,&w,&in,1/60.f);
                uint32_t raw[3];memcpy(raw,&sample.feet,sizeof(raw));for(int k=0;k<3;k++)hash=(hash^raw[k])*16777619u;
            }
        }
        printf("COLLISION DIGEST rays=%u frames=4800 hash=%08x\n",rays,hash);free(blob);return 0;
    }
    { // A saddle must follow the diagonal triangles, not a bilinear surface.
        uint16_t heights[9]={0,0,0,0,0,0,0,0,100};float config[80]={0};
        config[C_SCALE]=config[C_SCALE+1]=config[C_SCALE+2]=1;
        WorldHeader h={.width=3,.height=3};World surface={.header=&h,.height=heights,.config=config};
        assert(fabsf(terrain_surface_height(&surface,2,1.5f,.5f)-25)<.001f);
        assert(fabsf(terrain_surface_height(&surface,2,.5f,1.5f)-25)<.001f);
        assert(fabsf(terrain_surface_height(&surface,2,1,1)-50)<.001f);
    }
    { // A narrow painted path can be absent from all four coarse corners.
        uint8_t splat[8*8*4]={0};WorldHeader h={.width=9,.height=9,.version=2};World surface={.header=&h,.splat=splat};
        for(int i=0;i<64;i++)splat[i*4]=255;
        assert(terrain_paint_step(&surface,0,0,4)==4);
        for(int z=0;z<5;z++){splat[(z*8+2)*4]=0;splat[(z*8+2)*4+1]=255;}
        assert(terrain_paint_step(&surface,0,0,4)==1);
        printf("PASS: narrow source paths trigger fine paint sampling\n");
    }
    { // Local UVs retain source repeat phase at vertices and inside triangles.
        const float repeats[4]={3,5,4,7};float largest=0;unsigned samples=0;
        for(int layer=0;layer<4;layer++)for(int z=0;z<512;z+=4)for(int x=0;x<512;x+=4) {
            float uv[4][2];terrain_uv_quad(uv,x,z,4,w.config[C_SCALE],w.config[C_SCALE+2],repeats[layer]);
            float origin[2]={x*w.config[C_SCALE]/repeats[layer],-z*w.config[C_SCALE+2]/repeats[layer]};
            for(int axis=0;axis<2;axis++) {
                float shift=origin[axis]-uv[0][axis];assert(fabsf(shift-roundf(shift))<.0001f);
                for(int i=0;i<4;i++){assert(uv[i][axis]>=-.0001f&&uv[i][axis]<4);largest=fmaxf(largest,uv[i][axis]);}
            }
            for(int i=0;i<=4;i++) {
                float t=i*.25f;
                float source_u=(x+t*4)*w.config[C_SCALE]/repeats[layer];
                float source_v=-(z+t*4)*w.config[C_SCALE+2]/repeats[layer];
                float du=source_u-(uv[0][0]*(1-t)+uv[2][0]*t),dv=source_v-(uv[0][1]*(1-t)+uv[2][1]*t);
                assert(fabsf(du-roundf(du))<.0001f&&fabsf(dv-roundf(dv))<.0001f);samples++;
            }
        }
        printf("PASS: %u terrain UV samples preserve repeat phase, local maximum %.4f\n",samples,largest);
    }
    assert(w.header->sounds==9&&w.sound[4].samples>22050*9&&w.sound[4].offset==w.sound[5].offset);
    assert(w.sound[7].samples<22050);
    assert((w.sound[2].samples&AUDIO_COUNT)==22050*60&&source_environment_audio[1][6]==0);
    assert(w.sound[6].samples<22050&&w.sound[6].samples!=w.sound[8].samples);
    for(int id=0;id<9;id++) {
        const Sound *s=w.sound+id;unsigned count=s->samples&AUDIO_COUNT;int cursor=0;uint32_t crc=0xffffffffu;
        const uint8_t *raw=w.blob+s->offset;
        for(unsigned i=0;i<count;i++) {
            int16_t value=s->samples&AUDIO_ULAW?voice_ulaw_sample(raw,count,&cursor,0):voice_sample((const int16_t *)raw,count,&cursor,0);
            for(int byte=0;byte<2;byte++){crc^=((uint16_t)value>>(byte*8))&255;for(int bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}
        }
        assert((crc^0xffffffffu)==source_audio_crc32[id]&&cursor==-1);
    }
    printf("PASS: embedded source audio checksums and full ambient loop\n");
    unsigned tree_triangles[3]={0},view_triangles=0;
    for(unsigned i=0;i<w.header->meshes;i++) {
        const Mesh *m=w.mesh+i;
        if(m->flags&M_TREE){assert(m->id<15);if(m->id<3)tree_triangles[m->id]+=m->triangles;assert(fabsf(m->min[0])<20&&fabsf(m->min[2])<20);}
        if(m->flags&M_VIEW)view_triangles+=m->triangles;
    }
    assert(tree_triangles[0]==1044&&tree_triangles[1]==1544&&tree_triangles[2]==286);
    assert(view_triangles>=504);
    Game g;game_init(&g);
    for(unsigned i=0;i<MENU_ROUTE_COUNT;i++) {
        menu_fixture(&g,&w,menu_route[i].fixture);
        game_advance(&g,&w,&(Input){.pressed=menu_route[i].button},1/60.f);
        if(!menu_step_ok(&g,i))fprintf(stderr,"Menu route %u failed: state=%d page=%d selected=%d\n",i,g.state,g.menu_page,g.menu);
        assert(menu_step_ok(&g,i));tick(&g,(Input){0},.8f);
    }
    assert(g.quality==5);game_init(&g);game_update(&g,&w,&(Input){.pressed=B_CONFIRM},1/60.f);
    assert(g.state==ST_MENU&&g.menu_page==MENU_SINGLE);
    game_update(&g,&w,&(Input){.pressed=B_CONFIRM},1/60.f);assert(g.state==ST_PLAY&&g.collected==0);
    assert(fabsf(world_height(&w,w.config[C_CUSTARD],w.config[C_CUSTARD+2])-w.config[C_CUSTARD+1])<1);
    Input input=press(B_LIGHT);game_update(&g,&w,&input,1/60.f);assert(g.flashlight==0&&g.events==EV_SWITCH);game_update(&g,&w,&(Input){0},1/60.f);assert(g.flashlight==0);
    game_update(&g,&w,&(Input){.pressed=4096},1/60.f);assert(g.events==0&&g.flashlight==0);
    game_update(&g,&w,&(Input){.pressed=B_MAP},1/60.f);assert(g.map==1);
    input=press(B_PAUSE);game_update(&g,&w,&input,1/60.f);assert(g.state==ST_PAUSE);Vec3 before=g.enemy;tick(&g,(Input){0},2);assert(g.enemy.x==before.x&&g.enemy.z==before.z);
    game_update(&g,&w,&input,1/60.f);assert(g.state==ST_PLAY);
    game_start(&g,&w);input=press(B_JUMP);game_update(&g,&w,&input,1/60.f);assert(g.vy>0&&!g.grounded);tick(&g,(Input){0},2);assert(g.grounded);
    game_start(&g,&w);float oldz=g.feet.z;tick(&g,(Input){.move_y=1,.connected=1},1);assert(g.feet.z>oldz+1);
    Game fast=g,slow=g;Input forward={.move_y=1,.connected=1};
    tick(&fast,forward,1);for(int i=0;i<10;i++)game_advance(&slow,&w,&forward,.1f);
    assert(fabsf(fast.feet.z-slow.feet.z)<.01f&&fabsf(fast.time-slow.time)<.001f);
    for(int k=0;k<3;k++)assert(fabsf(fast.hoover.position[k]-slow.hoover.position[k])<.001f);
    { // The single source waypoint produces forward steering, not ping-pong.
        game_start(&g,&w);g.enemy=(Vec3){0,0,0};g.next_scare=10000;
        float last=0,turn=0,radius=0;
        for(int frame=0;frame<720;frame++) {
            game_advance(&g,&w,&(Input){0},1/60.f);
            float m[16];game_hoover_matrix(&w,&g,m);
            float angle=atan2f(m[8],m[10]);turn+=fabsf(remainderf(angle-last,6.2831853f));last=angle;
            if(frame>120)radius=fmaxf(radius,hypotf(g.hoover.position[0]-w.config[C_WAY],g.hoover.position[2]-w.config[C_WAY+2]));
        }
        assert(turn>6.2831853f&&radius<4&&g.hoover.speed==10);
        for(unsigned i=0;i<w.header->meshes;i++)if(w.mesh[i].flags&M_HOOVER) {
            const Vertex *v=(const Vertex *)(w.blob+w.mesh[i].vertex_offset);
            Vec3 p=game_mesh_vertex(&w,&g,w.mesh+i,v);float a=0,b=0;
            for(int k=0;k<3;k++){a+=powf((&v->x)[k]-w.config[C_HOOVER+k],2);b+=powf((&p.x)[k]-g.hoover.position[k],2);}
            assert(fabsf(a-b)<.002f);
        }
        printf("PASS: Noo-Noo source steering turns %.2f times, orbit %.3f m, model pivot preserved\n",turn/6.2831853f,radius);
    }
    game_start(&g,&w);game_advance(&g,&w,&(Input){.pressed=B_LIGHT|4096},.1f);
    assert(!g.flashlight&&g.events==EV_SWITCH);
    game_start(&g,&w);g.feet=(Vec3){w.config[C_DOOR],world_height(&w,w.config[C_DOOR],727),727};
    assert(world_occluded(&w,&g,(Vec3){g.feet.x,2,734},(Vec3){g.feet.x,2,736}));
    tick(&g,forward,2.5f);
    printf("House navigation: z=%.3f y=%.3f door=%.3f\n",g.feet.z,g.feet.y,g.door_open);
    assert(g.feet.z>741&&g.door_open>.99f);
    assert(!world_occluded(&w,&g,(Vec3){g.feet.x,2,734},(Vec3){g.feet.x,2,736}));
    tick(&g,(Input){.move_y=-1,.connected=1},2.5f);assert(g.feet.z<729);
    // Walk through the centre from several approaches, including the float32
    // triangle seams. A capsule must never drop through the house's flat floor.
    for(int route=0;route<24;route++) {
        game_start(&g,&w);float angle=route*6.2831853f/24;
        g.feet=(Vec3){502.25f-sinf(angle)*12,.8195f,759.4f-cosf(angle)*12};
        g.yaw=angle;g.enemy.z=0;g.next_scare=10000;
        for(int frame=0;frame<240;frame++) {
            game_advance(&g,&w,&forward,1/30.f);
            if(g.feet.y<.7f){fprintf(stderr,"HOUSE FALL route=%d frame=%d pos=%.6f,%.6f,%.6f\n",route,frame,g.feet.x,g.feet.y,g.feet.z);assert(g.feet.y>=.7f);}
        }
    }
    for(int route=0;route<8;route++) {
        game_start(&g,&w);float angle=route*6.2831853f/8;
        g.feet=(Vec3){502.25f-sinf(angle)*18,12,759.4f-cosf(angle)*18};
        g.grounded=0;g.yaw=angle;g.enemy.z=0;g.next_scare=10000;
        float lowest=100;int crossed=0;
        for(int frame=0;frame<240;frame++) {
            game_advance(&g,&w,&(Input){.connected=1,.move_y=frame>60?1:0},1/30.f);
            lowest=fminf(lowest,g.feet.y);
            float radius=hypotf(g.feet.x-source_roof_support[0],g.feet.z-source_roof_support[1]);
            if(radius<5){crossed=1;assert(g.feet.y>10.8f);}
            if(radius<18)assert(g.feet.y>6.5f);
        }
        printf("Roof route=%d min=%.3f end=%.3f,%.3f,%.3f\n",route,lowest,g.feet.x,g.feet.y,g.feet.z);
        assert(crossed);
        assert(hypotf(g.feet.x-source_roof_support[0],g.feet.z-source_roof_support[1])>12);
    }
    game_start(&g,&w);g.feet=(Vec3){150,world_height(&w,150,500),500};g.yaw=-1.5707963f;
    tick(&g,forward,3);assert(g.feet.x>=141.7f);
    game_start(&g,&w);g.enemy=(Vec3){g.feet.x,g.feet.y,g.feet.z+5};g.yaw=0;tick(&g,(Input){0},2.1f);assert(g.state==ST_DEAD);
    float death_yaw=g.yaw;tick(&g,(Input){.look_x=1},.5f);assert(g.state==ST_DEAD&&g.yaw>death_yaw+.5f);tick(&g,(Input){0},5);assert(g.state==ST_MENU);
    game_start(&g,&w);g.enemy=(Vec3){g.feet.x,g.feet.y,g.feet.z-20};float ez=g.enemy.z;tick(&g,(Input){0},1);assert(g.enemy.z>ez+4);
    game_start(&g,&w);g.next_scare=10000;
    g.enemy=(Vec3){g.feet.x,g.feet.y+.675f,g.feet.z+4.5f};
    game_update(&g,&w,&(Input){0},1/60.f);assert(g.enemy_visible&&(g.events&EV_ENEMY_SEEN)&&(g.events&EV_ENEMY_SHIFT));
    const Sound *cry=w.sound+4;const int16_t *pcm=(const int16_t *)(w.blob+cry->offset);int voice=-1;
    assert(voice_start_once(&voice,cry->samples));
    for(unsigned i=0;i<100;i++)assert(voice_sample(pcm,cry->samples,&voice,0)==pcm[i]);
    Vec3 seen_enemy=g.enemy;tick(&g,(Input){0},.1f);assert(g.enemy.x==seen_enemy.x&&g.enemy.z==seen_enemy.z&&!(g.events&EV_ENEMY_SEEN));
    g.yaw=-1.5707963f;game_update(&g,&w,&(Input){0},1/60.f);
    assert(!g.enemy_visible&&!(g.events&EV_ENEMY_SHIFT));
    assert(hypotf(g.enemy.x-g.feet.x,g.enemy.z-g.feet.z)>=w.config[C_FOLLOW]);
    g.yaw=atan2f(g.enemy.x-g.feet.x,g.enemy.z-g.feet.z);game_update(&g,&w,&(Input){0},1/60.f);
    assert(g.enemy_visible&&(g.events&EV_ENEMY_SEEN));
    assert(!voice_start_once(&voice,cry->samples)&&voice==100);
    for(unsigned i=100;i<cry->samples;i++)assert(voice_sample(pcm,cry->samples,&voice,0)==pcm[i]);
    assert(voice==-1&&voice_sample(pcm,cry->samples,&voice,0)==0);
    assert(!voice_start_ready(&voice,cry->samples,99,100)&&voice==-1);
    assert(voice_start_ready(&voice,cry->samples,100,100)&&voice==0);voice=-1;
    game_update(&g,&w,&(Input){0},1/60.f);assert(!(g.events&EV_ENEMY_SEEN));
    g.yaw+=3.1415927f;game_update(&g,&w,&(Input){0},1/60.f);assert(!(g.events&EV_ENEMY_SHIFT));
    g.yaw=atan2f(g.enemy.x-g.feet.x,g.enemy.z-g.feet.z);game_update(&g,&w,&(Input){0},1/60.f);
    assert((g.events&EV_ENEMY_SEEN)&&voice_start_once(&voice,cry->samples)&&voice==0);
    g.next_enemy_shift=0;game_update(&g,&w,&(Input){0},1/60.f);assert(g.events&EV_ENEMY_SHIFT);
    game_start(&g,&w);g.enemy=(Vec3){g.feet.x,g.feet.y+.675f,g.feet.z+1.5f};g.next_enemy_shift=10000;g.yaw=0;g.next_scare=10000;
    float enemy_z=g.enemy.z;tick(&g,forward,.15f);assert(g.feet.z<enemy_z-source_enemy_capsule[3]-.35f);
    tick(&g,(Input){.move_y=-1,.connected=1},.2f);assert(g.feet.z<enemy_z-1.5f);
    for(int i=0;i<4;i++) {
        float distances[4]={5.5f,8,15,25};game_start(&g,&w);g.yaw=0;
        g.enemy=(Vec3){g.feet.x,g.feet.y+.675f,g.feet.z+distances[i]};Vec3 before=g.enemy;
        game_update(&g,&w,&(Input){0},1/60.f);
        assert((g.events&EV_ENEMY_SEEN)&&!(g.events&EV_ENEMY_SHIFT)&&g.health==100&&!g.enemy_attacking);
        assert(hypotf(g.enemy.x-before.x,g.enemy.z-before.z)<.09f);
    }
    { // LookAt must aim upward as well as turn, without stretching the mesh.
        game_start(&g,&w);g.enemy=(Vec3){g.feet.x,g.feet.y+.675f,g.feet.z};
        for(int i=0;i<8;i++) {
            float angle=i*.7853982f;g.feet.x=g.enemy.x+sinf(angle)*.1f;g.feet.z=g.enemy.z+cosf(angle)*.1f;g.feet.y=g.enemy.y+2;
            float pose[16];game_enemy_matrix(&w,&g,pose);
            assert(pose[9]>.998f&&fabsf(pose[0]*pose[4]+pose[2]*pose[6])<.00001f);
            assert(fabsf(pose[4]*pose[4]+pose[5]*pose[5]+pose[6]*pose[6]-1)<.00001f);
        }
        // Descend onto the tilted capsule. It remains solid under the player.
        g.feet.x=g.enemy.x;g.feet.z=g.enemy.z;g.feet.y=g.enemy.y+.7f;g.pitch=0;g.grounded=0;
        g.next_enemy_shift=10000;g.next_scare=10000;g.enemy_visible=0;
        float height=g.enemy.y;tick(&g,(Input){0},.6f);
        assert(g.grounded&&g.feet.y>height+.45f&&g.feet.y<height+.6f);
        printf("PASS: distant sightings do not hurt or teleport; 3D enemy aim and tilted capsule support\n");
    }
    game_start(&g,&w);g.enemy=(Vec3){g.feet.x,g.feet.y+.675f,g.feet.z+4};
    World enclosed=w;WorldHeader enclosed_header=*w.header;enclosed_header.boxes=1;enclosed.header=&enclosed_header;
    Box enclosure={{g.feet.x-8,g.feet.y-1,g.feet.z-8},{g.feet.x+8,g.feet.y+4,g.feet.z+8}};enclosed.box=&enclosure;
    g.next_enemy_shift=0;g.yaw+=3.1415927f;seen_enemy=g.enemy;game_update(&g,&enclosed,&(Input){0},1/60.f);
    assert(!(g.events&EV_ENEMY_SHIFT)&&hypotf(g.enemy.x-seen_enemy.x,g.enemy.z-seen_enemy.z)<.1f);
    printf("PASS: visible encounter teleports, shift cooldown/blockers, enemy collision, one-shot scream and later replay\n");
    for(int kind=0;kind<3;kind++) {
        const Tree *tree=NULL;for(unsigned i=0;i<w.header->trees;i++)if((int)w.tree[i].type==kind){tree=w.tree+i;break;}
        assert(tree);const float *c=source_tree_capsules[kind];game_start(&g,&w);
        float x=tree->x+c[0]*tree->width,z=tree->z+c[2]*tree->width;
        g.feet=(Vec3){x,world_height(&w,x,z-3),z-3};g.yaw=0;g.enemy.z=0;
        tick(&g,forward,1);
        assert(g.feet.z<z-c[3]*tree->width-.35f);
        assert(world_occluded(&w,&g,(Vec3){x,tree->y+1,z-2},(Vec3){x,tree->y+1,z+2}));
    }
    game_start(&g,&w);
    {const float *p=w.config+C_CUSTARD;g.feet=(Vec3){p[0],world_height(&w,p[0],p[2]),p[2]};game_update(&g,&w,&(Input){0},1/60.f);assert(g.pickup_time==3);tick(&g,(Input){0},3.1f);assert(g.pickup_time==0);}
    game_start(&g,&w);
    for(int i=0;i<10;i++) {const float *p=w.config+C_CUSTARD+i*3;g.feet=(Vec3){p[0],world_height(&w,p[0],p[2]),p[2]};game_update(&g,&w,&(Input){0},1/60.f);assert(g.collected==i+1);if(i==4)assert(g.enemy_speed==w.config[C_FAST]);if(i==7)assert(g.enemy_speed==w.config[C_SPRINT]);}
    assert(g.state==ST_WON);game_update(&g,&w,&(Input){.pressed=B_CONFIRM},.05f);assert(g.state==ST_WON&&g.collected==10);
    tick(&g,(Input){0},4.8f);assert(g.state==ST_WON);
    tick(&g,(Input){0},.3f);assert(g.state==ST_WINSCENE);
    tick(&g,(Input){0},12);assert(g.state==ST_WINSCENE);
    game_update(&g,&w,&(Input){.pressed=B_CONFIRM},1/60.f);assert(g.state==ST_MENU&&g.menu_page==MENU_MAIN);
    printf("PASS: asset bounds, terrain orientation, menu, flashlight, retired R2 ignored, map, pause, jump, movement, frame timing, house entrance/exit, doors, boundary collision, visible damage, off-screen chase, pickups, AI speed, victory\n");
    if(argc>2) {
        FILE *mf=fopen("ps2/generated/menu/world.bin","rb");assert(mf);fseek(mf,0,SEEK_END);size_t mn=ftell(mf);rewind(mf);
        unsigned char *mb=malloc(mn);assert(fread(mb,1,mn,mf)==mn);fclose(mf);World menu_world;assert(world_open(&menu_world,mb,mn));render_set_menu_world(&menu_world);
        FILE *vf=fopen("ps2/generated/victory/world.bin","rb");assert(vf);fseek(vf,0,SEEK_END);size_t vn=ftell(vf);rewind(vf);
        unsigned char *vb=malloc(vn);assert(fread(vb,1,vn,vf)==vn);fclose(vf);World victory_world;assert(world_open(&victory_world,vb,vn));render_set_victory_world(&victory_world);
        FILE *af=fopen("ps2/generated/player_walk.bin","rb");assert(af);fseek(af,0,SEEK_END);size_t an=ftell(af);rewind(af);
        unsigned char *ab=malloc(an);assert(fread(ab,1,an,af)==an);fclose(af);assert(render_set_view_animation(ab,an));
        char path[1024];game_init(&g);snprintf(path,sizeof(path),"%s/menu.ppm",argv[2]);save(path,&g);
        unsigned char fire_before[80][60][3];
        for(int y=0;y<80;y++)memcpy(fire_before[y],pixels[340+y]+195,sizeof(fire_before[y]));
        tick(&g,(Input){.connected=1},1);
        snprintf(path,sizeof(path),"%s/menu_fire_later.ppm",argv[2]);save(path,&g);
        unsigned changed=0;
        for(int y=0;y<80;y++)for(int x=0;x<60;x++)
            if(memcmp(fire_before[y][x],pixels[340+y][195+x],3))changed++;
        assert(changed>100);
        printf("PASS: menu fire animates without input (%u changed pixels)\n",changed);
        for(int i=1;i<6;i++){g.menu_page=i;snprintf(path,sizeof(path),"%s/menu_page_%d.ppm",argv[2],i);save(path,&g);}
        g.state=ST_HELP;snprintf(path,sizeof(path),"%s/menu_help.ppm",argv[2]);save(path,&g);
        g.state=ST_CREDITS;snprintf(path,sizeof(path),"%s/menu_credits.ppm",argv[2]);save(path,&g);
        game_start(&g,&w);g.time_of_day=0;snprintf(path,sizeof(path),"%s/spawn_day.ppm",argv[2]);save(path,&g);
        g.state_time=6;g.pitch=-.5f;snprintf(path,sizeof(path),"%s/hand_flashlight.ppm",argv[2]);save(path,&g);
        g.pitch=-1;snprintf(path,sizeof(path),"%s/body_flashlight.ppm",argv[2]);save(path,&g);g.pitch=0;
        for(int angle=0;angle<2;angle++) {
            const Tree *t=w.tree+1;g.feet=(Vec3){t->x+(angle?10:0),t->y,t->z-(angle?0:10)};
            g.yaw=angle?-1.5707963f:0;g.pitch=.18f;
            snprintf(path,sizeof(path),"%s/tree_3d_%d.ppm",argv[2],angle);save(path,&g);
        }
        g.pitch=0;
        g.feet=(Vec3){w.config[C_DOOR],world_height(&w,w.config[C_DOOR],w.config[C_DOOR+2]),w.config[C_DOOR+2]-18};g.yaw=0;snprintf(path,sizeof(path),"%s/house_day.ppm",argv[2]);save(path,&g);
        g.time_of_day=2;g.feet=(Vec3){w.config[C_SPAWN],world_height(&w,w.config[C_SPAWN],w.config[C_SPAWN+2]),w.config[C_SPAWN+2]};g.yaw=0;snprintf(path,sizeof(path),"%s/spawn_night.ppm",argv[2]);save(path,&g);
        g.enemy=(Vec3){g.feet.x,world_height(&w,g.feet.x,g.feet.z+7)+.675f,g.feet.z+7};snprintf(path,sizeof(path),"%s/enemy.ppm",argv[2]);save(path,&g);
        game_start(&g,&w);g.time_of_day=0;g.state_time=6;
        const float *p=w.config+C_CUSTARD+27;g.feet=(Vec3){p[0],world_height(&w,p[0],p[2]-8),p[2]-8};g.yaw=0;
        snprintf(path,sizeof(path),"%s/custard_tree.ppm",argv[2]);save(path,&g);
        g.collected=1;g.pickup_time=2;snprintf(path,sizeof(path),"%s/pickup_notice.ppm",argv[2]);save(path,&g);
        g.state=ST_DEAD;g.pickup_time=0;snprintf(path,sizeof(path),"%s/death_scene.ppm",argv[2]);save(path,&g);
        g.state=ST_PLAY;g.feet.x+=20;g.feet.y=world_height(&w,g.feet.x,g.feet.z);g.pitch=-.35f;
        snprintf(path,sizeof(path),"%s/grass_close.ppm",argv[2]);save(path,&g);
        g.feet=(Vec3){264.22705f,world_height(&w,264.22705f,323.30664f),323.30664f};g.pitch=-.15f;g.yaw=0;
        snprintf(path,sizeof(path),"%s/grass_slope.ppm",argv[2]);save(path,&g);
        assert(grass_roots>400&&grass_root_gap<.005f);
        printf("PASS: %u projected grass roots touch the visible terrain (maximum error %.6f m)\n",grass_roots,grass_root_gap);
        g.feet=(Vec3){p[0]+20,world_height(&w,p[0]+20,p[2]-8),p[2]-8};g.pitch=-.35f;
        g.state=ST_WON;g.state_time=1;g.collected=10;g.custards=1023;
        snprintf(path,sizeof(path),"%s/win_notice.ppm",argv[2]);save(path,&g);
        g.state=ST_WINSCENE;snprintf(path,sizeof(path),"%s/win_scene.ppm",argv[2]);save(path,&g);
        free(mb);free(vb);free(ab);
    }
    free(blob);return 0;
}
