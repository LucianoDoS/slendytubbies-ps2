#include "game.h"
#include "render.h"
#include "audio_voice.h"
#include "../tests/menu_route.h"
#include "../generated/fidelity.h"
#include "../tests/benchmark.h"
#include "../generated/scene_visuals.h"
#include "../generated/scene_audio.h"
#include <gsKit.h>
#include <gsInline.h>
#include <dmaKit.h>
#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <libpad.h>
#include <audsrv.h>
#include <timer.h>
#include <math.h>
#include "../tests/walk_route.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>

extern unsigned char world_start[],world_end[];
extern unsigned char menu_world_start[],menu_world_end[];
extern unsigned char victory_world_start[],victory_world_end[];
extern unsigned char player_walk_start[],player_walk_end[];
extern unsigned char sio2_start[],sio2_end[],pad_start[],pad_end[],audsrv_start[],audsrv_end[];
static GSGLOBAL *gs;static GSTEXTURE textures[128];static World world,menu_world,victory_world;static Game game;
static unsigned char texture_bits[128][2];
typedef struct {u64 rgbaq,st,xyz;} PackedPoint;
static PackedPoint batch[1024] __attribute__((aligned(64)));
static int batch_count,batch_texture=-1,notice_texture=-1;
static PackedPoint packed_cache[2048] __attribute__((aligned(64)));
static unsigned packed_stamp[2048],packed_serial;
static GSQUEUE menu_cache;static int menu_cache_ready,menu_cache_key=-1,blend_mode;
static char pad_buffer[256] __attribute__((aligned(64)));
static uint32_t previous;static int analog_requested;
static int audio_ready;static int sound_pos[9];
static int enemy_test_enabled,enemy_voice_starts,enemy_voice_ignored,enemy_voice_finished,enemy_side_shifts;
static u64 nearby_audible_until;static int nearby_pending;
static int profile_enabled;
static int menu_music_pos,was_menu_audio=-1;
static int16_t audio_chunk[512] __attribute__((aligned(64)));
static void audio_pump(void);
static void upload_texture(GSTEXTURE *texture) {
    // The standalone gsKit uploader allocates a temporary packet as large as
    // the residual image block. Use references in our existing GS queue so
    // scene changes do not need another 64+ KiB of contiguous EE heap.
    gsKit_setup_tbw(texture);
    gsKit_texture_send_inline(gs,texture->Mem,texture->Width,texture->Height,
                             texture->Vram,texture->PSM,texture->TBW,GS_CLUT_NONE);
}
static u64 work_begin[2],work_total[2];
void platform_work(int kind,int begin) {
    if(!profile_enabled)return;
    u64 now=GetTimerSystemTime();if(begin)work_begin[kind]=now;else work_total[kind]+=now-work_begin[kind];
}
void platform_cache_reset(void) {
    if(++packed_serial==0){memset(packed_stamp,0,sizeof(packed_stamp));packed_serial=1;}
}
void platform_profile(int phase) {
    static u64 last,total[6];static unsigned frames;
    if(!profile_enabled)return;
    u64 now=GetTimerSystemTime();
    if(phase==0){last=now;return;}
    total[phase]+=now-last;last=now;
    if(phase==5&&++frames==60) {
        float scale=1000.f/(kBUSCLK*60.f);
        printf("STPS PROFILE state=%d terrain=%.2f meshes=%.2f trees=%.2f ui=%.2f gs=%.2f ms\n",game.state,
            total[1]*scale,total[2]*scale,total[3]*scale,total[4]*scale,total[5]*scale);
        printf("STPS WORK vertices=%.2f triangles=%.2f ms\n",work_total[0]*scale,work_total[1]*scale);
        memset(work_total,0,sizeof(work_total));
        memset(total,0,sizeof(total));frames=0;
    }
}

