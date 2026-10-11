// Production GoldCraft vertex/fragment shaders and Renderer common.h on the
// actual hidden GL context. Only shader loading and the engine UBOs are adapters.
GLuint Renderer::effect_program(const CCompileShaderArgs* args){
    const auto read=[](const std::string& path){std::ifstream file(path);assert(file.good());return std::string(std::istreambuf_iterator<char>(file),{});};
    const auto common=read(GOLDCRAFT_SHADER_COMMON);
    const auto stage=[&](GLenum kind,const char* name){
        auto source=read(std::string(GOLDCRAFT_SHADER_SOURCE)+"/"+name);
        const std::string include="#include \"common.h\"";
        const auto at=source.find(include);assert(at!=std::string::npos);source.replace(at,include.size(),common);
        const auto definitions=std::string(kind==GL_VERTEX_SHADER?"#define IS_VERTEX_SHADER\n":"#define IS_FRAGMENT_SHADER\n")+(args->fsdefine?args->fsdefine:"");
        source.insert(source.find('\n')+1,definitions);
        return compile(kind,source);
    };
    const auto vs=stage(GL_VERTEX_SHADER,"goldcraft.vert.glsl"),fs=stage(GL_FRAGMENT_SHADER,"goldcraft.frag.glsl");
    const auto program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    GLint ok{};glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){char log[8192]{};glGetProgramInfoLog(program,sizeof(log),nullptr,log);std::fprintf(stderr,"%s\n",log);}
    assert(ok);glDeleteShader(vs);glDeleteShader(fs);return program;
}
void effect_tests(Resources& r){
    using namespace goldcraft;
    const auto matches=[](const Pixels& actual,std::array<std::uint8_t,4> expected){
        std::size_t count=0;
        for(std::size_t i=0;i<actual.size();i+=4){
            bool same=true;for(unsigned c=0;c<4;++c)same&=std::abs(int(actual[i+c])-expected[c])<=1;
            count+=same;
        }
        return count;
    };
    programs.clear();r.adapter.effects_shader=true;
    scene_ubo_t scene{};scene.r_g=scene.v_gamma=scene.v_texgamma=scene.v_lightgamma=1;scene.r_g3=.125f;
    camera_ubo_t camera{};camera.numViews=1;camera.views[0].vup_zfar[3]=2048;
    for(int i=0;i<4;++i)camera.views[0].worldMatrix[i][i]=camera.views[0].projMatrix[i][i]=1;
    GLuint ubos[2]{};glGenBuffers(2,ubos);
    const void* contents[]{&scene,&camera};const GLsizeiptr sizes[]{sizeof(scene),sizeof(camera)};
    for(unsigned i=0;i<2;++i){glBindBuffer(GL_UNIFORM_BUFFER,ubos[i]);glBufferData(GL_UNIFORM_BUFFER,sizes[i],contents[i],GL_STATIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,i,ubos[i]);}
    const GLenum target=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&target);
    auto quad=r.quad;Mesh mesh;std::uint64_t revision=10;
    ParticleMaterial m;m.texture=particle_untextured;m.flags=particle_depth_write|particle_depth_test;m.depth_func=GL_LESS;m.color_mask=0;
    auto equal=m;equal.flags=particle_depth_test;equal.depth_func=GL_EQUAL;equal.color_mask=15;equal.color_mod={.2f,.4f,.6f,1};
    clear();r.sentinel();auto before=snapshot();
    {
        Pass pass(true,0);assert(pass.ready());
        // Feedback precedes effects in the real plugin; its stencil/blend/
        // polygon offset shader state must not leak into the ordered stream.
        pass.feedback(1);
        assert(pass.effect(m,0));mesh.draw(quad,revision,false);
        GLfloat depth{};glReadPixels(32,32,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);assert(std::abs(depth-.5f)<1e-6f);
        assert(color_count(pixels(),{0,0,0,0})==Size*Size);
        assert(pass.effect(equal,0));mesh.draw(quad,revision,false);
        const auto q=probe::queries,u=probe::uniforms,b=probe::bindings,l=probe::locations;
        const auto uploads=statistics().uploads;probe::count_allocations=true;probe::allocations=0;
        for(unsigned i=0;i<256;++i){assert(pass.effect(equal,0));mesh.draw(quad,revision,false);}
        probe::count_allocations=false;
        assert(probe::queries==q&&probe::locations==l&&probe::uniforms==u&&probe::bindings==b&&probe::allocations==0&&statistics().uploads==uploads);
    }
    assert(snapshot()==before);assert(color_count(pixels(),{51,102,153,255},1)==1024);
    // Equal fails at a distinct depth, and reversing the ordered two-pass
    // sequence leaves the color buffer empty.
    clear();r.sentinel();before=snapshot();
    {Pass pass(true,0);assert(pass.effect(equal,0));mesh.draw(quad,revision,false);assert(pass.effect(m,0));mesh.draw(quad,revision,false);}
    assert(snapshot()==before&&color_count(pixels(),{0,0,0,0})==Size*Size);
    {Pass pass(true,0);for(auto& v:quad)v.z=.25f;assert(pass.effect(equal,0));mesh.draw(quad,++revision,false);}
    assert(color_count(pixels(),{0,0,0,0})==Size*Size);quad=r.quad;++revision;

    // Constant-white material works with texture zero unbound. Separate blend
    // factors and repeated additive draws retain their captured material.
    auto add=ParticleMaterial{};add.texture=particle_untextured;add.flags=particle_blend|particle_depth_test;
    add.src_rgb=add.dst_rgb=add.src_alpha=add.dst_alpha=GL_ONE;add.color_mod={.25f,.125f,0,.5f};
    clear();r.sentinel();before=snapshot();
    {Pass pass(true,0);assert(pass.effect(add,0));mesh.draw(quad,revision,false);mesh.draw(quad,revision,false);}
    const auto additive_pixels=pixels();
    if(snapshot()!=before||matches(additive_pixels,{128,64,0,255})!=1024){
        const auto center=(32*Size+32)*4;
        std::fprintf(stderr,"effect additive restored=%d center=%u,%u,%u,%u\n",snapshot()==before,
            additive_pixels[center],additive_pixels[center+1],additive_pixels[center+2],additive_pixels[center+3]);
    }
    // Two RGBA8 blend stores may quantize .5 alpha downward to127 each.
    assert(snapshot()==before&&matches(additive_pixels,{128,64,0,255})==1024);
    auto subtract=add;subtract.equation_rgb=GL_FUNC_REVERSE_SUBTRACT;subtract.color_mod={.125f,0,0,0};subtract.alpha_cutoff=0;
    {Pass pass(true,0);assert(pass.effect(subtract,0));mesh.draw(quad,revision,false);}
    assert(matches(pixels(),{96,64,0,255})==1024);
    auto mask=ParticleMaterial{};mask.texture=particle_untextured;mask.flags=particle_cull;mask.color_mask=5;
    clear();r.sentinel();before=snapshot();
    {Pass pass(true,0);assert(pass.effect(mask,0));mesh.draw(quad,revision,false);}
    assert(snapshot()==before&&color_count(pixels(),{255,0,255,0})==1024);

    GLuint texture{};unsigned width{},height{};const std::array<std::uint8_t,4> half_alpha{255,255,255,128};
    upload_texture(texture,width,height,1,1,half_alpha);
    for(auto& v:quad)v.color=0x80ffffff;++revision;
    auto cutoff=ParticleMaterial{};cutoff.texture=1;cutoff.color_mod[3]=.5f;
    const auto cutoff_draw=[&](unsigned flags,float value,bool visible){
        clear();r.sentinel();const auto state=snapshot();cutoff.flags=flags;cutoff.alpha_cutoff=value;
        {Pass pass(true,0);assert(pass.effect(cutoff,texture));mesh.draw(quad,revision,false);}
        assert(snapshot()==state);
        assert(color_count(pixels(),visible?std::array<std::uint8_t,4>{255,255,255,32}:std::array<std::uint8_t,4>{0,0,0,0})==(visible?1024:Size*Size));
    };
    cutoff_draw(particle_cutoff_texture_only,.4f,true);
    cutoff_draw(particle_cutoff_before_mod,.4f,false);
    cutoff_draw(particle_cutoff_before_mod,.2f,true);
    cutoff_draw(0,.2f,false);
    cutoff_draw(0,.1f,true);
    // Exact zero does not depend on preservation of subnormal GL uniforms.
    cutoff.texture=particle_untextured;cutoff.color_mod={1,1,1,4};cutoff.alpha_cutoff=0;
    for(auto& v:quad)v.color=0x00ffffff;++revision;
    clear();r.sentinel();before=snapshot();
    {Pass pass(true,0);cutoff.flags=particle_cutoff_before_mod|particle_discard_zero_alpha;assert(pass.effect(cutoff,0));mesh.draw(quad,revision,false);}
    assert(snapshot()==before&&color_count(pixels(),{0,0,0,0})==Size*Size);
    {Pass pass(true,0);cutoff.flags=particle_cutoff_before_mod;assert(pass.effect(cutoff,0));mesh.draw(quad,revision,false);}
    assert(color_count(pixels(),{255,255,255,0})==1024);
    // The cached production program must leave effect mode on the next normal
    // scope; this also validates its original alpha/ambient path after effects.
    clear();r.sentinel();before=snapshot();quad=r.quad;++revision;
    {Pass pass(false,0);pass.texture(r.texture);pass.emissive(true);assert(!pass.effect(m,0));mesh.draw(quad,revision,false);}
    assert(snapshot()==before&&color_count(pixels(),{255,255,255,255})==1024);
    for(auto flags:{std::uint32_t(META_SCENE_GBUFFER),std::uint32_t(META_SCENE_SHADOW)}){
        Pass pass(true,flags);const auto state=snapshot();assert(!pass.effect(m,0));assert(snapshot()==state);
    }
    assert(glGetError()==GL_NO_ERROR);
    glDeleteTextures(1,&texture);glDeleteBuffers(2,ubos);programs.clear();r.adapter.effects_shader=false;
    const GLenum targets[]{GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};glDrawBuffers(4,targets);
    std::puts("{\"effectsProductionShaderGL\":true,\"orderedDepthEqual\":true,\"additiveWhite\":true,\"cutoffStages\":3,\"exactZero\":true,\"stateRestored\":true,\"warmDraws\":256,\"warmQueries\":0,\"warmLocations\":0,\"warmUniforms\":0,\"warmBinds\":0,\"warmAllocations\":0,\"warmUploads\":0}");
}
