"""Required cumulative server features, independent of a partial release freeze.

Keep new feature resources explicit: a DLL-only candidate must not silently drop
previously delivered plugin activation, translations, models or download assets.
"""
from pathlib import Path
import hashlib
from retired_resources import RETIRED_PLUGINS, retired_resource, payload_bytes


FEATURE_PLUGINS = (
    'zp50_zombie_armor.amxx',
    'zp50_zombie_respawn.amxx', 'zp50_zombie_knockback.amxx',
    'zp50_zombie_gore.amxx', 'zp50_effects_infect.amxx',
)
REQUIRED_PLUGINS = (
    'goldcraft.amxx', 'goldcraft_controls.amxx', 'goldcraft_connection.amxx',
    'goldcraft_zp_rules.amxx', 'goldcraft_zp_combat.amxx', *FEATURE_PLUGINS,
)
PLUGIN_DIRECTORY = 'cs/cstrike/addons/amxmodx/plugins/'
CONFIG_DIRECTORY = 'cs/cstrike/addons/amxmodx/configs/'
FROZEN_RESOURCES = (
    *(PLUGIN_DIRECTORY + name for name in FEATURE_PLUGINS),
    *(CONFIG_DIRECTORY + name for name in (
        'plugins-goldcraft-controls.ini', 'plugins-goldcraft-connection.ini',
        'plugins-goldcraft-zp-rules.ini',
        'plugins-zp50-zombie-armor.ini', 'plugins-zp50-zombie-respawn.ini',
        'plugins-zp50-zombie-knockback.ini', 'plugins-zp50-zombie-gore.ini',
    )),
    'cs/cstrike/addons/amxmodx/data/lang/zp50_zombie_respawn.txt',
    'cs/cstrike/sprites/zp_infection.spr',
)


def freeze_requirements(sources, runtime_resources, digest):
    """Read only, before output creation; require a complete verified freeze."""
    indexed = {}
    for name, source in sources.items():
        if name.startswith('server/plugins/'):
            destination = PLUGIN_DIRECTORY + Path(name).name
        elif name.startswith('server/configs/'):
            destination = CONFIG_DIRECTORY + Path(name).name
        elif name in runtime_resources:
            destination = 'cs/' + runtime_resources[name]
        else:
            continue
        if retired_resource(destination):
            continue
        data = payload_bytes(destination, source.read_bytes())
        row = dict(source=source, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        key = destination.casefold()
        if key in indexed and any(indexed[key][field] != row[field] for field in ('bytes', 'sha256')):
            raise ValueError('Conflicting frozen server destination: ' + destination)
        indexed[key] = row
    missing = [name for name in FROZEN_RESOURCES if name.casefold() not in indexed]
    if missing:
        raise ValueError('Cumulative server freeze is missing: ' + ', '.join(missing))
    empty = [name for name in FROZEN_RESOURCES if indexed[name.casefold()]['bytes'] <= 0]
    if empty:
        raise ValueError('Cumulative server freeze has empty resources: ' + ', '.join(empty))
    return {name: indexed[name.casefold()] for name in FROZEN_RESOURCES}


def verify_payload(rows, required_plugins, frozen):
    """Check actual payload and installer activation before publishing an archive."""
    if not set(REQUIRED_PLUGINS).issubset(required_plugins):
        raise ValueError('Cumulative server plugin activation is incomplete')
    if any(p.casefold() in RETIRED_PLUGINS for p in required_plugins):
        raise ValueError('Retired plugin in activation')
    indexed = {}
    for row in rows:
        if row['role'] != 'server':
            continue
        name = row['destination']
        if retired_resource(name):
            raise ValueError('Retired resource in payload: ' + name)
        key = name.casefold()
        if key in indexed or row['path'] != 'payload/server/' + name:
            raise ValueError('Duplicate or inconsistent server payload: ' + name)
        indexed[key] = row
    required = set(FROZEN_RESOURCES) | {PLUGIN_DIRECTORY + name for name in REQUIRED_PLUGINS}
    resources = {}
    for name in sorted(required):
        row = indexed.get(name.casefold())
        if row is None or type(row.get('bytes')) is not int or row['bytes'] <= 0:
            raise ValueError('Missing or empty cumulative server resource: ' + name)
        expected = frozen.get(name)
        if name in FROZEN_RESOURCES and expected is None:
            raise ValueError('Missing frozen cumulative server resource: ' + name)
        if expected is not None and any(row[field] != expected[field] for field in ('bytes', 'sha256')):
            raise ValueError('Cumulative server payload differs from freeze: ' + name)
        resources[name] = {field: row[field] for field in ('bytes', 'sha256')}
    return dict(schema=1, requiredPlugins=list(REQUIRED_PLUGINS), resources=resources)
