"""String tag reads outside engine/objects in sim code (roadmap ref6 t01, 2026-11-20).

Object-type tags are read through the typed handles of engine/objects (alamo/objects/type_tags.hpp: one default per
tag, GameObjectTypes::get(t, tags::X), name(t), sub_objects(t, tags::X)). A string read - types.get_float(t, "Tag"),
find_type_field("Tag"), raw().types().name[t] - outside engine/objects gives 0 on a typo and lets each reader carry its
own default, so this lint counts them per file in sim code and refuses any file whose count grows past
tools/lint/tag_reads_baseline.txt. Since 2026-11-20 the baseline is empty: every sim file reads 0, so any string read
fails (a hard rule). Read a new tag through a new ALAMO_TYPE_TAGS row; the objects test checks its default.

Sim code here: engine/{core,vfs,data,jobs,galactic,anim,commands,tactical,lua,ai} and engine/apps/{sim,headless};
tests, tools and benchmarks are not counted; engine/objects owns the strings.

Usage: check_tag_reads.py [--update | --selftest | --list FILE]
  (none)     exit 1 when a file reads more than its baseline (or a new file reads any); prints files now below it
  --update   rewrite the baseline from the code (only after lowering counts; a raise needs a reason in the commit)
  --list F   print the string reads of one file
Runs as the ctest entry alamo_objects_tag_reads_lint.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
BASELINE = os.path.join(HERE, 'tag_reads_baseline.txt')
SIM_DIRS = ['engine/' + d for d in ('core', 'vfs', 'data', 'jobs', 'galactic', 'anim', 'commands', 'tactical', 'lua', 'ai')] + \
    ['engine/apps/sim', 'engine/apps/headless']
SKIP_PARTS = ('/tests/', '/tools/', '/bench/', '/benchmark/', '/third_party/', '/testdata/')
READ = re.compile(r'(?:\.|->)get_(?:bool|int|float|string|vec|list|sub_objects|type_ref|type_refs|enum|mask)\s*\(\s*(?:[^;"()]|\([^;"()]*\))*?,\s*'  # one level of nested ( ) in the first argument
                  r'(?:(?:[^;",()]|\([^;"()]*\))*\?\s*)?"[A-Za-z_][A-Za-z0-9_]*"'  # also the first name of `cond ? "A" : "B"`
                  r'|\bfind_type_field\s*\(\s*"'
                  r'|(?:\.|->)raw\(\)\s*(?:\.|->)types\(\)\s*\.name\s*\[')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def count(text):
    return len(READ.findall(strip_comments(text)))


def scan(root=ROOT):
    out = {}
    for d in SIM_DIRS:
        base = os.path.join(root, d)
        for dp, _, fs in os.walk(base):
            for f in fs:
                if not f.endswith(('.cpp', '.hpp', '.inc', '.h')):
                    continue
                p = os.path.join(dp, f)
                rel = os.path.relpath(p, root).replace('\\', '/')
                if any(s in '/' + rel for s in SKIP_PARTS):
                    continue
                with open(p, encoding='utf8', errors='replace') as fh:
                    n = count(fh.read())
                if n:
                    out[rel] = n
    return out


def read_baseline(path=BASELINE):
    b = {}
    if os.path.isfile(path):
        for line in open(path, encoding='utf8'):
            line = line.strip()
            if line and not line.startswith('#'):
                rel, n = line.rsplit(' ', 1)
                b[rel] = int(n)
    return b


def write_baseline(counts, path=BASELINE):
    with open(path, 'w', encoding='utf8', newline='\n') as f:
        f.write('# String tag reads per sim file (tools/lint/check_tag_reads.py, ref6 t01). Only goes down; target 0.\n')
        f.write(f'# total {sum(counts.values())}\n')
        for rel in sorted(counts):
            f.write(f'{rel} {counts[rel]}\n')


def check(counts, base):
    bad, lower = [], []
    for rel, n in sorted(counts.items()):
        b = base.get(rel, 0)
        if n > b:
            bad.append(f'{rel}: {n} string tag reads, baseline {b} (read through alamo/objects/type_tags.hpp instead)')
        elif n < b:
            lower.append(f'{rel}: {n} < baseline {b}')
    for rel, b in sorted(base.items()):
        if rel not in counts and b:
            lower.append(f'{rel}: 0 < baseline {b}')
    return bad, lower


def selftest():
    fails = []
    t = 'x = types.get_float(t, "Max_Speed"); // types.get_int(t, "Gone")\n' \
        'y = ty.get_bool(\n  ti, "Is_Hero");\nz = types.get(t, tags::Guard_Chase_Range);\n' \
        'w = types.raw().types().name[t]; f = find_type_field("Ranged_Target_Z_Adjust"); s = get_string(t, name);\n'
    t += 'p->get_list(ti, "Target_Bones"); q.get_string(t, space ? "Space_Model_Name" : "Land_Model_Name");\n'
    t += 'r.get_int(static_cast<uint32_t>(i), "Tactical_Health"); eq(q.get_string(t, tags::Ship_Class), "bomber");\n'
    if count(t) != 7:
        fails.append(f'count {count(t)} != 7')
    bad, lower = check({'a.cpp': 3, 'b.cpp': 1}, {'a.cpp': 3, 'c.cpp': 2})
    if len(bad) != 1 or 'b.cpp' not in bad[0] or len(lower) != 1:
        fails.append(f'check {bad} {lower}')
    for f in fails:
        print('FAIL: ' + f)
    print('check_tag_reads selftest: ' + ('FAILED' if fails else 'ok'))
    return 1 if fails else 0


def main(argv):
    if argv == ['--selftest']:
        return selftest()
    if argv[:1] == ['--list'] and len(argv) == 2:
        text = strip_comments(open(argv[1], encoding='utf8').read())
        for m in READ.finditer(text):
            line = text.count('\n', 0, m.start()) + 1
            print(f'{argv[1]}:{line}: {m.group(0)[:100]}')
        return 0
    counts = scan()
    if argv == ['--update']:
        write_baseline(counts)
        print(f'check_tag_reads: baseline written ({sum(counts.values())} reads in {len(counts)} files)')
        return 0
    bad, lower = check(counts, read_baseline())
    for b in bad:
        print('check_tag_reads: ' + b)
    if lower:
        print(f'check_tag_reads: {len(lower)} file(s) below the baseline; lower it with check_tag_reads.py --update')
    print(f'check_tag_reads: {sum(counts.values())} string tag reads in sim code ({len(counts)} files)' + (' FAIL' if bad else ' ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
