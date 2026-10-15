#ifndef SLENDYTUBBIES_STEERING_H
#define SLENDYTUBBIES_STEERING_H
#include <math.h>
#include <string.h>

typedef struct { float position[3],rotation[4],speed; } SteeringBody;

static inline void steering_normalize(float q[4]) {
    float length=sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    for(int i=0;i<4;i++)q[i]/=length;
}
static inline void steering_init(SteeringBody *body,const float position[3],const float rotation[4]) {
    memcpy(body->position,position,sizeof(body->position));
    memcpy(body->rotation,rotation,sizeof(body->rotation));
    steering_normalize(body->rotation);body->speed=0;
}
/* Unity LookRotation followed by shortest-path quaternion Slerp. Keeping the
 * local forward motion is essential: moving directly toward the goal removes
 * the original vacuum's circular path around its single waypoint. */
static inline void steering_step(SteeringBody *body,const float goal[3],float accel,float limit,float damping,float dt) {
    float x=goal[0]-body->position[0],y=goal[1]-body->position[1],z=goal[2]-body->position[2];
    if(x*x+y*y+z*z>1e-10f) {
        float yaw=atan2f(x,z)*.5f,pitch=atan2f(y,sqrtf(x*x+z*z))*.5f;
        float sy=sinf(yaw),cy=cosf(yaw),sp=sinf(pitch),cp=cosf(pitch);
        float target[4]={-sp*cy,cp*sy,sp*sy,cp*cy},dot=0;
        for(int i=0;i<4;i++)dot+=body->rotation[i]*target[i];
        if(dot<0){dot=-dot;for(int i=0;i<4;i++)target[i]=-target[i];}
        float t=fminf(1,dt*damping),a=1-t,b=t;
        if(dot<.9995f) {
            float angle=acosf(fminf(1,dot)),den=sinf(angle);
            a=sinf((1-t)*angle)/den;b=sinf(t*angle)/den;
        }
        for(int i=0;i<4;i++)body->rotation[i]=a*body->rotation[i]+b*target[i];
        steering_normalize(body->rotation);
    }
    body->speed=fminf(limit,body->speed+accel*accel*dt*60);
    const float *q=body->rotation;float step=body->speed*dt;
    body->position[0]+=2*(q[0]*q[2]+q[1]*q[3])*step;
    body->position[1]+=2*(q[1]*q[2]-q[0]*q[3])*step;
    body->position[2]+=(1-2*(q[0]*q[0]+q[1]*q[1]))*step;
}
/* Source vertices already contain their initial world transform. Apply only
 * the rotation delta about the original root, then the animated translation. */
static inline void steering_matrix(float m[16],const SteeringBody *body,const float origin[3],const float initial[4]) {
    const float *q=body->rotation;
    float x=-q[3]*initial[0]+q[0]*initial[3]-q[1]*initial[2]+q[2]*initial[1];
    float y=-q[3]*initial[1]+q[0]*initial[2]+q[1]*initial[3]-q[2]*initial[0];
    float z=-q[3]*initial[2]-q[0]*initial[1]+q[1]*initial[0]+q[2]*initial[3];
    float w=q[3]*initial[3]+q[0]*initial[0]+q[1]*initial[1]+q[2]*initial[2];
    const float rotation[16]={1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,
        2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,
        2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,0,0,0,1};
    memcpy(m,rotation,sizeof(rotation));
    for(int row=0;row<3;row++)m[12+row]=body->position[row]-m[row]*origin[0]-m[4+row]*origin[1]-m[8+row]*origin[2];
}
#endif
