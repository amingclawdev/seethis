#!/usr/bin/env python3
"""Exercise production generator, Ninja dependencies and actual HTTP routes in isolated source fixtures."""
import hashlib
import pathlib
import shutil
import subprocess
import sys
import tempfile

SOURCE = pathlib.Path(__file__).resolve().parents[1]
GENERATOR = SOURCE / 'cmake/compile_settings_assets.py'

def run(args, *, succeeds=True, timeout=90):
    p = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    if (p.returncode == 0) != succeeds:
        raise RuntimeError('Unexpected command result: ' + ' '.join(map(str,args)) + '\n' + p.stdout + p.stderr)
    return p.stdout + p.stderr

with tempfile.TemporaryDirectory(prefix='seethis-fixed-settings-') as directory:
    temp = pathlib.Path(directory)
    html = temp / 'html'; javascript = temp / 'javascript'; out = temp / 'assets.h'
    html.write_bytes(b'<html>safe</html>'); javascript.write_bytes(b'void 0;')
    command = [sys.executable, str(GENERATOR), '--html', str(html), '--javascript', str(javascript), '--output', str(out)]
    run(command)
    original = out.read_bytes()
    run(command)
    assert out.read_bytes() == original, 'Generator is deterministic'
    for kind in ['missing', 'directory', 'fifo', 'device', 'oversize']:
        html.unlink(missing_ok=True)
        if kind == 'directory': html.mkdir()
        elif kind == 'fifo':
            import os
            os.mkfifo(html)
        elif kind == 'device': html.symlink_to('/dev/null')
        elif kind == 'oversize': html.write_bytes(b'x'*(262144+1))
        run(command, succeeds=False, timeout=5)
        assert out.read_bytes() == original, 'Failed generation must not replace previous header'
        if html.is_dir(): html.rmdir()
        else: html.unlink(missing_ok=True)
    html.write_bytes(b'<html>safe</html>')
    device_command=command.copy();device_command[device_command.index('--html')+1]='/dev/null'
    run(device_command,succeeds=False,timeout=5)
    javascript.write_bytes(b'x'*(262144+1))
    run(command,succeeds=False,timeout=5)
    javascript.write_bytes(b'void 0;')
    print('Missing/directory/FIFO/symlink/direct-device/both-input oversize generation rejected promptly',flush=True)
    # Only isolated fixture inputs are changed; the real candidate web sources remain untouched.
    fixture = temp / 'source'; fixture.mkdir()
    shutil.copy2(SOURCE / 'CMakeLists.txt', fixture / 'CMakeLists.txt')
    for path in ['src', 'tests', 'cmake', 'web/settings', 'assets/app-icon']:
        target = fixture / path; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(SOURCE / path, target, ignore=shutil.ignore_patterns('__pycache__'))
    source_html = fixture / 'web/settings/index.html'
    source_js = fixture / 'web/settings/settings.js'
    original_html, original_js = source_html.read_bytes(), source_js.read_bytes()
    for store in [True, False]:
        build = temp / ('store-build' if store else 'default-build')
        config = ['cmake', '-S', str(fixture), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.2', '-DBUILD_TESTING=ON', '-DSEETHIS_APP_STORE='+('ON' if store else 'OFF')]
        if store: config += ['-DSEETHIS_STORE_BUNDLE_ID=local.seethis.overlay','-DSEETHIS_STORE_VERSION=0.1.1','-DSEETHIS_STORE_BUILD=1']
        run(config)
        run(['cmake', '--build', str(build), '--target', 'reference_server_test', '-j2'])
        run([str(build/'reference_server_test'), '--settings-assets-only'], timeout=10)
        if store:
            header = build / 'generated/seethis/settings_assets.h'
            first_hash = hashlib.sha256(header.read_bytes()).hexdigest()
            # Maximum boundary + every byte value including NUL, with explicit lengths.
            source_html.write_bytes(bytes(range(256))*1024)
            log = run(['cmake', '--build', str(build), '--target', 'reference_server_test', '-j2'])
            assert 'Re-running CMake' in log, 'Changed source inputs did not trigger Ninja regeneration'
            assert hashlib.sha256(header.read_bytes()).hexdigest() != first_hash
            assert 'kIndexHTMLLength = 262144;' in header.read_text()
            run([str(build/'reference_server_test'), '--settings-assets-only'], timeout=10)
            html_only_hash=hashlib.sha256(header.read_bytes()).hexdigest()
            source_js.write_bytes(b'changed\x00\xff\r\nscript')
            js_log=run(['cmake','--build',str(build),'--target','reference_server_test','-j2'])
            assert 'Re-running CMake' in js_log, 'JS-only change did not trigger regeneration'
            assert hashlib.sha256(header.read_bytes()).hexdigest()!=html_only_hash
            run([str(build/'reference_server_test'),'--settings-assets-only'],timeout=10)
            source_html.write_bytes(original_html); source_js.write_bytes(original_js)
            run(['cmake', '--build', str(build), '--target', 'reference_server_test', '-j2'])
            assert hashlib.sha256(header.read_bytes()).hexdigest() == first_hash, 'Restoration must produce exact original arrays'
            run([str(build/'reference_server_test'), '--settings-assets-only'], timeout=10)
            print('Actual Store HTTP bytes, max/binary boundary, Ninja regeneration and restoration passed',flush=True)
        else:
            print('Actual default HTTP bytes/type/CSP/method/path/missing/FIFO/device/size guards passed',flush=True)
    assert source_html.read_bytes() == original_html and source_js.read_bytes() == original_js
print('Fixed settings asset generation/route checks passed')
