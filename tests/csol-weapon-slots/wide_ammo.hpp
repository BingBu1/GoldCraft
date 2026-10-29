// Replay engine calls captured by executing the exact copied CS ammo caller.
// Included inside client.cpp's fixture namespace.
struct AmmoOp {char kind{};std::array<int,8> a{};};
std::vector<AmmoOp> ammoOps;
int NativeAmmoFrame(float,int)
{
    for(const auto& op:ammoOps)
    {
        const auto& a=op.a;
        if(op.kind=='S')engine.pfnSPR_Set(a[0],a[1],a[2],a[3]);
        else if(op.kind=='D'){wrect_t r{a[3],a[4],a[5],a[6]};engine.pfnSPR_DrawAdditive(a[0],a[1],a[2],&r);}
        else engine.pfnFillRGBA(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);
    }
    return 17;
}
void WideAmmoChecks(const char* path)
{
    std::ifstream input(path);Check(bool(input));
    int width,height,magazine,legacy,opCount,rows{};
    const WeaponDefinition knife{220,1,1,17,-1,29,-1,1,0,"weapon_tknifeex2"};
    while(input>>width>>height>>magazine>>legacy>>opCount)
    {
        ++rows;ammoOps.clear();
        for(int i=0;i<opCount;++i){AmmoOp op;input>>op.kind;const int n=op.kind=='S'?4:op.kind=='D'?7:8;
            for(int k=0;k<n;++k)input>>op.a[k];ammoOps.push_back(op);}
        Check(bool(input));
        screenWidth=width;screenHeight=height;view.health=100;view.spectator=0;hud=1;
        ResetWeapons(true);WeaponVideoInit();downstream.HUD_Redraw=NativeAmmoFrame;
        Send(WeaponDefinitionMessage,EncodeWeaponDefinition(knife));Legacy();
        Send(WeaponCurrentMessage,EncodeWeaponCurrent({28,1,magazine,legacy,0}));
        auto draw=[&](int intermission=0){spriteDraws.clear();spriteColors.clear();rectangles.clear();Check(WeaponRedraw(now,intermission)==17);};
        draw();const auto baseline=spriteDraws;const auto baseRects=rectangles;const auto baseColors=spriteColors;
        Check(baseline.size()>=3);
        if(legacy!=255)continue;
        const int w=width<640?12:20;
        const std::size_t first=baseline.size()-4; // three reserve glyphs, then its ammo icon
        for(int count:{256,500,505,545,660,800,1000,32767})
        {
            Send(WeaponCurrentMessage,EncodeWeaponCurrent({28,1,magazine,count,0}));
            unsigned char bytes[]{2,255};const int nativeBefore=nativeMessages;
            messages["AmmoX"]("AmmoX",2,bytes);Check(nativeMessages==nativeBefore+1);
            draw();
            const std::string number=std::to_string(count);const int shift=(int(number.size())-3)*w;
            auto expected=baseline;expected.erase(expected.begin()+first,expected.begin()+first+3);
            for(std::size_t i=0;i<first;++i)expected[i][1]-=shift;
            constexpr int left[]{0,23,47,70,95,119,144,169,192,216};
            for(std::size_t i=0;i<number.size();++i){auto glyph=baseline[first];const int d=number[i]-'0';
                glyph[1]+=int(i)*w-shift;glyph[3]=width<640?d*w:left[d];glyph[4]=glyph[3]+w;
                expected.insert(expected.begin()+first+i,glyph);}
            Check(spriteDraws==expected); // no original 255 underneath, no change to icon
            auto expectedRects=baseRects;if(!expectedRects.empty())expectedRects[0][0]-=shift;
            Check(rectangles==expectedRects); // no opaque rectangle; only original divider may move
            for(auto color:spriteColors)Check(color[1]==baseColors[0][1] && color[2]==baseColors[0][2] && color[3]==baseColors[0][3]);
        }
        for(int gate=0;gate<8;++gate)
        {
            Send(WeaponCurrentMessage,EncodeWeaponCurrent({28,1,magazine,500,0}));
            unsigned char hidden=gate==0?1:gate==1?4:0;messages["HideWeapon"]("HideWeapon",1,&hidden);
            view.health=gate==2?0:100;view.spectator=gate==3;hud=gate==4?0:1;
            if(gate==6){unsigned char current[]{1,1,12};messages["CurWeapon"]("CurWeapon",3,current);}
            if(gate==7)ResetWeapons(false);
            draw(gate==5);Check(spriteDraws==baseline && rectangles==baseRects);
        }
        view.health=100;view.spectator=0;hud=1;ResetWeapons(true);draw();
        Check(spriteDraws==baseline); // ordinary server/session has no negotiated current state
    }
    Check(rows==12);std::cout<<"PASS wide ammo native replay "<<checks<<" checks / "<<rows<<" oracle cases\n";
}
