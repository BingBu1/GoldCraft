// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct cl_entity_s;
struct model_s;
struct mstudioevent_s;
struct ref_params_s;

namespace csol::client
{
void PrepareCofLightEquipment();
void ResetCofLightEquipment();
void CofLightEquipmentView(const ref_params_s& view);
void CofLightEquipmentEntity(const cl_entity_s& entity);
void CofLightEquipmentPose(const cl_entity_s* entity, const model_s* model, const void* header);
bool CofLightEquipmentEvent(const mstudioevent_s* event, const cl_entity_s* entity);
}
