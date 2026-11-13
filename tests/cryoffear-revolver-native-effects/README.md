# Revolver native sound consumers

Run the shared P345 native-effects driver with `--weapon revolver`. It validates the Revolver EmptyPlay vtable and compares actual native EmitSound calls against original x86 empty/sleeve/pickup/ammo consumers, including NaN suppression, quiet volume, channels, emitters, strict missing-backend failures and callback retirement. Fire PlaySFX, delayed shell-event audio and local Studio audio retain independent routes. No runtime/shop or gameplay acceptance is implied.
