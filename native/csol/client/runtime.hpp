// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GL/glew.h>
#include <metahook.h>
#include <com_model.h>
#include <cvardef.h>
#include <studio.h>
#include <r_studioint.h>
#include <cl_entity.h>
#include <ref_params.h>
#include <span>
#include <vector>
#include <string>
#include <memory>
#include <array>
#include <unordered_map>
#include <stdexcept>
#include <iosfwd>

namespace csol::client
{
extern metahook_api_t* api;
extern mh_interface_t* interfaces;
extern cl_enginefunc_t engine;
extern cl_exportfuncs_t downstream;
extern engine_studio_api_t studio;
extern r_studio_interface_t* studioInterface;
extern ref_params_t view;
extern bool glewReady;
extern int* hostTextureCache;
using Bones = std::array<std::array<std::array<float,4>,3>,MAXSTUDIOBONES>;
std::vector<std::byte> ReadResource(const char* path);
bool Exists(const char* path);
model_t* LoadEngineModel(const char* path);
void MountResources();
void InstallLoaders();
void RemoveHooks();
void ClearScene();
void ForgetSceneEntity(const cl_entity_t* entity);
void SynchronizeSession();
void SynchronizeFrameSession();
void SessionDiagnostics(std::ostream& out);
void InitializeScene();
void SceneFrame();
bool EnsureGraphics();
bool DrawModel(cl_entity_t* entity, int flags, int (*original)(int));
bool DrawWorldWeaponPoints();
extern void* currentStudioHeader;
extern model_t* currentStudioModel;
void StudioEvent(const mstudioevent_t* event, const cl_entity_t* entity);
void DrawWorldEffects();
void Report();
void* Resolve(const char* name, mh_gamesymbol_kind_t kind);
hook_t* Hook(void* address, void* replacement, void** original);
void RemoveHook(hook_t* hook);
}
