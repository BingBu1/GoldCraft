// Actual GL_NONE depth/stencil targets and production VS/GS/FS. These pixel and
// state oracles do not establish live FPS or Renderer callback placement.
namespace shadow_depth_probe {
constexpr std::uint8_t stencil_clear=0x5a;
constexpr std::uint32_t shadow_flags=META_SCENE_SHADOW;
unsigned cases{};

struct ExtraBlend {
    GLboolean enabled{},mask[4]{};
    GLint src_rgb{},dst_rgb{},src_alpha{},dst_alpha{},eq_rgb{},eq_alpha{};
    explicit ExtraBlend(unsigned i){
        enabled=glIsEnabledi(GL_BLEND,i);glGetBooleani_v(GL_COLOR_WRITEMASK,i,mask);
        glGetIntegeri_v(GL_BLEND_SRC_RGB,i,&src_rgb);glGetIntegeri_v(GL_BLEND_DST_RGB,i,&dst_rgb);
        glGetIntegeri_v(GL_BLEND_SRC_ALPHA,i,&src_alpha);glGetIntegeri_v(GL_BLEND_DST_ALPHA,i,&dst_alpha);
        glGetIntegeri_v(GL_BLEND_EQUATION_RGB,i,&eq_rgb);glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA,i,&eq_alpha);
    }
    void restore(unsigned i)const{
        if(enabled)glEnablei(GL_BLEND,i);else glDisablei(GL_BLEND,i);
        glColorMaski(i,mask[0],mask[1],mask[2],mask[3]);
        glBlendFuncSeparatei(i,src_rgb,dst_rgb,src_alpha,dst_alpha);glBlendEquationSeparatei(i,eq_rgb,eq_alpha);
    }
};

struct FixtureScope {
    // The fixture changes additional host state beyond Pass. Restore it locally
    // so the old effect/HUD/MRT oracles and comparison hash remain independent.
    goldcraft::render::DrawState draw{true,false,false};
    goldcraft::render::PixelState pack{true};
    Resources& r;
    decltype(programs) saved_programs;
    bool saved_shader;
    GLint draw_fbo{},read_fbo{},read_buffer{},viewport[4]{},scissor[4]{},uniform_buffer{};
    GLint texture3{},sampler3{},cube{},array{};
    GLboolean scissor_enabled{};
    std::vector<GLenum> targets;
    std::vector<ExtraBlend> extra_blend;
    struct Ubo {GLint buffer{};GLint64 start{},size{};} ubos[2];
    explicit FixtureScope(Resources& resources):r(resources),saved_programs(std::move(programs)),saved_shader(r.adapter.effects_shader){
        programs.clear();r.adapter.effects_shader=true;draw.save_feedback();draw.save_effects();
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_fbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_fbo);
        glGetIntegerv(GL_READ_BUFFER,&read_buffer);glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_SCISSOR_BOX,scissor);
        scissor_enabled=glIsEnabled(GL_SCISSOR_TEST);
        for(GLint i=0;i<max_draw_buffers;++i){GLint target{};glGetIntegerv(GL_DRAW_BUFFER0+i,&target);targets.push_back(target);}
        for(GLint i=0;i<max_draw_buffers;++i)extra_blend.emplace_back(i);
        glGetIntegerv(GL_UNIFORM_BUFFER_BINDING,&uniform_buffer);
        for(unsigned i=0;i<2;++i){
            glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,i,&ubos[i].buffer);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_START,i,&ubos[i].start);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE,i,&ubos[i].size);
        }
        glActiveTexture(GL_TEXTURE3);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture3);glGetIntegeri_v(GL_SAMPLER_BINDING,3,&sampler3);
        glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&cube);glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&array);
    }
    ~FixtureScope(){
        programs=std::move(saved_programs);r.adapter.effects_shader=saved_shader;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw_fbo);glDrawBuffers(static_cast<GLsizei>(targets.size()),targets.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);glReadBuffer(read_buffer);
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glScissor(scissor[0],scissor[1],scissor[2],scissor[3]);
        if(scissor_enabled)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
        for(unsigned i=0;i<extra_blend.size();++i)extra_blend[i].restore(i);
        for(unsigned i=0;i<2;++i){
            if(ubos[i].buffer&&ubos[i].size)glBindBufferRange(GL_UNIFORM_BUFFER,i,ubos[i].buffer,
                static_cast<GLintptr>(ubos[i].start),static_cast<GLsizeiptr>(ubos[i].size));
            else glBindBufferBase(GL_UNIFORM_BUFFER,i,ubos[i].buffer);
        }
        glBindBuffer(GL_UNIFORM_BUFFER,uniform_buffer);
        glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,texture3);glBindSampler(3,sampler3);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_CUBE_MAP,cube);glBindTexture(GL_TEXTURE_2D_ARRAY,array);
    }
};

