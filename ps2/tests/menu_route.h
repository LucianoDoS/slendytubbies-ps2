/* Explicit controller route through every exported menu camera and popup. */
typedef struct {unsigned button;int state,page,selection,fixture;} MenuStep;
static const MenuStep menu_route[]={
{0,ST_MENU,MENU_MAIN,0,0},
{B_CONFIRM,ST_MENU,MENU_SINGLE,0,0},
{B_RIGHT,ST_MENU,MENU_SINGLE,1,0},
{B_RIGHT,ST_MENU,MENU_SINGLE,2,0},
{B_CONFIRM,ST_PLAY,-1,-1,0},
{B_MAP,ST_PLAY,-1,-1,0},
{B_PAUSE,ST_PAUSE,-1,-1,0},
{B_PAUSE,ST_PLAY,-1,-1,0},
{B_MAP,ST_PLAY,-1,-1,0},
{B_LIGHT,ST_PLAY,-1,-1,0},
{4096,ST_PLAY,-1,-1,0}, /* Retired R2 action is ignored. */
{B_PAUSE,ST_PAUSE,-1,-1,0},
{B_BACK,ST_MENU,MENU_MAIN,0,0},
{B_DOWN,ST_MENU,MENU_MAIN,1,0},
{B_CONFIRM,ST_MENU,MENU_MULTI,0,0},
{B_CONFIRM,ST_MENU,MENU_COOP,0,0},
{B_RIGHT,ST_MENU,MENU_COOP,1,0},
{B_RIGHT,ST_MENU,MENU_COOP,2,0},
{B_CONFIRM,ST_NETWORK,MENU_COOP,2,0},
{B_BACK,ST_MENU,MENU_COOP,2,0},
{B_BACK,ST_MENU,MENU_MAIN,0,0},
{B_DOWN,ST_MENU,MENU_MAIN,1,0},
{B_CONFIRM,ST_MENU,MENU_MULTI,0,0},
{B_RIGHT,ST_MENU,MENU_MULTI,1,0},
{B_CONFIRM,ST_MENU,MENU_VERSUS,0,0},
{B_RIGHT,ST_MENU,MENU_VERSUS,1,0},
{B_CONFIRM,ST_NETWORK,MENU_VERSUS,1,0},
{B_BACK,ST_MENU,MENU_VERSUS,1,0},
{B_BACK,ST_MENU,MENU_MAIN,0,0},
{B_DOWN,ST_MENU,MENU_MAIN,1,0},
{B_DOWN,ST_MENU,MENU_MAIN,2,0},
{B_CONFIRM,ST_MENU,MENU_SETTINGS,0,0},
{B_CONFIRM,ST_MENU,MENU_SETTINGS,0,0},
{B_DOWN,ST_MENU,MENU_SETTINGS,1,0},
{B_DOWN,ST_MENU,MENU_SETTINGS,2,0},
{B_DOWN,ST_MENU,MENU_SETTINGS,3,0},
{B_DOWN,ST_MENU,MENU_SETTINGS,4,0},
{B_DOWN,ST_MENU,MENU_SETTINGS,5,0},
{B_CONFIRM,ST_MENU,MENU_SETTINGS,5,0},
{B_DOWN,ST_MENU,MENU_SETTINGS,6,0},
{B_CONFIRM,ST_MENU,MENU_MAIN,0,0},
{B_DOWN,ST_MENU,MENU_MAIN,1,0},
{B_DOWN,ST_MENU,MENU_MAIN,2,0},
{B_DOWN,ST_MENU,MENU_MAIN,3,0},
{B_CONFIRM,ST_CREDITS,MENU_MAIN,0,0},
{B_BACK,ST_MENU,MENU_MAIN,0,0},
{B_LEFT,ST_MENU,MENU_MAIN,4,0},
{B_CONFIRM,ST_HELP,MENU_MAIN,0,0},
{B_BACK,ST_MENU,MENU_MAIN,0,0},
{B_CONFIRM,ST_MENU,MENU_SINGLE,0,0},
{B_CONFIRM,ST_PLAY,-1,-1,0},
{0,ST_DEAD,-1,-1,1},
{0,ST_DEAD,-1,-1,0},
{B_CONFIRM,ST_MENU,MENU_MAIN,0,0},
{B_CONFIRM,ST_MENU,MENU_SINGLE,0,0},
{B_RIGHT,ST_MENU,MENU_SINGLE,1,0},
{B_CONFIRM,ST_PLAY,-1,-1,0},
{0,ST_WON,-1,-1,2},
{0,ST_WON,-1,-1,0},
{0,ST_WINSCENE,-1,-1,3},
{B_CONFIRM,ST_MENU,MENU_MAIN,0,0}
};
#define MENU_ROUTE_COUNT (sizeof(menu_route)/sizeof(menu_route[0]))
static void menu_fixture(Game *g,const World *w,int fixture) {
    if(fixture==1){g->enemy=(Vec3){g->feet.x,g->feet.y,g->feet.z+4};g->yaw=0;g->health=.01f;}
    if(fixture==2){g->custards=511;g->collected=9;g->feet=(Vec3){w->config[C_CUSTARD+27],w->config[C_CUSTARD+28],w->config[C_CUSTARD+29]};}
    if(fixture==3){g->state_time=5;}
}
static int menu_step_ok(const Game *g,int step) {
    const MenuStep *s=menu_route+step;
    return g->state==s->state&&(s->page<0||g->menu_page==s->page)&&(s->selection<0||g->menu==s->selection)
        &&(step!=32||g->quality==0)&&(step!=38||g->quality==5);
}
