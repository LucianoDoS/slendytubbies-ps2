#ifndef SLENDYTUBBIES_GAME_H
#define SLENDYTUBBIES_GAME_H
#include <stdint.h>
#include <stddef.h>
#include "steering.h"

typedef struct { float x,y,z; } Vec3;
typedef struct { float x,y,z,u,v,nx,ny,nz; } Vertex;
typedef struct {
    uint32_t vertices,triangles,vertex_offset,index_offset,texture,flags,id;
    float min[3],max[3],color[3];
} Mesh;
typedef struct { uint32_t width,height,offset,bytes; } Texture;
typedef struct { float min[3],max[3]; } Box;
typedef struct { float x,y,z,width,height,type; } Tree;
typedef struct { uint32_t offset,samples; } Sound;
typedef struct { uint32_t vertices,triangles,vertex_offset,index_offset; float min[3],max[3]; } CollisionMesh;
typedef struct {
    uint32_t magic,version,meshes,textures,boxes,trees,width,height;
    uint32_t mesh_offset,texture_offset,box_offset,tree_offset,height_offset,splat_offset,config_offset;
    uint32_t sounds,sound_offset,collision_meshes,collision_offset,bytes;
} WorldHeader;
typedef struct {
    const unsigned char *blob; const WorldHeader *header;
    const Mesh *mesh; const Texture *texture; const Box *box; const Tree *tree;
    const uint16_t *height; const uint8_t *splat; const float *config; const Sound *sound;
    const CollisionMesh *collision;
} World;
enum { M_COLLIDE=1,M_ENEMY=2,M_DOOR=4,M_HOOVER=8,M_COLLECT=16,M_TREE=32,M_VIEW=64,M_CULL=256,M_DAY_TREE=512,M_CUTOUT=1024 };
enum { C_SPAWN=0,C_ENEMY=3,C_DOOR=6,C_HOOVER=9,C_WAY=12,C_CUSTARD=15,
    C_WALK=45,C_CROUCH=46,C_RUN=47,C_CHASE=48,C_FAST=49,C_SPRINT=50,
    C_FOLLOW=51,C_RANGE=52,C_DAMAGE=53,C_OFFSCREEN=54,C_VISIBLE=55,
    C_ENV=56,C_SCALE=77,C_GROUND=80,C_TREE=84,C_TITLE=85,C_MAP=86,
    C_SCARE=87,C_WON=88,C_DEAD=89,C_POPUP=90,C_FONT=94 };
enum { B_JUMP=1,B_LIGHT=2,B_CROUCH=4,B_RUN=8,B_MAP=16,B_PAUSE=32,B_CONFIRM=64,B_BACK=128,B_UP=256,B_DOWN=512,B_LEFT=1024,B_RIGHT=2048 };
enum { ST_MENU,ST_PLAY,ST_PAUSE,ST_DEAD,ST_WON,ST_HELP,ST_CREDITS,ST_NETWORK,ST_WINSCENE };
enum { MENU_MAIN,MENU_SETTINGS,MENU_SINGLE,MENU_MULTI,MENU_COOP,MENU_VERSUS };
enum { EV_PICKUP=1,EV_SWITCH=2,EV_SCARE=4,EV_DEATH=8,EV_ENEMY_SEEN=16,EV_ENEMY_SHIFT=32 };
typedef struct { float move_x,move_y,look_x,look_y; uint32_t held,pressed; int connected; } Input;
typedef struct {
    Vec3 feet,enemy; SteeringBody hoover; float yaw,pitch,vy,eye,health,enemy_speed;
    float time,state_time,door_open,scare_time,next_scare;
    int state,menu,menu_page,quality,time_of_day,flashlight,map,crouching,grounded,moving;
    int collected,scare_image,events,enemy_visible,enemy_attacking,draw_grass;
    float pickup_time,next_enemy_shift;
    uint16_t custards;
    uint32_t random;
} Game;
int world_open(World *world,const void *blob,size_t length);
float world_height(const World *world,float x,float z);
Vec3 game_mesh_vertex(const World *world,const Game *game,const Mesh *mesh,const Vertex *vertex);
void game_enemy_matrix(const World *world,const Game *game,float matrix[16]);
void game_hoover_matrix(const World *world,const Game *game,float matrix[16]);
void game_init(Game *game);
void game_start(Game *game,const World *world);
void game_update(Game *game,const World *world,const Input *input,float dt);
void game_advance(Game *game,const World *world,const Input *input,float elapsed);
int world_occluded(const World *world,const Game *game,Vec3 from,Vec3 to);
#endif