std::vector<double> full_state(){
    auto result=snapshot();
    GLint value{};glGetIntegerv(GL_READ_BUFFER,&value);result.push_back(value);
    for(GLint i=0;i<max_draw_buffers;++i){
        glGetIntegerv(GL_DRAW_BUFFER0+i,&value);result.push_back(value);
        ExtraBlend blend(i);result.push_back(blend.enabled);
        for(auto channel:blend.mask)result.push_back(channel);
        for(auto parameter:{blend.src_rgb,blend.dst_rgb,blend.src_alpha,blend.dst_alpha,blend.eq_rgb,blend.eq_alpha})result.push_back(parameter);
    }
    for(auto parameter:{GL_VIEWPORT,GL_SCISSOR_BOX}){
        GLint box[4]{};glGetIntegerv(parameter,box);for(auto coordinate:box)result.push_back(coordinate);
    }
    for(auto parameter:{GL_UNIFORM_BUFFER_BINDING,GL_PIXEL_PACK_BUFFER_BINDING,GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS}){
        glGetIntegerv(parameter,&value);result.push_back(value);
    }
    for(unsigned i=0;i<2;++i){
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,i,&value);result.push_back(value);
        for(auto parameter:{GL_UNIFORM_BUFFER_START,GL_UNIFORM_BUFFER_SIZE}){
            GLint64 range{};glGetInteger64i_v(parameter,i,&range);result.push_back(static_cast<double>(range));
        }
    }
    GLint active{};glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);
    for(auto parameter:{GL_TEXTURE_BINDING_CUBE_MAP,GL_TEXTURE_BINDING_2D_ARRAY}){
        glGetIntegerv(parameter,&value);result.push_back(value);
    }
    glActiveTexture(active);
    return result;
}

void sentinel(Resources& r,bool stencil_enabled=true){
    r.sentinel();glViewport(0,0,Size,Size);
    glStencilFuncSeparate(GL_FRONT,GL_NEVER,0x31,0x73);
    glStencilFuncSeparate(GL_BACK,GL_NEVER,0x62,0xa9);
    if(!stencil_enabled)glDisable(GL_STENCIL_TEST);
    for(GLint i=0;i<max_draw_buffers;++i){
        if(i&1)glEnablei(GL_BLEND,i);else glDisablei(GL_BLEND,i);
        glColorMaski(i,i&1,0,1,0);
        glBlendFuncSeparatei(i,GL_ONE_MINUS_DST_COLOR,GL_DST_COLOR,GL_ZERO,GL_ONE);
        glBlendEquationSeparatei(i,(i&1)?GL_MAX:GL_FUNC_SUBTRACT,GL_FUNC_REVERSE_SUBTRACT);
    }
}

struct Target {
    GLuint fbo{},texture{},read_fbo{};
    GLenum kind;
    unsigned layers;
    Target(GLenum target,unsigned count):kind(target),layers(count){
        glGenFramebuffers(1,&fbo);glGenFramebuffers(1,&read_fbo);glGenTextures(1,&texture);
        glActiveTexture(GL_TEXTURE0);glBindTexture(kind,texture);
        if(kind==GL_TEXTURE_2D_ARRAY)glTexStorage3D(kind,1,GL_DEPTH32F_STENCIL8,Size,Size,layers);
        else glTexStorage2D(kind,1,GL_DEPTH32F_STENCIL8,Size,Size);
        glTexParameteri(kind,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(kind,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,texture,0);
        glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    }
    void clear(){
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
        glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);glStencilMask(0xff);
        glClearBufferfi(GL_DEPTH_STENCIL,0,1,stencil_clear);
    }
    struct Pixels {std::vector<float> depth;std::vector<std::uint8_t> stencil;};
    Pixels read(unsigned layer=0){
        assert(layer<layers);GLint previous{};glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);
        if(kind==GL_TEXTURE_2D_ARRAY)glFramebufferTextureLayer(GL_READ_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,texture,0,layer);
        else glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,
            kind==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+layer:GL_TEXTURE_2D,texture,0);
        glReadBuffer(GL_NONE);assert(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        Pixels result{std::vector<float>(Size*Size),std::vector<std::uint8_t>(Size*Size)};
        glReadPixels(0,0,Size,Size,GL_DEPTH_COMPONENT,GL_FLOAT,result.depth.data());
        glReadPixels(0,0,Size,Size,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,result.stencil.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER,previous);return result;
    }
    ~Target(){glDeleteFramebuffers(1,&read_fbo);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);}
};

