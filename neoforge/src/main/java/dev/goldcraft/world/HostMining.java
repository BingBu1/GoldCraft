package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.MapMining;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.net.MiningPayload;
import net.minecraft.block.BlockState;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.entity.projectile.ProjectileUtil;
import net.minecraft.item.ItemStack;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.world.GameMode;
import net.minecraft.world.RaycastContext;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

/** MC computes real tool progress; HLDS retraces damage and sampled cell commits. */
public final class HostMining {
    private static final Map<UUID, Intent> INTENTS = new HashMap<>();
    private static final Map<Long, Pending> PENDING = new HashMap<>();
    private static long event, tick, policyRevision;

    private record Target(int slot, int serial, int model, BlockPos cell, long edits) {
        static Target of(HostRaycast.Hit hit, HostWorldState host) {
            return new Target(hit.slot, hit.serial, hit.model, hit.getBlockPos(), host.edits().revision());
        }
    }

    private static final class Intent {
        long epoch, revision, lastIntent, nextTick, pending, generation, sampleUntil;
        long sampleEvent;
        int serial, life;
        boolean held, mineable;
        float progress;
        Target target;
        ItemStack tool, toolCopy;
        BlockState material;
        BlockPos miningCell;
        GameMode mode;

        void reset() {
            if (target != null) ++generation;
            target = null;
            tool = toolCopy = null;
            mode = null;
            material = null;
            progress = 0;
            sampleUntil = 0;
            sampleEvent = 0;
            miningCell = null;
            mineable = false;
        }
    }

    private record Pending(UUID player, HostWorldState.Actor actor, Target target, ItemStack tool,
                           ItemStack toolCopy, BlockState material, long revision, long generation, GameMode mode,
                           BlockPos miningCell, boolean sample, long expires) {}

    private HostMining() {}
    public static void clear() { INTENTS.clear(); PENDING.clear(); event = tick = policyRevision = 0; }
    public static void disconnect(UUID id) {
        INTENTS.remove(id); PENDING.values().removeIf(p -> p.player().equals(id));
    }

    public static void intent(ServerPlayerEntity player, MiningPayload payload) {
        var host = GoldCraft.HOST_WORLD;
        var actor = host.actor(player.getUuid());
        var policy = host.mining();
        if (actor == null || payload.epoch() != host.epoch() || payload.serial() != actor.serial()
            || payload.life() != actor.life() || payload.revision() != policy.revision()
            || !actor.minecraftForm() || (actor.flags() & 32) == 0) return;
        var state = INTENTS.computeIfAbsent(player.getUuid(), ignored -> new Intent());
        if (state.epoch != payload.epoch() || state.revision != payload.revision()
            || state.serial != payload.serial() || state.life != payload.life() || !payload.held()) state.reset();
        state.epoch = payload.epoch(); state.revision = payload.revision();
        state.serial = payload.serial(); state.life = payload.life();
        state.held = payload.held(); state.lastIntent = System.nanoTime();
    }

    private static boolean current(ServerPlayerEntity player, HostWorldState.Actor actor, HostWorldState host) {
        return player != null && player.isAlive() && actor != null && actor.minecraftForm()
            && (actor.flags() & 33) == 33
            && player.getWorld().getRegistryKey().getValue().toString().equals(host.dimension());
    }

