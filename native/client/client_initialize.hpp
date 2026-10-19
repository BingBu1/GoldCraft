#pragma once

namespace goldcraft::client {
// The runtime engine table can have trailing callbacks absent from this SDK.
// Forward its original address, changing selected callbacks during Initialize.
// The native client copies the full runtime ABI, including those callbacks.
template<class Initialize>
int initialize_with_events(cl_enginefunc_t* engine, int version,
                           event_api_s* events, Initialize initialize,
                           decltype(cl_enginefunc_t::pfnSPR_GetList) sprites = nullptr) {
    struct RestoreEvents {
        cl_enginefunc_t* engine;
        event_api_s* previous;
        decltype(cl_enginefunc_t::pfnSPR_GetList) sprite_list;
        ~RestoreEvents() { engine->pEventAPI = previous; engine->pfnSPR_GetList = sprite_list; }
    } restore{engine, engine->pEventAPI, engine->pfnSPR_GetList};
    engine->pEventAPI = events;
    if (sprites) engine->pfnSPR_GetList = sprites;
    return initialize(engine, version);
}
}
