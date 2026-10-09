#pragma once
// Engine-local calls: hull and line/point are already in model-local coordinates.
// Include the engine's precompiled.h first for trace_t (an anonymous typedef).
struct model_s;
struct hull_s;
struct pmtrace_s;
bool GoldCraft_MapTrace(int slot, model_s *model, hull_s *hull, const float *start,
                        const float *end, trace_t *trace);
bool GoldCraft_MapTrace(int slot, model_s *model, hull_s *hull, const float *start,
                        const float *end, pmtrace_s *trace);
bool GoldCraft_MapContents(int slot, model_s *model, hull_s *hull, const float *point,
                           int &contents);
void GoldCraft_MapPhysicsReset();
