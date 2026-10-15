package dev.goldcraft.test;

import com.google.gson.Gson;
import com.mojang.brigadier.arguments.IntegerArgumentType;
import com.mojang.brigadier.arguments.StringArgumentType;
import net.minecraft.commands.Commands;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.entity.item.PrimedTnt;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.TntBlock;
import net.neoforged.bus.api.EventPriority;
import net.neoforged.fml.common.Mod;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.RegisterCommandsEvent;
import net.neoforged.neoforge.event.entity.EntityJoinLevelEvent;
import net.neoforged.neoforge.event.level.ExplosionEvent;
import net.neoforged.neoforge.event.entity.living.LivingIncomingDamageEvent;
import net.neoforged.neoforge.event.entity.living.LivingDamageEvent;
import net.neoforged.neoforge.event.tick.ServerTickEvent;

import java.util.ArrayList;
import java.util.Base64;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Opt-in local dedicated-server fixture; never part of the production core. */
@Mod("goldcraft_explosion_fixture")
public final class ExplosionFixture {
    private static final Gson JSON = new Gson();
    private String mode = "off";
    private final List<Map<String, Object>> events = new ArrayList<>();
    private final List<Map<String, Object>> spawns = new ArrayList<>();
    private final List<Map<String, Object>> damage = new ArrayList<>();
    private static final BlockPos ROOT_TNT = new BlockPos(3, 76, -90);
    private final PairedTntPlayer paired = new PairedTntPlayer();

    public ExplosionFixture() {
        NeoForge.EVENT_BUS.addListener(this::commands);
        NeoForge.EVENT_BUS.addListener(EventPriority.LOWEST, this::start);
        NeoForge.EVENT_BUS.addListener(EventPriority.LOWEST, this::detonate);
        NeoForge.EVENT_BUS.addListener(this::joined);
        NeoForge.EVENT_BUS.addListener(EventPriority.LOWEST, this::incoming);
        NeoForge.EVENT_BUS.addListener(EventPriority.LOWEST, this::damagePre);
        NeoForge.EVENT_BUS.addListener(EventPriority.LOWEST, this::damagePost);
        NeoForge.EVENT_BUS.addListener(EventPriority.LOWEST, this::tick);
    }

    private void tick(ServerTickEvent.Post event) { paired.tick(event.getServer()); }

    private boolean chainTnt(PrimedTnt tnt) {
        return (mode.equals("chain") || mode.equals("damage")) && tnt.distanceToSqr(3.5, 76, -89.5) < 16 * 16;
    }

    private Map<String, Object> nativeOrigin(Entity source) {
        if (!(source instanceof PrimedTnt)) return null;
        var saved = source.saveWithoutId(new CompoundTag());
        if (!saved.contains("goldcraft_tnt_owner", 10)) return null;
        var origin = saved.getCompound("goldcraft_tnt_owner");
        Map<String, Object> value = new LinkedHashMap<>();
        value.put("epoch", Long.toUnsignedString(origin.getLong("epoch")));
        for (String key : List.of("slot", "serial", "spawn")) value.put(key, origin.getInt(key));
        value.put("paired", origin.hasUUID("paired") ? origin.getUUID("paired").toString() : "none");
        return value;
    }

    private void recordDamage(String phase, LivingEntity target, DamageSource source, float amount, boolean canceled) {
        if (!mode.equals("damage") || target.level().isClientSide()
            || !target.level().dimension().location().toString().equals("goldcraft:cs_assault_f6725c06")
            || target.distanceToSqr(3.5, 76, -89.5) >= 16 * 16) return;
        if (damage.size() >= 128) throw new IllegalStateException("Fixture damage capacity");
        Map<String, Object> row = new LinkedHashMap<>();
        row.put("phase", phase);
        row.put("tick", target.level().getGameTime());
        row.put("target", target.getUUID().toString());
        row.put("targetId", target.getId());
        row.put("targetType", target.getClass().getName());
        row.put("health", target.getHealth());
        row.put("amount", amount);
        row.put("canceled", canceled);
        row.put("source", source.getMsgId());
        row.put("direct", source.getDirectEntity() == null ? "none" : source.getDirectEntity().getUUID().toString());
        row.put("attacker", source.getEntity() == null ? "none" : source.getEntity().getUUID().toString());
        row.put("attackerType", source.getEntity() == null ? "none" : source.getEntity().getClass().getName());
        row.put("nativeOrigin", nativeOrigin(source.getDirectEntity()));
        damage.add(row);
    }

    private void incoming(LivingIncomingDamageEvent event) {
        recordDamage("incoming", event.getEntity(), event.getSource(), event.getAmount(), event.isCanceled());
    }

