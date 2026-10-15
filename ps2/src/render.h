#ifndef SLENDYTUBBIES_RENDER_H
#define SLENDYTUBBIES_RENDER_H
#include "game.h"
typedef struct { float x,y,z,u,v; uint8_t r,g,b,a; float q; unsigned cache_key; } DrawVertex;
void platform_clear(uint8_t r,uint8_t g,uint8_t b);
void platform_world(const World *world,int bank);
void platform_triangle(int texture,const DrawVertex *vertices);
void platform_sprite(int texture,float x,float y,float width,float height,float u,float v,float uw,float vh,uint32_t color);
void platform_finish(void);
void platform_profile(int phase);
void platform_blend(int mode);
void platform_cutout(int enabled);
void platform_fog(uint8_t r,uint8_t g,uint8_t b);
int platform_static_begin(int key);
void platform_static_end(void);
void platform_cache_reset(void);
void platform_work(int kind,int begin);
void render_frame(const World *world,const Game *game,int connected);
void render_set_menu_world(const World *world);
void render_set_victory_world(const World *world);
int render_set_view_animation(const void *data,size_t bytes);
#endif
