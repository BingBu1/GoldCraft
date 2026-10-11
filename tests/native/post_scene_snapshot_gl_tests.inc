// Real GPU copies from distinct scene attachments, using the production
// snapshot implementation. This is not evidence of live callback placement.
void post_scene_snapshot_tests(Resources& r){
    using goldcraft::render::PostSceneSnapshot;
    struct Source {
        GLuint fbo{},color[2]{},depth{},depth_view{};
        MetaRendererPostSceneFrame frame{};
        Source(unsigned width,unsigned height,unsigned format){
            glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
            glGenTextures(2,color);glGenTextures(1,&depth);
            for(unsigned i=0;i<2;++i){
                glBindTexture(GL_TEXTURE_2D,color[i]);glTexStorage2D(GL_TEXTURE_2D,1,format,width,height);
                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,GL_TEXTURE_2D,color[i],0);
            }
            glBindTexture(GL_TEXTURE_2D,depth);glTexStorage2D(GL_TEXTURE_2D,1,GL_DEPTH24_STENCIL8,width,height);
            glGenTextures(1,&depth_view);glTextureView(depth_view,GL_TEXTURE_2D,depth,GL_DEPTH24_STENCIL8,0,1,0,1);
            glBindTexture(GL_TEXTURE_2D,depth_view);glTexParameteri(GL_TEXTURE_2D,GL_DEPTH_STENCIL_TEXTURE_MODE,GL_DEPTH_COMPONENT);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,depth,0);
            const GLenum targets[]{GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};glDrawBuffers(2,targets);
            assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
            glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);glDepthMask(GL_TRUE);glStencilMask(0xff);
            for(unsigned i=0;i<2;++i)glColorMaski(i,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            const GLfloat first[]{.25f,.5f,format==GL_RGBA16F?2.0f:.75f,1},second[]{1,0,0,1};
            glClearBufferfv(GL_COLOR,0,first);glClearBufferfv(GL_COLOR,1,second);glClearBufferfi(GL_DEPTH_STENCIL,0,.375f,83);
            glEnable(GL_SCISSOR_TEST);glScissor(0,0,width/2,height/2);
            const GLfloat corner[]{0,1,0,.5f};glClearBufferfv(GL_COLOR,0,corner);glClearBufferfi(GL_DEPTH_STENCIL,0,.75f,41);
            glDisable(GL_SCISSOR_TEST);glReadBuffer(GL_COLOR_ATTACHMENT1);
            frame.struct_size=sizeof(frame);frame.framebuffer=fbo;frame.color_texture=color[0];frame.depth_texture=depth;
            frame.depth_sample_texture=depth_view;frame.width=width;frame.height=height;frame.color_format=format;
            frame.depth_format=GL_DEPTH24_STENCIL8;frame.samples=1;
            frame.flags=META_POST_SCENE_LINEAR;frame.near_plane=4;frame.far_plane=4096;
            for(unsigned i=0;i<16;i+=5)frame.world_matrix[i]=frame.projection_matrix[i]=frame.inverse_world_matrix[i]=frame.inverse_projection_matrix[i]=1;
            frame.viewport[0]=3;frame.viewport[1]=5;frame.viewport[2]=width-6;frame.viewport[3]=height-10;
        }
        ~Source(){glDeleteFramebuffers(1,&fbo);glDeleteTextures(2,color);glDeleteTextures(1,&depth);glDeleteTextures(1,&depth_view);}
    };
    const auto full_state=[](){
        auto result=snapshot();
        for(auto parameter:{GL_READ_FRAMEBUFFER_BINDING,GL_READ_BUFFER,GL_DRAW_FRAMEBUFFER_BINDING}){
            GLint value{};glGetIntegerv(parameter,&value);result.push_back(value);
        }
        GLint viewport[4]{},scissor[4]{};glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_SCISSOR_BOX,scissor);
        for(auto v:viewport)result.push_back(v);for(auto v:scissor)result.push_back(v);
        result.push_back(glIsEnabled(GL_FRAMEBUFFER_SRGB));result.push_back(glIsEnabled(GL_SCISSOR_TEST));
        return result;
    };
    struct Pixels {std::vector<float> color,depth;std::vector<unsigned char> stencil;bool operator==(const Pixels&) const=default;};
    const auto read=[](GLuint fbo,unsigned width,unsigned height){
        GLint old{},buffer{};glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);glGetIntegerv(GL_READ_BUFFER,&buffer);glReadBuffer(GL_COLOR_ATTACHMENT0);
        Pixels data{std::vector<float>(width*height*4),std::vector<float>(width*height),std::vector<unsigned char>(width*height)};
        glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,data.color.data());
        glReadPixels(0,0,width,height,GL_DEPTH_COMPONENT,GL_FLOAT,data.depth.data());
        glReadPixels(0,0,width,height,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,data.stencil.data());
        glReadBuffer(buffer);glBindFramebuffer(GL_READ_FRAMEBUFFER,old);return data;
    };
    {
        Source source(64,64,GL_RGBA8);
        const auto expected=read(source.fbo,64,64);
        glBindFramebuffer(GL_FRAMEBUFFER,r.fbo);r.sentinel();
        glReadBuffer(GL_COLOR_ATTACHMENT2);glViewport(7,9,37,41);glScissor(1,2,3,4);glEnable(GL_FRAMEBUFFER_SRGB);
        PostSceneSnapshot captured;const auto before=full_state();
        assert(captured.image().framebuffer==0&&captured.resident_bytes()==0);
        assert(captured.capture(source.frame));assert(full_state()==before);
        auto image=captured.image();assert(image.framebuffer&&image.color&&image.depth&&image.generation==1);
        assert(read(image.framebuffer,64,64)==expected&&read(source.fbo,64,64)==expected);
        // A native weapon clear after the callback cannot mutate the copy.
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,source.fbo);glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);glStencilMask(0xff);
        glClearBufferfi(GL_DEPTH_STENCIL,0,1,0);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,r.fbo);
        assert(read(image.framebuffer,64,64)==expected);
        r.sentinel();glViewport(7,9,37,41);glScissor(1,2,3,4);glEnable(GL_FRAMEBUFFER_SRGB);
        const auto warm_state=full_state();const auto old_allocations=captured.allocations();const auto old_queries=probe::queries;
        probe::allocations=0;probe::count_allocations=true;
        for(unsigned i=0;i<256;++i)assert(captured.capture(source.frame));
        probe::count_allocations=false;
        assert(probe::allocations==0&&captured.allocations()==old_allocations&&probe::queries-old_queries==256*5);
        assert(full_state()==warm_state&&captured.image().generation==image.generation&&captured.copies()==257);
        auto invalid=source.frame;invalid.samples=4;const auto copies=captured.copies();
        assert(!captured.capture(invalid)&&captured.copies()==copies);
        invalid=source.frame;invalid.width=invalid.height=8192;
        assert(!captured.capture(invalid)&&captured.resident_bytes()==64*64*8);
        invalid=source.frame;invalid.depth_format=GL_DEPTH_COMPONENT32F;assert(!captured.capture(invalid));
        invalid=source.frame;invalid.viewport[0]=60;assert(!captured.capture(invalid));
        invalid=source.frame;invalid.framebuffer=image.framebuffer;assert(!captured.capture(invalid));
        assert(full_state()==warm_state&&captured.capture(source.frame));
        // Malformed descriptors are rejected before touching driver state.
        const auto metadata_queries=probe::queries;
        invalid=source.frame;invalid.struct_size--;assert(!captured.capture(invalid));
        invalid=source.frame;invalid.flags=0;assert(!captured.capture(invalid));
        invalid=source.frame;invalid.flags=META_POST_SCENE_GAMMA|META_POST_SCENE_LINEAR;assert(!captured.capture(invalid));
        invalid=source.frame;invalid.flags|=0x80000000u;assert(!captured.capture(invalid));
        invalid=source.frame;invalid.view_index=1;assert(!captured.capture(invalid));
        invalid=source.frame;invalid.depth_sample_texture=0;assert(!captured.capture(invalid));
        invalid=source.frame;invalid.color_texture=invalid.depth_texture;assert(!captured.capture(invalid));
        invalid=source.frame;invalid.viewport[0]=-1;assert(!captured.capture(invalid));
        assert(probe::queries==metadata_queries&&captured.image().framebuffer==image.framebuffer);
        assert(captured.capture(source.frame));
        // A caller still bound to the borrowed snapshot cannot resize it.
        const auto saved_framebuffer=captured.image().framebuffer;
        glBindFramebuffer(GL_READ_FRAMEBUFFER,saved_framebuffer);
        const auto bound_state=full_state();
        assert(!captured.capture(source.frame)&&full_state()==bound_state&&glIsFramebuffer(saved_framebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,r.fbo);
        assert(captured.capture(source.frame));
        auto* owner=glfwGetCurrentContext();auto* other=glfwCreateWindow(16,16,"Post scene context isolation",nullptr,nullptr);assert(other);
        glfwMakeContextCurrent(other);const auto query_count=probe::queries;
        assert(!captured.image().framebuffer&&!captured.capture(source.frame)&&!captured.reset()&&probe::queries==query_count);
        glfwMakeContextCurrent(owner);glfwDestroyWindow(other);
        assert(captured.capture(source.frame)&&captured.image().framebuffer==image.framebuffer);
        {
            Source hdr(32,48,GL_RGBA16F);const auto hdr_pixels=read(hdr.fbo,32,48);
            glBindFramebuffer(GL_FRAMEBUFFER,r.fbo);r.sentinel();const auto hdr_state=full_state();
            const auto old_pixels=read(image.framebuffer,64,64);
            const auto old_copies=captured.copies();
            for(unsigned missing_storage:{1u,2u}){
                probe::snapshot_storage_failure=missing_storage;
                assert(!captured.capture(hdr.frame));
                assert(probe::snapshot_storage_failure==0&&captured.copies()==old_copies);
                assert(captured.allocations()==old_allocations&&captured.resident_bytes()==64*64*8);
                assert(full_state()==hdr_state&&read(image.framebuffer,64,64)==old_pixels);
                assert(read(hdr.fbo,32,48)==hdr_pixels&&glGetError()==GL_NO_ERROR);
            }
            assert(captured.capture(source.frame)&&captured.image().generation==image.generation);
            assert(captured.capture(hdr.frame)&&full_state()==hdr_state);
            const auto next=captured.image();assert(next.generation==2&&next.color_format==GL_RGBA16F&&captured.resident_bytes()==32*48*12);
            assert(read(next.framebuffer,32,48)==hdr_pixels&&hdr_pixels.color.back()==1);
            assert(hdr_pixels.color[(32*47+31)*4+2]==2);
            assert(!glIsTexture(image.color)&&!glIsTexture(image.depth)&&!glIsFramebuffer(image.framebuffer));
        }
        captured.invalidate();assert(!captured.image().framebuffer);assert(captured.reset()&&captured.resident_bytes()==0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,r.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);glDisable(GL_FRAMEBUFFER_SRGB);glViewport(0,0,Size,Size);
    assert(glGetError()==GL_NO_ERROR);
    std::puts("{\"postSceneRealGPUCopy\":true,\"scenePreserved\":true,\"colorDepthStencil\":true,\"weaponClearIsolation\":true,\"stateRestored\":true,\"hdrPreserved\":true,\"resize\":true,\"incompleteTargetRecovery\":true,\"metadataRejectedBeforeGL\":true,\"boundSnapshotRejected\":true,\"wrongContextRejected\":true,\"warmFrames\":256,\"warmAllocations\":0,\"warmStorageAllocations\":0,\"warmQueriesPerFrame\":5,\"cpuReadbackInProduction\":false,\"liveAcceptance\":false}");
}