static void flush(void) {
    // A menu frame may outlast audsrv's small IOP ring buffer. Feed it during
    // rendering as well, without waiting for the next gameplay update.
    audio_pump();
    if(batch_count) {
        GSTEXTURE *t=textures+batch_texture;int tw=texture_bits[batch_texture][0],th=texture_bits[batch_texture][1];
        gsKit_set_texfilter(gs,t->Filter);
        // REGLIST carries the same ST/Q, color and depth in half the A+D bytes.
        unsigned payload=(batch_count*3+1)/2,qsize=3+payload;
        u64 *p=gsKit_heap_alloc(gs,qsize,qsize*16,GIF_AD);
        *p++=GIF_TAG_AD(2);*p++=GIF_AD;
        // Texture alpha is needed by alpha testing even when blending is off.
        *p++=GS_SETREG_TEX0(t->Vram/256,t->TBW,t->PSM,tw,th,1,0,0,0,0,0,GS_CLUT_STOREMODE_NOLOAD);
        *p++=GS_TEX0_1+gs->PrimContext;
        *p++=GS_SETREG_PRIM(GS_PRIM_PRIM_TRIANGLE,1,1,gs->PrimFogEnable,gs->PrimAlphaEnable,gs->PrimAAEnable,0,gs->PrimContext,0);*p++=GS_PRIM;
        *p++=GIF_TAG(batch_count,1,0,0,GSKIT_GIF_FLG_REGLIST,3);
        *p++=GS_RGBAQ|(GS_ST<<4)|(GS_XYZF2<<8);
        memcpy(p,batch,batch_count*sizeof(*batch));
        if(batch_count&1)p[batch_count*3]=0;
    }
    batch_count=0;
}
void platform_blend(int mode) {
    flush();blend_mode=mode;gsKit_set_primalpha(gs,mode==1||mode==3?GS_SETREG_ALPHA(0,2,0,1,0):GS_SETREG_ALPHA(0,1,0,1,0),0);
    u64 *packet=gsKit_heap_alloc(gs,1,16,GIF_AD);
    packet[0]=GIF_TAG_AD(1);packet[1]=GIF_AD;
    packet[2]=GS_SETREG_ZBUF(gs->ZBuffer/8192,gs->PSMZ&15,mode!=0);packet[3]=GS_ZBUF_1+gs->PrimContext;
}
void platform_cutout(int enabled) {
    static int active;
    if(active==enabled)return;
    flush();active=enabled;
    // Unity foliage uses an opaque cutout. Low-alpha pixels must not write
    // depth or blend the white fog over branches that are behind them.
    gs->PrimAlphaEnable=enabled?GS_SETTING_OFF:GS_SETTING_ON;
    gs->Test->AREF=enabled?64:1;
    gsKit_set_test(gs,GS_ATEST_ON);
}
void platform_fog(uint8_t r,uint8_t g,uint8_t b) {
    flush();gs->PrimFogEnable=1;
    u64 *p=gsKit_heap_alloc(gs,1,16,GIF_AD);p[0]=GIF_TAG_AD(1);p[1]=GIF_AD;
    p[2]=(u64)r|((u64)g<<8)|((u64)b<<16);p[3]=GS_FOGCOL;
}
int platform_static_begin(int key) {
    flush();gsKit_queue_exec_real(gs,gs->Os_Queue);
    if(!menu_cache_ready){gsKit_queue_init(gs,&menu_cache,GS_PERSISTENT,1536*1024);menu_cache_ready=1;}
    gsKit_queue_set(gs,&menu_cache);
    if(key==menu_cache_key){gsKit_queue_exec_real(gs,&menu_cache);gsKit_queue_set(gs,gs->Os_Queue);return 0;}
    gsKit_queue_reset(&menu_cache);menu_cache_key=key;return 1;
}
void platform_static_end(void) {
    flush();
    printf("STPS CACHE key=%d bytes=%lu capacity=%lu\n",menu_cache_key,
        (unsigned long)((u8 *)menu_cache.pool_cur-(u8 *)menu_cache.pool[menu_cache.dbuf]),
        (unsigned long)((u8 *)menu_cache.pool_max[menu_cache.dbuf]-(u8 *)menu_cache.pool[menu_cache.dbuf]));
    gsKit_queue_exec_real(gs,&menu_cache);gsKit_queue_set(gs,gs->Os_Queue);
}
void platform_world(const World *w,int bank) {
    static const World *resident;static int resident_bank=-1;
    if(w==resident&&bank==resident_bank)return;
    menu_cache_key=-1;
    notice_texture=-1;
    dmaKit_wait_fast();gsKit_vram_clear(gs);
    for(uint32_t i=0;i<w->header->textures;i++) {
        if(w==&world) {
            // This title belongs to the old flat menu. The actual menu has
            // its own asset bank; release its 256 KiB for source foliage.
            if(i==(unsigned)w->config[C_TITLE])continue;
            int pickup=0;for(int p=1;p<9;p++)if(i==(unsigned)source_pickups[p][0])pickup=1;
            if(pickup)continue;
            int sky=0,current=0;
            for(int day=0;day<3;day++)for(int face=0;face<6;face++)if(i==scene_sky[day][face]){sky=1;if(day==bank-1)current=1;}
            if(sky&&!current)continue;
        }
        if(w==&menu_world) {
            int help=(int)w->config[86],credits=(int)w->config[87],font=(int)w->config[C_FONT];
            if(bank==0&&(i==(unsigned)help||i==(unsigned)credits))continue;
            if(bank&&i!=0&&i!=(unsigned)font&&i!=(unsigned)(bank==1?help:credits))continue;
        }
        const Texture *d=w->texture+i;GSTEXTURE *t=textures+i;memset(t,0,sizeof(*t));t->Width=d->width;t->Height=d->height;
        for(int axis=0;axis<2;axis++){unsigned n=axis?d->height:d->width,bits=0;while((1u<<bits)<n)bits++;texture_bits[i][axis]=bits;}
        t->PSM=GS_PSM_CT32;t->Filter=GS_FILTER_LINEAR;t->Mem=(u32 *)(w->blob+d->offset);
        t->Vram=gsKit_vram_alloc(gs,gsKit_texture_size(t->Width,t->Height,t->PSM),GSKIT_ALLOC_USERBUFFER);
        if(t->Vram==GSKIT_ALLOC_ERROR){printf("STPS VRAM ERROR texture=%lu bank=%d\n",(unsigned long)i,bank);SleepThread();}
        upload_texture(t);
    }
    if(w==&world)for(int p=1;p<9;p++) {
        unsigned id=(unsigned)source_pickups[p][0];textures[id]=textures[(unsigned)source_pickups[0][0]];
        textures[id].Mem=(u32 *)(w->blob+w->texture[id].offset);
        texture_bits[id][0]=texture_bits[(unsigned)source_pickups[0][0]][0];texture_bits[id][1]=texture_bits[(unsigned)source_pickups[0][0]][1];
    }
    gsKit_queue_exec(gs);dmaKit_wait_fast();
    resident=w;resident_bank=bank;
    printf("STPS TEXTURES scene=%s bank=%d vram=%lu\n",w==&menu_world?"menu":w==&victory_world?"victory":"game",bank,(unsigned long)gs->CurrentPointer);
}
void platform_clear(uint8_t r,uint8_t g,uint8_t b) {
    flush();
    // Clearing must overwrite both buffers regardless of transparent UI state.
    gs->PrimAlphaEnable=GS_SETTING_OFF;
    gs->PrimFogEnable=0;
    gsKit_clear(gs,GS_SETREG_RGBAQ(r,g,b,128,0));
    gs->PrimAlphaEnable=GS_SETTING_ON;
    gsKit_set_primalpha(gs,GS_SETREG_ALPHA(0,1,0,1,0),0);
    gsKit_set_test(gs,GS_ZTEST_ON);
}
void platform_triangle(int tex,const DrawVertex *v) {
    if(batch_texture!=tex||batch_count+3>1023) {flush();batch_texture=tex;}
    for(int i=0;i<3;i++) {
        PackedPoint *p=batch+batch_count++;unsigned key=v[i].cache_key&65535;
        if(key&&key<=2048&&packed_stamp[key-1]==packed_serial){*p=packed_cache[key-1];continue;}
        float q=v[i].q,tq=blend_mode==3?1:q;
        union {u64 raw;struct{uint32_t rgba;float q;} value;} color;
        union {u64 raw;struct{float s,t;} value;} uv;
        memcpy(&color.value.rgba,&v[i].r,4);color.value.q=tq;
        uv.value.s=v[i].u*tq;uv.value.t=v[i].v*tq;p->rgbaq=color.raw;p->st=uv.raw;
        unsigned x=gsKit_float_to_int_x(gs,v[i].x),y=gsKit_float_to_int_y(gs,v[i].y*gs->Height/448.f);
        uint32_t depth=(int)(.15f*q*65534);
        // Clipped light triangles need a small GS depth bias after XYZ quantization.
        if(blend_mode==3||blend_mode==4)depth=depth>65531?65535:depth+4;
        p->xyz=(u64)x|((u64)y<<16)|((u64)depth<<32)|((u64)(v[i].cache_key>>24)<<56);
        if(key&&key<=2048){packed_cache[key-1]=*p;packed_stamp[key-1]=packed_serial;}
    }
}
void platform_sprite(int tex,float x,float y,float width,float height,float u,float v,float uw,float vh,uint32_t color) {
    flush();float ys=gs->Height/448.f;GSTEXTURE *t=textures+tex;gs->PrimFogEnable=0;
    // All nine notices reuse one VRAM slot. Upload only the active notice.
    if(game.state==ST_PLAY||game.state==ST_PAUSE||game.state==ST_DEAD)for(int p=0;p<9;p++) {
        if(tex==(int)source_pickups[p][0]&&notice_texture!=tex){upload_texture(t);notice_texture=tex;break;}
    }
    gsKit_prim_sprite_texture(gs,t,x,y*ys,u*t->Width,v*t->Height,(x+width),(y+height)*ys,(u+uw)*t->Width,(v+vh)*t->Height,65535,
        GS_SETREG_RGBAQ(color&255,(color>>8)&255,(color>>16)&255,(color>>24)&255,0));gs->PrimFogEnable=1;
}
void platform_finish(void) {flush();gsKit_queue_exec(gs);gsKit_sync_flip(gs);}
static int module(void *start,void *end,const char *name) {
    int result=0,id=SifExecModuleBuffer(start,(unsigned char *)end-(unsigned char *)start,0,NULL,&result);
    printf("STPS MODULE %s id=%d result=%d\n",name,id,result);return id>=0&&result>=0;
}
static float stick(unsigned char byte) {
    float x=((int)byte-128)/127.f;if(fabsf(x)<.18f) return 0;
    return (x>0?1:-1)*(fabsf(x)-.18f)/.82f;
}
static Input read_input(void) {
    Input in={0};int state=padGetState(0,0);
    if(state==PAD_STATE_DISCONN) {previous=0;analog_requested=0;return in;}
    if(state!=PAD_STATE_STABLE&&state!=PAD_STATE_FINDCTP1) return in;
    if(!analog_requested) {padSetMainMode(0,0,PAD_MMODE_DUALSHOCK,PAD_MMODE_LOCK);analog_requested=1;return in;}
    struct padButtonStatus data;if(!padRead(0,0,&data)) return in;in.connected=1;
    uint32_t raw=0xffff^data.btns,b=0;
    if(raw&PAD_CROSS) b|=B_JUMP|B_CONFIRM;
    if(raw&PAD_CIRCLE) b|=B_CROUCH|B_BACK;
    if(raw&PAD_R1) b|=B_LIGHT;
    if(raw&(PAD_L1|PAD_L3)) b|=B_RUN;
    if(raw&PAD_TRIANGLE) b|=B_MAP;
    if(raw&PAD_START) b|=B_PAUSE;
    if(raw&PAD_SELECT) b|=B_BACK;
    if(raw&PAD_UP) b|=B_UP;
    if(raw&PAD_DOWN) b|=B_DOWN;
    if(raw&PAD_LEFT) b|=B_LEFT;
    if(raw&PAD_RIGHT) b|=B_RIGHT;
    in.held=b;in.pressed=b&~previous;previous=b;
    // Digital-only pads do not populate the four analog byte fields.
    if((data.mode>>4)==7) {in.move_x=stick(data.ljoy_h);in.move_y=-stick(data.ljoy_v);in.look_x=stick(data.rjoy_h);in.look_y=stick(data.rjoy_v);}
    if(raw&PAD_UP) in.move_y=1;
    if(raw&PAD_DOWN) in.move_y=-1;
    if(raw&PAD_LEFT) in.move_x=-1;
    if(raw&PAD_RIGHT) in.move_x=1;
    return in;
}
static int sample_world(const World *w,int id,int *cursor,int loop,float volume) {
    if(id<0||(unsigned)id>=w->header->sounds)return 0;
    const Sound *s=w->sound+id;const int16_t *pcm=(const int16_t *)(w->blob+s->offset);
    if(s->samples&AUDIO_ULAW)return (int)(voice_ulaw_sample((const uint8_t *)pcm,s->samples&AUDIO_COUNT,cursor,loop)*volume);
    return (int)(voice_sample(pcm,s->samples,cursor,loop)*volume);
}
static int sample(int id,int loop,float volume) {
    return sample_world(&world,id,sound_pos+id,loop,volume);
}
static void audio_pump(void) {
    if(!audio_ready||!world.header||!menu_world.header) return;
    static u64 last_pump;
    u64 now=GetTimerSystemTime();
    if(now-last_pump<kBUSCLK/125)return;
    last_pump=now;
    int menu_audio=game.state==ST_MENU||game.state==ST_HELP||game.state==ST_CREDITS||game.state==ST_NETWORK;
    if(menu_audio!=was_menu_audio) {
        if(menu_audio) {menu_music_pos=0;for(int i=4;i<9;i++)sound_pos[i]=-1;nearby_audible_until=0;nearby_pending=0;}
        else if(game.state==ST_PLAY)for(int i=0;i<4;i++)sound_pos[i]=0;
        printf("STPS MUSIC %s position=%d samples=%lu t=%.6f\n",menu_audio?"START":"STOP",menu_music_pos,
               menu_world.header->sounds?(unsigned long)(menu_world.sound[0].samples&AUDIO_COUNT):0,(double)now/kBUSCLK);
        was_menu_audio=menu_audio;
    }
    int playing=game.state==ST_PLAY||game.state==ST_WON;
    float gains[3];
    for(int i=0;i<3;i++) {
        const float *spec=source_environment_audio[i];gains[i]=spec[3];
        if(spec[6]) {
            Vec3 position=i==2?(Vec3){game.hoover.position[0],game.hoover.position[1],game.hoover.position[2]}:(Vec3){spec[0],spec[1],spec[2]};
            float dx=game.feet.x-position.x,dy=game.feet.y+game.eye-position.y,dz=game.feet.z-position.z;
            float distance=sqrtf(dx*dx+dy*dy+dz*dz);
            gains[i]*=spec[4]/fmaxf(spec[4],fminf(distance,spec[5]));
        }
    }
    // Keep latency below ~90 ms. Never block gameplay waiting for SPU2.
    for(int chunks=0;chunks<4&&audsrv_available()>=(int)sizeof(audio_chunk)&&audsrv_queued()<4096;chunks++) {
        int voice_active=sound_pos[4]>=0;
        if(nearby_pending&&voice_active) {
            int queued=audsrv_queued();if(queued<0)queued=0;
            // PCM is mixed ahead of playback. Include the existing IOP queue
            // so a new look cannot restart the voice during its audible tail.
            nearby_audible_until=GetTimerSystemTime()+((u64)(world.sound[4].samples+queued/2)*kBUSCLK)/22050;
            nearby_pending=0;
        }
        for(int i=0;i<512;i++) {
            int value=0;
            if(menu_audio)value+=sample_world(&menu_world,0,&menu_music_pos,(int)menu_world.config[100],menu_world.config[99]);
            if(playing) {value+=sample(0,1,gains[0]);if(game.moving&&game.grounded) value+=sample(1,1,.30f);
                value+=sample(2,1,gains[1])+sample(3,1,gains[2]);}
            value+=sample(4,0,.8f)+sample(5,0,.8f)+sample(6,0,.5f)+sample(7,0,.65f);
            if(value>32767)value=32767;
            if(value<-32768)value=-32768;
            audio_chunk[i]=(int16_t)value;
        }
        if(enemy_test_enabled&&voice_active&&sound_pos[4]<0){enemy_voice_finished++;printf("STPS ENEMY AUDIO PCM_END count=%d\n",enemy_voice_finished);}
        if(audsrv_play_audio((const char *)audio_chunk,sizeof(audio_chunk))<0) break;
    }
}
int main(int argc,char **argv) {
    int smoke=0,menus=0,models=0,menu_fire=0,benchmark=0,game_only=0,force_pal=0,profile=0,win_test=0,surface_test=0,audio_test=0,bench_focus=-1,walking=0,noonoo_test=0;
    for(int i=1;i<argc;i++) {if(!strcmp(argv[i],"--smoke"))smoke=1;if(!strcmp(argv[i],"--menus"))menus=1;if(!strcmp(argv[i],"--models"))models=1;if(!strcmp(argv[i],"--menufire"))menu_fire=1;if(!strcmp(argv[i],"--bench"))benchmark=1;if(!strcmp(argv[i],"--gameonly"))game_only=1;if(!strcmp(argv[i],"--pal"))force_pal=1;}
    for(int i=1;i<argc;i++)if(!strcmp(argv[i],"--profile"))profile=1;
    for(int i=1;i<argc;i++)if(!strcmp(argv[i],"--enemytest"))enemy_test_enabled=1;
    for(int i=1;i<argc;i++)if(!strcmp(argv[i],"--wintest"))win_test=1;
    for(int i=1;i<argc;i++)if(!strcmp(argv[i],"--surfaces"))surface_test=1;
    for(int i=1;i<argc;i++)if(!strcmp(argv[i],"--audiotest"))audio_test=1;
    for(int i=1;i<argc;i++)if(!strncmp(argv[i],"--benchfocus=",13))bench_focus=atoi(argv[i]+13);
    for(int i=1;i<argc;i++)if(!strcmp(argv[i],"--walkbench"))walking=1;
    for(int i=1;i<argc;i++)if(!strcmp(argv[i],"--noonootest"))noonoo_test=1;
    // Timing probes are optional so normal tests measure the production path.
    profile_enabled=profile;
    SifInitRpc(0);while(!SifIopReset("",0)){}while(!SifIopSync()){}
    SifInitRpc(0);SifLoadFileInit();sbv_patch_enable_lmb();
    int pad_ok=module(sio2_start,sio2_end,"freesio2")&&module(pad_start,pad_end,"freepad");
    if(pad_ok) {padInit(0);padPortOpen(0,0,pad_buffer);}
    SifLoadModule("rom0:LIBSD",0,NULL);
    if(module(audsrv_start,audsrv_end,"audsrv")&&audsrv_init()==0) {
        struct audsrv_fmt_t format={22050,16,1};audio_ready=audsrv_set_format(&format)==0;audsrv_set_volume(80);
    }
    for(int i=0;i<9;i++) sound_pos[i]=i<4?0:-1;
    gs=gsKit_init_global_custom(2*1024*1024,64*1024);
    // The release uses 60 Hz so half-rate frames remain nominally 30 FPS,
    // including on a PAL-region console connected to a 60 Hz display.
    gs->Mode=force_pal?GS_MODE_PAL:GS_MODE_NTSC;gs->Width=512;gs->Height=gs->Mode==GS_MODE_PAL?512:448;
    gs->Interlace=GS_INTERLACED;gs->Field=GS_FIELD;gs->PSM=GS_PSM_CT16;gs->PSMZ=GS_PSMZ_16;
    gs->PrimAlphaEnable=GS_SETTING_ON;gs->DoubleBuffering=GS_SETTING_ON;gs->ZBuffering=GS_SETTING_ON;
    // Asset alpha is 0=transparent, 128=opaque. gsKit's default is the reverse.
    gs->PrimAlpha=GS_SETREG_ALPHA(0,1,0,1,0);
    gs->Test->ATE=1;gs->Test->ATST=6;gs->Test->AREF=1;gs->Test->AFAIL=0;
    dmaKit_init(D_CTRL_RELE_OFF,D_CTRL_MFD_OFF,D_CTRL_STS_UNSPEC,D_CTRL_STD_OFF,D_CTRL_RCYC_8,1<<DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);gsKit_init_screen(gs);gsKit_mode_switch(gs,GS_ONESHOT);gsKit_set_clamp(gs,GS_CMODE_REPEAT);
    int valid=world_open(&world,world_start,world_end-world_start);
    printf("STPS WORLD valid=%d bytes=%ld\n",valid,(long)(world_end-world_start));
    if(!valid) {platform_clear(160,0,0);platform_finish();SleepThread();}
    int menu_valid=world_open(&menu_world,menu_world_start,menu_world_end-menu_world_start);
    printf("STPS MENU WORLD valid=%d bytes=%ld\n",menu_valid,(long)(menu_world_end-menu_world_start));
    if(!menu_valid)SleepThread();
    render_set_menu_world(&menu_world);
    int victory_valid=world_open(&victory_world,victory_world_start,victory_world_end-victory_world_start);
    printf("STPS VICTORY WORLD valid=%d bytes=%ld\n",victory_valid,(long)(victory_world_end-victory_world_start));
    if(!victory_valid)SleepThread();
    render_set_victory_world(&victory_world);
    int walk_valid=render_set_view_animation(player_walk_start,player_walk_end-player_walk_start);
    printf("STPS WALK valid=%d bytes=%ld\n",walk_valid,(long)(player_walk_end-player_walk_start));
    if(!walk_valid)SleepThread();
    printf("STPS READY textures=%lu meshes=%lu trees=%lu vram=%lu pad=%d audio=%d\n",(unsigned long)world.header->textures,(unsigned long)world.header->meshes,(unsigned long)world.header->trees,(unsigned long)gs->CurrentPointer,pad_ok,audio_ready);
    printf("STPS VIDEO %s\n",gs->Mode==GS_MODE_PAL?"PAL":"NTSC");
    game_init(&game);if(smoke||models||benchmark||enemy_test_enabled||win_test||surface_test||audio_test||noonoo_test) {game_start(&game,&world);game.time_of_day=0;}
    if(noonoo_test) {
        game.feet=(Vec3){world.config[C_WAY]-4,.8195f,world.config[C_WAY+2]-5};
        game.yaw=atan2f(4,5);game.pitch=-.15f;game.state_time=6;game.next_scare=10000;
    }
    if(enemy_test_enabled){game.state_time=6;game.next_scare=10000;game.enemy=(Vec3){game.feet.x,game.feet.y,game.feet.z-100};}
    u64 last=GetTimerSystemTime();float elapsed=benchmark&&game_only?18:0;unsigned frames=0;int smoke_stage=0,menu_stage=-1,model_stage=-1;float next_menu_step=0;
    if(bench_focus>=0&&bench_focus<(int)BENCHMARK_COUNT)elapsed=bench_focus*3;
    int bench_stage=-1,enemy_test_started=0;u64 bench_begin=0;unsigned bench_frames=0;
    int win_stage=0,win_last_state=-1;float win_notice_begin=0,win_scene_begin=0;
    int surface_stage=-1,surface_crossed=0;float surface_minimum=100;
    int audio_stage=-1,flash_switches=0,reserved_ignored=0;
    float hoover_angle=0,hoover_turn=0,hoover_minx=1e9f,hoover_maxx=-1e9f,hoover_minz=1e9f,hoover_maxz=-1e9f;
    int walk_stage=-1;u64 walk_begin_time=0;unsigned walk_frames=0,walk_slow=0;float walk_distance=0,walk_worst=0;
    if(walking&&bench_focus>=0&&bench_focus<(int)WALK_COUNT)elapsed=bench_focus*12;
    for(;;) {
        u64 now=GetTimerSystemTime();float dt=(float)(now-last)/(float)kBUSCLK;last=now;
        Input in=pad_ok?read_input():(Input){0};
        if(noonoo_test){in=(Input){.connected=1};game.enemy=(Vec3){0,0,0};}
        if(walking) {
            unsigned route=(unsigned)(elapsed/12);float phase=elapsed-route*12;
            if((int)route!=walk_stage) {
                if(walk_stage>=0){float seconds=(float)(now-walk_begin_time)/kBUSCLK;
                    printf("STPS BENCH scene=%s frames=%u seconds=%.3f fps=%.2f\n",walk_names[walk_stage],walk_frames,seconds,walk_frames/seconds);
                    printf("STPS WALK route=%s distance=%.2f slow=%u total=%u worst_ms=%.2f state=%d\n",walk_names[walk_stage],walk_distance,walk_slow,walk_frames,walk_worst,game.state);
                    if(walk_distance<8||game.state!=ST_PLAY){printf("STPS SMOKE FAIL walk-progress\n");SleepThread();}}
                if(route>=WALK_COUNT||(bench_focus>=0&&(int)route>bench_focus)){printf("STPS SMOKE PASS walk\n");SleepThread();}
                walk_stage=route;walk_begin_time=now;walk_frames=walk_slow=0;walk_distance=walk_worst=0;walk_begin(&game,&world,route);
                printf("STPS WALK START route=%s frame=%u\n",walk_names[route],frames);
            }
            in=walk_input(route,phase);
            if(phase>=1){if(!walk_frames)walk_begin_time=now;walk_frames++;if(dt>.036f)walk_slow++;walk_worst=fmaxf(walk_worst,dt*1000);}
        }
        if(audio_test) {
            int stage=(int)(elapsed/3);in=(Input){.connected=1};game.state_time=6;game.next_scare=10000;
            if(stage!=audio_stage) {
                audio_stage=stage;
                if(stage==1||stage==2)in.pressed=B_LIGHT;
                if(stage==3||stage==4)in.pressed=4096; // Retired input must have no effect.
                if(stage==5){game.feet.x=world.config[C_DOOR];game.feet.z=727;game.feet.y=world_height(&world,game.feet.x,game.feet.z);}
                if(stage==6){game.feet=(Vec3){world.config[C_SPAWN],world.config[C_SPAWN+1],world.config[C_SPAWN+2]};}
                printf("STPS AUDIO step=%d t=%.3f\n",stage,elapsed);
            }
            game.enemy=(Vec3){game.feet.x,game.feet.y,game.feet.z-100};
        }
        if(surface_test) {
            int stage=elapsed<28?(int)(elapsed/7):4+(int)((elapsed-28)/2);
            float phase=elapsed-(stage<4?stage*7:28+(stage-4)*2);
            if(stage!=surface_stage) {
                if(surface_stage>=0&&surface_stage<4) {
                    float exit=hypotf(game.feet.x-source_roof_support[0],game.feet.z-source_roof_support[1]);
                    printf("STPS ROOF route=%d centre=%d minimum=%.3f exit=%.3f\n",surface_stage,surface_crossed,surface_minimum,exit);
                    if(!surface_crossed||surface_minimum<6.5f||exit<5){printf("STPS SMOKE FAIL roof\n");SleepThread();}
                }
                if(stage>=7){printf("STPS SMOKE PASS surfaces frames=%u\n",frames);SleepThread();}
                surface_stage=stage;surface_crossed=0;surface_minimum=100;
                game_start(&game,&world);game.time_of_day=0;game.state_time=6;game.next_scare=10000;
                if(stage<4) {
                    game.yaw=stage*1.5707963f;game.pitch=-.35f;
                    game.feet=(Vec3){source_roof_support[0]-sinf(game.yaw)*18,12,source_roof_support[1]-cosf(game.yaw)*18};game.grounded=0;
                } else if(stage==4){game.pitch=-1.0472f;game.draw_grass=0;}
                else {const float *p=world.config+C_CUSTARD+27;game.feet=(Vec3){p[0]+20,world_height(&world,p[0]+20,p[2]-8),p[2]-8};game.pitch=-.35f;game.yaw=stage==6?.55f:0;}
                game.enemy=(Vec3){game.feet.x,game.feet.y,game.feet.z-100};
                printf("STPS SURFACE START scene=%s frame=%u\n",stage==4?"body-idle":stage==5?"grass-front":stage==6?"grass-side":"roof",frames);
            }
            in=(Input){.connected=1,.move_y=stage<4&&phase>.9f?1:0};
        }
        if(win_test) {
            in=(Input){.connected=1};game.next_scare=10000;
            if(win_stage==0&&elapsed>=1) {
                game.custards=511;game.collected=9;const float *p=world.config+C_CUSTARD+27;
                game.feet=(Vec3){p[0],world_height(&world,p[0],p[2]),p[2]};
                game.enemy=(Vec3){p[0],game.feet.y,p[2]-100};win_stage=1;win_notice_begin=elapsed;
            }
            if(win_stage==2&&elapsed-win_scene_begin>=2.5f)in.pressed=B_CONFIRM;
        }
        if(enemy_test_enabled) {
            in=(Input){.connected=1};
            if(elapsed>=1&&elapsed<3){game.enemy=(Vec3){game.feet.x,game.feet.y+.675f,game.feet.z+15};game.yaw=0;game.pitch=0;}
            if(elapsed>=3) {
                if(!enemy_test_started){
                    if(game.health!=100||enemy_side_shifts||game.enemy_attacking){printf("STPS SMOKE FAIL distant-damage health=%.3f shifts=%d\n",game.health,enemy_side_shifts);SleepThread();}
                    printf("STPS ENEMY DISTANT PASS health=%.1f shifts=%d\n",game.health,enemy_side_shifts);
                    enemy_test_started=1;game.enemy=(Vec3){game.feet.x,game.feet.y+.675f,game.feet.z+4.5f};game.enemy_visible=0;
                }
                float phase=elapsed-3;
                float duration=world.sound[4].samples/22050.f;
                int looking=phase<.12f||(phase>=.2f&&phase<.35f)||(phase>=2&&phase<2.15f)||(phase>=5&&phase<5.15f)||
                    (phase>=duration+.7f&&phase<duration+.9f)||(phase>=duration+1.1f&&phase<duration+1.25f);
                game.yaw=atan2f(game.enemy.x-game.feet.x,game.enemy.z-game.feet.z)+(looking?0:3.1415927f);
            }
        }
        if(menu_fire)in=(Input){.connected=1};
        if(benchmark) {
            unsigned scene=(unsigned)(elapsed/3);
            if((int)scene!=bench_stage) {
                if(bench_stage>=0){float seconds=(float)(now-bench_begin)/kBUSCLK;
                    printf("STPS BENCH scene=%s frames=%u seconds=%.3f fps=%.2f\n",benchmark_names[bench_stage],bench_frames,seconds,bench_frames/seconds);}
                if(scene>=BENCHMARK_COUNT||(bench_focus>=0&&(int)scene>bench_focus)){printf("STPS SMOKE PASS benchmark\n");SleepThread();}
                bench_stage=scene;bench_begin=0;bench_frames=0;
                printf("STPS BENCH START scene=%s frame=%u\n",benchmark_names[scene],frames);
            }
            in=(Input){.connected=1};benchmark_fixture(&game,&world,scene,elapsed-scene*3);
            if(elapsed-scene*3>=.5f){if(!bench_begin)bench_begin=now;bench_frames++;}
        }
        int expect=-1;
        if(menus) {
            in=(Input){.connected=1};
            if(elapsed>=next_menu_step && menu_stage+1<(int)MENU_ROUTE_COUNT) {
                menu_stage++;next_menu_step=elapsed+.8f;
                const MenuStep *s=menu_route+menu_stage;
                in.pressed=s->button;expect=s->state;menu_fixture(&game,&world,s->fixture);
            }
        }
        if(smoke) {in=(Input){.connected=1};
            if(smoke_stage==0) in.look_x=.3f;
            if(elapsed>3&&smoke_stage==0) {game.feet.x=world.config[C_DOOR];game.feet.z=727;game.feet.y=world_height(&world,game.feet.x,game.feet.z);game.yaw=0;smoke_stage=1;printf("STPS SMOKE HOUSE\n");}
            if(smoke_stage==1) {in.move_y=1;if(game.feet.z>741) {smoke_stage=2;printf("STPS SMOKE ENTERED\n");}}
            if(smoke_stage==2) {in.move_y=-1;if(game.feet.z<729) {smoke_stage=3;game.time_of_day=2;printf("STPS SMOKE EXITED NIGHT\n");}}
        }
        if(models) {
            in=(Input){.connected=1};int stage=(int)(elapsed/2);if(stage>5)stage=5;
            game.state_time=6;
            game.time_of_day=stage<2?2:0;game.flashlight=stage!=1;game.pitch=stage<2?-.5f:.18f;
            game.feet=(Vec3){world.config[C_SPAWN],world.config[C_SPAWN+1],world.config[C_SPAWN+2]};game.yaw=0;
            if(stage>=2) {
                int kind=stage<=3?0:stage-3;const Tree *chosen=NULL;
                for(unsigned i=0;i<world.header->trees;i++)if((int)world.tree[i].type==kind) {
                    if(!chosen||world.tree[i].y<chosen->y)chosen=world.tree+i;
                }
                if(chosen){game.feet.x=chosen->x+(stage==3?10:0);game.feet.z=chosen->z-(stage==3?0:10);game.yaw=stage==3?-1.5707963f:0;}
            }
            game.feet.y=world_height(&world,game.feet.x,game.feet.z);game.vy=0;game.health=100;game.next_scare=10000;
            game.enemy=(Vec3){game.feet.x,game.feet.y,game.feet.z-100};
            if(stage!=model_stage){model_stage=stage;printf("STPS MODEL step=%d pitch=%.2f light=%d\n",stage,game.pitch,game.flashlight);}
        }
        if(!in.connected&&game.state==ST_PLAY) {game.state=ST_PAUSE;game.moving=0;}
        u64 update_start=GetTimerSystemTime();
        Vec3 previous_feet=game.feet;
        game_advance(&game,&world,&in,dt);
        if(walking)walk_distance+=hypotf(game.feet.x-previous_feet.x,game.feet.z-previous_feet.z);
        if(surface_test&&surface_stage<4) {
            float radius=hypotf(game.feet.x-source_roof_support[0],game.feet.z-source_roof_support[1]);
            if(radius<18)surface_minimum=fminf(surface_minimum,game.feet.y);
            if(radius<5){surface_crossed=1;if(game.feet.y<10.8f){printf("STPS SMOKE FAIL roof-centre\n");SleepThread();}}
        }
        if(win_test&&game.state!=win_last_state) {
            printf("STPS WIN STATE state=%d frame=%u t=%.3f count=%d\n",game.state,frames,elapsed,game.collected);
            win_last_state=game.state;
            if(game.state==ST_WINSCENE){win_stage=2;win_scene_begin=elapsed;
                if(elapsed-win_notice_begin<4.95f){printf("STPS SMOKE FAIL win-duration\n");SleepThread();}}
            if(win_stage==2&&game.state==ST_MENU)win_stage=3;
        }
        static u64 update_ticks;update_ticks+=GetTimerSystemTime()-update_start;
        if(profile_enabled&&frames%120==119){printf("STPS UPDATE %.2f ms\n",update_ticks*(1000.f/(kBUSCLK*120.f)));update_ticks=0;}
        if(expect>=0) {
            printf("STPS MENU step=%d expected=%d actual=%d day=%d map=%d light=%d\n",menu_stage,expect,game.state,game.time_of_day,game.map,game.flashlight);
            if(!menu_step_ok(&game,menu_stage)) {printf("STPS SMOKE FAIL menu=%d\n",menu_stage);SleepThread();}
        }
        if(game.events&EV_SWITCH) sound_pos[6]=0;
        if(audio_test&&in.pressed==4096) {
            if(game.events){printf("STPS SMOKE FAIL retired-input\n");SleepThread();}
            reserved_ignored++;
        }
        if(audio_test&&(game.events&EV_SWITCH)) {
            flash_switches++;
            printf("STPS AUDIO SWITCH flashlight=%d t=%.3f\n",flash_switches,elapsed);
        }
        if(game.events&EV_SCARE)sound_pos[7]=0;
        if(game.events&EV_ENEMY_SEEN) {
            int started=voice_start_ready(sound_pos+4,world.sound[4].samples,GetTimerSystemTime(),nearby_audible_until);
            if(started)nearby_pending=1;
            if(enemy_test_enabled) {
                if(started)enemy_voice_starts++;else enemy_voice_ignored++;
                printf("STPS ENEMY AUDIO %s t=%.3f cursor=%d\n",started?"START":"BUSY",elapsed,sound_pos[4]);
            }
        }
        if(enemy_test_enabled&&(game.events&EV_ENEMY_SHIFT)){enemy_side_shifts++;printf("STPS ENEMY SIDE t=%.3f pos=%.3f,%.3f,%.3f\n",elapsed,game.enemy.x,game.enemy.y,game.enemy.z);}
        if(game.events&EV_DEATH) {sound_pos[4]=-1;nearby_pending=0;sound_pos[5]=0;}
        if(noonoo_test) {
            float m[16];game_hoover_matrix(&world,&game,m);float angle=atan2f(m[8],m[10]);
            hoover_turn+=fabsf(remainderf(angle-hoover_angle,6.2831853f));hoover_angle=angle;
            hoover_minx=fminf(hoover_minx,game.hoover.position[0]);hoover_maxx=fmaxf(hoover_maxx,game.hoover.position[0]);
            hoover_minz=fminf(hoover_minz,game.hoover.position[2]);hoover_maxz=fmaxf(hoover_maxz,game.hoover.position[2]);
            if(frames%30==0)printf("STPS NOONOO frame=%u pos=%.3f,%.3f,%.3f forward=%.3f,%.3f,%.3f\n",frames,
                game.hoover.position[0],game.hoover.position[1],game.hoover.position[2],m[8],m[9],m[10]);
        }
        audio_pump();render_frame(&world,&game,in.connected);elapsed+=dt;frames++;
        if(noonoo_test&&elapsed>8) {
            int pass=hoover_turn>6.2831853f&&hoover_maxx-hoover_minx>1&&hoover_maxz-hoover_minz>1;
            printf("STPS NOONOO SUMMARY turns=%.3f x_span=%.3f z_span=%.3f\n",hoover_turn/6.2831853f,hoover_maxx-hoover_minx,hoover_maxz-hoover_minz);
            printf("STPS SMOKE %s noonoo frames=%u\n",pass?"PASS":"FAIL",frames);SleepThread();
        }
        if(audio_test&&elapsed>21) {
            int pass=flash_switches==2&&reserved_ignored==2&&game.flashlight&&game.state==ST_PLAY;
            printf("STPS SMOKE %s audio frames=%u flashlight=%d reserved_ignored=%d\n",pass?"PASS":"FAIL",frames,flash_switches,reserved_ignored);SleepThread();
        }
        if(win_test&&win_stage==3) {printf("STPS SMOKE PASS victory frames=%u notice=%.3f final=%.3f\n",frames,win_scene_begin-win_notice_begin,elapsed-win_scene_begin);SleepThread();}
        if(frames%120==0) printf("STPS FRAME %u t=%.2f state=%d pos=%.2f,%.2f,%.2f health=%.1f\n",frames,elapsed,game.state,game.feet.x,game.feet.y,game.feet.z,game.health);
        if(enemy_test_enabled&&elapsed>world.sound[4].samples/22050.f*2+5) {
            int pass=enemy_voice_starts==2&&enemy_voice_ignored>=2&&enemy_voice_finished==2&&enemy_side_shifts>=2&&game.state==ST_PLAY&&game.health>0;
            printf("STPS ENEMY SUMMARY starts=%d ignored=%d finished=%d shifts=%d health=%.1f\n",enemy_voice_starts,enemy_voice_ignored,enemy_voice_finished,enemy_side_shifts,game.health);
            printf("STPS SMOKE %s enemy frames=%u\n",pass?"PASS":"FAIL",frames);SleepThread();
        }
        if(smoke&&elapsed>12) {printf("STPS SMOKE %s frames=%u stage=%d\n",smoke_stage==3?"PASS":"FAIL",frames,smoke_stage);SleepThread();}
        if(models&&elapsed>12) {printf("STPS SMOKE PASS models frames=%u\n",frames);SleepThread();}
        if(menu_fire&&elapsed>6) {printf("STPS SMOKE PASS menufire frames=%u menu_time=%.2f\n",frames,game.state_time);SleepThread();}
        if(menus&&menu_stage+1==(int)MENU_ROUTE_COUNT&&elapsed>=next_menu_step) {printf("STPS SMOKE PASS menus frames=%u\n",frames);SleepThread();}
    }
}
