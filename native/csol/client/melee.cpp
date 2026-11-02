// SPDX-License-Identifier: GPL-3.0-or-later
#include "melee.hpp"
#include "weapon_inventory.hpp"
#include "../melee_identity.hpp"
#include "../melee_prediction.hpp"
#include "../melee_resources.hpp"
#include <r_efx.h>
#include <event_api.h>
#include <pmtrace.h>
#include <pm_defs.h>
#include <in_buttons.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ostream>

namespace csol::client {
namespace {
int presentedWeapon{}, presentedModel{}, presentedAnimation{-1};
std::uint16_t presentedSerial{};
int predictedAttacks{}, completions{}, dragonEffects{}, rejected{};
unsigned effectEpoch{};
std::array<std::uint16_t, 2> consumedSerial{};
std::array<bool, 2> haveConsumedSerial{};
int authoritativeAnimation{-1};

std::size_t WeaponIndex(int id) { return id == melee::DragonSwordId ? 0 : 1; }
void ClearPresentation()
{
    if (melee::IsWeapon(presentedWeapon)) {
        const auto index = WeaponIndex(presentedWeapon);
        consumedSerial[index] = presentedSerial; haveConsumedSerial[index] = true;
    }
    presentedWeapon = presentedModel = 0;
    presentedAnimation = authoritativeAnimation = -1; presentedSerial = 0;
    ++effectEpoch;
}

void PresentAnimation(int sequence)
{
    auto* model = engine.GetViewModel ? engine.GetViewModel() : nullptr;
    if (engine.pfnWeaponAnim) engine.pfnWeaponAnim(sequence, model ? model->curstate.body : 0);
    presentedAnimation = sequence;
}
void FollowDragonEffect(TEMPENTITY* temp, float, float now)
{
    if (std::uint32_t(temp->entity.curstate.iuser4) != effectEpoch ||
        CurrentExtendedWeapon().id != melee::DragonSwordId || !CurrentExtendedWeapon().state) {
        temp->die = now; temp->entity.curstate.renderamt = 0; return;
    }
    // Original 02563560 copies the local render origin/angles every update.
    // This is a camera-bound first-person effect, not a weapon attachment.
    std::copy_n(view.vieworg, 3, temp->entity.origin);
    std::copy_n(view.viewangles, 3, temp->entity.angles);
}
void DragonEffect(int subtype, float animationTime)
{
    if ((subtype != 0 && subtype != 2) || !engine.pEfxAPI || !engine.pEfxAPI->R_TempModel ||
        !engine.pEventAPI || !engine.pEventAPI->EV_FindModelIndex) return;
    const int model = engine.pEventAPI->EV_FindModelIndex("models/ef_dragonsword.mdl");
    if (model <= 0) return;
    float origin[3], angles[3]{}, velocity[3]{};
    std::copy_n(view.vieworg, 3, origin); origin[2] += 16;
    auto* temp = engine.pEfxAPI->R_TempModel(origin, velocity, angles, .35f, model, 0);
    if (!temp) return;
    // Original client 02563390: R_TempModel .35, fade 3, additive alpha128,
    // frameMax256 and a callback that follows the view. No stock gravity.
    temp->flags &= ~(FTENT_GRAVITY | FTENT_COLLIDEWORLD);
    temp->flags |= FTENT_CLIENTCUSTOM | FTENT_SPRANIMATE | FTENT_FADEOUT;
    temp->callback = FollowDragonEffect;
    temp->tentOffset[2] = 16;
    const auto* local = engine.GetLocalPlayer ? engine.GetLocalPlayer() : nullptr;
    temp->clientIndex = local ? short(local->index) : 0;
    std::copy_n(view.viewangles, 3, temp->entity.angles);
    temp->entity.curstate.body = 0;
    temp->entity.curstate.frame = 0;
    temp->fadeSpeed = 3;
    temp->frameMax = 256;
    temp->entity.curstate.rendermode = kRenderTransAdd;
    temp->entity.curstate.renderamt = temp->entity.baseline.renderamt = 128;
    temp->entity.curstate.animtime = animationTime;
    temp->entity.curstate.framerate = 1;
    const bool right = !engine.pfnGetCvarFloat || engine.pfnGetCvarFloat("cl_righthand") != 0;
    temp->entity.curstate.sequence = subtype + (right ? 0 : 1);
    temp->entity.curstate.iuser4 = int(effectEpoch);
    ++dragonEffects;
}
int ReceiveCompletion(const char*, int size, void* pointer)
{
    if (size != melee::EffectBytes || !pointer) { ++rejected; return 0; }
    const auto* bytes = static_cast<const unsigned char*>(pointer);
    const int id = bytes[1] | (bytes[2] << 8);
    const auto serial = std::uint16_t(bytes[3] | (bytes[4] << 8));
    const int animation = bytes[5], contact = bytes[6];
    int offset = bytes[7] | (bytes[8] << 8);
    if (offset >= 32768) offset -= 65536;
    const auto* definition = FindWeaponDefinition(id);
    if (bytes[0] != 1 || !melee::IsWeapon(id) || contact > 2 || !definition ||
        definition->name != melee::Resource(melee::WeaponVariant(id)).classname ||
        CurrentExtendedWeapon().id != id || !CurrentExtendedWeapon().state ||
        !WeaponPresentationAllowed(id)) { ++rejected; return 0; }
    const auto variant = melee::WeaponVariant(id);
    const bool validAnimation = variant == melee::Variant::DragonSword
        ? animation == 1 || animation == 2 || animation == 4 : animation == 8 || animation == 2;
    if (!validAnimation) { ++rejected; return 0; }
    const auto index = WeaponIndex(id);
    if (haveConsumedSerial[index] && std::uint16_t(serial - consumedSerial[index]) >= 32768) { ++rejected; return 0; }
    if (haveConsumedSerial[index] && serial == consumedSerial[index]) return 1;
    // An older response must not restart a later attack after packet delay.
    if (presentedWeapon == id && serial != presentedSerial &&
        std::uint16_t(presentedSerial - serial) < 32768) { ++rejected; return 0; }
    const auto attack = melee::PendingAttack(variant, animation);
    const auto complete = melee::CompleteAttack(attack, melee::Contact(contact));
    consumedSerial[index] = serial; haveConsumedSerial[index] = true;
    if (complete.animation >= 0 && presentedAnimation != complete.animation) PresentAnimation(complete.animation);
    if (complete.dragonEffect >= 0 && engine.GetClientTime)
        DragonEffect(complete.dragonEffect, float(engine.GetClientTime()) + offset / 100.f);
    ++completions;
    return 1;
}
melee::Contact PredictedContact(const melee::Attack& attack, const clientdata_t& source,
    const usercmd_t& command)
{
    auto* event = engine.pEventAPI;
    if (!event || !engine.pfnAngleVectors || !event->EV_SetTraceHull || !event->EV_PlayerTrace ||
        !event->EV_PushPMStates || !event->EV_PopPMStates || !event->EV_SetSolidPlayers) return melee::Contact::None;
    float origin[3], end[3], forward[3], right[3], up[3];
    engine.pfnAngleVectors(const_cast<float*>(command.viewangles), forward, right, up);
    for (int i = 0; i < 3; ++i) {
        origin[i] = source.origin[i] + source.view_ofs[i];
        end[i] = origin[i] + forward[i] * attack.distance;
    }
    const auto* local = engine.GetLocalPlayer ? engine.GetLocalPlayer() : nullptr;
    event->EV_PushPMStates();
    event->EV_SetSolidPlayers(local ? local->index - 1 : -1);
    event->EV_SetTraceHull(2); // event API point hull
    pmtrace_t trace{};
    event->EV_PlayerTrace(origin, end, PM_NORMAL, -1, &trace);
    melee::Contact result = melee::Contact::None;
    if (trace.fraction < 1) {
        const int index = event->EV_IndexFromTrace ? event->EV_IndexFromTrace(&trace) : 0;
        auto* entity = index > 0 && engine.GetEntityByIndex ? engine.GetEntityByIndex(index) : nullptr;
        result = entity && entity->curstate.solid != SOLID_BSP ? melee::Contact::Damageable : melee::Contact::World;
    } else {
        for (int i = 0; i < 3; ++i) end[i] = origin[i] + forward[i] * (attack.distance - float(std::sqrt(512.0)));
        event->EV_SetTraceHull(0); // event API standing human hull
        event->EV_PlayerTrace(origin, end, PM_NORMAL, -1, &trace);
        if (trace.fraction != 1) result = melee::Contact::World;
    }
    event->EV_PopPMStates();
    // Full multi-target sector damage remains server authority. Its completion
    // message corrects lateral-target hits that the local trace cannot know.
    return result;
}
}
void InitializeMeleeWeapons() { engine.pfnHookUserMsg(melee::EffectMessage, ReceiveCompletion); }
void ResetMeleeWeapons()
{
    ClearPresentation(); consumedSerial = {}; haveConsumedSerial = {};
}
void ReceiveMeleeState(const WeaponCurrent& current)
{
    if (!current.state || current.id != presentedWeapon) ClearPresentation();
}
bool PredictMeleeWeapon(local_state_t* from, local_state_t* to, usercmd_t* command,
    int runFunctions, double time, unsigned seed)
{
    const int id = from->client.m_iId;
    const auto* definition = melee::IsWeapon(id) ? FindWeaponDefinition(id) : nullptr;
    if (!definition || from->weapondata[ActiveWeaponSlot].m_iId != id ||
        definition->name != melee::Resource(melee::WeaponVariant(id)).classname) {
        if (runFunctions && presentedWeapon) ClearPresentation();
        return false;
    }
    const auto variant = melee::WeaponVariant(id);
    const auto source = from->client;
    auto weapon = from->weapondata[ActiveWeaponSlot];
    auto prediction = melee::ReadPrediction(weapon);
    from->client.m_iId = 0;
    downstream.HUD_PostRunCmd(from, to, command, runFunctions, time, seed);
    from->client.m_iId = id; to->client.m_iId = id;
    to->client.viewmodel = source.viewmodel; to->client.weaponanim = source.weaponanim;
    to->client.m_flNextAttack = source.m_flNextAttack; to->client.iuser3 = source.iuser3;
    std::copy(std::begin(source.vuser4), std::end(source.vuser4), std::begin(to->client.vuser4));
    to->playerstate.oldbuttons = command->buttons;
    const bool alive = source.health > 0 && !source.deadflag;
    const bool predict = !engine.pfnGetCvarFloat || engine.pfnGetCvarFloat("cl_lw") != 0;
    int animation = -1;
    if (predict && alive && prediction.pendingAnimation && prediction.pendingTime <= 0) {
        const auto attack = melee::PendingAttack(variant, prediction.pendingAnimation);
        const auto complete = melee::CompleteAttack(attack, PredictedContact(attack, source, *command));
        prediction.pendingAnimation = 0; prediction.pendingTime = -1;
        weapon.m_flNextPrimaryAttack = complete.remaining.primary;
        weapon.m_flNextSecondaryAttack = complete.remaining.secondary;
        weapon.m_flTimeWeaponIdle = complete.remaining.idle;
        animation = complete.animation;
    }
    if (predict && alive && source.m_flNextAttack <= 0 && !prediction.pendingAnimation) {
        const bool canShoot = (source.iuser3 & 1) && !(source.iuser3 & 2);
        const bool secondary = (command->buttons & IN_ATTACK2) && weapon.m_flNextSecondaryAttack <= 0;
        const bool primary = (command->buttons & IN_ATTACK) && weapon.m_flNextPrimaryAttack <= 0;
        if (canShoot && (secondary || primary)) {
            const auto button = secondary ? melee::Button::Secondary : melee::Button::Primary;
            const auto attack = melee::BeginAttack(variant, button, prediction.primaryOrdinal);
            if (button == melee::Button::Primary) ++prediction.primaryOrdinal;
            ++prediction.serial;
            prediction.pendingAnimation = attack.animation;
            prediction.pendingTime = attack.hitDelay;
            weapon.m_flNextPrimaryAttack = attack.initial.primary;
            weapon.m_flNextSecondaryAttack = attack.initial.secondary;
            weapon.m_flTimeWeaponIdle = attack.initial.idle;
            animation = attack.animation;
            if (runFunctions) ++predictedAttacks;
        } else if (!(command->buttons & (IN_ATTACK | IN_ATTACK2)) && weapon.m_flTimeWeaponIdle <= 0) {
            animation = 0;
            weapon.m_flTimeWeaponIdle = variant == melee::Variant::DragonSword ? 18 : 20;
        }
    }
    if (animation >= 0) to->client.weaponanim = animation;
    if (runFunctions && alive && WeaponPresentationAllowed(id)) {
        if (animation >= 0 || presentedWeapon != id || presentedModel != source.viewmodel ||
            (!predict && authoritativeAnimation != source.weaponanim))
            PresentAnimation(to->client.weaponanim);
        presentedWeapon = id; presentedModel = source.viewmodel; presentedSerial = prediction.serial;
        authoritativeAnimation = source.weaponanim;
    } else if (runFunctions && presentedWeapon) ClearPresentation();
    const float dt = command->msec / 1000.f;
    weapon.m_flNextPrimaryAttack = std::max(-.001f, weapon.m_flNextPrimaryAttack - dt);
    weapon.m_flNextSecondaryAttack = std::max(-.001f, weapon.m_flNextSecondaryAttack - dt);
    weapon.m_flTimeWeaponIdle = std::max(-.001f, weapon.m_flTimeWeaponIdle - dt);
    to->client.m_flNextAttack = std::max(-.001f, to->client.m_flNextAttack - dt);
    if (prediction.pendingAnimation) prediction.pendingTime = std::max(-.001f, prediction.pendingTime - dt);
    melee::WritePrediction(prediction, weapon);
    to->weapondata[ActiveWeaponSlot] = weapon;
    return true;
}
void MeleeDiagnostics(std::ostream& out)
{
    out << ",\"melee\":{\"weapon\":" << presentedWeapon << ",\"attacks\":" << predictedAttacks
        << ",\"completions\":" << completions << ",\"dragonEffects\":" << dragonEffects
        << ",\"rejected\":" << rejected << '}';
}
}
