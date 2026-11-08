"""Ports the party-game UI (the original 5 party games of the have_api DLL) from ToyHeroes' scene XML into another
client's scene XML (ToyBattles) by a three-way merge:

  base   = ToyHeroes' own scene XML, from its ui.dat (no party UI)
  theirs = ToyHeroes' modded loose XML (data/ui/scene: the party dialogs, controls, components, textures, fonts), with
           the lines make_hardpoint_client_files.py adds (Hardpoint / Headquarters / Prop Hunt) removed: that tool adds
           them again on top of the result
  ours   = the target client's scene XML (e.g. ToyBattles' data/ui/scene)

Every change base -> theirs is applied to ours where the base lines it touches are found unchanged in ours (the two
clients' XML differ in a few attributes only). A change whose base lines are different in ours is a CONFLICT: it is
merged node by node, attribute by attribute (an attribute both sides changed alike is fine); an attribute
changed differently on both sides keeps the target's value (reported as "kept"); a block that cannot be merged at all
is a CONFLICT: reported and not applied (fix by hand). The XML is handled as bytes (Korean font names in a legacy code page).

Usage: python port_party_ui.py <ToyHeroes game dir> <target game dir> <output dir>
         -> <output dir>/SCENE_ROOM.xml, SCENE_BATTLE.xml + a report of what was applied
"""
import difflib
import os
import re
import sys
import zipfile

sys.path.insert(0, os.path.dirname(__file__))
from make_hardpoint_client_files import strip_previous  # noqa: E402

FILES = ('SCENE_ROOM.xml', 'SCENE_BATTLE.xml')
KEPT = []  # attributes both sides changed differently: the target's value kept


def base_xml(game, name):
    with zipfile.ZipFile(os.path.join(game, 'data', 'ui.dat')) as z:
        entry = next(n for n in z.namelist() if n.lower() == ('scene/' + name).lower())
        return z.read(entry)


def merge(base, theirs, ours):
    """(merged lines, applied changes, conflicts). Lines without their line ending."""
    b = base.split(b'\n')
    t = theirs.split(b'\n')
    o = ours.split(b'\n')
    # base line -> our line, where equal
    base_to_ours = {}
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, b, o, autojunk=False).get_opcodes():
        if tag == 'equal':
            for k in range(i2 - i1):
                base_to_ours[i1 + k] = j1 + k
    edits = []  # (our start, our end, new lines)
    conflicts = []
    applied = 0
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, b, t, autojunk=False).get_opcodes():
        if tag == 'equal':
            continue
        if i1 < i2:  # replace / delete: every touched base line must be unchanged in ours, consecutive
            mapped = [base_to_ours.get(i) for i in range(i1, i2)]
            if None in mapped or mapped != list(range(mapped[0], mapped[0] + len(mapped))):
                resolved = merge_by_node(b[i1:i2], t[j1:j2], o, edits)
                if resolved is not None:
                    conflicts.append((tag, i1, i2, resolved))
                    continue
                applied += 1
                continue
            edits.append((mapped[0], mapped[-1] + 1, t[j1:j2]))
        else:  # insert before base line i1: anchor on the base line before it (or after it)
            if i1 - 1 in base_to_ours:
                at = base_to_ours[i1 - 1] + 1
            elif i1 in base_to_ours:
                at = base_to_ours[i1]
            else:
                conflicts.append((tag, i1, i2, t[j1][:100]))
                continue
            edits.append((at, at, t[j1:j2]))
        applied += 1
    merged = list(o)
    for start, end, lines in sorted(edits, key=lambda e: (e[0], e[1]), reverse=True):
        merged[start:end] = lines
    return merged, applied, conflicts


def node_name(line):
    m = re.search(rb'nodename="([^"]*)"', line)
    return m.group(1) if m else None


def attributes(line):
    return dict(re.findall(rb' ([A-Za-z_0-9]+)="([^"]*)"', line))


