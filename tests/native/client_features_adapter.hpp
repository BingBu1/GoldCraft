// Engine lookup adapter; capability parsing stays in the production header.
bool HasClientFeature(edict_t* entity, client_features::Feature feature, const char* legacy) {
    return entity && client_features::supports([&](const char* key) {
        return g_engfuncs.pfnInfoKeyValue(g_engfuncs.pfnGetInfoKeyBuffer(entity), key);
    }, feature, legacy);
}
