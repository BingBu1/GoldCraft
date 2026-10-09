package dev.goldcraft.compatibility;

import com.github.tartaricacid.touhoulittlemaid.entity.passive.EntityMaid;
import com.mojang.authlib.GameProfile;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import io.netty.buffer.Unpooled;
import java.lang.reflect.Proxy;
import java.nio.file.Files;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import net.EFTLM.EF.API.Event.MaidSkillRemoveEvent;
import net.EFTLM.EF.Capability.MaidPatch;
import net.EFTLM.EF.Crafting.RandomAltarRecipe;
import net.EFTLM.EF.Item.MaidSkillBookItem;
import net.EFTLM.EF.Network.Packet.Server.OpenMaidSkillScreenPacket;
import net.EFTLM.EF.Network.Packet.Server.ForgetMaidSkillPacket;
import net.EFTLM.EF.Skill.MaidSkillManager;
import net.EFTLM.EF.Skill.MaidSkillDataManager;
import net.EFTLM.EF.Utils.CompoundTagManager;
import net.EFTLM.TLM.Task.FightModeTask;
import net.minecraft.core.BlockPos;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.network.RegistryFriendlyByteBuf;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.entity.monster.Zombie;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.phys.AABB;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.ModList;
import net.neoforged.bus.api.EventPriority;
import net.neoforged.bus.api.SubscribeEvent;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.common.util.FakePlayerFactory;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import net.neoforged.neoforge.event.tick.ServerTickEvent;
import net.neoforged.neoforge.network.handling.IPayloadContext;
import yesman.epicfight.world.capabilities.EpicFightCapabilities;
import org.spongepowered.asm.mixin.extensibility.IMixinConfigPlugin;

/** Runs against the installed third-party Mods in a disposable production server. */
@Mod("goldcraft_maid_probe")
public final class EpicFightMaidProbe extends OpenMaidSkillScreenPacket {
    private static final JsonArray checks = new JsonArray();
    private static EntityMaid fighter;
    private static Zombie target;
    private static float initialHealth;
    private static int ticks;
    private static boolean done;

    public EpicFightMaidProbe() {
        super(0);
        NeoForge.EVENT_BUS.addListener(EpicFightMaidProbe::started);
        NeoForge.EVENT_BUS.addListener(EpicFightMaidProbe::tick);
    }

    private static void check(String name, boolean condition) {
        if (!condition) throw new AssertionError(name);
        checks.add(name);
    }

    /** Production server handler with controlled dispatch; this is not a network/GUI test. */
    private static void forget(ServerPlayer sender, int maidId, ResourceLocation skill) {
        var context = (IPayloadContext) Proxy.newProxyInstance(IPayloadContext.class.getClassLoader(),
                new Class<?>[]{IPayloadContext.class}, (proxy, method, args) -> switch (method.getName()) {
                    case "player" -> sender;
                    case "enqueueWork" -> {
                        ((Runnable) args[0]).run();
                        yield CompletableFuture.completedFuture(null);
                    }
                    default -> throw new AssertionError("Unexpected payload context call: " + method.getName());
                });
        ForgetMaidSkillPacket.handle(new ForgetMaidSkillPacket(maidId, skill), context);
    }

    public static final class CancelForgetting {
        @SubscribeEvent(priority = EventPriority.LOWEST)
        public static void cancel(MaidSkillRemoveEvent event) { event.setCanceled(true); }
    }

