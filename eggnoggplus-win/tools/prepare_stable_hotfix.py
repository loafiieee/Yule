"""Backport the solid/preview fixes onto the verified 1.932 native source tree.

The main checkout remains EOS-capable. This creates a separate build directory,
preserves stable v17 networking, and never installs or launches the game.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FIX_FILES = ('entity_package.c', 'entity_package.h', 'entity_package_json.c', 'entity_world.h')
NETWORK_FILES = ('ggpo_net.c', 'ggpo_net.h', 'net_ext.c', 'net_ext.h',
                 'online_control.c', 'online_control.h')
STABLE_1932_DLL = '83583201f3ff5dfe0e44f82d3d42660797315da41107a833ef85a6aeeca5ac74'

def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-tree', type=Path,
        default=ROOT / 'build/private_beta/channel_bridge_source')
    parser.add_argument('--out-dir', type=Path,
        default=ROOT / 'build/stable-solid-release/source')
    parser.add_argument('--version', default='1.933')
    parser.add_argument('--fix-base', default='1090a5d')
    parser.add_argument('--fix-ref', help='Committed fixes; omission includes the working tree.')
    args = parser.parse_args()
    base, target = args.base_tree.resolve(), args.out_dir.resolve()
    if target.exists():
        raise RuntimeError('Output already exists; preserve it before preparing another build.')
    if not re.fullmatch(r'[0-9]+(?:\.[0-9]+)*', args.version):
        raise ValueError('Version must contain numeric components.')
    prefix = git('rev-parse', '--show-prefix').decode().strip()
    provenance = json.loads((base / 'bridge_provenance.json').read_text())
    assert provenance['version'] == '1.932' and provenance['p2p_protocol'] == 17 and not provenance['eos']
    assert digest(base / 'SDL2.dll') == STABLE_1932_DLL, 'Unverified stable baseline DLL'
    for name in NETWORK_FILES:
        assert digest(base / name) == provenance['unchanged_network_sha256'][name], name
        assert (base / name).read_bytes() == git('show', provenance['source_commit'] + ':' + prefix + name), name
    for name in FIX_FILES:
        # These modules were shared by 1.932 and the EOS development baseline.
        assert (base / name).read_bytes() == git('show', args.fix_base + ':' + prefix + name), name

    diff_args = ['diff', args.fix_base]
    if args.fix_ref:
        diff_args.append(args.fix_ref)
    patch = git(*diff_args, '--unified=3', '--', 'hooks.c').decode()
    hooks = (base / 'hooks.c').read_text(encoding='utf-8')
    hunks = re.split(r'(?=^@@ )', patch, flags=re.M)[1:]
    assert hunks, 'No solid/preview hook changes to backport'
    for hunk in hunks:
        old, new = [], []
        for line in hunk.splitlines()[1:]:
            if line.startswith((' ', '-')):
                old.append(line[1:])
            if line.startswith((' ', '+')):
                new.append(line[1:])
        before, after = '\n'.join(old), '\n'.join(new)
        if hooks.count(before) != 1:
            raise RuntimeError('Stable hook context does not match uniquely: ' + hunk.splitlines()[0])
        hooks = hooks.replace(before, after, 1)
    assert 'yule_eos_' not in hooks and '"eos_runtime.h"' not in hooks
    assert 'hooked_check_map_collide' in hooks and 'hooks_resolve_native_solid_body' in hooks

    target.mkdir(parents=True)
    for path in base.iterdir():
        if path.is_file() and (path.suffix in {'.c', '.h', '.inc', '.def', '.dll'} or path.name == 'compile.sh'):
            if path.name.startswith('EOS'):
                raise RuntimeError('Stable baseline unexpectedly contains the EOS runtime')
            shutil.copy2(path, target / path.name)
    for name in ('third_party', 'tools', 'dist/installer'):
        shutil.copytree(base / name, target / name)
    (target / 'hooks.c').write_text(hooks, encoding='utf-8', newline='\n')
    for name in FIX_FILES:
        data = git('show', args.fix_ref + ':' + prefix + name) if args.fix_ref else (ROOT / name).read_bytes()
        (target / name).write_bytes(data)
    header = (target / 'update_ext.h').read_text(encoding='utf-8')
    header, count = re.subn(r'#define FRAMEWORK_VERSION "[^"]+"',
        '#define FRAMEWORK_VERSION "' + args.version + '"', header)
    assert count == 1
    (target / 'update_ext.h').write_text(header, encoding='utf-8', newline='\n')
    shutil.copytree(ROOT / 'tests', target / 'tests', ignore=shutil.ignore_patterns('__pycache__'))
    shutil.copytree(ROOT / 'docs/examples', target / 'docs/examples')
    shutil.copytree(ROOT / 'greggnogg', target / 'greggnogg', ignore=shutil.ignore_patterns('node_modules'))
    for name in ('AGENTS.md', 'eggnoggplus.exe'):
        shutil.copy2(ROOT / name, target / name)
    for name in NETWORK_FILES:
        assert (target / name).read_bytes() == (base / name).read_bytes(), name
    compile_source = (target / 'compile.sh').read_text()
    assert '-DYULE_ENABLE_EOS' not in compile_source and 'eos_runtime.c' not in compile_source
    report = {'version': args.version, 'stable_baseline': provenance,
        'baseline_dll_sha256': STABLE_1932_DLL, 'hook_hunks': len(hunks),
        'fix_base': git('rev-parse', args.fix_base).decode().strip(),
        'fix_ref': git('rev-parse', args.fix_ref).decode().strip() if args.fix_ref else 'working tree',
        'p2p_protocol': 17, 'eos': False,
        'source_sha256': {name: digest(target / name) for name in ('hooks.c', *FIX_FILES, *NETWORK_FILES)}}
    (target / 'hotfix_provenance.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Prepared stable ' + args.version + ': ' + str(target))
    print(str(len(hunks)) + ' verified hook hunks; stable networking unchanged; EOS excluded.')

if __name__ == '__main__':
    main()
