// Production initialize/shutdown on the real GL context; Renderer discovery
// and its public interfaces are adapters, not a running game or a FPS test.
namespace initialize_probe {
struct Callbacks : IMetaRendererSceneCallbacks {void DrawOpaqueScene(std::uint32_t) override {}};
struct Scene : IMetaRendererScene {
    IMetaRendererSceneCallbacks* registered=nullptr;
    unsigned registrations=0,revocations=0;
    void RegisterSceneCallbacks(IMetaRendererSceneCallbacks* value) override {registered=value;++registrations;}
    void UnregisterSceneCallbacks(IMetaRendererSceneCallbacks* value) override {assert(value==registered);registered=nullptr;++revocations;}
    void SetDynamicLightShadowSize(int,unsigned) override {}
};
struct Edits : IMetaRendererWorldEdit {
    std::uint64_t Begin(model_s*,std::uint64_t,std::uint64_t,const MetaWorldEditBox*,std::uint32_t) noexcept override {return 0;}
    MetaWorldEditStatus Poll(std::uint64_t) noexcept override {return MetaWorldEditStatus::Missing;}
    bool Commit(std::uint64_t) noexcept override {return false;}
    void Cancel(std::uint64_t) noexcept override {}
    void Reset() noexcept override {}
};
Renderer* renderer=nullptr;
Scene* scene=nullptr;
Edits* edits=nullptr;
bool main_available=true,optional_available=true;
IBaseInterface* factory(const char* name,int*){
    if(!std::strcmp(name,METARENDERER_INTERFACE_VERSION))return main_available?renderer:nullptr;
    if(!optional_available)return nullptr;
    if(!std::strcmp(name,METARENDERER_SCENE_INTERFACE_VERSION))return scene;
    if(!std::strcmp(name,METARENDERER_WORLD_EDIT_INTERFACE_VERSION))return edits;
    return nullptr;
}
}
void initialize_tests(GLFWwindow* window){
    namespace init=initialize_probe;
    shutdown();
    Renderer renderer;init::Scene scene;init::Edits edits;init::Callbacks callback;
    init::renderer=&renderer;init::scene=&scene;init::edits=&edits;
    init::main_available=init::optional_available=true;
    probe::initialize_factory=init::factory;
    const auto calls=probe::loader_calls;
    const auto reuses=statistics().gl_loader_reuses;
    const auto lookups=probe::factory_lookups;
    assert(initialize(&callback,log_message));
    const auto queries=probe::queries;
    for(unsigned i=0;i<9;++i)assert(initialize(&callback,log_message));
    assert(probe::loader_calls==calls+1&&statistics().gl_loader_reuses==reuses+9);
    assert(probe::queries==queries&&probe::factory_lookups==lookups+10);
    assert(scene.registered==&callback&&world_edit()==&edits&&scene_active());
    // Cache only GL dispatch/limits. Query optional/main providers on every reset.
    init::optional_available=false;assert(initialize(&callback,log_message));
    assert(!scene.registered&&!scene_active()&&!world_edit()&&!post_scene());
    init::optional_available=true;assert(initialize(&callback,log_message));
    init::main_available=false;assert(!initialize(&callback,log_message));
    assert(!scene.registered&&!scene_active()&&!world_edit()&&!statistics().renderer);
    init::main_available=true;assert(initialize(&callback,log_message));
    auto before=probe::loader_calls;
    shutdown();assert(!scene.registered);
    assert(initialize(&callback,log_message)&&probe::loader_calls==before+1);
    // No-current-context failure revokes providers and the dispatch cache.
    before=probe::loader_calls;
    glfwMakeContextCurrent(nullptr);assert(!initialize(&callback,log_message));
    assert(!scene_active()&&!world_edit()&&!scene.registered&&probe::loader_calls==before);
    glfwMakeContextCurrent(window);
    assert(initialize(&callback,log_message)&&probe::loader_calls==before+1);
    before=probe::loader_calls;
    auto* alternate=glfwCreateWindow(Size,Size,"GoldCraft context lifecycle",nullptr,nullptr);assert(alternate);
    glfwMakeContextCurrent(alternate);
    assert(initialize(&callback,log_message)&&probe::loader_calls==before+1);
    shutdown();glfwMakeContextCurrent(window);glfwDestroyWindow(alternate);
    assert(initialize(&callback,log_message)&&probe::loader_calls==before+2);
    shutdown();
    // A context that keeps reporting loss must return, and allow a later retry.
    probe::persistent_error=GL_CONTEXT_LOST;probe::errors=0;
    assert(!initialize(&callback,log_message));
    assert(probe::errors==32&&!goldcraft::render::gl_initialized&&!scene_active()&&!world_edit());
    probe::persistent_error=GL_NO_ERROR;
    before=probe::loader_calls;assert(initialize(&callback,log_message)&&probe::loader_calls==before+1);
    shutdown();
    probe::loader_failure=GLEW_ERROR_NO_GL_VERSION;
    assert(!initialize(&callback,log_message)&&!goldcraft::render::gl_initialized&&!scene_active());
    probe::loader_failure=GLEW_OK;
    before=probe::loader_calls;assert(initialize(&callback,log_message)&&probe::loader_calls==before+1);
    shutdown();probe::initialize_factory=nullptr;
    std::printf("{\"initializeDispatchCache\":true,\"providerRefreshAndRevocation\":true,\"persistentErrorBound\":32,\"retry\":true}\n");
}
