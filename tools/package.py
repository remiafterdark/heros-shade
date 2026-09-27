"""Pack the per-platform bundles CI builds into one .dusk.

    python tools/package.py --bundle a.dusk --bundle b.dusk ... --out <path>

A .dusk is a zip: mod.json, and the mod's library under lib/<platform>/. Dusklight loads the one for
the platform it is running on (windows-amd64, linux-x86_64, macos-arm64, android-aarch64,
ios-arm64...), so a .dusk that carries all of them works everywhere. CI builds each platform with
the SDK's own packaging, one lib/ folder per bundle; this takes the lib/ folder out of each and adds
mod.json (and the listing art in res/, if there is any) once.
"""

import argparse
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bundle', action='append', default=[], required=True,
                    help='a per-platform .dusk whose lib/ folder to include (repeatable)')
    ap.add_argument('--out', required=True)
    args = ap.parse_args()

    entries = [(os.path.join(HERE, 'mod.json'), 'mod.json')]
    for art in ('icon.png', 'banner.png'):
        full = os.path.join(HERE, 'res', art)
        if os.path.isfile(full):
            entries.append((full, 'res/' + art))
    libs = []
    platforms = set()
    for bundle in args.bundle:
        with zipfile.ZipFile(bundle) as zb:
            for name in zb.namelist():
                if not name.startswith('lib/') or name.endswith('/'):
                    continue
                if name in {n for _, n in libs}:
                    continue
                libs.append((bundle, name))
                platforms.add(name.split('/')[1])
    if not libs:
        sys.exit('no lib/ folder in any bundle')
    print('platforms: ' + ', '.join(sorted(platforms)))

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with zipfile.ZipFile(args.out, 'w', zipfile.ZIP_DEFLATED) as z:
        for src, dst in entries:
            z.write(src, dst)
        for bundle, name in libs:
            with zipfile.ZipFile(bundle) as zb:
                z.writestr(name, zb.read(name))
    print('wrote %s (%d files, %.1f MB)' % (
        args.out, len(entries) + len(libs), os.path.getsize(args.out) / (1024.0 * 1024.0)))


if __name__ == '__main__':
    main()
