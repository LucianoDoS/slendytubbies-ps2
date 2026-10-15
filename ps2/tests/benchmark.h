#ifndef SLENDYTUBBIES_BENCHMARK_H
#define SLENDYTUBBIES_BENCHMARK_H
static const char *benchmark_names[]={"menu-main","menu-settings","menu-single","menu-multi","menu-coop","menu-versus",
    "forest-day","forest-side","forest-night","house-outside","house-inside","flashlight-floor",
    "night-light-off","enemy-visible","forest-up","forest-turn","custard-tree","grass-close","pickup-notice","death-scene","grass-off","win-notice","win-scene",
    "roof-outside","roof-centre","body-down","grass-billboard-side","pine-leaves-close","alder-leaves-close","grass-slope",
    "path-detail","enemy-above","forest-distant","enemy-close"};
#define BENCHMARK_COUNT (sizeof(benchmark_names)/sizeof(benchmark_names[0]))
static void benchmark_fixture(Game *g,const World *w,unsigned scene,float phase) {
    if(scene<6){g->state=ST_MENU;g->menu_page=scene;g->menu=0;return;}
    g->state=ST_PLAY;g->time_of_day=scene==8||scene==12?2:0;g->flashlight=scene!=12;g->draw_grass=scene!=20;
    g->feet=(Vec3){w->config[C_SPAWN],w->config[C_SPAWN+1],w->config[C_SPAWN+2]};g->yaw=0;g->pitch=0;
    if(scene==7)g->yaw=1.5707963f;
    if(scene==9||scene==10){g->feet.x=w->config[C_DOOR];g->feet.z=scene==9?727:744;g->yaw=scene==10?3.1415927f:0;}
    if(scene==11)g->pitch=-.55f;
    if(scene==14)g->pitch=.65f;
    if(scene==15)g->yaw=phase*.9f;
    if(scene>=16) {
        const float *p=w->config+C_CUSTARD+9*3;
        g->feet.x=p[0];g->feet.z=p[2]-8;g->yaw=0;
        if(scene==17||scene==20){g->feet.x+=20;g->pitch=-.35f;}
        g->state_time=6;g->collected=0;g->pickup_time=0;
        if(scene==18){g->collected=1;g->pickup_time=2;}
        if(scene==19){g->state=ST_DEAD;g->state_time=.2f;g->enemy_visible=0;}
        if(scene==21){g->state=ST_WON;g->state_time=1;g->collected=10;g->custards=1023;}
        if(scene==22){g->state=ST_WINSCENE;g->state_time=1;}
        if(scene==23||scene==24){g->feet.x=source_roof_support[0]+(scene==23?14:0);g->feet.z=source_roof_support[1];g->pitch=-.45f;}
        if(scene==25){g->pitch=-1.0472f;g->draw_grass=0;}
        if(scene==26){g->feet.x+=20;g->yaw=.55f;g->pitch=-.35f;}
        if(scene==27||scene==28) {
            static const Tree *chosen[2];int kind=scene==27?1:0;
            if(!chosen[kind])for(unsigned i=0;i<w->header->trees;i++)if((int)w->tree[i].type==kind&&
                (!chosen[kind]||w->tree[i].y<chosen[kind]->y))chosen[kind]=w->tree+i;
            if(chosen[kind]){g->feet.x=chosen[kind]->x;g->feet.z=chosen[kind]->z-6;g->yaw=0;g->pitch=.85f;}
        }
        if(scene==29){g->feet.x=264.22705f;g->feet.z=323.30664f;g->yaw=0;g->pitch=-.15f;}
        if(scene==30){g->feet.x=293;g->feet.z=267;g->yaw=0;g->pitch=-.55f;g->draw_grass=0;}
        if(scene==31){g->pitch=-1.0472f;g->draw_grass=0;}
        if(scene==32){g->feet.x=264.22705f;g->feet.z=300;g->yaw=.55f;g->pitch=.15f;}
    }
    g->feet.y=world_height(w,g->feet.x,g->feet.z);g->health=100;g->vy=0;g->eye=1.7f;
    if(scene==10)g->feet.y=.82f;
    if(scene==23)g->feet.y=9.6f;
    if(scene==24)g->feet.y=source_roof_support[3];
    g->enemy=(Vec3){g->feet.x,g->feet.y,g->feet.z-100};g->next_scare=10000;
    g->next_enemy_shift=0;
    if(scene==13)g->enemy=(Vec3){g->feet.x,g->feet.y+.675f,g->feet.z+8};
    if(scene==31){g->feet.y+=3;g->enemy=(Vec3){g->feet.x+.3f,g->feet.y-2.325f,g->feet.z+.5f};}
    if(scene==33){g->enemy=(Vec3){g->feet.x,g->feet.y+.675f,g->feet.z+4};g->next_enemy_shift=1000000;}
}
#endif