def merge_by_node(base_lines, their_lines, ours, edits):
    """A block both sides changed: line by line by node name, attribute by attribute. An attribute theirs changed is
    set in ours unless ours changed it to something else (then: conflict); a node only theirs has is inserted after the
    previous node; a node theirs removed is removed. None when resolved, else a description of the conflict."""
    base_by_name = {node_name(l): l for l in base_lines if node_name(l)}
    ours_index = {node_name(l): k for k, l in enumerate(ours) if node_name(l)}
    previous = None
    for line in their_lines:
        name = node_name(line)
        if name is None:
            return 'a line without a node name: %r' % line[:80]
        if name in base_by_name and name in ours_index:
            base_attrs, their_attrs = attributes(base_by_name[name]), attributes(line)
            k = ours_index[name]
            merged = ours[k]
            for key in set(base_attrs) | set(their_attrs):
                if base_attrs.get(key) == their_attrs.get(key):
                    continue
                our_value = attributes(merged).get(key)
                if our_value == their_attrs.get(key):
                    continue  # ours made the same change
                if our_value != base_attrs.get(key):
                    # both changed it differently (ToyBattles' own layout / text): the target's value stays
                    KEPT.append('%s: kept the target value %s=%r (ToyHeroes mod: %r)' % (
                        name.decode('latin1'), key.decode(), our_value, their_attrs.get(key)))
                    continue
                if key not in their_attrs or key not in attributes(merged):
                    return '%s: attribute %s added or removed' % (name, key)
                merged = re.sub(rb' ' + key + rb'="[^"]*"', b' ' + key + b'="' + their_attrs[key] + b'"', merged, count=1)
            if merged != ours[k]:
                edits.append((k, k + 1, [merged]))
            previous = k
        elif name not in base_by_name and name not in ours_index:
            if previous is None:
                return '%s: new node without a known node before it' % name
            edits.append((previous + 1, previous + 1, [line]))
        elif name in ours_index and name not in base_by_name:
            previous = ours_index[name]  # ours has it already
        else:
            return '%s: in base but not in ours' % name
    for name in base_by_name:
        if name not in {node_name(l) for l in their_lines} and name in ours_index:
            k = ours_index[name]
            edits.append((k, k + 1, []))
    return None


def check_ids(data, name):
    """Problems a game parser would refuse: a duplicate attribute in a tag; duplicate component ids."""
    problems = []
    for m in re.finditer(rb'<[A-Za-z]+ [^>]*>', data):
        attrs = re.findall(rb' ([A-Za-z_0-9]+)="', m.group(0))
        if len(attrs) != len(set(attrs)):
            problems.append('%s: duplicate attribute in %s' % (name, m.group(0)[:80]))
    ids = re.findall(rb'<Component id="(\d+)"', data)
    duplicates = {i for i in ids if ids.count(i) > 1}
    if duplicates:
        problems.append('%s: duplicate component ids %s' % (name, sorted(duplicates)[:10]))
    return problems


def main(toyheroes, target, out):
    os.makedirs(out, exist_ok=True)
    report = []
    for name in FILES:
        base = base_xml(toyheroes, name)
        theirs = strip_previous(open(os.path.join(toyheroes, 'data', 'ui', 'scene', name), 'rb').read())
        ours = open(os.path.join(target, 'data', 'ui', 'scene', name), 'rb').read()
        merged, applied, conflicts = merge(base, theirs, ours)
        data = b'\n'.join(merged)
        open(os.path.join(out, name), 'wb').write(data)
        report.append('%s: %d changes applied, %d conflicts' % (name, applied, len(conflicts)))
        report += ['  CONFLICT %s base lines %d..%d: %s' % c for c in conflicts]
        report += ['  ' + p for p in check_ids(data, name)]
        report += ['  kept: ' + k for k in KEPT]
        KEPT.clear()
    open(os.path.join(out, 'port_report.txt'), 'w').write('\n'.join(report) + '\n')
    print('\n'.join(report))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3])
