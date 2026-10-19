// Independent original vertex + fragment draws; no rewritten HSV formula is
// used as the oracle. The reference always materializes both RGBA8 targets.
struct HsvReference : ImpactReference {
    GLuint hsv{};
    HsvReference(const std::string& folder,unsigned width,unsigned height):ImpactReference(folder,width,height){
        hsv=program(read(folder+"/blit.vsh"),read(folder+"/hsv_filter.fsh"));
    }
    ~HsvReference(){glDeleteProgram(hsv);}
    void draw(const Target& source,const PostEffectStage& stage){
        begin(blit,temporary,source.color);
        glUniform4fv(glGetUniformLocation(blit,"ColorModulate"),1,stage.params.data()+3);
        glDrawArrays(GL_TRIANGLE_FAN,0,4);
        begin(hsv,output,temporary.color);
        glUniform2fv(glGetUniformLocation(hsv,"Hue_W"),1,stage.params.data());
        glUniform1f(glGetUniformLocation(hsv,"Alpha"),stage.params[2]);
        glDrawArrays(GL_TRIANGLE_FAN,0,4);assert(glGetError()==GL_NO_ERROR);
    }
};
PostEffectStage hsv_stage(){return {8,101,0,0,{1,.97f,.6f,1,1,1,1,0}};}
void hsv_pixels(PostEffectReplay& replay,const std::string& directory,Sentinel& sentinel){
    for(GLenum format:{GL_RGBA8,GL_RGBA16F})for(unsigned width:{32u,64u}){
        Target host(width,32,format,true);HsvReference reference(directory,width,32);
        host.pattern();const auto depth=host.depth_stencil();
        const auto extra=host.pixels(1);
        std::vector<Color> input(width*32);
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<width;++x)
            input[y*width+x]={float(x)/width*2-.2f,float(y)/32,float((x^y)&7)/7,float(x%5)/4};
        auto restore=[&](){ordinary();glBindTexture(GL_TEXTURE_2D,host.color);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,32,GL_RGBA,GL_FLOAT,input.data());glBindTexture(GL_TEXTURE_2D,0);};
        for(float alpha:{0.f,.2f,1.f})for(bool tinted:{false,true}){
            restore();auto s=hsv_stage();s.params[2]=alpha;
            if(tinted){s.params[3]=.5f;s.params[4]=1.3f;s.params[5]=.8f;s.params[6]=.4f;}
            reference.draw(host,s);const auto expected=reference.output.pixels();
            ordinary();sentinel.set();const auto saved=state();const auto passes=replay.statistics().passes;
            assert(checked_replay(replay,host.frame(),{&s,1},nullptr,nullptr));assert(state()==saved);
            assert(replay.statistics().passes-passes==(format==GL_RGBA8?2:4));
            compare_pixels(host.pixels(),expected,2.1f/255);assert(host.depth_stencil()==depth&&host.pixels(1)==extra);
        }
        restore();auto first=hsv_stage(),last=hsv_stage();last.params[2]=1;last.params[3]=.7f;
        reference.draw(host,first);reference.draw(reference.output,last);const auto chained=reference.output.pixels();
        const std::array<PostEffectStage,2> graph{first,last};
        ordinary();assert(checked_replay(replay,host.frame(),graph,nullptr,nullptr));compare_pixels(host.pixels(),chained,2.1f/255);
        restore();const auto original=host.pixels();auto inset=host.frame();inset.viewport[0]=4;inset.viewport[1]=3;inset.viewport[2]=width-8;inset.viewport[3]=24;
        reference.draw(host,first);auto expected=reference.output.pixels();
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<width;++x)if(x<4||x>=width-4||y<3||y>=27)expected[y*width+x]=original[y*width+x];
        ordinary();assert(checked_replay(replay,inset,{&first,1},nullptr,nullptr));compare_pixels(host.pixels(),expected,2.1f/255);
        assert(host.depth_stencil()==depth&&host.pixels(1)==extra);
    }
}