    public static void tick(MinecraftServer server, HostWorldState host) {
        ++tick;
        var policy = host.mining();
        if (policyRevision != policy.revision()) {
            policyRevision = policy.revision();
            INTENTS.values().forEach(s -> { s.held = false; s.reset(); });
        }
        PENDING.entrySet().removeIf(entry -> {
            if (tick < entry.getValue().expires()) return false;
            var state = INTENTS.get(entry.getValue().player());
            if (state != null && state.pending == entry.getKey()) { state.pending = 0; state.reset(); }
            return true;
        });
        for (var entry : INTENTS.entrySet()) {
            var state = entry.getValue();
            var player = server.getPlayerManager().getPlayer(entry.getKey());
            var actor = host.actor(entry.getKey());
            if (!policy.enabled() || host.freeze() || !state.held || System.nanoTime() - state.lastIntent > 350_000_000L
                || !current(player, actor, host) || player.isUsingItem() || state.epoch != host.epoch()
                || state.revision != policy.revision() || state.serial != actor.serial() || state.life != actor.life()) {
                state.reset(); continue;
            }
            var mode = player.interactionManager.getGameMode();
            if (mode != GameMode.CREATIVE && mode != GameMode.SURVIVAL) { state.reset(); continue; }
            double reach = Math.min(6, player.getBlockInteractionRange());
            if (!Double.isFinite(reach) || reach <= 0) { state.reset(); continue; }
            var eye = player.getEyePos();
            var end = eye.add(player.getRotationVec(1).multiply(reach));
            var hit = player.getWorld().raycast(new RaycastContext(eye, end, RaycastContext.ShapeType.OUTLINE,
                                                                 RaycastContext.FluidHandling.NONE, player));
            if (!(hit instanceof HostRaycast.Hit target) || !policy.canRequest(target.model)
                || ProjectileUtil.raycast(player, eye, hit.getPos(), new Box(eye, hit.getPos()).expand(1),
                    e -> !e.isSpectator() && e.canHit(), eye.squaredDistanceTo(hit.getPos())) != null) {
                state.reset(); continue;
            }
            var key = Target.of(target, host);
            var tool = player.getMainHandStack();
            if (mode != state.mode || !key.equals(state.target) || tool != state.tool || !ItemStack.areEqual(tool, state.toolCopy)) {
                state.reset(); state.target = key; state.tool = tool; state.toolCopy = tool.copy(); state.mode = mode;
            }
            if (state.pending != 0 || tick < state.nextTick) continue;
            if (state.material == null || tick >= state.sampleUntil) {
                send(entry.getKey(), state, actor, target, policy, reach, 1, true);
                continue;
            }
            var material = state.material;
            // Pinned NeoForge APIs include BreakSpeed and HarvestCheck, tool components,
            // mining attributes, effects, and the water/air penalties.
            var cell = state.miningCell;
            if (!state.mineable || !player.getWorld().canPlayerModifyAt(player, cell)
                || !tool.getItem().canMine(material, player.getWorld(), cell, player)
                || mode != GameMode.CREATIVE && material.getHardness(player.getWorld(), cell) < 0) {
                state.progress = 0; continue;
            }
            float delta = mode == GameMode.CREATIVE ? 1 : material.calcBlockBreakingDelta(player, player.getWorld(), cell);
            if (!Float.isFinite(delta) || delta <= 0) { state.progress = 0; continue; }
            state.progress = Math.min(1, state.progress + delta);
            if (state.progress < 1) continue;
            double base = player.getAttributeValue(EntityAttributes.GENERIC_ATTACK_DAMAGE) * 5;
            if (!Double.isFinite(base) || base <= 0) { state.reset(); continue; }
            // Preserve native HP/TakeDamage/Ham. A material strike cannot bypass
            // entity immunity or a native plugin veto.
            float amount = mode == GameMode.CREATIVE ? 5000 : (float)Math.min(5000, base);
            if (send(entry.getKey(), state, actor, target, policy, reach, amount, false)) {
                state.progress = 0; state.nextTick = tick + 4;
            }
        }
    }

