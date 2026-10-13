#if defined _goldcraft_effects_included
    #endinput
#endif
#define _goldcraft_effects_included
#pragma reqlib goldcraft
#pragma loadlib goldcraft

// Requires the matched GoldCraftEffects002 host and GoldCraft AMXX module.
// Successful apply/update registers a state declaration. It does not acknowledge
// native or Minecraft physics execution, a committed mutation, or a client ACK.
// A new deferred motion intent must not be labelled already applied in native CS.

#define GC_EFFECT_TARGET_PLAYER 1
#define GC_EFFECT_TARGET_OBJECT 2

#define GC_EFFECT_INPUT_LOCK (1 << 0)
#define GC_EFFECT_POSITION_HOLD (1 << 1)
#define GC_EFFECT_WISH_SPEED_CAP (1 << 2)
#define GC_EFFECT_GRAVITY_FACTOR (1 << 3)

#define GC_EFFECT_LOCK_MOVE (1 << 0)
#define GC_EFFECT_LOCK_JUMP (1 << 1)
#define GC_EFFECT_LOCK_ATTACK (1 << 2)
#define GC_EFFECT_LOCK_USE (1 << 3)

#define GC_EFFECT_HOLD_X (1 << 0)
#define GC_EFFECT_HOLD_Y (1 << 1)
#define GC_EFFECT_HOLD_Z (1 << 2)
#define GC_EFFECT_MAX_LEASE_MS 60000

// Errors are negative. Service rejections are -100 minus ServiceStatus from the
// matching native service contract. Target/effect handles are opaque positive
// cells local to this AMX caller and are never reusable ownership tokens.
#define GC_EFFECT_ERR_ARITY -1
#define GC_EFFECT_ERR_CALLER -2
#define GC_EFFECT_ERR_UNAVAILABLE -3
#define GC_EFFECT_ERR_HANDLE -4
#define GC_EFFECT_ERR_INPUT -5
#define GC_EFFECT_ERR_CAPACITY -6
#define GC_EFFECT_ERR_ALIAS_EXHAUSTED -7
#define GC_EFFECT_ERR_ALLOCATION -8
#define GC_EFFECT_ERR_SERVICE_ANOMALY -9

// Pawn schema remains 1: declarations and payloads are unchanged. The matched
// native module/host now require GoldCraftEffects002 (QPC clock and failure gate).
// A clock fault disables this service until restart; map reload cannot clear it.
#define GC_EFFECT_ERR_CLOCK_FAILED -115
native gc_effect_api_version();

// Player keys are slots 1..64 with keyHigh=0. Object keyLow/keyHigh preserve the
// unsigned bit halves of an authoritative uint64 object key. The service resolves
// world/connection/life identity; callers cannot supply or override that identity.
native gc_effect_target(kind, keyLow, keyHigh = 0);

// Inactive fields use 0/0/0.0/1.0. Wish cap is host units/second and caps input,
// not total velocity. Normalized gravity 0.0 explicitly means zero gravity.
// Lease is 1..GC_EFFECT_MAX_LEASE_MS and must be renewed before expiry.
// Returns an effect handle, or a negative error. No physical execution guarantee.
native gc_effect_apply(targetHandle, capabilities, inputLocks, holdAxes,
                       Float:wishSpeedCap, Float:gravityFactor, leaseMs);

// Returns 1 on registration update, or a negative error. Expired effects cannot
// be revived by update; resolve the current target and register a new declaration.
native gc_effect_update(effectHandle, capabilities, inputLocks, holdAxes,
                        Float:wishSpeedCap, Float:gravityFactor, leaseMs);

// Effect handles revoke this source. Target handles release only the identity
// ticket, leaving effects already registered using that ticket intact.
native gc_effect_drop(handle);

// Revokes this caller's effects and discards its handles. Explicitly invoke from
// plugin_pause/plugin_end where possible; no public SDK notification detects all
// pause paths. Independent host expiry remains required even when Pawn is paused.
native gc_effect_owner_release();
