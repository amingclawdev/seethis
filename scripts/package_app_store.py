#!/usr/bin/env python3
"""Local Store preparation/package only. Never logs in, uploads, installs or notarizes."""
import argparse
import base64
import datetime
import hashlib
import json
import os
import pathlib
import plistlib
import re
import stat
import subprocess

CAPABILITIES = {
    'com.apple.security.app-sandbox': True,
    'com.apple.security.network.server': True,
    'com.apple.security.automation.apple-events': True,
    'com.apple.security.temporary-exception.apple-events': ['com.google.Chrome'],
}

def run(args):
    return subprocess.run(args, check=True, capture_output=True).stdout

def validate_identifiers(bundle, version, build):
    if not re.fullmatch(r'[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+', bundle) or re.search(r'sandbox|probe', bundle, re.I):
        raise ValueError('A production bundle ID is required; no sandbox/probe IDs')
    if not re.fullmatch(r'\d+\.\d+(?:\.\d+)?', version) or not re.fullmatch(r'\d+(?:\.\d+){0,2}', build):
        raise ValueError('Numeric Store marketing version/build required')

def validate_profile(profile, bundle, team, app_certificate_sha1, now=None):
    now = now or datetime.datetime.now(datetime.timezone.utc)
    if profile.get('Platform') != ['OSX']:
        raise ValueError('Exact macOS (OSX) Store profile required; missing/iOS platform rejected')
    expiry = profile.get('ExpirationDate')
    if not isinstance(expiry, datetime.datetime):
        raise ValueError('Profile expiration missing')
    if expiry.replace(tzinfo=datetime.timezone.utc) <= now:
        raise ValueError('Profile expired')
    if team not in profile.get('TeamIdentifier', []):
        raise ValueError('Profile Team does not match')
    if profile.get('ProvisionedDevices') or profile.get('ProvisionsAllDevices'):
        raise ValueError('Store distribution profile required; not development/Developer ID')
    ent = profile.get('Entitlements', {})
    app_key = 'com.apple.application-identifier' if 'com.apple.application-identifier' in ent else 'application-identifier'
    if ent.get(app_key) != team + '.' + bundle:
        raise ValueError('Profile exact App ID does not match')
    if ent.get('com.apple.developer.team-identifier') != team:
        raise ValueError('Profile entitlement Team does not match')
    if ent.get('get-task-allow') or ent.get('com.apple.security.get-task-allow'):
        raise ValueError('Debuggable profile forbidden')
    if app_certificate_sha1.upper() not in {hashlib.sha1(c).hexdigest().upper() for c in profile.get('DeveloperCertificates', [])}:
        raise ValueError('Selected application certificate is not authorized by profile')
    # These sandbox capabilities are not provisioned restricted services. If the
    # profile explicitly constrains one, the constraint must allow our exact value.
    for key, value in CAPABILITIES.items():
        if key in ent and ent[key] != value:
            raise ValueError('Profile contradicts requested sandbox capability: ' + key)
    return CAPABILITIES | {app_key: ent[app_key], 'com.apple.developer.team-identifier': team}

def identities():
    # Metadata captured privately; never print certificate names or keychain dump.
    text = run(['security', 'find-identity', '-v']).decode()
    return {m[1].upper(): m[2] for m in re.findall(r'\b([0-9]+)\)\s+([A-Fa-f0-9]{40})\s+"([^"]+)"', text)}

def require_store_identity(identity_map, sha, installer=False):
    name = identity_map.get(sha.upper(), '')
    prefixes = ('3rd Party Mac Developer Installer:',) if installer else ('Apple Distribution:', '3rd Party Mac Developer Application:')
    if not name.startswith(prefixes):
        raise ValueError('Usable Store ' + ('Installer' if installer else 'Application') + ' identity required; Developer ID/ad-hoc rejected')


def source_binding(source, expected):
    head = run(['git', '-C', str(source), 'rev-parse', 'HEAD']).decode().strip()
    tree = run(['git', '-C', str(source), 'rev-parse', 'HEAD^{tree}']).decode().strip()
    status = run(['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=all']).decode()
    if head != expected or status:
        raise ValueError('Exact clean committed source required, including untracked files')
    return {'commit': head, 'tree': tree}


