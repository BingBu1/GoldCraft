#pragma once

namespace goldcraft::client {
// The runtime engine table can have trailing callbacks absent from this SDK.
// Forward its original address, changing only the event table during Initialize.
// The native client copies the full runtime ABI, including those callbacks.
template<class Initialize>
int initialize_with_events(cl_enginefunc_t* engine, int version,
                           event_api_s* events, Initialize initialize) {
    struct RestoreEvents {
        cl_enginefunc_t* engine;
        event_api_s* previous;
        ~RestoreEvents() { engine->pEventAPI = previous; }
    } restore{engine, engine->pEventAPI};
    engine->pEventAPI = events;
    return initialize(engine, version);
}
}