    private void damagePre(LivingDamageEvent.Pre event) {
        recordDamage("pre", event.getEntity(), event.getSource(), event.getNewDamage(), false);
    }

    private void damagePost(LivingDamageEvent.Post event) {
        recordDamage("post", event.getEntity(), event.getSource(), event.getNewDamage(), false);
    }

    private void joined(EntityJoinLevelEvent event) {
        if (event.getLevel().isClientSide()
            || !event.getLevel().dimension().location().toString().equals("goldcraft:cs_assault_f6725c06")
            || !(event.getEntity() instanceof PrimedTnt tnt) || !chainTnt(tnt)) return;
        if (spawns.size() >= 16) throw new IllegalStateException("Fixture spawn capacity");
        Map<String, Object> row = new LinkedHashMap<>();
        row.put("source", tnt.getUUID().toString());
        row.put("tick", event.getLevel().getGameTime());
        row.put("fuse", tnt.getFuse());
        row.put("position", List.of(tnt.getX(), tnt.getY(), tnt.getZ()));
        row.put("owner", tnt.getOwner() == null ? "none" : tnt.getOwner().getUUID().toString());
        row.put("ownerType", tnt.getOwner() == null ? "none" : tnt.getOwner().getClass().getName());
        row.put("nativeOrigin", nativeOrigin(tnt));
        spawns.add(row);
    }

    private boolean accepts(ExplosionEvent event) {
        return !mode.equals("off") && !event.getLevel().isClientSide()
            && event.getLevel().dimension().location().toString().equals("goldcraft:cs_assault_f6725c06")
            && event.getExplosion().getDirectSourceEntity() instanceof PrimedTnt tnt
            && (tnt.getTags().contains("goldcraft_explosion_fixture") || chainTnt(tnt));
    }

    private Map<String, Object> record(String phase, ExplosionEvent event) {
        var explosion = event.getExplosion();
        var source = explosion.getDirectSourceEntity();
        Map<String, Object> row = new LinkedHashMap<>();
        row.put("phase", phase);
        row.put("mode", mode);
        row.put("tick", event.getLevel().getGameTime());
        row.put("source", source.getUUID().toString());
        row.put("removed", source.isRemoved());
        row.put("nativeOrigin", nativeOrigin(source));
        row.put("power", explosion.radius());
        row.put("center", List.of(explosion.center().x, explosion.center().y, explosion.center().z));
        row.put("owner", explosion.getIndirectSourceEntity() == null ? "none"
            : explosion.getIndirectSourceEntity().getUUID().toString());
        if (events.size() >= 32) throw new IllegalStateException("Fixture event capacity");
        events.add(row);
        return row;
    }

    private void start(ExplosionEvent.Start event) {
        if (!accepts(event)) return;
        var row = record("start", event);
        row.put("canceledBefore", event.isCanceled());
        if (mode.equals("cancel")) event.setCanceled(true);
        row.put("canceledAfter", event.isCanceled());
    }

    private void detonate(ExplosionEvent.Detonate event) {
        if (!accepts(event)) return;
        var row = record("detonate", event);
        var blocks = event.getAffectedBlocks();
        row.put("before", positions(blocks));
        if (mode.equals("clear")) blocks.clear();
        if (mode.equals("prune")) blocks.removeIf(p -> p.getX() >= 3);
        row.put("after", positions(blocks));
        row.put("entities", event.getAffectedEntities().stream().map(e -> Map.of(
            "id", e.getId(), "uuid", e.getUUID().toString(), "type", e.getClass().getName())).toList());
    }

    private static List<List<Integer>> positions(List<BlockPos> blocks) {
        return blocks.stream().map(p -> List.of(p.getX(), p.getY(), p.getZ())).toList();
    }

    private static String snapshot() throws ReflectiveOperationException {
        Object world = Class.forName("dev.goldcraft.GoldCraft").getField("HOST_WORLD").get(null);
        Object edits = world.getClass().getMethod("edits").invoke(world);
        Object snapshot = edits.getClass().getMethod("snapshot").invoke(edits);
        byte[] bytes = (byte[]) snapshot.getClass().getMethod("encode").invoke(snapshot);
        return Base64.getEncoder().encodeToString(bytes);
    }

