"""Layout files. A layout may extend another one: {"extends": "<file next to it>", "corridor_keys": {"<corridor name>":
{key: value}}, "map_keys": {"<map id>": {key: value}}, ...}; corridor_keys and map_keys add or replace keys of the base
corridors and maps, the other keys replace the base ones."""
import json
import os


def load(path):
    with open(path) as f:
        lay = json.load(f)
    base = lay.pop('extends', None)
    if not base:
        return lay
    out = load(os.path.join(os.path.dirname(os.path.abspath(path)), base))
    extra = lay.pop('corridor_keys', {})
    corridors = {c['name']: c for c in out.get('corridors', [])}
    unknown = set(extra) - set(corridors)
    assert not unknown, 'corridor_keys for unknown corridors: %s' % sorted(unknown)
    for name, keys in extra.items():
        corridors[name].update(keys)
    map_extra = lay.pop('map_keys', {})
    maps = {str(m['id']): m for m in out.get('maps', [])}
    unknown = set(map_extra) - set(maps)
    assert not unknown, 'map_keys for unknown maps: %s' % sorted(unknown)
    for mid, keys in map_extra.items():
        maps[mid].update(keys)
    out.update(lay)
    return out
