"""Exercise production gameplay publication/classification with observing Java adapters."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, default=ROOT / 'build/player-gameplay-check')
a = p.parse_args()
out = a.output.resolve()
if not out.is_relative_to(ROOT / 'build'):
    raise ValueError('Output must be in workspace build')
for part in (out, *out.parents):
    if part.is_symlink() or part.is_junction():
        raise ValueError('Redirected output')
    if part == ROOT:
        break
src, classes = out / 'src', out / 'classes'
classes.mkdir(parents=True, exist_ok=True)
adapters = {
    'net/minecraft/util/Identifier.java': 'public record Identifier(String namespace,String path){public static Identifier of(String n,String p){return new Identifier(n,p);}}',
    'net/minecraft/registry/RegistryKeys.java': 'public class RegistryKeys {public static final String DAMAGE_TYPE="damage_type";}',
    'net/minecraft/registry/tag/TagKey.java': 'public record TagKey<T>(String registry,net.minecraft.util.Identifier id){public static <T> TagKey<T> of(String r,net.minecraft.util.Identifier i){return new TagKey<T>(r,i);}}',
    'net/minecraft/entity/damage/DamageType.java': 'public record DamageType(String name){}',
    'net/minecraft/entity/damage/DamageTypes.java': '''public class DamageTypes {
public static final DamageType PLAYER_ATTACK=new DamageType("melee"),ARROW=new DamageType("arrow"),TRIDENT=new DamageType("trident");}''',
    'net/minecraft/entity/damage/DamageSource.java': '''public record DamageSource(Object attacker,DamageType type,boolean tagged){
public Object getAttacker(){return attacker;}public boolean isOf(DamageType t){return type.equals(t);}
public boolean isIn(net.minecraft.registry.tag.TagKey<DamageType> tag){return tagged;}}''',
    'net/minecraft/entity/EquipmentSlot.java': '''public enum EquipmentSlot {
HEAD(Type.HUMANOID_ARMOR),CHEST(Type.HUMANOID_ARMOR),LEGS(Type.HUMANOID_ARMOR),FEET(Type.HUMANOID_ARMOR),MAINHAND(Type.HAND),BODY(Type.ANIMAL_ARMOR);
public enum Type{HUMANOID_ARMOR,HAND,ANIMAL_ARMOR}final Type type;EquipmentSlot(Type t){type=t;}public Type getType(){return type;}}''',
    'net/minecraft/entity/attribute/EntityAttributes.java': 'public class EntityAttributes {public static final String GENERIC_ARMOR="armor";}',
    'net/minecraft/item/ArmorItem.java': 'public class ArmorItem {}',
    'net/minecraft/item/ItemStack.java': '''import java.util.function.BiConsumer;import net.minecraft.entity.EquipmentSlot;
public record ItemStack(Object item,String attribute,double amount,EquipmentSlot slot){
public boolean isEmpty(){return item==null;}public Object getItem(){return item;}
public record Modifier(double value){}
public void applyAttributeModifiers(EquipmentSlot target,BiConsumer<String,Modifier> action){if(slot==target)action.accept(attribute,new Modifier(amount));}}''',
    'net/minecraft/server/network/ServerPlayerEntity.java': '''import java.util.EnumMap;import net.minecraft.entity.EquipmentSlot;import net.minecraft.item.ItemStack;
public class ServerPlayerEntity {public boolean removed,creative,alive=true;public final EnumMap<EquipmentSlot,ItemStack> equipment=new EnumMap<>(EquipmentSlot.class);
public static class Connection {public boolean open=true;public boolean isOpen(){return open;}}
public static class Handler {public ServerPlayerEntity player;public Connection connection=new Connection();public Connection getConnection(){return connection;}}
public Handler networkHandler=new Handler();public ServerPlayerEntity(){networkHandler.player=this;}
public boolean isRemoved(){return removed;}public boolean isCreative(){return creative;}public boolean isAlive(){return alive;}
public ItemStack getEquippedStack(EquipmentSlot slot){return equipment.getOrDefault(slot,new ItemStack(null,"",0,slot));}}''',
    'net/minecraft/server/MinecraftServer.java': '''import java.util.*;import net.minecraft.server.network.ServerPlayerEntity;
public class MinecraftServer {public final Map<UUID,ServerPlayerEntity> players=new HashMap<>();public MinecraftServer getPlayerManager(){return this;}
public ServerPlayerEntity getPlayer(UUID id){return players.get(id);}}''',
    'dev/goldcraft/bridge/HostWorldState.java': '''import java.util.*;
public class HostWorldState {public final List<Actor> actors=new ArrayList<>();public long world=11;public long epoch(){return world;}public List<Actor> actors(){return actors;}
public record Actor(int slot,int serial,int spawn,int life,UUID minecraftPlayer,int flags){
public Actor(int slot,int serial,int spawn,int life,UUID id){this(slot,serial,spawn,life,id,1);}}}''',
    'dev/goldcraft/GoldCraft.java': '''import java.util.*;import dev.goldcraft.bridge.HostWorldState;
public class GoldCraft {public static final HostWorldState HOST_WORLD=new HostWorldState();public static boolean available=true,send=true;
public static final List<byte[]> packets=new ArrayList<>();public static boolean playerGameplayAvailable(){return available;}
public static boolean sendPlayerGameplay(int type,byte[] packet){if(!send)return false;packets.add(packet);return true;}}''',
}
for name, body in adapters.items():
    path = src / name
    path.parent.mkdir(parents=True, exist_ok=True)
    package = name.rsplit('/', 1)[0].replace('/', '.')
    path.write_text(f'package {package};\n{body}\n', encoding='utf-8')
production = [ROOT / name for name in (
    'neoforge/src/main/java/dev/goldcraft/world/PlayerGameplay.java',
    'neoforge/src/main/java/dev/goldcraft/world/ActivePlayerDamage.java',
    'neoforge/src/main/java/dev/goldcraft/bridge/Wire.java',
    'neoforge/src/main/java/dev/goldcraft/bridge/NativeArmorProtocol.java',
    'tests/player-gameplay/PlayerGameplayProbe.java')]
java = next((ROOT / '.tools/java').glob('jdk-21*/bin'))
args = ['--release', '21', '-encoding', 'UTF-8', '-proc:none', '-d', str(classes),
        *map(str, production), *map(str, src.rglob('*.java'))]
argfile = out / 'javac.args'
argfile.write_text('\n'.join('"' + s.replace('\\', '/') + '"' for s in args), encoding='utf-8')
runs = []
for name, command in [('compile', [str(java / 'javac.exe'), '@' + str(argfile)]),
                      ('test', [str(java / 'java.exe'), '-ea', '-cp', str(classes), 'dev.goldcraft.world.PlayerGameplayProbe'])]:
    result = subprocess.run(command, capture_output=True, timeout=60)
    (out / (name + '.log')).write_bytes(result.stdout + result.stderr)
    runs.append(dict(name=name, exitCode=result.returncode))
    print(name, result.returncode, (result.stdout + result.stderr).decode('utf-8', errors='replace'))
    if result.returncode:
        break
report = dict(passed=len(runs)==2 and all(r['exitCode']==0 for r in runs), runs=runs,
              sourceHashes={f.relative_to(ROOT).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in production},
              boundary='Production Java classes with observing player/equipment/transport adapters; not a live Minecraft/ZP test.')
(out / 'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
raise SystemExit(0 if report['passed'] else 1)
