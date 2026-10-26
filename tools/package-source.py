"""Package first-party sources, required notices and the current validation evidence."""
from pathlib import Path
import hashlib
import zipfile

root = Path(__file__).resolve().parent.parent
dist = root / 'dist'
dist.mkdir(exist_ok=True)
archive = dist / 'DesktopBoxes-0.2.0-source.zip'
files = [root / p for p in ('.clang-format', '.gitignore', 'CMakeLists.txt', 'README.md')]
for directory in ('src', 'tests', 'tools', 'docs', 'licenses', 'installer', 'assets'):
    files.extend(p for p in (root / directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
rejected_photos = root / 'docs/references/wallpaper'
files = [p for p in files if not (p.parent == rejected_photos and
         (p.name in {f'{n:02}.jpg' for n in range(1, 9)} or p.name == 'sources.json'))]
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for path in sorted(files):
        if path.is_symlink() or not path.resolve().is_relative_to(root):
            raise RuntimeError(f'Unexpected source link: {path}')
        z.write(path, 'DesktopBoxes/' + path.relative_to(root).as_posix())
    for name in ('core-tests.log', 'build.log', 'bootstrap-v2.log', 'compatibility-suite.log',
                 'core-tests-wine.log', 'windows-tests-wine.log', 'ui-smoke-wine.log',
                 'features-tests-wine.log', 'desktop-probe-wine.log', 'wallpaper-tests-wine.log',
                 'visual-capture-wine.log', 'wallpaper-motion-capture.log', 'pe-info.txt',
                 'preview-pe-info.txt', 'compatibility-environment.json', 'cmake-configure.log',
                 'cmake-build.log', 'cmake-tests.log', 'installer-wine.log', 'installer-pe-info.txt',
                 'validation-manifest.json'):
        path = root / 'build' / name
        if path.is_file():
            z.write(path, 'DesktopBoxes/verification/' + name)
artifacts = [dist / name for name in ('DesktopBoxes.exe', 'DesktopBoxes-0.2.0-x64-Setup.exe', archive.name)]
with (dist / 'SHA256SUMS.txt').open('w', encoding='ascii', newline='\n') as output:
    for path in artifacts:
        output.write(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name + '\n')
with zipfile.ZipFile(archive) as z:
    if z.testzip() is not None:
        raise RuntimeError('Corrupt source archive')
print(f'Packaged {len(files)} source files and available verification logs: {archive.name}')
