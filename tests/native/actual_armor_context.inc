bool GoldCraft_QueueNativeArmor(edict_t* player,float damage,edict_t* inflictor,edict_t* attacker,int bits,edict_t* logged_attacker,bool team_attack,int armor_hit){
    if(player==final_minecraft_damage||player==minecraft_attack)return false;
    return QueueNativeArmor(player,damage,inflictor,attacker,bits,logged_attacker,team_attack,armor_hit);
}
const char* GoldCraft_EquipmentWeapon(edict_t* player){return player==settling_native_armor?settling_native_weapon:nullptr;}
GoldCraft_EquipmentDamageScope::GoldCraft_EquipmentDamageScope() noexcept
    :player_(settling_native_armor),factor_(settling_native_enchantment),weapon_(settling_native_weapon){
    settling_native_armor=nullptr;settling_native_enchantment=1;settling_native_weapon=nullptr;
}
GoldCraft_EquipmentDamageScope::~GoldCraft_EquipmentDamageScope() noexcept{
    settling_native_armor=player_;settling_native_enchantment=factor_;settling_native_weapon=weapon_;
}
