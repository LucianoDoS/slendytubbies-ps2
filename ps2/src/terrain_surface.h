#ifndef SLENDYTUBBIES_TERRAIN_SURFACE_H
#define SLENDYTUBBIES_TERRAIN_SURFACE_H
#include "game.h"
#include <math.h>
#include <stdlib.h>
static inline int terrain_step(const Game *g,int menu,int victory) {
    return victory?6:menu||g->quality<2||g->time_of_day==0?4:2;
}
/* Remove one common integer repeat from the entire quad before clipping/STQ.
 * Large world-space UVs lose GS interpolation precision on real hardware.
 * Wrapping each vertex separately would change the scale or create seams. */
static inline void terrain_uv_quad(float uv[4][2],int x,int z,int step,float sx,float sz,float repeat) {
    float u=x*sx/repeat,v=-z*sz/repeat,du=step*sx/repeat,dv=step*sz/repeat;
    u-=floorf(u);v-=floorf(v-dv);
    uv[0][0]=uv[3][0]=u;uv[1][0]=uv[2][0]=u+du;
    uv[0][1]=uv[1][1]=v;uv[2][1]=uv[3][1]=v-dv;
}
/* Detect paint hidden between coarse vertices, including narrow paths. */
static inline int terrain_paint_step(const World *w,int x,int z,int step) {
    int width=w->header->width-1,error=0;
    if(x+step>=width||z+step>=width)return step;
    const uint8_t *corners[4]={w->splat+(z*width+x)*4,w->splat+(z*width+x+step)*4,
        w->splat+((z+step)*width+x+step)*4,w->splat+((z+step)*width+x)*4};
    for(int v=0;v<=step;v++)for(int u=0;u<=step;u++)for(int layer=0;layer<4;layer++) {
        int expected=u>=v?corners[0][layer]*(step-u)+corners[1][layer]*(u-v)+corners[2][layer]*v:
            corners[0][layer]*(step-v)+corners[2][layer]*u+corners[3][layer]*(v-u);
        int delta=abs((int)w->splat[((z+v)*width+x+u)*4+layer]*step-expected);
        if(delta>error)error=delta;
    }
    return error>48*step?1:error>12*step&&step>2?2:step;
}
/* Height on the two rendered triangles, including their shared 0-to-2 diagonal. */
static inline float terrain_surface_height(const World *w,int step,float x,float z) {
    float gx=x/w->config[C_SCALE],gz=z/w->config[C_SCALE+2];
    int ix=(int)floorf(gx/step)*step,iz=(int)floorf(gz/step)*step;
    if(ix<0||iz<0||ix+step>=(int)w->header->width||iz+step>=(int)w->header->height)return world_height(w,x,z);
    float u=(gx-ix)/step,v=(gz-iz)/step;
    const uint16_t *p=w->height+iz*w->header->width+ix;
    float h0=p[0],h1=p[step],h2=p[step*w->header->width+step],h3=p[step*w->header->width];
    return (u>=v?h0*(1-u)+h1*(u-v)+h2*v:h0*(1-v)+h2*u+h3*(v-u))*w->config[C_SCALE+1];
}
#endif