struct Camera {
    scene_ubo_t scene{};
    camera_ubo_t camera{};
    GLuint buffers[2]{};
    Camera(){
        scene.r_g=scene.v_gamma=scene.v_texgamma=scene.v_lightgamma=1;scene.r_g3=.125f;
        glGenBuffers(2,buffers);glBindBuffer(GL_UNIFORM_BUFFER,buffers[0]);
        glBufferData(GL_UNIFORM_BUFFER,sizeof(scene),&scene,GL_STATIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,0,buffers[0]);
        identity();
    }
    void identity(unsigned views=1){
        camera={};camera.numViews=views;
        for(unsigned view=0;view<views;++view){
            camera.views[view].vup_zfar[3]=256;
            for(unsigned i=0;i<4;++i)camera.views[view].worldMatrix[i][i]=camera.views[view].projMatrix[i][i]=1;
        }
        upload();
    }
    void upload(){glBindBuffer(GL_UNIFORM_BUFFER,buffers[1]);glBufferData(GL_UNIFORM_BUFFER,sizeof(camera),&camera,GL_STATIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,1,buffers[1]);}
    ~Camera(){glDeleteBuffers(2,buffers);}
};

void fast_calls(){
    assert(probe::draw_buffer_queries==static_cast<std::size_t>(max_draw_buffers));
    assert(probe::limit_queries==0&&probe::stencil_parameter_queries==0&&probe::color_state_queries==0);
    assert(probe::stencil_parameter_writes==0&&probe::color_state_writes==0);
}
void stencil_untouched(const Target::Pixels& pixels){
    assert(std::all_of(pixels.stencil.begin(),pixels.stencil.end(),[](auto value){return value==stencil_clear;}));
}
void quad_depth(const Target::Pixels& pixels,float depth,bool half=false){
    for(int y=0;y<Size;++y)for(int x=0;x<Size;++x){
        const bool inside=x>=(half?32:16)&&x<48&&y>=16&&y<48;
        assert(std::abs(pixels.depth[y*Size+x]-(inside?depth:1.f))<1e-6f);
    }
}

