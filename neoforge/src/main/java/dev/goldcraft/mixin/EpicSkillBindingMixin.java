package dev.goldcraft.mixin;

import dev.goldcraft.world.EpicSkillSync;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Coerce;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Pseudo
@Mixin(targets="yesman.epicfight.skill.SkillContainer",remap=false)
abstract class EpicSkillBindingMixin {
    @Inject(method="setSkill(Lyesman/epicfight/skill/Skill;Z)Z",at=@At(value="INVOKE",
        target="Lyesman/epicfight/skill/Skill;onInitiate(Lyesman/epicfight/skill/SkillContainer;Lyesman/epicfight/api/event/EntityEventListener;)V"),require=1)
    private void goldcraft$bindBeforeFeedback(@Coerce Object skill,boolean initialize,CallbackInfoReturnable<Boolean> cir) {
        EpicSkillSync.beforeInitiate(this,initialize);
    }
}