def notice_bytes(source):
    notices = {}
    for name in ['LICENSE', 'THIRD_PARTY_NOTICES.md']:
        descriptor = os.open(source / name, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        try:
            if not stat.S_ISREG(os.fstat(descriptor).st_mode):
                raise ValueError('Regular source notice required: ' + name)
            with os.fdopen(descriptor, 'rb', closefd=False) as stream:
                data = stream.read()
            if not data:
                raise ValueError('Nonempty source notice required: ' + name)
            notices[name] = data
        finally:
            os.close(descriptor)
    return notices


def stage_notices(notices, staged):
    resources = staged / 'Contents/Resources'
    resources.mkdir(parents=True, exist_ok=True)
    for name, data in notices.items():
        (resources / name).write_bytes(data)


def certificate_team(sha, name):
    pem = run(['security', 'find-certificate', '-a', '-c', name, '-p'])
    for block in re.findall(rb'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----', pem, re.S):
        body = block.replace(b'-----BEGIN CERTIFICATE-----', b'').replace(b'-----END CERTIFICATE-----', b'')
        if hashlib.sha1(base64.b64decode(body)).hexdigest().upper() != sha.upper():
            continue
        subject = subprocess.run(['openssl', 'x509', '-noout', '-subject', '-nameopt', 'RFC2253'], input=block, capture_output=True, check=True).stdout.decode()
        match = re.search(r'(?:^|,)OU=([A-Z0-9]+)(?:,|$)', subject.replace('subject=', '').strip())
        if match: return match.group(1)
    raise ValueError('Exact selected certificate Team could not be verified')


def require_certificate_team(actual, expected):
    if actual != expected:
        raise ValueError('Selected application/installer certificate belongs to another Team')


def verify_package_team(signature_text, team):
    if not re.search(r'3rd Party Mac Developer Installer:.*\(' + re.escape(team) + r'\)', signature_text):
        raise ValueError('Actual package signature is not the intended Store Installer Team')


def prepare(args):
    source = pathlib.Path(args.source).resolve()
    binding = source_binding(source, args.commit)
    output = pathlib.Path(args.output).resolve()
    if output.exists():
        raise ValueError('Use a fresh output directory; no existing artifacts overwritten')
    output.mkdir(parents=True)
    build = output / 'build'
    command = ['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.2', '-DBUILD_TESTING=ON', '-DSEETHIS_APP_STORE=ON', '-DSEETHIS_STORE_BUNDLE_ID=' + args.bundle_id, '-DSEETHIS_STORE_VERSION=' + args.version, '-DSEETHIS_STORE_BUILD=' + args.build]
    for name, cmd in [('configure', command), ('build', ['cmake', '--build', str(build), '-j2'])]:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / (name + '.log')).write_bytes(p.stdout)
        if p.returncode:
            raise RuntimeError(name + ' failed; inspect local log')
    app = build / 'seethis.app'
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    if info['CFBundleIdentifier'] != args.bundle_id or info['CFBundleVersion'] != args.build:
        raise ValueError('Built Store identity/version mismatch')
    if source_binding(source, args.commit) != binding:
        raise ValueError('Source binding changed during build')
    receipt = {'status': 'NOT_STORE_DISTRIBUTION_SIGNED_NOT_UPLOADABLE', 'source_binding': binding, 'app': str(app), 'bundle_id': args.bundle_id, 'version': args.version, 'build': args.build, 'store_distribution_signed': False, 'ad_hoc_linker_signature_possible': True, 'sandbox_template': CAPABILITIES, 'runtime_not_run': True}
    (output / 'preparation.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))


def package(args):
    source = pathlib.Path(args.source).resolve()
    binding = source_binding(source, args.commit)
    notices = notice_bytes(source)
    app = pathlib.Path(args.app).resolve()
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    validate_identifiers(info['CFBundleIdentifier'], info['CFBundleShortVersionString'], info['CFBundleVersion'])
    ids = identities()
    require_store_identity(ids, args.application_identity)
    require_store_identity(ids, args.installer_identity, installer=True)
    for sha in [args.application_identity, args.installer_identity]:
        require_certificate_team(certificate_team(sha, ids[sha.upper()]), args.team)
    profile_path = pathlib.Path(args.profile).resolve()
    profile = plistlib.loads(run(['security', 'cms', '-D', '-i', str(profile_path)]))
    ent = validate_profile(profile, info['CFBundleIdentifier'], args.team, args.application_identity)
    output = pathlib.Path(args.output).resolve()
    if output.exists():
        raise ValueError('Use a fresh output directory; no existing artifacts overwritten')
    output.mkdir(parents=True)
    staged = output / 'SeeThis.app'
    run(['ditto', str(app), str(staged)])
    stage_notices(notices, staged)
    (staged / 'Contents/embedded.provisionprofile').write_bytes(profile_path.read_bytes())
    ent_path = output / 'store-signing.entitlements'
    ent_path.write_bytes(plistlib.dumps(ent))
    run(['codesign', '--force', '--sign', args.application_identity, '--options', 'runtime', '--entitlements', str(ent_path), str(staged)])
    run(['codesign', '--verify', '--strict', str(staged)])
    # Exact signed entitlements must match our requested capabilities + profile identity.
    signed = plistlib.loads(run(['codesign', '-d', '--entitlements', ':-', str(staged)]))
    if signed != ent:
        raise ValueError('Unexpected signed entitlements')
    details = subprocess.run(['codesign', '-dv', str(staged)], capture_output=True, check=True).stderr.decode()
    if 'TeamIdentifier=' + args.team not in details:
        raise ValueError('Signed Team does not match profile')
    pkg = output / 'SeeThis.pkg'
    run(['productbuild', '--component', str(staged), '/Applications', '--sign', args.installer_identity, str(pkg)])
    verify_package_team(run(['pkgutil', '--check-signature', str(pkg)]).decode(), args.team)
    if source_binding(source, args.commit) != binding:
        raise ValueError('Packaging source binding changed during signing')
    receipt = {'packaging_source_binding': binding, 'notice_sha256': {k: hashlib.sha256(v).hexdigest() for k, v in notices.items()}, 'status': 'signed_local_store_package_UPLOAD_NOT_RUN', 'bundle_id': info['CFBundleIdentifier'], 'version': info['CFBundleShortVersionString'], 'build': info['CFBundleVersion'], 'package': str(pkg), 'sha256': hashlib.sha256(pkg.read_bytes()).hexdigest(), 'restricted_entitlements': [k for k in ent if k not in CAPABILITIES], 'sandbox_capabilities': CAPABILITIES, 'asc_validation_not_run': True, 'app_review_not_run': True}
    (output / 'package.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='action', required=True)
    prep = sub.add_parser('prepare')
    for key in ['source', 'commit', 'output', 'bundle-id', 'version', 'build']:
        prep.add_argument('--' + key, required=True)
    pkg = sub.add_parser('package')
    for key in ['source', 'commit', 'app', 'output', 'application-identity', 'installer-identity', 'profile', 'team']:
        pkg.add_argument('--' + key, required=True)
    args = p.parse_args()
    if args.action == 'prepare':
        validate_identifiers(args.bundle_id, args.version, args.build)
        prepare(args)
    else:
        package(args)

if __name__ == '__main__':
    main()
