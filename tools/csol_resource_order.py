"""Resolve extracted-resource candidates in the verified CSO mount order.

This is a preparation tool; gameplay reads the resulting common directory.
Archive entry timestamps are metadata, not lookup priority. The caller must
verify readable payloads before applying the selected candidates.
"""

MOUNT_ORDER = (
    "fixtrike", "locale_chn", "cstrike_chn", "common",
    "fstrike", "estrike", "dstrike", "cstrike", "valve",
)
NAR_ORDER = tuple(name + ".nar" for name in MOUNT_ORDER
                  if name not in ("locale_chn", "common"))


def select_resources(entries):
    """Return original row objects keyed by case-insensitive relative path.

    Each row supplies relative, mount, source and bytes; digest fields are used
    only to prove identical duplicates inside one mount. Conflicting payloads
    inside one prefix need a separate archive-order decision, so they fail here.
    """
    priority = {name: index for index, name in enumerate(MOUNT_ORDER)}
    mounts = {}
    for row in entries:
        mount = row["mount"]
        if mount not in priority:
            raise ValueError(f"Unverified CSO mount: {mount}")
        key = row["relative"].lower()
        candidates = mounts.setdefault(mount, {})
        old = candidates.get(key)
        if old is None:
            candidates[key] = row
            continue
        conflicting_digest = any(old.get(kind) and row.get(kind) and
                                 old[kind] != row[kind] for kind in ("sha256", "md5"))
        same_source = (old["source"] == row["source"] and old["bytes"] == row["bytes"]
                       and old.get("offset") == row.get("offset") and not conflicting_digest)
        same_digest = old["bytes"] == row["bytes"] and any(
            old.get(kind) and old.get(kind) == row.get(kind) for kind in ("sha256", "md5"))
        if conflicting_digest or (not same_source and not same_digest):
            raise ValueError(f"Ambiguous resources within mount {mount}: {row['relative']}")
    selected = {}
    for mount in MOUNT_ORDER:
        for key, row in mounts.get(mount, {}).items():
            selected.setdefault(key, row)
    return selected
