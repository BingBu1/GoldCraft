package dev.goldcraft.test;

import com.google.gson.Gson;
import com.mojang.brigadier.arguments.StringArgumentType;
import net.minecraft.commands.Commands;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.world.entity.item.PrimedTnt;
import net.neoforged.bus.api.EventPriority;
import net.neoforged.fml.common.Mod;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.RegisterCommandsEvent;
import net.neoforged.neoforge.event.level.ExplosionEvent;

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

    public ExplosionFixture() {
        NeoForge.EVENT_BUS.addListener(this::commands);
        NeoForge.EVENT_BUS.addListener(EventPriority.LOWEST, this::start);
        NeoForge.EVENT_BUS.addListener(EventPriority.LOWEST, this::detonate);
    }

    private boolean accepts(ExplosionEvent event) {
        return !mode.equals("off") && !event.getLevel().isClientSide()
            && event.getLevel().dimension().location().toString().equals("goldcraft:cs_assault_f6725c06")
            && event.getExplosion().getDirectSourceEntity() instanceof PrimedTnt tnt
            && tnt.getTags().contains("goldcraft_explosion_fixture");
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

    private void commands(RegisterCommandsEvent event) {
        event.getDispatcher().register(Commands.literal("gc_explosion_fixture")
            .requires(s -> s.hasPermission(4) && s.getEntity() == null)
            .then(Commands.argument("action", StringArgumentType.word()).executes(context -> {
                String action = StringArgumentType.getString(context, "action");
                var source = context.getSource();
                if (source.getServer().getPlayerList().getPlayerCount() != 0) {
                    source.sendFailure(Component.literal("Fixture requires an empty local MC server"));
                    return 0;
                }
                Map<String, Object> result = new LinkedHashMap<>();
                if (List.of("off", "pass", "cancel", "clear", "prune").contains(action)) {
                    mode = action;
                    events.clear();
                } else if (action.equals("report")) {
                    result.put("events", events);
                } else if (action.equals("snapshot")) {
                    try { result.put("snapshot", snapshot()); }
                    catch (ReflectiveOperationException failure) { throw new IllegalStateException(failure); }
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