    private static boolean send(UUID id, Intent state, HostWorldState.Actor actor, HostRaycast.Hit target,
                                MapMining.Policy policy, double reach, float amount, boolean sample) {
        if (event == Long.MAX_VALUE) throw new IllegalStateException("Mining event sequence exhausted");
        long sequence = ++event;
        var point = HostCollision.goldsrc(target.getPos().x, target.getPos().y, target.getPos().z);
        var request = MapMining.request(policy, sequence, actor, target.slot, target.serial, target.model,
            point.x(), point.y(), point.z(), amount, (float)(reach * Wire.UNITS_PER_BLOCK), sample ? 0 : state.sampleEvent);
        if (!GoldCraft.sendToHost(sample ? Wire.MAP_MINING_SAMPLE : Wire.MAP_MINING_REQUEST, request)) return false;
        state.pending = sequence;
        PENDING.put(sequence, new Pending(id, actor, state.target, state.tool, state.tool.copy(), state.material,
            policy.revision(), state.generation, state.mode, state.miningCell, sample, tick + 40));
        return true;
    }

    private static Pending take(HostWorldState host, MapMining.Result result, boolean sample) {
        var pending = PENDING.get(result.event());
        if (pending == null || pending.sample() != sample || result.epoch() != host.epoch()
            || result.slot() != pending.actor().slot() || result.serial() != pending.actor().serial()
            || result.life() != pending.actor().life() || result.target() != pending.target().slot()
            || result.targetSerial() != pending.target().serial()) return null;
        PENDING.remove(result.event());
        var state = INTENTS.get(pending.player());
        if (state != null && state.pending == result.event()) state.pending = 0;
        return pending;
    }

    private static boolean validResult(Pending pending, MapMining.Result result, ServerPlayerEntity player,
                                       HostWorldState.Actor actor, HostWorldState host) {
        return result.status() == MapMining.APPLIED && result.revision() == pending.revision()
            && result.revision() == host.mining().revision() && current(player, actor, host)
            && actor.serial() == result.serial() && actor.life() == result.life()
            && player.interactionManager.getGameMode() == pending.mode()
            && player.getMainHandStack() == pending.tool()
            && ItemStack.areEqual(pending.tool(), pending.toolCopy());
    }

    public static void surface(MinecraftServer server, HostWorldState host, byte[] bytes) {
        var surface = MapMining.surface(bytes);
        var result = surface.result();
        var pending = take(host, result, true);
        if (pending == null) return;
        var state = INTENTS.get(pending.player());
        if (state == null) return;
        var player = server.getPlayerManager().getPlayer(pending.player());
        if (!validResult(pending, result, player, host.actor(pending.player()), host)
            || state.generation != pending.generation() || !pending.target().equals(state.target) || !state.held) {
            state.reset(); return;
        }
        var material = HostMiningMaterials.state(surface.material());
        if (surface.editRevision() != 0 && surface.editRevision() != host.edits().revision()) {
            state.reset(); return;
        }
        var nativeCell = surface.cell();
        var cell = surface.editRevision() != 0
            ? new BlockPos(nativeCell.x(), nativeCell.z() + (int)Wire.Y_OFFSET, -nativeCell.y() - 1)
            : state.target.cell();
        if (state.material != material || !cell.equals(state.miningCell)) state.progress = 0;
        state.material = material;
        state.miningCell = cell;
        state.sampleEvent = surface.editRevision() != 0 ? result.event() : 0;
        state.mineable = surface.kind() == 1 || host.mining().mode() == MapMining.ALL_GEOMETRY
            && (host.mining().capabilities() & MapMining.GEOMETRY_CARVING) != 0 && surface.editRevision() != 0;
        state.sampleUntil = tick + 10;
    }

    public static void result(MinecraftServer server, HostWorldState host, byte[] bytes) {
        var result = MapMining.result(bytes);
        var pending = take(host, result, false);
        if (pending == null) return;
        var state = INTENTS.get(pending.player());
        if (state != null) { state.material = null; state.progress = 0; }
        var player = server.getPlayerManager().getPlayer(pending.player());
        if (result.after() > 0 || !validResult(pending, result, player, host.actor(pending.player()), host)
            || player.interactionManager.getGameMode() != GameMode.SURVIVAL) return;
        pending.tool().postMine(player.getWorld(), pending.material(), pending.miningCell(), player);
    }
}