void tests(Resources& r){
    assert(max_draw_buffers==probe::draw_buffer_slots&&max_draw_buffers>=4);
    const auto before_fixture=full_state();
    {
    FixtureScope restore(r);
    Target flat(GL_TEXTURE_2D,1),cube(GL_TEXTURE_CUBE_MAP,6),cascades(GL_TEXTURE_2D_ARRAY,4);
    Camera camera;
    GLuint color{};glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,Size,Size);
    GLuint texture{};unsigned width{},height{};
    const std::array<std::uint8_t,8> cutout{255,255,255,0,255,255,255,255};
    upload_texture(texture,width,height,2,1,cutout);
    Mesh mesh;auto quad=r.quad;std::uint64_t revision=1;
    const auto draw=[&](Target& target,std::uint32_t flags,bool emissive=false,bool depth_write=true){
        target.clear();sentinel(r);const auto before=full_state();probe::reset();
        const auto initial_passes=statistics().depth_only_shadow_passes;
        {Pass pass(false,flags);assert(pass.ready());pass.texture(texture);pass.emissive(emissive);
            pass.depth_write(depth_write);pass.view_offset(true);mesh.draw(quad,revision,false);}
        fast_calls();assert(statistics().depth_only_shadow_passes==initial_passes+1);
        assert(full_state()==before&&glGetError()==GL_NO_ERROR);++cases;
    };

    draw(flat,shadow_flags);
    auto pixels=flat.read();quad_depth(pixels,.5f,true);stencil_untouched(pixels);
    // Having a color attachment is harmless when every draw route is NONE.
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    draw(flat,shadow_flags|META_SCENE_GAMMA_BLEND);quad_depth(flat.read(),.5f,true);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,0,0);
    draw(flat,shadow_flags,false,false);quad_depth(flat.read(),1.f);stencil_untouched(flat.read());

    // Alpha cutoff is texture alpha multiplied by the real exported vertex
    // alpha, not alpha-to-coverage or a synthetic fixture fragment shader.
    for(const auto test:std::array<std::array<unsigned,3>,6>{{{25,255,0},{26,255,1},{255,25,0},{255,26,1},{128,50,0},{128,51,1}}}){
        const std::array<std::uint8_t,4> texel{255,255,255,static_cast<std::uint8_t>(test[0])};
        upload_texture(texture,width,height,1,1,texel);
        for(auto& vertex:quad)vertex.color=(test[1]<<24)|0xffffff;++revision;
        draw(flat,shadow_flags);pixels=flat.read();quad_depth(pixels,test[2]?.5f:1.f);stencil_untouched(pixels);
    }
    upload_texture(texture,width,height,2,1,cutout);quad=r.quad;++revision;

    // Spot/projected linear shadow: real GS-unit distance, including the
    // emissive near-light discard; no matrix/shader approximation in Pass.
    camera.camera.views[0].projMatrix[0][0]=camera.camera.views[0].projMatrix[1][1]=1.f/32;
    camera.camera.views[0].projMatrix[2][2]=0;camera.upload();
    for(bool near_light:{true,false})for(bool emissive:{false,true}){
        for(auto& vertex:quad){vertex.x*=32;vertex.y*=32;vertex.z=near_light?16.f:64.f;}++revision;
        draw(flat,shadow_flags|META_SCENE_LINEAR_DEPTH,emissive);pixels=flat.read();stencil_untouched(pixels);
        for(int y=0;y<Size;++y)for(int x=0;x<Size;++x){
            const bool inside=x>=32&&x<48&&y>=16&&y<48&&!(near_light&&emissive);
            const float px=x+.5f-32,py=y+.5f-32,pz=near_light?16.f:64.f;
            const float expected=inside?std::sqrt(px*px+py*py+pz*pz)/256:1.f;
            assert(std::abs(pixels.depth[y*Size+x]-expected)<2e-6f);
        }
        quad=r.quad;
    }
    ++revision;camera.identity();

    // CSM uses the real geometry shader and gl_Layer on a four-layer target.
    // Distinct per-cascade projections make a missing/wrong layer observable.
    const auto geometry_before=probe::geometry_shaders;
    camera.identity(4);
    for(unsigned i=0;i<4;++i)camera.camera.views[i].projMatrix[3][2]=-.6f+.4f*i;
    camera.upload();draw(cascades,shadow_flags|META_SCENE_MULTIVIEW);
    assert(probe::geometry_shaders>geometry_before);
    for(unsigned i=0;i<4;++i){pixels=cascades.read(i);quad_depth(pixels,.2f+.2f*i,true);stencil_untouched(pixels);}

    // Six actual cubemap faces with 90-degree perspective views. Quads are
    // centered on +/-X,+/-Y,+/-Z at distinct radii; each face has an alpha hole.
    const std::array<std::array<float,3>,6> forward{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    const std::array<std::array<float,3>,6> right{{{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}}};
    const std::array<std::array<float,3>,6> up{{{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}}};
    camera.camera={};camera.camera.numViews=6;
    std::vector<Vertex> cube_quads;
    for(unsigned face=0;face<6;++face){
        auto& view=camera.camera.views[face];const float radius=64.f+8*face;
        for(unsigned axis=0;axis<3;++axis){
            view.worldMatrix[axis][0]=right[face][axis];view.worldMatrix[axis][1]=up[face][axis];
            view.worldMatrix[axis][2]=-forward[face][axis];
        }
        view.worldMatrix[3][3]=1;view.vup_zfar[3]=256;
        view.projMatrix[0][0]=view.projMatrix[1][1]=1;
        view.projMatrix[2][2]=-(256.f+.1f)/(256.f-.1f);view.projMatrix[2][3]=-1;
        view.projMatrix[3][2]=-2*256.f*.1f/(256.f-.1f);
        for(auto vertex:r.quad){
            const float x=vertex.x*radius,y=vertex.y*radius;
            vertex.x=forward[face][0]*radius+right[face][0]*x+up[face][0]*y;
            vertex.y=forward[face][1]*radius+right[face][1]*x+up[face][1]*y;
            vertex.z=forward[face][2]*radius+right[face][2]*x+up[face][2]*y;cube_quads.push_back(vertex);
        }
    }
    camera.upload();cube.clear();sentinel(r);auto before=full_state();probe::reset();
    {Pass pass(false,shadow_flags|META_SCENE_MULTIVIEW|META_SCENE_LINEAR_DEPTH);assert(pass.ready());
        pass.texture(texture);pass.emissive(true);pass.view_offset(true);mesh.draw(cube_quads,++revision,false);}
    fast_calls();assert(full_state()==before&&glGetError()==GL_NO_ERROR);++cases;
    for(unsigned face=0;face<6;++face){
        pixels=cube.read(face);stencil_untouched(pixels);const float radius=64.f+8*face;
        for(int y=0;y<Size;++y)for(int x=0;x<Size;++x){
            const bool inside=x>=32&&x<48&&y>=16&&y<48;
            const float px=(x+.5f-32)*radius/32,py=(y+.5f-32)*radius/32;
            const float expected=inside?std::sqrt(px*px+py*py+radius*radius)/256:1.f;
            assert(std::abs(pixels.depth[y*Size+x]-expected)<2e-6f);
        }
    }
    camera.identity();quad=r.quad;++revision;

    // Alternate actual draw routing on the same FBO, including a high slot
    // outside our four shader outputs. Even that case must retain the old path.
    for(unsigned slot:{0u,1u,static_cast<unsigned>(max_draw_buffers-1)}){
        const GLenum attachment=slot==1?GL_COLOR_ATTACHMENT1:GL_COLOR_ATTACHMENT0;
        flat.clear();glFramebufferTexture2D(GL_FRAMEBUFFER,attachment,GL_TEXTURE_2D,color,0);
        std::vector<GLenum> routes(max_draw_buffers,GL_NONE);routes[slot]=attachment;
        glDrawBuffers(static_cast<GLsizei>(routes.size()),routes.data());
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        sentinel(r);before=full_state();probe::reset();
        const auto initial_passes=statistics().depth_only_shadow_passes;
        {Pass pass(false,shadow_flags);assert(pass.ready());pass.texture(texture);mesh.draw(quad,revision,false);}
        assert(statistics().depth_only_shadow_passes==initial_passes);
        assert(probe::draw_buffer_queries==slot+1&&probe::stencil_parameter_queries==14&&probe::color_state_queries==8);
        assert(probe::stencil_parameter_writes>0&&probe::color_state_writes>0&&full_state()==before);
        pixels=flat.read();quad_depth(pixels,.5f,true);assert(pixels.stencil[32*Size+40]==0&&pixels.stencil[32*Size+24]==stencil_clear);++cases;
        glFramebufferTexture2D(GL_FRAMEBUFFER,attachment,GL_TEXTURE_2D,0,0);
        draw(flat,shadow_flags);stencil_untouched(flat.read());
    }

    // Wrong category, unknown flags and unknown context limits fail closed.
    for(const auto flags:{0u,std::uint32_t(META_SCENE_GBUFFER),shadow_flags|META_SCENE_GBUFFER,shadow_flags|(1u<<29)}){
        flat.clear();sentinel(r);before=full_state();probe::reset();
        {Pass pass(false,flags);assert(pass.ready());pass.texture(texture);mesh.draw(quad,revision,false);}
        assert(probe::draw_buffer_queries==0&&probe::stencil_parameter_queries==14&&probe::color_state_queries==8);
        assert(full_state()==before&&glGetError()==GL_NO_ERROR);++cases;
    }
    flat.clear();sentinel(r);before=full_state();probe::reset();
    {Pass pass(true,shadow_flags);assert(pass.ready());pass.texture(texture);mesh.draw(quad,revision,false);}
    assert(probe::draw_buffer_queries==0&&probe::stencil_parameter_queries==0&&probe::color_state_queries==32);
    assert(full_state()==before);quad_depth(flat.read(),1.f);++cases;
    MetaRendererPostSceneFrame post{};for(unsigned i=0;i<16;i+=5)post.world_matrix[i]=post.projection_matrix[i]=1;
    flat.clear();sentinel(r);before=full_state();probe::reset();
    {Pass pass(false,shadow_flags,&post);assert(pass.ready());pass.texture(texture);mesh.draw(quad,revision,false);}
    assert(probe::draw_buffer_queries==0&&probe::stencil_parameter_queries==14&&probe::color_state_queries==8);
    assert(full_state()==before&&glGetError()==GL_NO_ERROR);++cases;
    const auto limit=max_draw_buffers;max_draw_buffers=0;
    flat.clear();sentinel(r);before=full_state();probe::reset();
    {Pass pass(false,shadow_flags);assert(pass.ready());pass.texture(texture);mesh.draw(quad,revision,false);}
    assert(probe::draw_buffer_queries==0&&probe::stencil_parameter_queries==14&&probe::color_state_queries==8);
    assert(full_state()==before);max_draw_buffers=limit;++cases;
    const auto owned_context=current_context;current_context=nullptr;
    flat.clear();sentinel(r);before=full_state();probe::reset();
    {Pass pass(false,shadow_flags);assert(pass.ready());pass.texture(texture);}
    assert(probe::draw_buffer_queries==0&&probe::stencil_parameter_queries==14&&probe::color_state_queries==8);
    assert(full_state()==before&&glGetError()==GL_NO_ERROR);current_context=owned_context;++cases;

    // Direct feedback is not a normal shadow caller, but must neither touch
    // color state nor leave its line/offset/depth changes behind.
    for(bool stencil_enabled:{true,false}){
        flat.clear();sentinel(r,stencil_enabled);before=full_state();probe::reset();
        {Pass pass(false,shadow_flags);assert(pass.ready());pass.texture(texture);
            pass.feedback(1);mesh.draw(quad,revision,false);pass.feedback(2);mesh.draw(quad,revision,false);}
        fast_calls();assert(full_state()==before&&glGetError()==GL_NO_ERROR);
        pixels=flat.read();quad_depth(pixels,1.f);stencil_untouched(pixels);++cases;
    }
    // Constructor early return on a known unavailable shader still restores
    // the captured host state. The zero entry avoids inducing unrelated GL
    // invalid-program errors from the compile adapter.
    const auto valid_program=programs.at(shadow_flags);programs[shadow_flags]={};
    flat.clear();sentinel(r);before=full_state();probe::reset();
    const auto initial_passes=statistics().depth_only_shadow_passes;
    {Pass pass(false,shadow_flags);assert(!pass.ready());}
    assert(statistics().depth_only_shadow_passes==initial_passes);
    fast_calls();assert(full_state()==before&&glGetError()==GL_NO_ERROR);programs[shadow_flags]=valid_program;++cases;
    struct Unwind {};
    before=full_state();probe::reset();
    try{Pass pass(false,shadow_flags);assert(pass.ready());pass.feedback(1);throw Unwind{};}catch(const Unwind&){}
    fast_calls();assert(full_state()==before&&glGetError()==GL_NO_ERROR);++cases;
    // A pending GL error still reaches the ordinary diagnostic path after
    // restoring all host state; it must not bypass depth-only cleanup.
    const auto previous_error=stats.error;const auto previous_errors=stats.error_count;
    before=full_state();probe::reset();glEnable(0xdead);
    {Pass pass(false,shadow_flags);assert(pass.ready());pass.texture(texture);mesh.draw(quad,revision,false);}
    fast_calls();assert(full_state()==before&&glGetError()==GL_NO_ERROR);
    assert(stats.error==GL_INVALID_ENUM&&stats.error_count==previous_errors+1);
    stats.error=previous_error;stats.error_count=previous_errors;++cases;

    // Genuine production lazy dispatch: NONE admission must not move outside
    // the first drawable section/entity, including static cache callbacks.
    namespace plugin=plugin_draw_probe;
    GLuint opaque{};unsigned opaque_width{},opaque_height{};
    const std::array<std::uint8_t,4> white{255,255,255,255};
    upload_texture(opaque,opaque_width,opaque_height,1,1,white);
    const auto reset_plugin=[&]{
        plugin::reset(r);plugin::atlas_texture=opaque;plugin::atlas_width=plugin::atlas_height=1;
        plugin::atlas_alpha.assign(1,255);plugin::atlas_pixel_tiles.reset(1,1);
    };
    reset_plugin();flat.clear();sentinel(r);before=full_state();probe::reset();
    probe::count_allocations=true;plugin::Draw(false,shadow_flags);assert(plugin::DrawStaticShadow(shadow_flags));probe::count_allocations=false;
    assert(probe::queries==0&&probe::allocations==0&&probe::bindings==0&&probe::errors==0&&full_state()==before);++cases;
    struct Cull final:IMetaRendererSceneCulling {bool CullShadowBounds(const float*,const float*)noexcept override{return true;}} culler;
    plugin::section(r);goldcraft::render::scene_culling=&culler;probe::reset();probe::count_allocations=true;
    plugin::Draw(false,shadow_flags);assert(plugin::DrawStaticShadow(shadow_flags));probe::count_allocations=false;
    assert(probe::queries==0&&probe::allocations==0&&probe::bindings==0&&probe::errors==0&&full_state()==before);++cases;
    goldcraft::render::scene_culling=nullptr;
    // A real nonempty static section and the ordinary entity callback both use
    // the fast Pass; their depth pixels survive the hostile stencil sentinel.
    flat.clear();sentinel(r);before=full_state();probe::reset();assert(plugin::DrawStaticShadow(shadow_flags));
    fast_calls();assert(full_state()==before);quad_depth(flat.read(),.5f);stencil_untouched(flat.read());++cases;
    reset_plugin();plugin::entity_batches.push_back(plugin::entity(r,0,2));
    flat.clear();sentinel(r);before=full_state();probe::reset();plugin::Draw(false,shadow_flags);
    fast_calls();assert(full_state()==before);quad_depth(flat.read(),.5f);stencil_untouched(flat.read());++cases;
    plugin::reset(r);
    glDeleteTextures(1,&opaque);

    // Warm Pass/Mesh calls still perform fresh draw-routing queries, with no
    // allocations, shader lookups, uploads or stencil/color parameter work.
    draw(flat,shadow_flags);const auto uploads=statistics().uploads;
    const auto warm_passes=statistics().depth_only_shadow_passes;
    sentinel(r);before=full_state();probe::reset();probe::count_allocations=true;
    for(unsigned i=0;i<32;++i){Pass pass(false,shadow_flags);pass.texture(texture);mesh.draw(quad,revision,false);}
    probe::count_allocations=false;
    assert(probe::queries==32u*(12u+static_cast<unsigned>(max_draw_buffers)));
    assert(probe::draw_buffer_queries==32u*max_draw_buffers&&probe::limit_queries==0);
    assert(statistics().depth_only_shadow_passes==warm_passes+32);
    assert(probe::color_state_queries==0&&probe::stencil_parameter_queries==0&&probe::color_state_writes==0&&probe::stencil_parameter_writes==0);
    assert(probe::allocations==0&&probe::locations==0&&statistics().uploads==uploads&&full_state()==before);++cases;
    glDeleteTextures(1,&texture);
    glDeleteTextures(1,&color);
    }
    assert(full_state()==before_fixture&&glGetError()==GL_NO_ERROR);
    std::printf("{\"depthOnlyProductionShaderGL\":true,\"cases\":%u,\"drawBufferSlots\":%d,\"depthStencilFormat\":\"DEPTH32F_STENCIL8\",\"cubeFaces\":6,\"csmLayers\":4,\"alphaCutoffCases\":6,\"stencilPixelsPreserved\":true,\"stateRestored\":true,\"warmQueriesPerPass\":%d,\"foregroundFPSValidated\":false}\n",cases,max_draw_buffers,12+max_draw_buffers);
}
} // namespace shadow_depth_probe

void shadow_depth_only_tests(Resources& r){shadow_depth_probe::cases=0;shadow_depth_probe::tests(r);}