    private int ignite(net.minecraft.commands.CommandSourceStack source, int slot) {
        if ((!mode.equals("chain") && !mode.equals("damage")) || !paired.accepts(source.getServer())) {
            source.sendFailure(Component.literal("Chain mode and an empty local MC server are required"));
            return 0;
        }
        ServerLevel level = null;
        for (ServerLevel candidate : source.getServer().getAllLevels()) {
            if (candidate.dimension().location().toString().equals("goldcraft:cs_assault_f6725c06")) level = candidate;
        }
        if (level == null || !level.getBlockState(ROOT_TNT).is(Blocks.TNT) || !spawns.isEmpty()) {
            source.sendFailure(Component.literal("Expected one prepared TNT trial in the host dimension"));
            return 0;
        }
        Player owner = null;
        Map<String, Object> result = new LinkedHashMap<>();
        try {
            if (slot == -1) {
                owner = paired.owner();
                result.put("paired", paired.report());
            } else if (slot != 0) {
                var type = Class.forName("dev.goldcraft.world.NativePlayers");
                var players = (List<?>) type.getMethod("targetingPlayers", ServerLevel.class, List.class)
                    .invoke(null, level, List.of());
                for (Object entry : players) {
                    Object actor = entry.getClass().getMethod("actor").invoke(entry);
                    if ((int) actor.getClass().getMethod("slot").invoke(actor) == slot) {
                        owner = (Player) entry;
                        result.put("slot", slot);
                        result.put("serial", actor.getClass().getMethod("serial").invoke(actor));
                        result.put("life", actor.getClass().getMethod("life").invoke(actor));
                        break;
                    }
                }
                if (owner == null || !owner.isAlive() || owner.isRemoved()) {
                    source.sendFailure(Component.literal("No live native identity at this slot"));
                    return 0;
                }
            }
        } catch (Exception failure) {
            throw new IllegalStateException(failure);
        }
        result.put("owner", owner == null ? "none" : owner.getUUID().toString());
        // Actual NeoForge/vanilla ignition. Children are spawned only by the
        // real explosion hitting adjacent TNT blocks; the fixture just observes.
        ((TntBlock) Blocks.TNT).onCaughtFire(level.getBlockState(ROOT_TNT), level, ROOT_TNT, null, owner);
        level.removeBlock(ROOT_TNT, false);
        result.put("spawns", spawns);
        String message = "GC_EXPLOSION_FIXTURE " + JSON.toJson(result);
        source.sendSuccess(() -> Component.literal(message), false);
        return 1;
    }

    private void commands(RegisterCommandsEvent event) {
        event.getDispatcher().register(Commands.literal("gc_explosion_fixture")
            .requires(s -> s.hasPermission(4) && s.getEntity() == null)
            .then(Commands.literal("ignite_ownerless").executes(c -> ignite(c.getSource(), 0)))
            .then(Commands.literal("ignite_paired").executes(c -> ignite(c.getSource(), -1)))
            .then(Commands.literal("pair_create")
                .then(Commands.argument("slot", IntegerArgumentType.integer(1, 32)).executes(c -> {
                    try {
                        for (ServerLevel level : c.getSource().getServer().getAllLevels()) {
                            if (!level.dimension().location().toString().equals("goldcraft:cs_assault_f6725c06")) continue;
                            paired.create(c.getSource().getServer(), level, IntegerArgumentType.getInteger(c, "slot"));
                            c.getSource().sendSuccess(() -> Component.literal("GC_PAIRED created"), false);
                            return 1;
                        }
                        return 0;
                    } catch (Exception failure) { throw new IllegalStateException(failure); }
                })))
            .then(Commands.literal("ignite_native")
                .then(Commands.argument("slot", IntegerArgumentType.integer(1, 32))
                    .executes(c -> ignite(c.getSource(), IntegerArgumentType.getInteger(c, "slot")))))
            .then(Commands.argument("action", StringArgumentType.word()).executes(context -> {
                String action = StringArgumentType.getString(context, "action");
                var source = context.getSource();
                if (!paired.accepts(source.getServer())) {
                    source.sendFailure(Component.literal("Fixture requires an empty local MC server"));
                    return 0;
                }
                Map<String, Object> result = new LinkedHashMap<>();
                if (List.of("off", "pass", "cancel", "clear", "prune", "chain", "damage").contains(action)) {
                    mode = action;
                    events.clear();
                    spawns.clear();
                    damage.clear();
                } else if (action.equals("report")) {
                    result.put("events", events);
                    result.put("spawns", spawns);
                    result.put("damage", damage);
                } else if (action.equals("snapshot")) {
                    try { result.put("snapshot", snapshot()); }
                    catch (ReflectiveOperationException failure) { throw new IllegalStateException(failure); }
                } else if (action.equals("pair_report") || action.equals("pair_remove")) {
                    try {
                        if (action.equals("pair_remove")) paired.remove(source.getServer());
                        result.put("paired", paired.report());
                    } catch (Exception failure) { throw new IllegalStateException(failure); }
                } else {
                    source.sendFailure(Component.literal("Unknown fixture action"));
                    return 0;
                }
                result.put("mode", mode);
                String message = "GC_EXPLOSION_FIXTURE " + JSON.toJson(result);
                source.sendSuccess(() -> Component.literal(message), false);
                return 1;
            })));
    }
}
