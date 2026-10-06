# Building the Mac App Store variant

The Mac App Store build is an opt-in macOS variant. `SEETHIS_APP_STORE` defaults to `OFF`, so the normal GitHub build and Developer ID packaging continue to use their existing configuration. For the normal build, see [CONTRIBUTING.md](../CONTRIBUTING.md).

## Prepare and test

Use macOS with Apple clang, CMake, Ninja and Python 3. The preparation script requires a clean committed checkout and a new output directory. Set `COMMIT` to the full commit hash you are building, and substitute your registered bundle identifier, marketing version and build number:

```sh
python3 scripts/package_app_store.py prepare \
  --source /absolute/path/to/clean/checkout --commit "$COMMIT" \
  --output /absolute/path/to/new/store-preparation \
  --bundle-id com.example.visualcontext --version 0.1.1 --build 1
ctest --test-dir /absolute/path/to/new/store-preparation/build --output-on-failure -j1
```

The script configures a Release build for Apple Silicon with macOS 14.2 as the deployment target and builds the app and tests. It checks the source commit and tree before and after the build. This step does not produce a Store distribution signature; a linker-generated ad-hoc signature is possible.

The Store variant includes an App Sandbox entitlement template, a privacy manifest, and compiled settings HTML/JavaScript assets. The settings assets keep their existing HTTP routes and security checks while avoiding runtime reads from the app bundle. The native menu includes a Privacy Policy action.

## Sign and package

Use your own Store application and installer signing identities and an explicit macOS Store provisioning profile. Developer ID identities cannot be used for this command. Set `COMMIT` to the clean checkout's full commit hash:

```sh
python3 scripts/package_app_store.py package \
  --source /absolute/path/to/clean/checkout --commit "$COMMIT" \
  --app /absolute/path/to/store-preparation/build/seethis.app \
  --output /absolute/path/to/new/store-package \
  --application-identity STORE_APPLICATION_CERT_SHA1 \
  --installer-identity STORE_INSTALLER_CERT_SHA1 \
  --profile /private/path/to/store.provisionprofile --team YOUR_TEAM_ID
```

The command validates certificate types and team, profile platform, exact App ID, expiry and certificate authorization. It stages a copy of the app, includes the unchanged license and third-party notices, embeds the profile, signs the app, and creates an installer package for `/Applications`. It checks the resulting app entitlements and installer signature. The original app is preserved.

The commands do not log in, upload, install, or notarize the app. Keep signing keys and provisioning profiles outside the repository. Store validation, runtime testing and App Review are separate from building this source.

## Sandbox capabilities

- The local reference and settings server uses `com.apple.security.network.server`; the production server binds to the loopback address.
- Chrome page association uses Apple Events after user consent. The temporary Apple Events exception targets only `com.google.Chrome`.
- Input Monitoring, Screen Recording and Chrome Automation are distinct permissions. A different signing identity can require fresh grants.

The entitlement template alone does not prove sandbox enforcement or App Review approval. Validate the exact signed package and explain each required capability when submitting it.
