// Adapt the map's existing zmspawn markers to native CS player spawn entities.
// Waypoint export uses actual ReHLDS standing hull traces, including brush entities.
#include <amxmodx>
#include <engine>
#include <fakemeta>
#include <reapi>

#define MAX_NODES 1024
#define GRID_SIZE 64
new bool:gMap;
new gTrace,gCount,gGrid[GRID_SIZE][GRID_SIZE];
new Float:gPoint[MAX_NODES][3],gLinks[MAX_NODES][8];
new const gDx[8]={-1,1,0,0,-1,-1,1,1};
new const gDy[8]={0,0,-1,1,-1,1,-1,1};

public plugin_init()
{
    register_plugin("GoldCraft Bloodmoon CS adapter","0.1.0","GoldCraft contributors");
    register_srvcmd("gc_bloodmoon_nav","ExportNavigation");
    register_srvcmd("gc_bloodmoon_spawns","SpawnStatus");
    new map[32];get_mapname(map,charsmax(map));gMap=equali(map,"sy_zombie2_Bloodmoon")!=0;
    if(!gMap)return;
    gTrace=create_tr2();
    new marker,count,name[64],Float:origin[3],Float:floor[3];
    while((marker=find_ent_by_class(marker,"info_target"))>0){
        pev(marker,pev_targetname,name,charsmax(name));
        if(containi(name,"zmspawn")!=0)continue;
        pev(marker,pev_origin,origin);
        if(!FindFloor(origin,floor))continue;
        // ReGameDLL's spawn selection/count uses its classname hash table.
        // engine.create_entity alone is invisible to UTIL_CountEntities.
        new entity=rg_create_entity((count%2)==0?"info_player_deathmatch":"info_player_start",true);
        if(!entity)continue;
        entity_set_origin(entity,floor);DispatchSpawn(entity);count++;
    }
    server_print("[GoldCraft Bloodmoon] Added %d hull-checked CS spawn points from map markers.",count);
    // CheckLevelInitialized can run before AMXX plugin_init. Refresh the native
    // TeamFull limits from the entities actually created, without bypassing it.
    set_member_game(m_iSpawnPointCount_Terrorist,CountSpawns("info_player_deathmatch"));
    set_member_game(m_iSpawnPointCount_CT,CountSpawns("info_player_start"));
}
stock CountSpawns(const classname[])
{
    new entity,count;
    while((entity=find_ent_by_class(entity,classname))>0)count++;
    return count;
}
public SpawnStatus()
{
    server_print("[GoldCraft Bloodmoon] Native spawns T=%d CT=%d; entity spawns T=%d CT=%d",
        get_member_game(m_iSpawnPointCount_Terrorist),get_member_game(m_iSpawnPointCount_CT),
        CountSpawns("info_player_deathmatch"),CountSpawns("info_player_start"));
    return PLUGIN_HANDLED;
}
public plugin_end(){if(gTrace)free_tr2(gTrace);}
stock bool:Clear(const Float:a[3],const Float:b[3])
{
    engfunc(EngFunc_TraceHull,a,b,IGNORE_MONSTERS,HULL_HUMAN,0,gTrace);
    new Float:fraction;get_tr2(gTrace,TR_flFraction,fraction);
    return !get_tr2(gTrace,TR_StartSolid)&&!get_tr2(gTrace,TR_AllSolid)&&fraction>=0.9999;
}
stock bool:FindFloor(const Float:position[3],Float:result[3])
{
    new Float:top[3],Float:bottom[3],Float:fraction,Float:normal[3];
    for(new i=0;i<3;i++){top[i]=position[i];bottom[i]=position[i];}
    top[2]+=18.0;bottom[2]-=128.0;
    engfunc(EngFunc_TraceHull,top,bottom,IGNORE_MONSTERS,HULL_HUMAN,0,gTrace);
    get_tr2(gTrace,TR_flFraction,fraction);get_tr2(gTrace,TR_vecPlaneNormal,normal);
    if(get_tr2(gTrace,TR_StartSolid)||get_tr2(gTrace,TR_AllSolid)||fraction>=1.0||normal[2]<0.7)return false;
    get_tr2(gTrace,TR_vecEndPos,result);result[2]+=0.5;
    return Clear(result,result);
}
stock bool:Walk(const Float:a[3],const Float:b[3])
{
    if(floatabs(a[2]-b[2])>18.0)return false;
    if(!Clear(a,b)){
        new Float:upA[3],Float:upB[3];
        for(new i=0;i<3;i++){upA[i]=a[i];upB[i]=b[i];}
        upA[2]+=18.0;upB[2]+=18.0;
        if(!Clear(a,upA)||!Clear(upA,upB)||!Clear(upB,b))return false;
    }
    new steps=floatround(get_distance_f(a,b)/24.0,floatround_ceil),Float:p[3],Float:floor[3];
    for(new n=1;n<steps;n++){
        for(new i=0;i<3;i++)p[i]=a[i]+(b[i]-a[i])*float(n)/float(steps);
        if(!FindFloor(p,floor)||floatabs(p[2]-floor[2])>18.0)return false;
    }
    return true;
}
public ExportNavigation()
{
    if(!gMap||read_argc()!=7||get_playersnum()>0){
        server_print("gc_bloodmoon_nav minX minY maxX maxY originZ spacing; requires an empty Bloodmoon server.");
        return PLUGIN_HANDLED;
    }
    new arg[24],Float:v[6];
    for(new i=0;i<6;i++){read_argv(i+1,arg,charsmax(arg));v[i]=str_to_float(arg);if(floatabs(v[i])>8192.0)return PLUGIN_HANDLED;}
    if(v[5]<64.0||v[5]>256.0)return PLUGIN_HANDLED;
    new width=floatround((v[2]-v[0])/v[5],floatround_floor)+1;
    new height=floatround((v[3]-v[1])/v[5],floatround_floor)+1;
    if(width<1||height<1||width>GRID_SIZE||height>GRID_SIZE)return PLUGIN_HANDLED;
    gCount=0;
    new Float:position[3],Float:floor[3];position[2]=v[4];
    for(new x=0;x<width;x++)for(new y=0;y<height;y++){
        gGrid[x][y]=-1;position[0]=v[0]+x*v[5];position[1]=v[1]+y*v[5];
        if(!FindFloor(position,floor))continue;
        if(gCount>=MAX_NODES){server_print("[GoldCraft navigation] Too many nodes; use wider spacing.");return PLUGIN_HANDLED;}
        for(new axis=0;axis<3;axis++)gPoint[gCount][axis]=floor[axis];
        gGrid[x][y]=gCount++;
    }
    new edges;
    for(new x=0;x<width;x++)for(new y=0;y<height;y++){
        new from=gGrid[x][y];if(from<0)continue;
        for(new d=0;d<8;d++){
            gLinks[from][d]=-1;new nx=x+gDx[d],ny=y+gDy[d];
            if(nx<0||nx>=width||ny<0||ny>=height)continue;
            new to=gGrid[nx][ny];if(to<0||!Walk(gPoint[from],gPoint[to]))continue;
            gLinks[from][d]=to;edges++;
        }
    }
    new file=fopen("addons/sypb/wptdefault/bloodmoon-hull-nav.csv","wt");
    if(!file)return PLUGIN_HANDLED;
    fprintf(file,"id,x,y,z,n0,n1,n2,n3,n4,n5,n6,n7^n");
    for(new i=0;i<gCount;i++)fprintf(file,"%d,%.4f,%.4f,%.4f,%d,%d,%d,%d,%d,%d,%d,%d^n",i,gPoint[i][0],gPoint[i][1],gPoint[i][2],gLinks[i][0],gLinks[i][1],gLinks[i][2],gLinks[i][3],gLinks[i][4],gLinks[i][5],gLinks[i][6],gLinks[i][7]);
    fclose(file);server_print("[GoldCraft navigation] Exported %d nodes and %d directed standing-hull paths.",gCount,edges);
    return PLUGIN_HANDLED;
}
