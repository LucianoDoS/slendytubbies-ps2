#ifndef SLENDYTUBBIES_WALK_ROUTE_H
#define SLENDYTUBBIES_WALK_ROUTE_H
/* Reposition only at route boundaries. Movement and contact use the same
 * input and game_advance path as a normal game for twelve seconds. */
static const char *walk_names[]={"walk-day","walk-turn","walk-slope","walk-night","walk-house","walk-dusk","walk-user","walk-user-night"};
#define WALK_COUNT (sizeof(walk_names)/sizeof(walk_names[0]))
static void walk_begin(Game *g,const World *w,unsigned route) {
    game_start(g,w);g->time_of_day=route==3?2:route==5?1:0;g->state_time=6;g->next_scare=10000;
    if(route==1){g->feet.x=550.46f;g->feet.z=186.24f;}
    if(route==2||route==5){g->feet.x=264.22705f;g->feet.z=300;g->yaw=.55f;}
    if(route==4){g->feet.x=w->config[C_DOOR];g->feet.z=723;}
    if(route>=6){g->feet.x=860.44f;g->feet.z=382.85f;g->quality=5;}
    if(route==7)g->time_of_day=2;
    g->feet.y=world_height(w,g->feet.x,g->feet.z);
}
static Input walk_input(unsigned route,float phase) {
    Input in={.connected=1,.move_y=.85f,.move_x=.3f*sinf(phase*.8f),.look_x=.1f,.look_y=.04f*cosf(phase*.6f)};
    if(route==1){in.look_x=.65f;in.move_x=.65f;in.move_y=.6f;}
    if(route==2)in.held=B_RUN;
    if(route==4){in.look_x=0;in.move_x=0;in.look_y=0;in.move_y=phase<5?1:-1;}
    if(route==5)in.look_x=-.23f;
    if(route>=6){in.look_x=.7f;in.move_x=.6f;}
    return in;
}
#endif
