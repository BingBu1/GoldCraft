# P345 original-art host presentation

Checks the actual author's P345 inventory-art mapping, full RGBA modern card,
original magazine sprite, separately authored native CS selection sprite and
unchanged default Glock output. It compares palette/alpha pixels, both TGA image
origins, malformed images and all generated resource hashes. Original assets
are supplied read-only and are never published.

```bash
python tests/cryoffear-p345-presentation/run.py \
  --original "$COF_ORIGINAL_GAME" \
  --baseline sandbox/cs-server/Half-Life/cstrike \
  --output build/cof-p345-presentation-validation
```

The modern card uses the original inventory table's picture; the stock fallback
is an explicit 216-color/index255 conversion. This does not claim a reconstruction
of CoF inventory layout or a real purchase-menu gameplay acceptance.
