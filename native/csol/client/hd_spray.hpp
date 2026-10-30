// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../hd_spray.hpp"
#include <filesystem>
#include <memory>
namespace csol::client {
struct SpraySettings { bool enabled{};spray::Bytes bytes;spray::Dimensions original; };
const SpraySettings& CurrentSpray();
spray::Animation ImportSpray(const std::filesystem::path&,spray::Dimensions* original=nullptr);
void ApplySpray(const SpraySettings&);
const wchar_t* SprayStatus();
bool IsSprayUploading();
void CancelSprayUpload();
void InitializeSpray();
void ResetSpray();
void SprayFrame();
void SprayVideoReset();
void DrawSprays();
void PrepareSprayGeometry();
void ShutdownSprayGraphics();
void ShutdownSpray();
void CreateSprayOptions(void* page);
void ApplySprayOptions(void* page);
void SprayOptionsFrame();
void ReleaseSprayOptions();
// Positions are model-local for BSP entities and world coordinates for entity0.
struct SprayPlacement { unsigned id{},asset{},entity{},model{};std::array<float,3> position{},normal{};spray::Dimensions original;float started{}; };
void AddSprayGeometry(const SprayPlacement&,std::shared_ptr<const spray::Animation>);
void RemoveSprayGeometry(unsigned id);
}