    private static void started(ServerStartedEvent event) {
        try {
            ServerLevel level = event.getServer().overworld();
            level.setDayTime(18000);
            level.getChunk(0, 0);
            for (int x = -4; x <= 12; x++) for (int z = -4; z <= 12; z++)
                level.setBlockAndUpdate(new BlockPos(x, 0, z), Blocks.STONE.defaultBlockState());
            var skillId = MaidSkillManager.getNonWeaponSkillName().stream().sorted().findFirst().orElseThrow();
            check("core skills registered", MaidSkillManager.getSkillRegisterName().containsAll(List.of(
                    ResourceLocation.parse("ef_tlm:blade_clash"), ResourceLocation.parse("ef_tlm:step"))));
            var optionalSkills = List.of("board_blade_innate", "hf_murasama_innate", "hf_blade_innate", "yamato_innate",
                    "meen_innate", "claw_innate", "scythe_innate", "kusabimaru_innate", "blood_lust_innate");
            boolean nightfall = ModList.get().isLoaded("efn");
            check("NightFall skills follow optional Mod presence", optionalSkills.stream().allMatch(id ->
                    MaidSkillManager.hasSkillFor(ResourceLocation.fromNamespaceAndPath("ef_tlm", id)) == nightfall));
            var guard = (IMixinConfigPlugin) Class.forName("com.ysmef.geomodel.mixin.YsmGeoMixinPlugin").getConstructor().newInstance();
            check("optional YSM mixin requires YSM and yields to its main compat", guard.shouldApplyMixin(
                    "com.elfmcys.yesstevemodel.geckolib3.core.controller.AnimationControllerRuntime",
                    "com.ysmef.geomodel.mixin.ysm.YsmAnimationTransitionGuardMixin")
                    == (ModList.get().isLoaded("yes_steve_model") && !ModList.get().isLoaded("ysm_epicfight_compat")));
            fighter = new EntityMaid(level);
            fighter.moveTo(3.5, 1, 3.5, 0, 0);
            check("spawn actual maid", level.addFreshEntity(fighter));
            MaidPatch<?> patch = EpicFightCapabilities.getEntityPatch(fighter, MaidPatch.class);
            check("Epic Fight maid patch attached", patch != null);
            patch.setStamina(10);
            check("expanded synchronized stamina", patch.getStamina() == 10 && patch.getMaxStamina() > 0);
            patch.addLearnedSkill(skillId);
            check("learn skill", patch.hasLearnedSkill(skillId));
            CompoundTag saved = fighter.saveWithoutId(new CompoundTag());
            saved.remove("UUID");
            EntityMaid restored = new EntityMaid(level);
            restored.load(saved);
            restored.moveTo(10.5, 1, 10.5, 0, 0);
            check("load persisted maid", level.addFreshEntity(restored));
            var restoredPatch = EpicFightCapabilities.getEntityPatch(restored, MaidPatch.class);
            check("skill survives real entity NBT reload", restoredPatch != null && restoredPatch.hasLearnedSkill(skillId));
            restored.discard();
            var recipe = event.getServer().getRecipeManager().byKey(ResourceLocation.parse("ef_tlm:altar/random_skill_book")).orElseThrow().value();
            check("1.21.1 altar recipe loaded", recipe instanceof RandomAltarRecipe);
            ((RandomAltarRecipe) recipe).spawnOutputEntity(level, new BlockPos(10, 1, 10), List.of());
            check("altar outputs valid skill component", level.getEntitiesOfClass(ItemEntity.class,
                    new AABB(9, 0, 9, 12, 4, 12)).stream().anyMatch(e -> MaidSkillBookItem.getContainSkill(e.getItem()) != null));
            var buf = new RegistryFriendlyByteBuf(Unpooled.buffer(), level.registryAccess());
            try {
                ForgetMaidSkillPacket.CODEC.encode(buf, new ForgetMaidSkillPacket(fighter.getId(), skillId));
                var copy = ForgetMaidSkillPacket.CODEC.decode(buf);
                check("skill packet decoded completely", !buf.isReadable());
                ForgetMaidSkillPacket.CODEC.encode(buf, copy);
                check("skill packet entity and registry ID survive", buf.readVarInt() == fighter.getId() && buf.readResourceLocation().equals(skillId));
            } finally { buf.release(); }
            ServerPlayer owner = FakePlayerFactory.get(level, new GameProfile(UUID.fromString("79e84fd9-6ed7-458b-b635-d2d1da5c54fa"), "MaidProbeOwner"));
            owner.moveTo(4.5, 1, 3.5, 0, 0);
            level.addNewPlayer(owner);
            // TLM resolves owners from the server roster, not ServerLevel.players().
            // FakePlayerFactory skips login, so register only this disposable fixture there.
            var rosterField = net.minecraft.server.players.PlayerList.class.getDeclaredField("playersByUUID");
            rosterField.setAccessible(true);
            @SuppressWarnings("unchecked")
            var roster = (Map<UUID, ServerPlayer>) rosterField.get(event.getServer().getPlayerList());
            roster.put(owner.getUUID(), owner);
            fighter.tame(owner);
            if(!stillValid(owner,fighter))throw new AssertionError("nearby owner menu: owned="+fighter.isOwnedBy(owner)
                    +", owner="+fighter.getOwner()+", ownerUUID="+fighter.getOwnerUUID()+", playerUUID="+owner.getUUID()
                    +", sleeping="+fighter.isSleeping()+", alive="+fighter.isAlive()+", reach="+owner.canInteractWithEntity(fighter,3));
            check("owner may access nearby maid menu", true);
            ServerPlayer outsider = FakePlayerFactory.get(level, new GameProfile(UUID.fromString("79e84fd9-6ed7-458b-b635-d2d1da5c54fb"), "MaidProbeOther"));
            outsider.moveTo(4.5, 1, 3.5, 0, 0);
            check("other player denied maid menu", !stillValid(outsider, fighter));
            owner.moveTo(100, 1, 100, 0, 0);
            check("distant owner denied maid menu", !stillValid(owner, fighter));
            owner.moveTo(4.5, 1, 3.5, 0, 0);
            var skill = MaidSkillManager.getSkillFor(skillId);
            var dataKey = MaidSkillDataManager.SkillDataKey.createDataKey(
                    ResourceLocation.parse("goldcraft_maid_probe:forget_state"), MaidSkillDataManager.ValueType.integer());
            patch.registerData(skill, dataKey, 42);
            forget(outsider, fighter.getId(), skillId);
            check("other player cannot forget owned maid skill", patch.hasLearnedSkill(skillId) && patch.hasData(skill, dataKey));
            owner.moveTo(100, 1, 100, 0, 0);
            forget(owner, fighter.getId(), skillId);
            check("distant owner cannot forget skill", patch.hasLearnedSkill(skillId));
            owner.moveTo(4.5, 1, 3.5, 0, 0);
            forget(owner, Integer.MAX_VALUE, skillId);
            forget(owner, fighter.getId(), ResourceLocation.parse("goldcraft_maid_probe:missing_skill"));
            check("unknown entity and skill leave state intact", patch.hasLearnedSkill(skillId) && patch.getDataValue(skill, dataKey) == 42);
            NeoForge.EVENT_BUS.register(CancelForgetting.class);
            try {
                forget(owner, fighter.getId(), skillId);
                check("cancelled forgetting preserves skill and data", patch.hasLearnedSkill(skillId)
                        && patch.hasData(skill, dataKey) && patch.getDataValue(skill, dataKey) == 42);
            } finally { NeoForge.EVENT_BUS.unregister(CancelForgetting.class); }
            forget(owner, fighter.getId(), skillId);
            check("owner forget handler removes skill and data", !patch.hasLearnedSkill(skillId) && !patch.hasData(skill, dataKey));
            var persistent = fighter.getPersistentData().getCompound(CompoundTagManager.MaidCap);
            check("forgotten skill data does not persist", !persistent.getCompound(CompoundTagManager.SkillData).contains(skillId.toString()));
            restoredPatch.registerData(skill, dataKey, 7);
            restoredPatch.deserializeNBT(patch.serializeNBT());
            check("empty skill update clears prior data", !restoredPatch.hasLearnedSkill(skillId) && !restoredPatch.hasData(skill, dataKey));
            restoredPatch.addLearnedSkill(skillId);
            restoredPatch.registerData(skill, dataKey, 7);
            restoredPatch.removeLearnedSkill(skillId);
            check("direct skill removal also clears saved data", !restoredPatch.hasData(skill, dataKey)
                    && !restored.getPersistentData().getCompound(CompoundTagManager.MaidCap)
                    .getCompound(CompoundTagManager.SkillData).contains(skillId.toString()));
            restoredPatch.addLearnedSkill(skillId);
            restoredPatch.registerData(skill, dataKey, 7);
            restoredPatch.clearLearnedSkills();
            check("clearing skills clears data before persistence", restoredPatch.getLearnedSkills().isEmpty()
                    && !restoredPatch.hasData(skill, dataKey)
                    && !restored.getPersistentData().getCompound(CompoundTagManager.MaidCap).contains(CompoundTagManager.SkillData));
            patch.addLearnedSkill(skillId);
            fighter.setItemInHand(InteractionHand.MAIN_HAND, new ItemStack(Items.IRON_SWORD));
            fighter.setSchedule(com.github.tartaricacid.touhoulittlemaid.entity.ai.brain.MaidSchedule.ALL);
            fighter.setTask(new FightModeTask());
            fighter.setHomeModeEnable(false);
            target = new Zombie(EntityType.ZOMBIE, level);
            target.setNoAi(true);
            target.setPersistenceRequired();
            target.moveTo(3.5, 1, 5.5, 180, 0);
            check("spawn hostile combat target", level.addFreshEntity(target));
            initialHealth = target.getHealth();
            check("actual maid fight task enabled", patch.isFightMode());
        } catch (Throwable error) { finish(error); }
    }

    private static void tick(ServerTickEvent.Post event) {
        if (done || target == null) return;
        try {
            ticks++;
            if (target.getHealth() < initialHealth) {
                check("autonomous Epic Fight maid damages hostile mob", true);
                check("vanilla damage retains maid attacker", target.getLastHurtByMob() == fighter);
                finish(null);
            } else if (ticks >= 500) {
                throw new AssertionError("maid did not damage hostile in 500 server ticks; target="
                        + fighter.getTarget() + ", position=" + fighter.position());
            }
        } catch (Throwable error) { finish(error); }
    }

    private static void finish(Throwable error) {
        done = true;
        JsonObject report = new JsonObject();
        report.addProperty("passed", error == null);
        report.add("checks", checks);
        report.addProperty("ticks", ticks);
        if (error != null) {
            report.addProperty("failure", error.toString());
            error.printStackTrace();
        }
        try { Files.writeString(FMLPaths.GAMEDIR.get().resolve("maid-checks.json"), report.toString()); }
        catch (Exception failure) { throw new IllegalStateException(failure); }
    }
}
