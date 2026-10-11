// Independent original vendor vertex + two original fragments. Both reference
// targets deliberately use the real Minecraft RGBA8 target format, even when
// the host input is HDR. No ported fragment math acts as the pixel oracle.
struct ImpactReference {
    GLuint blit{},impact{},vao{},vbo{};
    Target temporary,output;
    explicit ImpactReference(const std::string& folder,unsigned width,unsigned height):temporary(width,height),output(width,height){
        const auto vertex=read(folder+"/blit.vsh");blit=program(vertex,read(folder+"/blit.fsh"));impact=program(vertex,read(folder+"/black_white_simple.fsh"));
        glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,nullptr);
        const float vertices[]={0,0,float(width),0,float(width),float(height),0,float(height)};
        glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    }
    ~ImpactReference(){glDeleteProgram(blit);glDeleteProgram(impact);glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&vbo);}
    void begin(GLuint p,const Target& destination,GLuint source){
        ordinary();glBindFramebuffer(GL_DRAW_FRAMEBUFFER,destination.fbo);glViewport(0,0,destination.width,destination.height);
        glUseProgram(p);glBindVertexArray(vao);const float w=float(destination.width),h=float(destination.height);
        const float ortho[]={2/w,0,0,0,0,2/h,0,0,0,0,-2.f/999.9f,0,-1,-1,-1000.1f/999.9f,1};
        glUniformMatrix4fv(glGetUniformLocation(p,"ProjMat"),1,GL_FALSE,ortho);
        glUniform2f(glGetUniformLocation(p,"OutSize"),w,h);glUniform1i(glGetUniformLocation(p,"DiffuseSampler"),0);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source);
    }
    void draw(const Target& source,const PostEffectStage& stage){
        const auto& v=stage.params;
        begin(blit,temporary,source.color);const float unused_clear[]={1,1,1,0};glClearBufferfv(GL_COLOR,0,unused_clear);
        glUniform4fv(glGetUniformLocation(blit,"ColorModulate"),1,v.data()+26);glDrawArrays(GL_TRIANGLE_FAN,0,4);
        begin(impact,output,temporary.color);
        const char* names[]={"Contrast","Brightness","Time","Intensity","Speed","Mode","ImpactThreshold","ImpactThresholdLerp",
            "FocalUV","ChromaticStrength","LensDistortStrength","ColorDark","ColorLight","InvertFactor","CappedGrayscale","RadialStrength","Samples"};
        const unsigned offsets[]={0,1,2,3,4,5,6,7,8,10,11,12,15,18,19,20,21};
        const unsigned counts[]={1,1,1,1,1,1,1,1,2,1,1,3,3,1,1,1,0};
        for(unsigned i=0;i<std::size(names);++i){const auto location=glGetUniformLocation(impact,names[i]);assert(location>=0);
            if(counts[i]==0)glUniform1i(location,int(v[offsets[i]]));
            else if(counts[i]==1)glUniform1fv(location,1,v.data()+offsets[i]);
            else if(counts[i]==2)glUniform2fv(location,1,v.data()+offsets[i]);
            else glUniform3fv(location,1,v.data()+offsets[i]);
        }
        glEnablei(GL_BLEND,0);glBlendEquationi(0,GL_FUNC_ADD);glBlendFunci(0,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glClearBufferfv(GL_COLOR,0,v.data()+22);glDrawArrays(GL_TRIANGLE_FAN,0,4);assert(glGetError()==GL_NO_ERROR);
    }
};
PostEffectStage impact_stage(bool heavy=false){
    return {7,9999,0,0,{2.25f,.65f,.1f,heavy?.85f:.9f,heavy?1.f:1.3f,heavy?1.f:0.f,heavy?.45f:.35f,heavy?.12f:.1f,
        .5f,.5f,heavy?.03f:.05f,heavy?-.25f:-.3f,.02f,.02f,.08f,.95f,.95f,1,0,1,.02f,16,0,0,0,0,1,1,1,1}};
}
void impact_pixels(PostEffectReplay& replay,const std::string& directory,Sentinel& sentinel){
    unsigned cases=0;float maximum_error=0;
    for(GLenum format:{GL_RGBA8,GL_RGBA16F}){
        Target host(64,64,format,true);ImpactReference reference(directory,64,64);host.pattern();
        std::vector<Color> source(64*64);
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
            source[y*64+x]={float(x)/32-.2f,float(y)/40,float((x^y)&15)/15,float(x%5)/4};
        auto restore=[&](){ordinary();glBindTexture(GL_TEXTURE_2D,host.color);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,64,64,GL_RGBA,GL_FLOAT,source.data());glBindTexture(GL_TEXTURE_2D,0);};
        restore();const auto original=host.pixels();const auto depth=host.depth_stencil();const auto extra=host.pixels(1);
        for(bool heavy:{false,true})for(float t:{0.f,.08f,std::nextafter(.33f,0.f),.33f,std::nextafter(.33f,1.f),
            std::nextafter(.5f,0.f),.5f,std::nextafter(.5f,1.f),std::nextafter(.66f,0.f),.66f,std::nextafter(.66f,1.f),.83f,std::nextafter(1.f,0.f),1.f}){
            for(unsigned variant=0;variant<3;++variant){
                auto s=impact_stage(heavy);s.params[2]=t;
                if(variant==1){
                    s.params[0]=3.4f;s.params[1]=.37f;s.params[3]=.72f;s.params[4]=.8f;
                    s.params[6]=.28f;s.params[7]=.23f;s.params[8]=.125f;s.params[9]=.875f;
                    s.params[10]=.017f;s.params[11]=.22f;s.params[12]=.18f;s.params[13]=.09f;s.params[14]=.24f;
                    s.params[15]=.78f;s.params[16]=.89f;s.params[17]=.61f;s.params[18]=.3f;s.params[19]=.83f;s.params[20]=.047f;s.params[21]=33;
                    s.params[22]=.12f;s.params[23]=.24f;s.params[24]=.36f;s.params[25]=.4f;
                    s.params[26]=.7f;s.params[27]=1.2f;s.params[28]=.4f;s.params[29]=.6f;
                }else if(variant==2){s.params[8]=0;s.params[9]=1;s.params[21]=-1;s.params[22]=2;s.params[23]=-1;s.params[25]=.7f;s.params[29]=1.4f;}
                restore();reference.draw(host,s);const auto expected=reference.output.pixels();
                ordinary();sentinel.set();const auto saved=state();const auto passes=replay.statistics().passes;
                assert(checked_replay(replay,host.frame(),{&s,1},nullptr,nullptr));assert(state()==saved);
                assert(replay.statistics().passes-passes==(format==GL_RGBA8?2:4));
                compare_pixels(host.pixels(),expected,2.1f/255);assert(host.depth_stencil()==depth&&host.pixels(1)==extra);
                // Independent alpha equation catches accidentally disabling
                // blending, separate alpha factors, or omitting clear alpha.
                const auto q=[](float f){return std::round(std::clamp(f,0.f,1.f)*255)/255;};
                const auto actual=host.pixels();
                for(unsigned i=0;i<actual.size();++i)for(unsigned c=0;c<4;++c)maximum_error=std::max(maximum_error,std::abs(actual[i][c]-expected[i][c]));
                for(unsigned i=0;i<actual.size();++i){float a=q(original[i][3]*s.params[29]);float expected_alpha=q(a*a+q(s.params[25])*(1-a));assert(std::abs(actual[i][3]-expected_alpha)<=1.1f/255);}
                ++cases;
            }
        }
        // Empty/expired frame never reapplies the last impact. First application
        // to transparent input changes RGB using captured clear, not old ping data.
        restore();auto impact=impact_stage(true);impact.params[2]=.41f;impact.params[25]=.3f;
        std::array<PostEffectStage,3> ordered{stage(6),impact,stage(5)};ordinary();assert(checked_replay(replay,host.frame(),ordered,nullptr,nullptr));const auto ordered_pixels=host.pixels();
        restore();ordinary();for(const auto& item:ordered)assert(checked_replay(replay,host.frame(),{&item,1},nullptr,nullptr));compare_pixels(host.pixels(),ordered_pixels);
        std::reverse(ordered.begin(),ordered.end());restore();ordinary();assert(checked_replay(replay,host.frame(),ordered,nullptr,nullptr));const auto reversed=host.pixels();assert(reversed!=ordered_pixels);
        sentinel.set();const auto saved=state();const auto passes=replay.statistics().passes;assert(checked_replay(replay,host.frame(),{},nullptr,nullptr));assert(state()==saved&&replay.statistics().passes==passes);compare_pixels(host.pixels(),reversed,0);
        // A failing stage after an impact cannot publish the intermediate output.
        restore();Mask mask;mask.fail_on=1;std::array<PostEffectStage,2> failing{impact,stage(2)};ordinary();assert(!checked_replay(replay,host.frame(),failing,Mask::draw,&mask));compare_pixels(host.pixels(),original,0);
        // Final copy only affects the supplied viewport after the two exact passes.
        restore();reference.draw(host,impact);auto expected=original;const auto full=reference.output.pixels();
        auto inset=host.frame();inset.viewport[0]=8;inset.viewport[1]=12;inset.viewport[2]=48;inset.viewport[3]=36;
        for(unsigned y=12;y<48;++y)for(unsigned x=8;x<56;++x)expected[y*64+x]=full[y*64+x];ordinary();assert(checked_replay(replay,inset,{&impact,1},nullptr,nullptr));compare_pixels(host.pixels(),expected,2.1f/255);
        ordinary();const auto allocations=replay.statistics().allocations;const auto storages=probe::storages,locations=probe::locations,uploads=probe::uploads;
        probe::allocations=0;probe::count=true;for(unsigned i=0;i<32;++i)assert(checked_replay(replay,host.frame(),{&impact,1},nullptr,nullptr));probe::count=false;
        assert(probe::allocations==0&&probe::storages==storages&&probe::locations==locations&&probe::uploads==uploads&&replay.statistics().allocations==allocations);
        ordinary();glUseProgram(0);glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);
    }
    std::printf("{\"impactReferenceCases\":%u,\"maximumPixelError\":%.9f,\"modes\":2,\"phaseBoundaries\":true,\"alphaBlendEquation\":true,\"effectiveOverrides\":true,\"rgba8QuantizationOnHdr\":true,\"ldrPasses\":2,\"hdrPasses\":4,\"reorder\":true,\"emptyNoop\":true,\"failureAtomic\":true,\"warmAllocations\":0,\"warmStorage\":0,\"warmUniformLookups\":0,\"warmBufferUploads\":0}\n",cases,maximum_error);
}
