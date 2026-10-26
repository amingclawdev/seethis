import sys
sys.dont_write_bytecode = True
import datetime
import hashlib
import importlib.util
import pathlib
import plistlib
import subprocess
import tempfile
import unittest
from unittest import mock

SOURCE = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('store_package', SOURCE / 'scripts/package_app_store.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class StorePackage(unittest.TestCase):
    def profile(self):
        return {'Platform': ['OSX'], 'ExpirationDate': datetime.datetime(2099, 1, 1), 'TeamIdentifier': ['ABCDEFGHIJ'], 'Entitlements': {'com.apple.application-identifier': 'ABCDEFGHIJ.local.seethis.overlay', 'com.apple.developer.team-identifier': 'ABCDEFGHIJ', 'get-task-allow': False}, 'DeveloperCertificates': [b'test certificate bytes']}
    def validate(self, profile):
        return module.validate_profile(profile, 'local.seethis.overlay', 'ABCDEFGHIJ', hashlib.sha1(b'test certificate bytes').hexdigest())
    def test_exact_store_profile_and_unrestricted_capabilities(self):
        ent = self.validate(self.profile())
        self.assertEqual({k:ent[k] for k in module.CAPABILITIES}, module.CAPABILITIES)
        self.assertNotIn('get-task-allow', ent)
        self.assertNotIn('com.apple.security.network.client', ent)
    def test_only_explicit_macos_profile_is_accepted(self):
        for platform in [None, [], ['iOS'], ['OSX', 'iOS']]:
            with self.subTest(platform=platform):
                profile = self.profile()
                if platform is None: profile.pop('Platform')
                else: profile['Platform'] = platform
                with self.assertRaises(ValueError): self.validate(profile)
    def test_profile_mismatch_expiry_debug_and_identity_rejected(self):
        changes = [({'ExpirationDate': datetime.datetime(2000, 1, 1)}, None), ({'TeamIdentifier': ['WRONG']}, None), ({'DeveloperCertificates': [b'other']}, None), ({'ProvisionedDevices': ['fake']}, None), ({'ProvisionsAllDevices': True}, None), ({}, {'get-task-allow': True}), ({}, {'com.apple.application-identifier': 'ABCDEFGHIJ.*'}), ({}, {'com.apple.security.network.server': False})]
        for fields, ent in changes:
            with self.subTest(fields=fields, ent=ent):
                profile = self.profile(); profile.update(fields)
                if ent: profile['Entitlements'].update(ent)
                with self.assertRaises(ValueError): self.validate(profile)
    def test_developer_id_and_adhoc_cannot_be_store_certificates(self):
        for name in ['Developer ID Application: test', 'Developer ID Installer: test', '-', 'Apple Development: test']:
            with self.assertRaises(ValueError): module.require_store_identity({'A':name}, 'A')
            with self.assertRaises(ValueError): module.require_store_identity({'A':name}, 'A', installer=True)
        module.require_store_identity({'A':'Apple Distribution: test'}, 'A')
        module.require_store_identity({'A':'3rd Party Mac Developer Application: test'}, 'A')
        module.require_store_identity({'A':'3rd Party Mac Developer Installer: test'}, 'A', installer=True)
    def test_clean_source_and_changed_head_guards(self):
        with mock.patch.object(module, 'run', side_effect=[b'expected\n', b'tree\n', b'?? untracked-product\n']):
            with self.assertRaises(ValueError): module.source_binding(SOURCE, 'expected')
        with mock.patch.object(module, 'run', side_effect=[b'changed\n', b'tree\n', b'']):
            with self.assertRaises(ValueError): module.source_binding(SOURCE, 'expected')
        with mock.patch.object(module, 'run', side_effect=[b'expected\n', b'tree\n', b'']):
            self.assertEqual(module.source_binding(SOURCE, 'expected'), {'commit':'expected', 'tree':'tree'})
    def test_wrong_installer_team_and_actual_package_rejected(self):
        with self.assertRaises(ValueError): module.require_certificate_team('OTHERTEAM', 'ABCDEFGHIJ')
        module.require_certificate_team('ABCDEFGHIJ', 'ABCDEFGHIJ')
        with self.assertRaises(ValueError): module.verify_package_team('1. 3rd Party Mac Developer Installer: Name (OTHERTEAM)', 'ABCDEFGHIJ')
        module.verify_package_team('1. 3rd Party Mac Developer Installer: Name (ABCDEFGHIJ)', 'ABCDEFGHIJ')
    def test_probe_identity_rejected(self):
        for identifier in ['local.seethis.sandboxprobe.r1', 'local.seethis.sandboxconsentprobe.r2', 'bad&id']:
            with self.assertRaises(ValueError): module.validate_identifiers(identifier, '0.1.1', '1')
    def test_exact_notices_staged_before_signing(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / 'source'; source.mkdir()
            staged = pathlib.Path(directory) / 'staged'
            expected = {'LICENSE': b'copyright\npermission\x00', 'THIRD_PARTY_NOTICES.md': b'exact notices\n'}
            for name, data in expected.items(): (source / name).write_bytes(data)
            notices = module.notice_bytes(source)
            module.stage_notices(notices, staged)
            for name, data in expected.items():
                self.assertEqual((staged / 'Contents/Resources' / name).read_bytes(), data)
    def test_invalid_notice_inputs_fail_closed(self):
        import os
        for kind in ['missing', 'empty', 'directory', 'fifo', 'symlink']:
            for name in ['LICENSE', 'THIRD_PARTY_NOTICES.md']:
                with self.subTest(kind=kind, name=name), tempfile.TemporaryDirectory() as directory:
                    source = pathlib.Path(directory)
                    for file in ['LICENSE', 'THIRD_PARTY_NOTICES.md']: (source / file).write_bytes(b'valid')
                    bad = source / name; bad.unlink()
                    if kind == 'empty': bad.write_bytes(b'')
                    elif kind == 'directory': bad.mkdir()
                    elif kind == 'fifo': os.mkfifo(bad)
                    elif kind == 'symlink': bad.symlink_to(source / ('THIRD_PARTY_NOTICES.md' if name == 'LICENSE' else 'LICENSE'))
                    with self.assertRaises((ValueError, OSError)): module.notice_bytes(source)
    def test_actual_cmake_variant_configuration(self):
        with tempfile.TemporaryDirectory(prefix='seethis-store-config-') as directory:
            for store in [False, True]:
                build = pathlib.Path(directory) / ('store' if store else 'default')
                command = ['cmake','-S',str(SOURCE),'-B',str(build),'-G','Ninja','-DBUILD_TESTING=OFF','-DCMAKE_BUILD_TYPE=Release','-DSEETHIS_APP_STORE='+('ON' if store else 'OFF')]
                if store: command += ['-DSEETHIS_STORE_BUNDLE_ID=local.seethis.overlay','-DSEETHIS_STORE_VERSION=0.1.1','-DSEETHIS_STORE_BUILD=1']
                p = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
                info = plistlib.loads((build/'seethis.app/Contents/Info.plist').read_bytes())
                self.assertEqual(info['CFBundleIdentifier'], 'local.seethis.overlay')
                self.assertEqual(info['CFBundleShortVersionString'], '0.1.1' if store else '0.1.6')
                self.assertEqual(info['CFBundleVersion'], '1' if store else '0.1.6')
                ninja = (build/'build.ninja').read_text()
                self.assertEqual('PrivacyInfo.xcprivacy' in ninja, store)
                self.assertEqual('NSHumanReadableCopyright' in info, store)
                if store: self.assertEqual(info['LSApplicationCategoryType'], 'public.app-category.productivity')
    def test_manifest_maps_only_own_container_file_metadata(self):
        manifest = plistlib.loads((SOURCE/'src/platform/mac/PrivacyInfo.xcprivacy').read_bytes())
        self.assertEqual(manifest, {'NSPrivacyAccessedAPITypes':[{'NSPrivacyAccessedAPIType':'NSPrivacyAccessedAPICategoryFileTimestamp','NSPrivacyAccessedAPITypeReasons':['C617.1']}]})
        self.assertEqual(plistlib.loads((SOURCE/'src/platform/mac/seethis-app-store.entitlements').read_bytes()), module.CAPABILITIES)

if __name__ == '__main__': unittest.main()
