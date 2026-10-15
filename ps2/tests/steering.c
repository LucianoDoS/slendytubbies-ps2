/* Asset-free tests for the same steering and model transform used on PS2. */
#include "steering.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const float origin[3]={0,0,0},identity[4]={0,0,0,1},goal[3]={3,.4f,0};
    SteeringBody body;steering_init(&body,origin,identity);
    float last=0,turn=0,maximum=0;
    for(int frame=0;frame<3600;frame++) {
        steering_step(&body,goal,.8f,10,6,1/60.f);
        float matrix[16];steering_matrix(matrix,&body,origin,identity);
        float norm=0;for(int i=0;i<4;i++)norm+=body.rotation[i]*body.rotation[i];
        assert(fabsf(norm-1)<.00001f&&body.speed<=10);
        for(int a=0;a<3;a++)for(int b=0;b<3;b++) {
            float dot=0;for(int k=0;k<3;k++)dot+=matrix[a*4+k]*matrix[b*4+k];
            assert(fabsf(dot-(a==b))<.00001f);
        }
        float angle=atan2f(matrix[8],matrix[10]);
        float delta=remainderf(angle-last,6.2831853f);last=angle;turn+=fabsf(delta);
        float dx=body.position[0]-goal[0],dz=body.position[2]-goal[2];
        if(frame>120)maximum=fmaxf(maximum,sqrtf(dx*dx+dz*dz));
        assert(isfinite(body.position[0])&&isfinite(body.position[1])&&isfinite(body.position[2]));
    }
    assert(turn>6.2831853f*10&&maximum<4);
    const float initial[4]={0,.70710678f,0,.70710678f},offset[3]={20,-3,12};
    steering_init(&body,offset,initial);float m[16];steering_matrix(m,&body,goal,initial);
    for(int col=0;col<3;col++)for(int row=0;row<3;row++)assert(fabsf(m[col*4+row]-(col==row))<.00001f);
    for(int row=0;row<3;row++)assert(fabsf(m[12+row]-(offset[row]-goal[row]))<.00001f);
    printf("PASS steering: 60 seconds, %.1f turns, bounded orbit %.3f m, rigid model transform\n",turn/6.2831853f,maximum);
    return 0;
}
