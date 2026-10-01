#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
script="$root/scripts/package_macos.sh"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/seethis-package-test.XXXXXX")
tmp=$(cd "$tmp" && pwd -P)
trap 'rm -rf "$tmp"' EXIT
mockbin="$tmp/mockbin"; mkdir -p "$mockbin"
export MOCK_REAL_PYTHON3=$(command -v python3)

cat >"$mockbin/python3" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${1:-}" == - && "${3:-}" == seethis-proc-pidpath-proof-v1 ]]; then
  [[ -z "${MOCK_PROC_PIDPATH_CALL_LOG:-}" ]] || printf '%s\n' "${2:-}" >>"$MOCK_PROC_PIDPATH_CALL_LOG"
  [[ "${MOCK_PROC_PIDPATH_FAIL:-0}" != 1 && "${2:-}" == "${MOCK_PROC_PIDPATH_PID:-}" ]] || exit 1
  printf '%s\n' "${MOCK_PROC_PIDPATH_VALUE:?}"
  exit 0
fi
exec "${MOCK_REAL_PYTHON3:?}" "$@"
MOCK

cat >"$mockbin/security" <<'MOCK'
#!/usr/bin/env bash
printf '%s\n' "${MOCK_SECURITY_OUTPUT:-1) AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA \"Fixture Stable\"}"
MOCK
cat >"$mockbin/codesign" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
last=''; for item in "$@"; do last=$item; done
measurement_selected() {
  case "${MOCK_MEASURE_TARGET:-none}" in
    left) [[ "$last" == *'/Left.app'* ]] ;;
    right) [[ "$last" == *'/Right.app'* ]] ;;
    both) [[ "$last" == *'/Left.app'* || "$last" == *'/Right.app'* ]] ;;
    *) return 1 ;;
  esac
}
case " $* " in
  *" --verify "*)
    if [[ -f "$last/.mock-signed" && ! -f "$last/.mock-invalid" ]]; then exit 0; fi
    [[ -f "$last.mock-signed" && ! -f "$last.mock-invalid" ]]
    ;;
  *" -d -r- "*)
    if [[ "${MOCK_MEASURE_KIND:-}" == requirement ]] &&
       { measurement_selected || [[ "${MOCK_REQUIREMENT_FIXTURE:-0}" == 1 ]]; }; then
      case "${MOCK_MEASURE_MODE:-}" in
        fail) exit 77 ;;
        commented) printf '# designated => %s\n' "${MOCK_REQUIREMENT_VALUE:-REQ_ADHOC}" >&2; exit 0 ;;
        explicit) printf 'designated => %s\n' "${MOCK_REQUIREMENT_VALUE:-REQ_EXPLICIT}" >&2; exit 0 ;;
        empty) printf '# designated => \n' >&2; exit 0 ;;
        missing) printf 'codesign output without a requirement marker\n' >&2; exit 0 ;;
        near) printf '#designated => REQ_NEAR\n' >&2; exit 0 ;;
        prefix) printf 'prefix designated => REQ_PREFIX\n' >&2; exit 0 ;;
        multiple) printf '# designated => REQ_DUPLICATE\n# designated => REQ_DUPLICATE\n' >&2; exit 0 ;;
        ambiguous) printf '# designated => REQ_ONE\ndesignated => REQ_TWO\n' >&2; exit 0 ;;
        invalid) printf 'requirement unavailable\n' >&2; exit 0 ;;
      esac
    fi
    [[ -f "$last/.mock-requirement" ]]; printf 'designated => %s\n' "$(<"$last/.mock-requirement")" >&2
    ;;
  *" -d --verbose=4 "*)
    if [[ "${MOCK_MEASURE_KIND:-}" == cdhash ]] && measurement_selected; then
      case "${MOCK_MEASURE_MODE:-}" in
        fail) exit 77 ;;
        missing) printf 'CodeDirectory without a CDHash field\n' >&2; exit 0 ;;
        empty) printf 'CDHash=\n' >&2; exit 0 ;;
        whitespace) printf 'CDHash=   \n' >&2; exit 0 ;;
        malformed|invalid) printf 'CDHash=not-a-hash\n' >&2; exit 0 ;;
        wrong_length) printf 'CDHash=abc\n' >&2; exit 0 ;;
        duplicate) printf 'CDHash=cccccccccccccccccccccccccccccccccccccccc\nCDHash=cccccccccccccccccccccccccccccccccccccccc\n' >&2; exit 0 ;;
        multiple) printf 'CDHash=cccccccccccccccccccccccccccccccccccccccc\nCDHash=ddddddddddddddddddddddddddddddddddddd\n' >&2; exit 0 ;;
      esac
    fi
    [[ -f "$last/.mock-signed" || -f "$last.mock-signed" ]]
    printf 'CDHash=cccccccccccccccccccccccccccccccccccccccc\n' >&2
    if [[ -f "$last/.mock-display" ]]; then /bin/cat "$last/.mock-display" >&2; fi
    if [[ -f "$last.mock-display" ]]; then /bin/cat "$last.mock-display" >&2; fi
    ;;
  *" -d -v "*) exit 64 ;;
  *" --sign "*)
    req=${MOCK_SIGN_REQUIREMENT:-REQ_STABLE}; [[ " $* " != *" --sign - "* ]] || req=REQ_ADHOC
    printf '%s\n' "$req" >"$last/.mock-requirement"; : >"$last/.mock-signed" ;;
  *) exit 64 ;;
esac
MOCK
cat >"$mockbin/cmake" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
[[ "${1:-}" != --version ]] || { echo 'cmake version fixture'; exit 0; }
if [[ "${1:-}" == --build ]]; then
  build=$2; source=$(<"$build/.source"); app="$build/seethis.app"
  mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
  /bin/cp "$source/payload" "$app/Contents/MacOS/seethis"; chmod 0755 "$app/Contents/MacOS/seethis"
  /bin/cp "$source/resource" "$app/Contents/Resources/resource"
  /bin/cp "$source/resource" "$app/Contents/Resources/SeeThis.icns"
  ln -s resource "$app/Contents/Resources/resource-link"
  id=$(<"$source/bundle-id")
  printf '<?xml version="1.0"?><plist version="1.0"><dict><key>CFBundleIdentifier</key><string>%s</string><key>CFBundleExecutable</key><string>seethis</string><key>CFBundleShortVersionString</key><string>0.1.0</string><key>CFBundleVersion</key><string>0.1.0</string><key>CFBundleIconFile</key><string>SeeThis.icns</string><key>LSMinimumSystemVersion</key><string>14.2</string></dict></plist>\n' "$id" >"$app/Contents/Info.plist"
  chmod 0644 "$app/Contents/Info.plist"
  exit 0
fi
printf '%s\n' "$*" >>"${MOCK_CMAKE_ARGS:?}"
source=''; build=''
while (($#)); do case "$1" in -S) source=$2; shift 2;; -B) build=$2; shift 2;; *) shift;; esac; done
[[ "$source" != "${FIXTURE_SOURCE:?}" ]]
mkdir -p "$build"; printf '%s\n' "$source" >"$build/.source"; printf '%s\n' "$source" >>"${MOCK_CMAKE_LOG:?}"
MOCK
cat >"$mockbin/clang" <<'MOCK'
#!/usr/bin/env bash
echo 'fixture clang'
MOCK
cat >"$mockbin/ditto" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
previous=''; last=''; for item in "$@"; do previous=$last; last=$item; done
if [[ "$1" == -x ]]; then
  archive=$previous; destination=$last; /bin/cp -a "$archive.contents/." "$destination/"
  if [[ "${MOCK_ARCHIVE_MUTATE:-0}" == 1 ]]; then echo mutation >>"$(/usr/bin/find "$destination" -name resource -type f -print -quit)"; fi
else
  source=$previous; archive=$last; echo archive >"$archive"; mkdir -p "$archive.contents"; /bin/cp -a "$source" "$archive.contents/"
fi
MOCK
cat >"$mockbin/lsregister" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$1" == -dump ]]; then [[ -z "${MOCK_LS_DUMP:-}" ]] || /bin/cat "$MOCK_LS_DUMP"; exit; fi
[[ "$1" == -f ]]; printf '%s\n' "$2" >>"${MOCK_LS_LOG:?}"; [[ "${MOCK_LS_FAIL:-0}" != 1 ]]
MOCK
cat >"$mockbin/ps" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
[[ "${MOCK_PS_FAIL:-0}" != 1 ]] || exit 77
rows=${MOCK_PS_BEFORE:-}; [[ -z "${MOCK_OPEN_MARKER:-}" || ! -f "$MOCK_OPEN_MARKER" ]] || rows=${MOCK_PS_AFTER:-}
if [[ "$1" == -axo ]]; then
  if [[ -n "${MOCK_RETARGET_ALIAS_ON_SNAPSHOT:-}" ]]; then
    /bin/rm "$MOCK_RETARGET_ALIAS_ON_SNAPSHOT"
    /bin/ln -s "${MOCK_RETARGET_ALIAS_TARGET:?}" "$MOCK_RETARGET_ALIAS_ON_SNAPSHOT"
  fi
  if [[ -n "${MOCK_PS_MUTATE_APP:-}" && ! -f "${MOCK_PS_MUTATE_MARKER:?}" ]]; then
    /usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier ${MOCK_PS_MUTATE_ID:?}" "$MOCK_PS_MUTATE_APP/Contents/Info.plist" >/dev/null
    [[ "${MOCK_PS_MUTATE_INVALIDATE:-0}" != 1 ]] || : >"$MOCK_PS_MUTATE_APP/.mock-invalid"
    : >"$MOCK_PS_MUTATE_MARKER"
  fi
  [[ -z "${MOCK_REAPPEAR_PATH:-}" ]] || mkdir -p "$MOCK_REAPPEAR_PATH"
  if [[ -n "${MOCK_PS_CALL_COUNT:-}" ]]; then
    count=0; [[ ! -f "$MOCK_PS_CALL_COUNT" ]] || count=$(<"$MOCK_PS_CALL_COUNT")
    count=$((count + 1)); printf '%s\n' "$count" >"$MOCK_PS_CALL_COUNT"
    [[ "$count" != "${MOCK_PS_EMPTY_ON_CALL:-0}" ]] || exit 0
    if [[ ! -f "${MOCK_OPEN_MARKER:-/nonexistent}" && -n "${MOCK_PS_REPLACE_ON_CALL:-}" ]] && ((count >= MOCK_PS_REPLACE_ON_CALL)); then
      rows=${MOCK_PS_REPLACEMENT:?}
    fi
  fi
  [[ -z "$rows" ]] || /bin/cat "$rows"
  exit
fi
pid=$2; [[ ",${MOCK_STALE_PIDS:-}," != *",$pid,"* ]] || exit 1
if [[ "$pid" == "${MOCK_RETARGET_ALIAS_ON_LIVE_PID:-}" ]]; then
  /bin/rm "${MOCK_RETARGET_ALIAS_ON_LIVE:?}"
  /bin/ln -s "${MOCK_RETARGET_ALIAS_TARGET:?}" "$MOCK_RETARGET_ALIAS_ON_LIVE"
fi
if [[ "$pid" == "${MOCK_LIVE_OVERRIDE_PID:-}" ]]; then printf '%s\n' "${MOCK_LIVE_OVERRIDE_PATH:?}"; exit 0; fi
awk -v p="$pid" '$1==p {$1=""; sub(/^[[:space:]]+/,""); print; ok=1} END{exit !ok}' "$rows"
MOCK
cat >"$mockbin/open" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
app=$1; printf '%s\n' "$app" >>"${MOCK_OPEN_LOG:?}"
[[ "${MOCK_MUTATE_EXEC:-0}" != 1 ]] || echo mutation >>"$app/Contents/MacOS/seethis"
[[ "${MOCK_MUTATE_REQ:-0}" != 1 ]] || echo REQ_MUTATED >"$app/.mock-requirement"
: >"${MOCK_OPEN_MARKER:?}"
MOCK
cat >"$mockbin/cp" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
dest=''; for item in "$@"; do dest=$item; done
if [[ "${MOCK_CP_FAIL:-0}" == 1 && "$dest" == *'/.seethis-install.'*'/candidate.app' ]]; then exit 77; fi
if [[ -n "${MOCK_REAPPEAR_TARGET:-}" && "$dest" == *'/.seethis-install.'*'/candidate.app' ]]; then
  /bin/cp "$@"
  if [[ -n "${MOCK_REAPPEAR_SOURCE:-}" ]]; then
    /bin/cp -a "$MOCK_REAPPEAR_SOURCE" "$MOCK_REAPPEAR_TARGET"
  else
    mkdir -p "$MOCK_REAPPEAR_TARGET"
  fi
  exit 0
fi
exec /bin/cp "$@"
MOCK
cat >"$mockbin/mv" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
source_path=$1
destination=$2
if [[ "${MOCK_MUTATE_SOURCE_TARGET_AFTER_SWAP:-0}" == 1 && "$source_path" == *'/.seethis-install.'*'/candidate.app' ]]; then
  /bin/mv "$source_path" "$destination"
  printf 'post-stage mutation\n' >>"${MOCK_MUTATE_SOURCE_APP:?}/Contents/Resources/resource"
  printf 'post-stage mutation\n' >>"$destination/Contents/Resources/resource"
  exit 0
fi
exec /bin/mv "$@"
MOCK
cat >"$mockbin/sleep" <<'MOCK'
#!/usr/bin/env bash
exit 0
MOCK
cat >"$mockbin/shasum" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
last=''; for item in "$@"; do last=$item; done
measurement_selected() {
  case "${MOCK_MEASURE_TARGET:-none}" in
    left) [[ "$last" == *'/Left.app/'* ]] ;;
    right) [[ "$last" == *'/Right.app/'* ]] ;;
    both) [[ "$last" == *'/Left.app/'* || "$last" == *'/Right.app/'* ]] ;;
    *) return 1 ;;
  esac
}
if [[ "${MOCK_FAIL_STDIN_SHASUM:-0}" == 1 && "$#" == 2 ]]; then /bin/cat >/dev/null; exit 77; fi
if [[ "${MOCK_FAIL_TREE_SHASUM:-0}" == 1 && "$last" == *'/seethis-tree-hash.'*'/listing' ]]; then exit 77; fi
selected_measurement=0
if [[ "${MOCK_MEASURE_KIND:-}" == tree && "$last" == */.tree-probe ]] && measurement_selected; then selected_measurement=1; fi
if [[ "${MOCK_MEASURE_KIND:-}" == executable_hash && "$last" == */Contents/MacOS/seethis ]] && measurement_selected; then selected_measurement=1; fi
if ((selected_measurement)); then
  case "${MOCK_MEASURE_MODE:-}" in
    fail) exit 77 ;;
    empty) exit 0 ;;
    invalid) printf 'not-a-sha256  %s\n' "$last"; exit 0 ;;
  esac
fi
exec /usr/bin/shasum "$@"
MOCK
cat >"$mockbin/xcrun" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
case "$*" in
  '--sdk macosx --find clang++') printf '%s/clang\n' "${MOCK_TOOLBIN:?}" ;;
  '--sdk macosx --show-sdk-path') printf '%s\n' "${MOCK_SDK:?}" ;;
  '--sdk macosx --show-sdk-version') printf '26.5\n' ;;
  lipo*) printf '%s\n' "${MOCK_ARCHITECTURES:-arm64}" ;;
  otool*) printf 'cmd LC_BUILD_VERSION\nminos %s\n' "${MOCK_MINIMUM_MACOS:-14.2}" ;;
  *) exit 64 ;;
esac
MOCK
cat >"$mockbin/xcode-select" <<'MOCK'
#!/usr/bin/env bash
printf '%s\n' "${MOCK_SDK:?}"
MOCK
chmod +x "$mockbin"/*

# Only task-owned fixtures override standard tools from the caller's PATH.
export PATH="$mockbin:$PATH" MOCK_TOOLBIN="$mockbin" MOCK_SDK="$tmp/sdk"
export MOCK_CMAKE_ARGS="$tmp/cmake-args.log"
unset PYTHONPATH
mkdir -p "$MOCK_SDK"
export FIXTURE_SOURCE="$tmp/source" MOCK_CMAKE_LOG="$tmp/cmake.log" MOCK_LS_LOG="$tmp/ls.log"
: >"$MOCK_CMAKE_LOG"; : >"$MOCK_LS_LOG"
mkdir -p "$FIXTURE_SOURCE"; git -C "$FIXTURE_SOURCE" init -q
git -C "$FIXTURE_SOURCE" config user.email fixture@example.invalid; git -C "$FIXTURE_SOURCE" config user.name fixture
echo local.seethis.fixture >"$FIXTURE_SOURCE/bundle-id"
printf '#!/bin/sh\necho fixture\n' >"$FIXTURE_SOURCE/payload"; chmod +x "$FIXTURE_SOURCE/payload"
echo resource >"$FIXTURE_SOURCE/resource"
mkdir -p "$FIXTURE_SOURCE/assets/app-icon"
cp "$FIXTURE_SOURCE/resource" "$FIXTURE_SOURCE/assets/app-icon/SeeThis.icns"
mkdir -p "$FIXTURE_SOURCE/src/platform/mac"
printf '%s\n' '<?xml version="1.0"?><plist version="1.0"><dict><key>com.apple.security.automation.apple-events</key><true/></dict></plist>' >"$FIXTURE_SOURCE/src/platform/mac/seethis.entitlements"
git -C "$FIXTURE_SOURCE" add .; git -C "$FIXTURE_SOURCE" commit -qm v1
commit=$(git -C "$FIXTURE_SOURCE" rev-parse HEAD)

success() {
  name=$1; shift; log="$tmp/$name.log"
  "$@" >"$log" 2>&1 || { echo "FAIL $name" >&2; sed -n '1,80p' "$log" >&2; exit 1; }
}
failure() {
  name=$1; reason=$2; shift 2; log="$tmp/$name.log"
  set +e; "$@" >"$log" 2>&1; rc=$?; set -e
  [[ $rc -ne 0 ]] && grep -Fqx "ERROR: $reason" "$log" || { echo "FAIL $name expected: $reason" >&2; sed -n '1,80p' "$log" >&2; exit 1; }
}
has() { grep -Fqx "$2" "$1"; }
canonical() { python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$1"; }
assert_copy_metadata() {
  local name=$1 app=$2 actual
  actual=$(stat -f '%Lp' "$app/Contents/Info.plist")
  [[ "$actual" == 644 ]] || { echo "FAIL $name Info.plist mode: $actual" >&2; exit 1; }
  actual=$(stat -f '%Lp' "$app/Contents/MacOS/seethis")
  [[ "$actual" == 755 ]] || { echo "FAIL $name executable mode: $actual" >&2; exit 1; }
  [[ -L "$app/Contents/Resources/resource-link" ]] || { echo "FAIL $name resource-link is not a symbolic link" >&2; exit 1; }
  actual=$(readlink "$app/Contents/Resources/resource-link")
  [[ "$actual" == resource ]] || { echo "FAIL $name resource-link target: $actual" >&2; exit 1; }
}
reset_state() {
  cat >"$tmp/ls.dump" <<'EOF'
Launch Services database snapshot
--------------------------------------------------------------------------------
record type: application
name: Unrelated
bundle id: local.unrelated.complete
path: /Applications/Unrelated.app
flags: registered
--------------------------------------------------------------------------------
record type: metadata-claim
bundle id: local.unrelated.metadata-only
roles: viewer

EOF
  printf '101 /usr/bin/true\n' >"$tmp/ps.before"
  printf '101 /usr/bin/true\n' >"$tmp/ps.after"
  rm -f "$tmp/open.marker" "$tmp/ps.calls"
  : >"$tmp/open.log"
  : >"$MOCK_LS_LOG"
  export MOCK_LS_DUMP="$tmp/ls.dump" MOCK_PS_BEFORE="$tmp/ps.before" MOCK_PS_AFTER="$tmp/ps.after"
  export MOCK_OPEN_MARKER="$tmp/open.marker" MOCK_OPEN_LOG="$tmp/open.log"
  unset MOCK_ARCHIVE_MUTATE MOCK_SIGN_REQUIREMENT MOCK_PS_FAIL MOCK_STALE_PIDS MOCK_CP_FAIL MOCK_LS_FAIL MOCK_MUTATE_EXEC MOCK_MUTATE_REQ
  unset MOCK_MUTATE_SOURCE_TARGET_AFTER_SWAP MOCK_MUTATE_SOURCE_APP MOCK_REAPPEAR_TARGET MOCK_REAPPEAR_SOURCE
  unset MOCK_FAIL_STDIN_SHASUM MOCK_FAIL_TREE_SHASUM MOCK_PS_CALL_COUNT MOCK_PS_EMPTY_ON_CALL MOCK_REAPPEAR_PATH
  unset MOCK_PS_REPLACE_ON_CALL MOCK_PS_REPLACEMENT MOCK_LIVE_OVERRIDE_PID MOCK_LIVE_OVERRIDE_PATH
  unset MOCK_PS_MUTATE_APP MOCK_PS_MUTATE_ID MOCK_PS_MUTATE_MARKER MOCK_PS_MUTATE_INVALIDATE
  unset MOCK_PROC_PIDPATH_PID MOCK_PROC_PIDPATH_VALUE MOCK_PROC_PIDPATH_FAIL MOCK_PROC_PIDPATH_CALL_LOG
  unset MOCK_RETARGET_ALIAS_ON_SNAPSHOT MOCK_RETARGET_ALIAS_ON_LIVE_PID MOCK_RETARGET_ALIAS_ON_LIVE MOCK_RETARGET_ALIAS_TARGET
  unset MOCK_MEASURE_KIND MOCK_MEASURE_MODE MOCK_MEASURE_TARGET MOCK_REQUIREMENT_FIXTURE MOCK_REQUIREMENT_VALUE
}
lsrow() { printf 'bundle id: %s\npath: %s\n\n' "$1" "$2" >>"$tmp/ls.dump"; }
ls_live_row() {
  printf '%s\n' '--------------------------------------------------------------------------------' >>"$tmp/ls.dump"
  printf 'bundle id: SeeThis (0x1930)\nidentifier: %s\npath: %s (0x1a2b)\nclass: kLSBundleClassApplication (0x2)\nitem flags: package  application  container  native-app (000000000000000e)\n' "$1" "$2" >>"$tmp/ls.dump"
  printf '%s\n' '--------------------------------------------------------------------------------' >>"$tmp/ls.dump"
}

make_manual_bundle() {
  app=$1; id=$2; executable_name=$3
  mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
  printf '#!/bin/sh\nexit 0\n' >"$app/Contents/MacOS/$executable_name"
  chmod +x "$app/Contents/MacOS/$executable_name"
  printf '<?xml version="1.0"?><plist version="1.0"><dict><key>CFBundleIdentifier</key><string>%s</string><key>CFBundleExecutable</key><string>%s</string></dict></plist>\n' \
    "$id" "$executable_name" >"$app/Contents/Info.plist"
  printf '%s\n' REQ_STABLE >"$app/.mock-requirement"
  : >"$app/.mock-signed"
  cat >"$app/Contents/Resources/seethis-delivery-provenance.v1" <<EOF
schema=seethis-delivery-provenance-v1
source_commit=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
source_tree=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
mode=stable
signing_identity_sha1=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
bundle_id=$id
executable=$executable_name
EOF
}

fixture_tree_hash() {
  app=$1; listing="$tmp/tree-listing"; : >"$listing"
  while IFS= read -r -d '' item; do
    item=${item#./}; mode=$(cd "$app" && stat -f '%Lp' "$item")
    if [[ -L "$app/$item" ]]; then
      printf 'link\t%s\t%s\t%s\n' "$item" "$mode" "$(cd "$app" && readlink "$item")" >>"$listing"
    else
      digest=$(/usr/bin/shasum -a 256 "$app/$item" | awk '{print $1}')
      printf 'file\t%s\t%s\t%s\n' "$item" "$mode" "$digest" >>"$listing"
    fi
  done < <(cd "$app" && LC_ALL=C find . \( -type f -o -type l \) -print0 | LC_ALL=C sort -z)
  /usr/bin/shasum -a 256 "$listing" | awk '{print $1}'
}

make_owned_manifest() {
  app=$1; manifest=$2; id=$3
  tree=$(fixture_tree_hash "$app")
  req_hash=$(printf '%s' REQ_STABLE | /usr/bin/shasum -a 256 | awk '{print $1}')
  printf 'schema=seethis-owned-artifacts-v1\napp_path=%s\nbundle_tree_sha256=%s\nbundle_id=%s\ndesignated_requirement_sha256=%s\n' \
    "$(canonical "$app")" "$tree" "$id" "$req_hash" >"$manifest"
}

if [[ "${SEETHIS_COPY_MODE_FOCUSED:-0}" == 1 ]]; then
  reset_state
  success copy_mode_package "$script" package --source "$FIXTURE_SOURCE" --commit "$commit" --output "$tmp/copy-mode-package" --mode development
  assert_copy_metadata retained_copy "$tmp/copy-mode-package/SeeThis.app"

  reset_state
  copy_mode_destination="$tmp/copy-mode-install"
  success copy_mode_install "$script" install --app "$tmp/copy-mode-package/SeeThis.app" --destination "$copy_mode_destination" --acknowledge
  assert_copy_metadata staging_copy "$copy_mode_destination/SeeThis.app"

  echo 'package_macos_test: PASS (retained and staging copies preserve modes and symbolic links)'
  exit 0
fi

if [[ "${SEETHIS_REQUIREMENT_FOCUSED:-0}" == 1 ]]; then
  functions_file="$tmp/package-functions.sh"
  sed '/^\[\[ \$# -gt 0 \]\]/,$d' "$script" >"$functions_file"
  source "$functions_file"
  parser_app="$tmp/parser/Parser.app"
  make_manual_bundle "$parser_app" local.seethis.parser seethis
  export MOCK_MEASURE_KIND=requirement MOCK_REQUIREMENT_FIXTURE=1
  parser_success() {
    local mode=$1 expected=$2 actual
    export MOCK_MEASURE_MODE=$mode MOCK_REQUIREMENT_VALUE=$expected
    actual=$(designated_requirement "$parser_app") || {
      echo "FAIL parser_$mode unexpectedly failed" >&2
      exit 1
    }
    [[ "$actual" == "$expected" ]] || {
      echo "FAIL parser_$mode expected: $expected" >&2
      echo "actual: $actual" >&2
      exit 1
    }
  }
  parser_failure() {
    local mode=$1
    export MOCK_MEASURE_MODE=$mode
    if designated_requirement "$parser_app" >/dev/null; then
      echo "FAIL parser_$mode unexpectedly passed" >&2
      exit 1
    fi
  }
  parser_success commented 'cdhash H"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"'
  parser_success explicit 'anchor apple generic and identifier "local.seethis.overlay"'
  for parser_mode in missing empty near prefix multiple ambiguous; do
    parser_failure "$parser_mode"
  done
  echo 'package_macos_test: PASS (designated requirement parser fixtures)'
  exit 0
fi

if [[ "${SEETHIS_CDHASH_FOCUSED:-0}" == 1 ]]; then
  functions_file="$tmp/package-functions.sh"
  sed '/^\[\[ \$# -gt 0 \]\]/,$d' "$script" >"$functions_file"
  source "$functions_file"
  cdhash_app="$tmp/cdhash/Left.app"
  make_manual_bundle "$cdhash_app" local.seethis.cdhash seethis
  export MOCK_MEASURE_KIND=cdhash MOCK_MEASURE_TARGET=left
  cdhash_success() {
    local mode=$1 expected=$2 actual
    export MOCK_MEASURE_MODE=$mode
    actual=$(cdhash "$cdhash_app") || {
      echo "FAIL cdhash_$mode unexpectedly failed" >&2
      exit 1
    }
    [[ "$actual" == "$expected" ]] || {
      echo "FAIL cdhash_$mode expected: $expected" >&2
      echo "actual: $actual" >&2
      exit 1
    }
  }
  cdhash_failure() {
    local mode=$1
    export MOCK_MEASURE_MODE=$mode
    if cdhash "$cdhash_app" >/dev/null; then
      echo "FAIL cdhash_$mode unexpectedly passed" >&2
      exit 1
    fi
  }
  cdhash_success valid cccccccccccccccccccccccccccccccccccccccc
  for cdhash_mode in missing empty whitespace malformed wrong_length duplicate multiple fail; do
    cdhash_failure "$cdhash_mode"
  done
  echo 'package_macos_test: PASS (verbose-4 CDHash fixtures)'
  exit 0
fi

if [[ "${SEETHIS_MEASUREMENT_FOCUSED:-0}" == 1 ]]; then
  reset_state
  success measurement_package "$script" package --source "$FIXTURE_SOURCE" --commit "$commit" --output "$tmp/measurement-dev" --mode development
  success measurement_bootstrap "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/measurement-bootstrap" --mode stable --identity 'Fixture Stable' --stable-action bootstrap
  success measurement_compatible_update "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/measurement-update" --mode stable --identity AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA --stable-action update --prior-app "$tmp/measurement-bootstrap/SeeThis.app"

  reset_state
  measurement_install_dest="$tmp/measurement-install"
  mkdir -p "$measurement_install_dest"
  cp -R "$tmp/measurement-bootstrap/SeeThis.app" "$measurement_install_dest/SeeThis.app"
  lsrow local.seethis.fixture "$measurement_install_dest/SeeThis.app"
  success measurement_install "$script" install --app "$tmp/measurement-update/SeeThis.app" --destination "$measurement_install_dest" --acknowledge

  reset_state
  measurement_rollback_dest="$tmp/measurement-rollback"
  mkdir -p "$measurement_rollback_dest"
  cp -R "$tmp/measurement-bootstrap/SeeThis.app" "$measurement_rollback_dest/SeeThis.app"
  printf 'preserve\n' >"$measurement_rollback_dest/SeeThis.app/old"
  lsrow local.seethis.fixture "$measurement_rollback_dest/SeeThis.app"
  export MOCK_LS_FAIL=1
  failure measurement_registration_rollback 'Launch Services registration failed; previous canonical app was restored' \
    "$script" install --app "$tmp/measurement-update/SeeThis.app" --destination "$measurement_rollback_dest" --acknowledge
  has "$measurement_rollback_dest/SeeThis.app/old" preserve

  reset_state
  measurement_launch_app="$tmp/measurement-launch/SeeThis.app"
  mkdir -p "$(dirname "$measurement_launch_app")"
  cp -R "$tmp/measurement-update/SeeThis.app" "$measurement_launch_app"
  lsrow local.seethis.fixture "$measurement_launch_app"
  printf '101 /usr/bin/true\n5151 %s\n' "$(canonical "$measurement_launch_app/Contents/MacOS/seethis")" >"$tmp/ps.after"
  success measurement_exact_launch "$script" launch --app "$measurement_launch_app" --acknowledge

  reset_state
  measurement_frozen_dest="$tmp/measurement-frozen-baseline"
  mkdir -p "$measurement_frozen_dest"
  cp -R "$tmp/measurement-bootstrap/SeeThis.app" "$measurement_frozen_dest/SeeThis.app"
  printf 'preserve\n' >"$measurement_frozen_dest/SeeThis.app/old"
  lsrow local.seethis.fixture "$measurement_frozen_dest/SeeThis.app"
  export MOCK_MUTATE_SOURCE_TARGET_AFTER_SWAP=1 MOCK_MUTATE_SOURCE_APP="$tmp/measurement-update/SeeThis.app"
  failure measurement_frozen_candidate_tuple 'installed candidate content differs; previous canonical app was restored' \
    "$script" install --app "$tmp/measurement-update/SeeThis.app" --destination "$measurement_frozen_dest" --acknowledge
  has "$measurement_frozen_dest/SeeThis.app/old" preserve

  functions_file="$tmp/package-functions.sh"
  sed '/^\[\[ \$# -gt 0 \]\]/,$d' "$script" >"$functions_file"
  source "$functions_file"

  prepare_measurement_pair() {
    rm -rf "$tmp/measurement-pair"
    measurement_left="$tmp/measurement-pair/Left.app"
    measurement_right="$tmp/measurement-pair/Right.app"
    make_manual_bundle "$measurement_left" local.seethis.measurement seethis
    make_manual_bundle "$measurement_right" local.seethis.measurement seethis
    printf 'tree probe\n' >"$measurement_left/.tree-probe"
    printf 'tree probe\n' >"$measurement_right/.tree-probe"
    mv "$measurement_left/Contents/MacOS/seethis" "$measurement_left/Contents/MacOS/payload-real"
    mv "$measurement_right/Contents/MacOS/seethis" "$measurement_right/Contents/MacOS/payload-real"
    ln -s payload-real "$measurement_left/Contents/MacOS/seethis"
    ln -s payload-real "$measurement_right/Contents/MacOS/seethis"
  }
  measurement_expected_error() {
    local target=$1 measurement=$2 left_error right_error
    left_error="left fixture $measurement is unavailable or invalid"
    right_error="right fixture $measurement is unavailable or invalid"
    case "$target" in
      left) printf '%s' "$left_error" ;;
      right) printf '%s' "$right_error" ;;
      both) printf '%s; %s' "$left_error" "$right_error" ;;
    esac
  }
  measurement_failure() {
    local name=$1 expected=$2
    if compare_complete_bundles "$measurement_left" "$measurement_right" 'left fixture' 'right fixture' retained_archive; then
      echo "FAIL $name unexpectedly passed" >&2
      exit 1
    fi
    [[ "$bundle_comparison_error" == "$expected" ]] || {
      echo "FAIL $name expected: $expected" >&2
      echo "actual: $bundle_comparison_error" >&2
      exit 1
    }
    printf 'CASE %s expected=failure reason=%s\n' "$name" "$bundle_comparison_error"
  }
  measurement_success() {
    local context=$1
    compare_complete_bundles "$measurement_left" "$measurement_right" 'left fixture' 'right fixture' "$context" || {
      echo "FAIL measurement_success_$context: $bundle_comparison_error" >&2
      exit 1
    }
    printf 'CASE measurement_success_%s expected=success\n' "$context"
  }
  mutate_measurement_plist() {
    local app=$1 key=$2 mode=$3
    python3 - "$app/Contents/Info.plist" "$key" "$mode" <<'PY'
import plistlib
import sys
path, key, mode = sys.argv[1:]
with open(path, "rb") as stream:
    data = plistlib.load(stream)
if mode == "fail":
    data.pop(key, None)
elif mode == "empty":
    data[key] = ""
elif key == "CFBundleIdentifier":
    data[key] = "invalid bundle id"
else:
    data[key] = "invalid/name"
with open(path, "wb") as stream:
    plistlib.dump(data, stream)
PY
  }
  mutate_selected_plists() {
    local target=$1 key=$2 mode=$3
    [[ "$target" == right ]] || mutate_measurement_plist "$measurement_left" "$key" "$mode"
    [[ "$target" == left ]] || mutate_measurement_plist "$measurement_right" "$key" "$mode"
  }

  prepare_measurement_pair
  measurement_success build_retained
  measurement_success retained_archive
  measurement_success staged_candidate
  measurement_success installed_candidate

  for measurement_kind in tree executable_hash requirement cdhash; do
    case "$measurement_kind" in
      tree) measurement_label='tree hash' ;;
      executable_hash) measurement_label='executable hash' ;;
      requirement) measurement_label='designated requirement' ;;
      cdhash) measurement_label='CDHash' ;;
    esac
    for measurement_mode in fail empty invalid; do
      for measurement_target in left right both; do
        prepare_measurement_pair
        export MOCK_MEASURE_KIND=$measurement_kind MOCK_MEASURE_MODE=$measurement_mode MOCK_MEASURE_TARGET=$measurement_target
        measurement_failure "${measurement_kind}_${measurement_mode}_${measurement_target}" \
          "$(measurement_expected_error "$measurement_target" "$measurement_label")"
        unset MOCK_MEASURE_KIND MOCK_MEASURE_MODE MOCK_MEASURE_TARGET
      done
    done
  done

  for measurement_kind in bundle_id executable_name; do
    case "$measurement_kind" in
      bundle_id) plist_key=CFBundleIdentifier; measurement_label='bundle identifier' ;;
      executable_name) plist_key=CFBundleExecutable; measurement_label='executable name' ;;
    esac
    for measurement_mode in fail empty invalid; do
      for measurement_target in left right both; do
        prepare_measurement_pair
        mutate_selected_plists "$measurement_target" "$plist_key" "$measurement_mode"
        measurement_failure "${measurement_kind}_${measurement_mode}_${measurement_target}" \
          "$(measurement_expected_error "$measurement_target" "$measurement_label")"
      done
    done
  done

  echo 'package_macos_test: PASS (focused complete bundle measurements and preserved delivery controls)'
  exit 0
fi

if [[ "${SEETHIS_LSID_FOCUSED:-0}" == 1 ]]; then
  ls_id=local.seethis.lsid
  ls_candidate="$tmp/lsid/Candidate.app"
  ls_historical="$tmp/lsid/Historical.app"
  ls_pseudo="$tmp/lsid/Pseudo.app"
  make_manual_bundle "$ls_candidate" "$ls_id" seethis
  make_manual_bundle "$ls_historical" "$ls_id" seethis
  mkdir -p "$ls_pseudo"
  make_owned_manifest "$ls_historical" "$tmp/lsid-owned.manifest" "$ls_id"

  reset_state; ls_live_row "$ls_id" "$ls_candidate"
  success ls_live_candidate "$script" preflight --app "$ls_candidate"
  has "$log" "registered_same_bundle_apps=$(canonical "$ls_candidate")"

  reset_state
  printf 'bundle id: System Library (0x168)\nidentifier: com.apple.system-library\npath: /System/Library (0xea8)\nclass: kLSBundleClassSystemLibrary (0x8)\n\n' >>"$tmp/ls.dump"
  printf 'identifier: com.apple.ExampleExtension\npath: /System/Applications/Example.app/Contents/PlugIns/Example.appex (0x11ac)\n\n' >>"$tmp/ls.dump"
  printf 'container id: / (0x4)\npath: / (0x4)\n\n' >>"$tmp/ls.dump"
  printf 'bundle id: liquiddetectiond.app (0x36c)\npath: /System/Library/CoreServices/liquiddetectiond.app (0x10fc)\nclass: kLSBundleClassApplication (0x2)\nbundle flags: no-info.plist (0020000000000000)\nitem flags: package  application  container (000000000000000e)\n\n' >>"$tmp/ls.dump"
  printf 'bundle id: Pseudo.app (0xc1c)\npath: %s (0x1b98)\nclass: kLSBundleClassApplication (0x2)\nitem flags: file  application  unsupported-format (0000000000500085)\n\n' "$ls_pseudo" >>"$tmp/ls.dump"
  printf 'bundle id: Example.service (0x1918)\nidentifier: com.apple.Example.Service\npath: /System/Library/Services/Example.service (0x28dc)\nclass: kLSBundleClassApplication (0x2)\nitem flags: package  application  services (000000000000000e)\n\n' >>"$tmp/ls.dump"
  ls_live_row "$ls_id" "$ls_candidate"
  success ls_live_mixed "$script" preflight --app "$ls_candidate"
  has "$log" "registered_same_bundle_apps=$(canonical "$ls_candidate")"

  reset_state; ls_live_row "$ls_id" "$ls_candidate"
  ls_live_row "$ls_id" "$ls_historical"
  ls_live_row "$ls_id" "$ls_historical"
  failure ls_live_historical "unresolved registered same-bundle app: $(canonical "$ls_historical")" \
    "$script" preflight --app "$ls_candidate"
  success ls_live_owned "$script" preflight --app "$ls_candidate" --owned-manifest "$tmp/lsid-owned.manifest"
  has "$log" "registered_same_bundle_apps=$(canonical "$ls_candidate"),$(canonical "$ls_historical")"

  reset_state
  printf 'record type: application\nbundle id: SeeThis (0x1930)\npath: %s (0x1a2b)\n\n' "$ls_candidate" >>"$tmp/ls.dump"
  failure ls_display_label_only 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$ls_candidate"

  reset_state
  printf 'record type: application\nidentifier: %s\nbundle identifier: local.seethis.other\npath: %s (0x1a2b)\n\n' \
    "$ls_id" "$ls_candidate" >>"$tmp/ls.dump"
  failure ls_contradictory_identity 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$ls_candidate"

  reset_state
  printf 'record type: application\nidentifier: %s\nbundle id: %s\npath: %s (0x1a2b)\n\n' \
    "$ls_id" local.seethis.other "$ls_candidate" >>"$tmp/ls.dump"
  failure ls_contradictory_legacy_id 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$ls_candidate"

  reset_state
  printf 'record type: application\nidentifier: %s\nbundle id: SeeThis (0x1930)\n\n' "$ls_id" >>"$tmp/ls.dump"
  failure ls_incomplete_relevant 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$ls_candidate"

  reset_state
  printf 'record type: application\npath: %s (0x1a2b)\n\n' "$ls_candidate" >>"$tmp/ls.dump"
  failure ls_unknown_identity 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$ls_candidate"

  reset_state
  printf 'bundle id: Unknown (0x21c)\npath: %s (0x1a2b)\nclass: kLSBundleClassApplication (0x2)\nitem flags: package  application  container (000000000000000e)\n\n' \
    "$ls_candidate" >>"$tmp/ls.dump"
  failure ls_live_unknown_app 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$ls_candidate"

  reset_state
  printf 'bundle id: SeeThis (0x1930)\nidentifier: %s\npath: /System/Library/Services/Example.service (0x28dc)\nclass: kLSBundleClassApplication (0x2)\nitem flags: package  application  services (000000000000000e)\n\n' \
    "$ls_id" >>"$tmp/ls.dump"
  failure ls_relevant_service_path 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$ls_candidate"

  reset_state
  printf 'bundle id: System Library (0x168)\nidentifier: %s\npath: %s (0xea8)\nclass: kLSBundleClassSystemLibrary (0x8)\n\n' \
    "$ls_id" "$ls_candidate" >>"$tmp/ls.dump"
  failure ls_relevant_nonapplication_class 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$ls_candidate"

  reset_state
  printf 'record type: metadata-claim\nidentifier: %s\npath: %s\n\n' "$ls_id" "$ls_candidate" >>"$tmp/ls.dump"
  failure ls_relevant_metadata_claim 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$ls_candidate"

  reset_state
  printf 'bundle id: Unknown (0x21c)\npath: %s (0x1a2b)\nclass: kLSBundleClassApplication (0x2)\ninfoDictionary: 1 values (0x11c)\nitem flags: file  application  unsupported-format (0000000000500085)\n\n' \
    "$ls_candidate" >>"$tmp/ls.dump"
  failure ls_unsupported_with_info_dictionary 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$ls_candidate"

  reset_state
  printf 'bundle id: Unknown (0x21c)\npath: %s (0x1a2b)\nclass: kLSBundleClassApplication (0x2)\nitem flags: file  application  unsupported-format (0000000000500085)\n\n' \
    "$ls_candidate" >>"$tmp/ls.dump"
  failure ls_unsupported_with_live_info_plist 'Launch Services snapshot has an unidentified app bundle' "$script" preflight --app "$ls_candidate"

  reset_state
  printf 'record type: application\nidentifier: %s\npath: %s (not-a-handle)\n\n' "$ls_id" "$ls_candidate" >>"$tmp/ls.dump"
  failure ls_invalid_handle 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$ls_candidate"

  echo 'package_macos_test: PASS (Launch Services live identifier, handles, legacy identity, and historical paths)'
  exit 0
fi

if [[ "${SEETHIS_DI3_FOCUSED:-0}" == 1 ]]; then
  focused_id=local.seethis.focused
  candidate="$tmp/focused/Candidate.app"
  competitor="$tmp/focused/Competitor.app"
  make_manual_bundle "$candidate" "$focused_id" seethis
  make_manual_bundle "$competitor" "$focused_id" rogue
  make_owned_manifest "$competitor" "$tmp/focused-owned.manifest" "$focused_id"

  reset_state
  success focused_complete_zero "$script" preflight --app "$candidate"
  printf 'record type: metadata-claim\nbundle id: %s\nroles: viewer\n\n' "$focused_id" >>"$tmp/ls.dump"
  success focused_non_application_id "$script" preflight --app "$candidate"
  reset_state; : >"$tmp/ps.before"
  failure focused_empty_ps 'running-process snapshot is empty or incomplete' "$script" preflight --app "$candidate"
  reset_state; printf ' \t\r\v\f\n\t\n' >"$tmp/ps.before"
  failure focused_whitespace_ps 'running-process snapshot is empty or incomplete' "$script" preflight --app "$candidate"
  reset_state; printf ' \t\r\v\f\nX\n' >"$tmp/ps.before"
  failure focused_nonblank_ps 'running-process snapshot is malformed or truncated' "$script" preflight --app "$candidate"
  reset_state; awk 'BEGIN { for (i=0; i<10000; i++) print "  \t  " }' >"$tmp/ps.before"
  failure focused_large_whitespace_ps 'running-process snapshot is empty or incomplete' "$script" preflight --app "$candidate"
  reset_state; awk 'BEGIN { pad=sprintf("%080d", 0); for (i=0; i<772; i++) printf "%d /nonexistent/%s\n", 10000+i, pad }' >"$tmp/ps.before"
  success focused_large_nonblank_ps "$script" preflight --app "$candidate"
  reset_state; : >"$tmp/ls.dump"
  failure focused_empty_ls 'Launch Services snapshot is empty or incomplete' "$script" preflight --app "$candidate"
  reset_state; printf ' \t\r\v\f\n\t\n' >"$tmp/ls.dump"
  failure focused_whitespace_ls 'Launch Services snapshot is empty or incomplete' "$script" preflight --app "$candidate"
  reset_state; printf ' \t\r\v\f\nX\n' >"$tmp/ls.dump"
  failure focused_nonblank_ls 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$candidate"
  reset_state; printf 'record type: application\nbundle id: %s\n' "$focused_id" >>"$tmp/ls.dump"
  failure focused_truncated_ls 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$candidate"
  reset_state
  awk 'BEGIN { for (i=0; i<250000; i++) print "  \t  " }' >>"$tmp/ls.dump"
  lsrow "$focused_id" "$candidate"
  lsrow "$focused_id" "$competitor"
  lsrow "$focused_id" "$competitor"
  failure focused_large_duplicate_ls "unresolved registered same-bundle app: $(canonical "$competitor")" "$script" preflight --app "$candidate"
  reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$(canonical "$competitor/Contents/MacOS/rogue")" >"$tmp/ps.before"
  failure focused_different_executable "unresolved running same-bundle app: $(canonical "$competitor")" "$script" preflight --app "$candidate"

  reset_state; lsrow "$focused_id" "$competitor"
  success focused_owned "$script" preflight --app "$candidate" --owned-manifest "$tmp/focused-owned.manifest"
  cp "$tmp/focused-owned.manifest" "$tmp/focused-blank.manifest"
  sed -i '' -e 's/^bundle_tree_sha256=.*/bundle_tree_sha256=/' -e 's/^designated_requirement_sha256=.*/designated_requirement_sha256=/' "$tmp/focused-blank.manifest"
  failure focused_blank_manifest "unresolved registered same-bundle app: $(canonical "$competitor")" "$script" preflight --app "$candidate" --owned-manifest "$tmp/focused-blank.manifest"
  export MOCK_FAIL_STDIN_SHASUM=1
  failure focused_requirement_hash_failure "unresolved registered same-bundle app: $(canonical "$competitor")" "$script" preflight --app "$candidate" --owned-manifest "$tmp/focused-owned.manifest"
  unset MOCK_FAIL_STDIN_SHASUM
  export MOCK_FAIL_TREE_SHASUM=1
  failure focused_tree_hash_failure "unresolved registered same-bundle app: $(canonical "$competitor")" "$script" preflight --app "$candidate" --owned-manifest "$tmp/focused-owned.manifest"
  unset MOCK_FAIL_TREE_SHASUM

  reset_state; lsrow "$focused_id" "$candidate"
  printf '101 /usr/bin/true\n5151 %s\n' "$(canonical "$candidate/Contents/MacOS/seethis")" >"$tmp/ps.after"
  export MOCK_PS_CALL_COUNT="$tmp/ps.calls" MOCK_PS_EMPTY_ON_CALL=2
  failure focused_launch_incomplete_baseline 'running-process snapshot is empty or incomplete' "$script" launch --app "$candidate" --acknowledge
  [[ ! -s "$tmp/open.log" ]]
  reset_state; lsrow "$focused_id" "$candidate"
  printf '5151 /usr/bin/true\n' >"$tmp/ps.before"
  printf '5151 %s\n' "$(canonical "$candidate/Contents/MacOS/seethis")" >"$tmp/ps.after"
  failure focused_launch_existing_pid 'launch produced no unique newly observed process' "$script" launch --app "$candidate" --acknowledge
  reset_state; lsrow "$focused_id" "$candidate"
  printf '101 /usr/bin/true\n5151 %s\n' "$(canonical "$candidate/Contents/MacOS/seethis")" >"$tmp/ps.after"
  success focused_launch_unique "$script" launch --app "$candidate" --acknowledge
  echo 'package_macos_test: PASS (focused DI3/DI5 completeness, classification, manifest, and launch hazards)'
  exit 0
fi

reset_state
success development "$script" package --source "$FIXTURE_SOURCE" --commit "$commit" --output "$tmp/dev" --mode development
has "$tmp/dev/evidence.txt" "commit=$commit"; has "$tmp/dev/evidence.txt" bundle_id=local.seethis.fixture
has "$tmp/dev/evidence.txt" CFBundleExecutable=seethis; grep -F '/dev/work.' "$MOCK_CMAKE_LOG" >/dev/null
has "$tmp/dev/evidence.txt" build_configuration=Debug
success release_adhoc "$script" package --source "$FIXTURE_SOURCE" --commit "$commit" --output "$tmp/release" --mode development --configuration Release
has "$tmp/release/evidence.txt" build_configuration=Release
has "$tmp/release/evidence.txt" signature_kind=adhoc
has "$tmp/release/evidence.txt" developer_id_signed=false
has "$tmp/release/evidence.txt" notarized=false
has "$tmp/release/evidence.txt" architectures=arm64
has "$tmp/release/evidence.txt" minimum_macos=14.2
grep -F -- '--preset macos-release' "$MOCK_CMAKE_ARGS" >/dev/null
grep -F -- '-DCMAKE_BUILD_TYPE=Release' "$MOCK_CMAKE_ARGS" >/dev/null
(cd "$tmp/release" && /usr/bin/shasum -a 256 -c SHA256SUMS) >/dev/null
has "$tmp/release/SeeThis.app/Contents/Resources/seethis-delivery-provenance.v1" build_configuration=Release
failure bad_configuration 'configuration must be Debug or Release' "$script" package --source "$FIXTURE_SOURCE" --configuration RelWithDebInfo
failure wrong_architecture 'built executable must contain arm64 only' env MOCK_ARCHITECTURES=x86_64 "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/wrongarch" --configuration Release
failure wrong_minimum 'built executable minimum macOS version must be 14.2' env MOCK_MINIMUM_MACOS=13.0 "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/wrongminimum" --configuration Release

[[ ! -s "$MOCK_LS_LOG" && ! -e "$tmp/open.log" ]]
failure no_prebuilt 'unknown package option: --bundle' "$script" package --source "$FIXTURE_SOURCE" --bundle "$tmp/dev/SeeThis.app"
failure stable_selector 'stable mode requires --stable-action bootstrap or update' "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/nosel" --mode stable --identity 'Fixture Stable'
failure identity_exact 'stable identity is unavailable' env 'MOCK_SECURITY_OUTPUT=1) BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB "Fixture Stable Extended"' "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/wrongid" --mode stable --identity 'Fixture Stable' --stable-action bootstrap
failure update_missing 'stable update requires --prior-app naming an accepted prior stable app' "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/noprior" --mode stable --identity 'Fixture Stable' --stable-action update

success bootstrap "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/bootstrap" --configuration Release --mode stable --identity 'Fixture Stable' --stable-action bootstrap
has "$tmp/bootstrap/evidence.txt" stable_action=bootstrap
has "$tmp/bootstrap/evidence.txt" build_configuration=Release
has "$tmp/bootstrap/evidence.txt" 'continuity_note=no inherited TCC continuity is claimed; field authorization may be required'
success update "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/update" --mode stable --identity AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA --stable-action update --prior-app "$tmp/bootstrap/SeeThis.app"
has "$tmp/update/evidence.txt" stable_action=update

reset_state; dev_dest="$tmp/dev-replacement"; mkdir -p "$dev_dest"
cp -R "$tmp/dev/SeeThis.app" "$dev_dest/SeeThis.app"
printf '%s\n' REQ_OLD_ADHOC >"$dev_dest/SeeThis.app/.mock-requirement"
lsrow local.seethis.fixture "$dev_dest/SeeThis.app"
success changed_adhoc_preflight "$script" preflight --app "$tmp/dev/SeeThis.app" --canonical-target "$dev_dest/SeeThis.app"
has "$log" 'continuity_note=development ad hoc identity changed; no TCC grant continuity is claimed; exact-path reauthorization may be required'
success changed_adhoc_install "$script" install --app "$tmp/dev/SeeThis.app" --destination "$dev_dest" --acknowledge
has "$log" 'continuity_note=development ad hoc identity changed; no TCC grant continuity is claimed; exact-path reauthorization may be required'
reset_state; stage_race_dest="$tmp/stage-race"; mkdir -p "$stage_race_dest"
lsrow local.seethis.fixture "$stage_race_dest/SeeThis.app"
export MOCK_REAPPEAR_TARGET="$stage_race_dest/SeeThis.app"
failure target_reappeared_during_stage 'canonical target appeared during staging' \
  "$script" install --app "$tmp/dev/SeeThis.app" --destination "$stage_race_dest" --acknowledge
[[ ! -s "$MOCK_LS_LOG" ]]
reset_state; registered_race_dest="$tmp/registered-race"; mkdir -p "$registered_race_dest"
registered_race_path="$tmp/previously-missing-owned.app"
cp -a "$tmp/bootstrap/SeeThis.app" "$registered_race_path"
make_owned_manifest "$registered_race_path" "$tmp/registered-race-owned.manifest" local.seethis.fixture
python3 - "$registered_race_path" <<'PY'
import shutil,sys
shutil.rmtree(sys.argv[1])
PY
lsrow local.seethis.fixture "$registered_race_path"
export MOCK_REAPPEAR_TARGET="$registered_race_path" MOCK_REAPPEAR_SOURCE="$tmp/bootstrap/SeeThis.app"
failure noncanonical_missing_registration_reappeared 'missing registered app set changed during staging' \
  "$script" install --app "$tmp/dev/SeeThis.app" --destination "$registered_race_dest" --acknowledge --owned-manifest "$tmp/registered-race-owned.manifest"
[[ ! -e "$registered_race_dest/SeeThis.app" && ! -s "$MOCK_LS_LOG" ]]
reset_state; stable_dest="$tmp/stable-replacement"; mkdir -p "$stable_dest"
cp -R "$tmp/bootstrap/SeeThis.app" "$stable_dest/SeeThis.app"
printf '%s\n' REQ_OTHER >"$stable_dest/SeeThis.app/.mock-requirement"
failure stable_preflight_mismatch 'canonical target designated requirement is incompatible' "$script" preflight --app "$tmp/update/SeeThis.app" --canonical-target "$stable_dest/SeeThis.app"
reset_state; mode_dest="$tmp/mode-replacement"; mkdir -p "$mode_dest"
cp -R "$tmp/bootstrap/SeeThis.app" "$mode_dest/SeeThis.app"
failure mode_transition 'canonical target delivery mode is incompatible' "$script" preflight --app "$tmp/dev/SeeThis.app" --canonical-target "$mode_dest/SeeThis.app"
reset_state; unproven_prior="$tmp/unproven-prior.app"; cp -R "$tmp/dev/SeeThis.app" "$unproven_prior"
rm "$unproven_prior/Contents/Resources/seethis-delivery-provenance.v1"
failure missing_prior_provenance 'canonical target signed provenance invalid' "$script" preflight --app "$tmp/dev/SeeThis.app" --canonical-target "$unproven_prior"
reset_state; bad_provenance="$tmp/bad-provenance.app"; cp -R "$tmp/dev/SeeThis.app" "$bad_provenance"
sed -i '' 's/^mode=development$/mode=unknown/' "$bad_provenance/Contents/Resources/seethis-delivery-provenance.v1"
failure malformed_candidate_provenance 'candidate signed provenance invalid' "$script" preflight --app "$bad_provenance"
reset_state; missing_provenance="$tmp/missing-provenance.app"; cp -R "$tmp/dev/SeeThis.app" "$missing_provenance"
rm "$missing_provenance/Contents/Resources/seethis-delivery-provenance.v1"
failure missing_candidate_provenance 'candidate signed provenance invalid' "$script" preflight --app "$missing_provenance"
reset_state; missing_registered="$tmp/no-longer-present.app"; lsrow local.seethis.fixture "$missing_registered"
success missing_registration "$script" preflight --app "$tmp/dev/SeeThis.app"
has "$log" "missing_registered_same_bundle_apps=$(canonical "$missing_registered")"
reset_state; lsrow local.seethis.fixture "$missing_registered"
printf '101 /usr/bin/true\n4242 %s\n' "$missing_registered/Contents/MacOS/seethis" >"$tmp/ps.before"
failure missing_registration_running 'stale or unresolved app process evidence for PID 4242' "$script" preflight --app "$tmp/dev/SeeThis.app"
reset_state; reappearing="$tmp/reappearing.app"; lsrow local.seethis.fixture "$reappearing"
export MOCK_REAPPEAR_PATH="$reappearing"
failure missing_registration_reappeared "missing registration reappeared: $(canonical "$reappearing")" "$script" preflight --app "$tmp/dev/SeeThis.app"
reset_state; symlink_path="$tmp/symlink-registration.app"; ln -s "$tmp/dev/SeeThis.app" "$symlink_path"; lsrow local.seethis.fixture "$symlink_path"
failure ambiguous_registered_symlink "ambiguous registered same-bundle path: $symlink_path" "$script" preflight --app "$tmp/dev/SeeThis.app"
failure canonical_target_symlink 'canonical target must not be a symbolic link' "$script" preflight --app "$tmp/dev/SeeThis.app" --canonical-target "$symlink_path"

# Exercise the historical-proof verifier with task-local evidence. The sourced
# function's pinned digest is replaced only inside this fixture process.
functions_file="$tmp/di-package-functions.sh"
sed '/^\[\[ \$# -gt 0 \]\]/,$d' "$script" >"$functions_file"
source "$functions_file"
audit_app="$tmp/Legacy.app"; make_manual_bundle "$audit_app" local.seethis.fixture seethis
audit_app=$(canonical "$audit_app")
rm "$audit_app/Contents/Resources/seethis-delivery-provenance.v1"
audit_zip="$tmp/original-signed.zip"; ditto -c -k --keepParent "$audit_app" "$audit_zip"
audit_index="$tmp/audit-index.json"
python3 - "$audit_index" "$audit_app" "$audit_zip" "$(sha256_file "$audit_zip")" <<'PY'
import json,sys
index,app,archive,digest=sys.argv[1:]
json.dump({'schema':'seethis_legacy_audit_bundle_proof.v2','derived_from_sha256':'a'*64,'rows':[{'path':app,'exists':True,'strict_signature_valid':True,'signed_provenance_exists':False,'exact_archive_match':True,'extracted_signature_valid':True,'exact_archive_path':archive,'artifact_zip_candidates':[{'path':archive,'sha256':digest}]}]},open(index,'w'))
PY
audit_expected_sha=$(sha256_file "$audit_index")
reset_state; lsrow local.seethis.fixture "$audit_app"
success original_zip_audit preflight_action --app "$tmp/dev/SeeThis.app" --historical-audit-index "$audit_index" --historical-audit-index-sha256 "$audit_expected_sha"
has "$log" "known_historical_audit_apps=$audit_app"
failure audit_index_digest_required 'historical audit index requires its exact expected SHA-256' \
  "$script" preflight --app "$tmp/dev/SeeThis.app" --historical-audit-index "$audit_index"
failure audit_index_digest_mismatch 'historical audit index does not match the expected SHA-256' \
  "$script" preflight --app "$tmp/dev/SeeThis.app" --historical-audit-index "$audit_index" --historical-audit-index-sha256 "$(printf '%064d' 0)"
printf 'mutation\n' >>"$audit_zip"
failure tampered_original_zip "unresolved registered same-bundle app: $audit_app" \
  bash -c 'source "$1"; preflight_action --app "$2" --historical-audit-index "$3" --historical-audit-index-sha256 "$4"' \
  _ "$functions_file" "$tmp/dev/SeeThis.app" "$audit_index" "$audit_expected_sha"
audit_seal="$tmp/original-file-seal.sha256"
python3 - "$audit_app" "$audit_seal" <<'PY'
import hashlib,pathlib,sys
app=pathlib.Path(sys.argv[1])
with open(sys.argv[2],'w') as out:
    for p in sorted(app.rglob('*')):
        if p.is_file(): print(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(app.parent)),file=out)
PY
python3 - "$audit_index" "$audit_app" "$audit_seal" <<'PY'
import json,sys
index,app,seal=sys.argv[1:]
import hashlib
seal_sha=hashlib.sha256(open(seal,'rb').read()).hexdigest()
json.dump({'schema':'seethis_legacy_audit_bundle_proof.v2','derived_from_sha256':'a'*64,'rows':[{'path':app,'exists':True,'strict_signature_valid':True,'signed_provenance_exists':False,'original_file_seal_verified':True,'original_file_seal_path':seal,'original_file_seal_sha256':seal_sha}]},open(index,'w'))
PY
audit_expected_sha=$(sha256_file "$audit_index")
historical_audit_matches "$audit_app" "$audit_index" "$audit_expected_sha"
echo mutation >>"$audit_seal"
if historical_audit_matches "$audit_app" "$audit_index" "$audit_expected_sha"; then echo 'FAIL mutated original seal passed' >&2; exit 1; fi
printf 'mutation\n' >>"$audit_app/Contents/MacOS/seethis"
python3 - "$audit_app" "$audit_seal" <<'PY'
import hashlib,pathlib,sys
app=pathlib.Path(sys.argv[1])
with open(sys.argv[2],'w') as out:
    for p in sorted(app.rglob('*')):
        if p.is_file(): print(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(app.parent)),file=out)
PY
if historical_audit_matches "$audit_app" "$audit_index" "$audit_expected_sha"; then echo 'FAIL paired seal and app mutation passed' >&2; exit 1; fi
cp -R "$audit_zip.contents/Legacy.app" "$tmp/pristine-legacy.app"
rm -rf "$audit_app"; mv "$tmp/pristine-legacy.app" "$audit_app"
audit_build_copy="$tmp/original-build-copy.app"; cp -R "$audit_app" "$audit_build_copy"
audit_manifest="$tmp/QA-MANIFEST.sha256"
python3 - "$audit_index" "$audit_app" "$audit_build_copy" "$audit_manifest" "$(bundle_tree_hash "$audit_build_copy")" <<'PY'
import hashlib,json,pathlib,sys
index,app,copy,manifest,tree=sys.argv[1:]
copy_path=pathlib.Path(copy)
with open(manifest,'w') as out:
    for p in sorted(copy_path.rglob('*')):
        if p.is_file(): print(hashlib.sha256(p.read_bytes()).hexdigest()+'  ./'+str(p.relative_to(copy_path.parent)),file=out)
manifest_sha=hashlib.sha256(open(manifest,'rb').read()).hexdigest()
json.dump({'schema':'seethis_legacy_audit_bundle_proof.v2','derived_from_sha256':'a'*64,'rows':[{'path':app,'exists':True,'strict_signature_valid':True,'signed_provenance_exists':False,'original_build_copy_exact':True,'original_build_copy_path':copy,'original_build_copy_tree_sha256':tree,'original_qa_manifest_path':manifest,'original_qa_manifest_sha256':manifest_sha}]},open(index,'w'))
PY
audit_expected_sha=$(sha256_file "$audit_index")
historical_audit_matches "$audit_app" "$audit_index" "$audit_expected_sha"
echo mutation >>"$audit_build_copy/Contents/MacOS/seethis"
if historical_audit_matches "$audit_app" "$audit_index" "$audit_expected_sha"; then echo 'FAIL mutated original build copy passed' >&2; exit 1; fi
echo mutation >>"$audit_app/Contents/MacOS/seethis"
if historical_audit_matches "$audit_app" "$audit_index" "$audit_expected_sha"; then echo 'FAIL paired original build and app mutation passed' >&2; exit 1; fi
reset_state; lsrow local.seethis.fixture "$audit_app"
failure unknown_without_proof "unresolved registered same-bundle app: $audit_app" "$script" preflight --app "$tmp/dev/SeeThis.app"
cp -R "$tmp/bootstrap/SeeThis.app" "$tmp/badprior.app"; : >"$tmp/badprior.app/.mock-invalid"
failure invalid_prior 'prior stable app signature invalid' "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/badpriorout" --mode stable --identity 'Fixture Stable' --stable-action update --prior-app "$tmp/badprior.app"
failure incompatible_req 'stable update designated requirement is incompatible with prior stable app' env MOCK_SIGN_REQUIREMENT=REQ_OTHER "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/badreq" --mode stable --identity 'Fixture Stable' --stable-action update --prior-app "$tmp/bootstrap/SeeThis.app"
echo local.seethis.changed >"$FIXTURE_SOURCE/bundle-id"; git -C "$FIXTURE_SOURCE" add bundle-id; git -C "$FIXTURE_SOURCE" commit -qm changed
failure changed_id 'stable update bundle identifier differs from prior stable app' "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/changed" --mode stable --identity 'Fixture Stable' --stable-action update --prior-app "$tmp/bootstrap/SeeThis.app"
echo local.seethis.fixture >"$FIXTURE_SOURCE/bundle-id"; git -C "$FIXTURE_SOURCE" add bundle-id; git -C "$FIXTURE_SOURCE" commit -qm restored
failure archive_mismatch 'retained and archive bundle content differ' env MOCK_ARCHIVE_MUTATE=1 "$script" package --source "$FIXTURE_SOURCE" --output "$tmp/badarchive"

reset_state; success preflight_complete_zero "$script" preflight --app "$tmp/bootstrap/SeeThis.app"; has "$log" status=PASS
reset_state; : >"$tmp/ls.dump"
failure ls_empty 'Launch Services snapshot is empty or incomplete' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf 'record type: application\nbundle id: local.seethis.fixture\n' >>"$tmp/ls.dump"
failure ls_candidate_truncated 'Launch Services snapshot is malformed or truncated' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
cp -R "$tmp/bootstrap/SeeThis.app" "$tmp/invalid.app"; : >"$tmp/invalid.app/.mock-invalid"
failure invalid_signature 'candidate signature invalid' "$script" preflight --app "$tmp/invalid.app"
mkdir -p "$tmp/arbitrary-staging"; cp -R "$tmp/update/SeeThis.app" "$tmp/arbitrary-staging/Other.app"; lsrow local.seethis.fixture "$tmp/arbitrary-staging/Other.app"
failure staging_substring "unresolved registered same-bundle app: $(canonical "$tmp/arbitrary-staging/Other.app")" "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; lsrow local.seethis.fixture "$tmp/update/SeeThis.app"
failure duplicate "unresolved registered same-bundle app: $(canonical "$tmp/update/SeeThis.app")" "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
success owned "$script" preflight --app "$tmp/bootstrap/SeeThis.app" --owned-manifest "$tmp/update/owned-artifacts.txt"
cp "$tmp/update/owned-artifacts.txt" "$tmp/wrong-tree"; sed -i '' 's/^bundle_tree_sha256=.*/bundle_tree_sha256=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff/' "$tmp/wrong-tree"
failure owned_tree "unresolved registered same-bundle app: $(canonical "$tmp/update/SeeThis.app")" "$script" preflight --app "$tmp/bootstrap/SeeThis.app" --owned-manifest "$tmp/wrong-tree"
cp "$tmp/update/owned-artifacts.txt" "$tmp/wrong-id"; sed -i '' 's/^bundle_id=.*/bundle_id=local.wrong/' "$tmp/wrong-id"
failure owned_id "unresolved registered same-bundle app: $(canonical "$tmp/update/SeeThis.app")" "$script" preflight --app "$tmp/bootstrap/SeeThis.app" --owned-manifest "$tmp/wrong-id"
cp "$tmp/update/owned-artifacts.txt" "$tmp/blank-hashes"
sed -i '' -e 's/^bundle_tree_sha256=.*/bundle_tree_sha256=/' -e 's/^designated_requirement_sha256=.*/designated_requirement_sha256=/' "$tmp/blank-hashes"
failure owned_blank_hashes "unresolved registered same-bundle app: $(canonical "$tmp/update/SeeThis.app")" "$script" preflight --app "$tmp/bootstrap/SeeThis.app" --owned-manifest "$tmp/blank-hashes"
export MOCK_FAIL_STDIN_SHASUM=1
failure owned_requirement_hash_failure "unresolved registered same-bundle app: $(canonical "$tmp/update/SeeThis.app")" "$script" preflight --app "$tmp/bootstrap/SeeThis.app" --owned-manifest "$tmp/update/owned-artifacts.txt"
unset MOCK_FAIL_STDIN_SHASUM
export MOCK_FAIL_TREE_SHASUM=1
failure owned_tree_hash_failure "unresolved registered same-bundle app: $(canonical "$tmp/update/SeeThis.app")" "$script" preflight --app "$tmp/bootstrap/SeeThis.app" --owned-manifest "$tmp/update/owned-artifacts.txt"
unset MOCK_FAIL_TREE_SHASUM
[[ ! -s "$MOCK_LS_LOG" && ! -s "$tmp/open.log" ]]

reset_state; export MOCK_PS_FAIL=1
failure ps_unavailable 'running-process snapshot is unavailable' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; : >"$tmp/ps.before"
failure ps_empty 'running-process snapshot is empty or incomplete' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; echo truncated >"$tmp/ps.before"
failure ps_truncated 'running-process snapshot is malformed or truncated' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state
rogue="$tmp/Rogue.app"; cp -R "$tmp/update/SeeThis.app" "$rogue"
mv "$rogue/Contents/MacOS/seethis" "$rogue/Contents/MacOS/rogue"
/usr/libexec/PlistBuddy -c 'Set :CFBundleExecutable rogue' "$rogue/Contents/Info.plist"
printf '101 /usr/bin/true\n4242 %s\n' "$(canonical "$rogue/Contents/MacOS/rogue")" >"$tmp/ps.before"
failure different_executable_running "unresolved running same-bundle app: $(canonical "$rogue")" "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; exe="$tmp/bootstrap/SeeThis.app/Contents/MacOS/seethis"; printf '99999 %s\n' "$exe" >"$tmp/ps.before"; export MOCK_STALE_PIDS=99999
failure stale 'stale or unverifiable process evidence for PID 99999' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
unset MOCK_STALE_PIDS; failure running 'canonical target is running' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"

other_app="$tmp/Unrelated.app"; make_manual_bundle "$other_app" local.unrelated.fixture unrelated
other_exe=$(canonical "$other_app/Contents/MacOS/unrelated")
candidate_exe=$(canonical "$tmp/bootstrap/SeeThis.app/Contents/MacOS/seethis")
framework="$tmp/System/Library/PrivateFrameworks/Fixture.framework"
mkdir -p "$framework/Versions/A/Resources" "$framework/Versions/B/Resources"
ln -s A "$framework/Versions/Current"
ln -s Versions/Current/Resources "$framework/Resources"
make_manual_bundle "$framework/Versions/A/Resources/Helper.app" local.unrelated.framework helper
cp -R "$framework/Versions/A/Resources/Helper.app" "$framework/Versions/B/Resources/Helper.app"
framework_raw="$framework/Resources/Helper.app/Contents/MacOS/helper"
framework_canonical=$(canonical "$framework_raw")
framework_other=$(canonical "$framework/Versions/B/Resources/Helper.app/Contents/MacOS/helper")
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$framework_raw" >"$tmp/ps.before"
export MOCK_PROC_PIDPATH_PID=4242 MOCK_PROC_PIDPATH_VALUE="$framework_canonical"
success protected_framework_alias "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$framework_raw" >"$tmp/ps.before"
export MOCK_PROC_PIDPATH_PID=4242 MOCK_PROC_PIDPATH_VALUE="$candidate_exe"
failure framework_alias_kernel_candidate 'ambiguous process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$framework_raw" >"$tmp/ps.before"
export MOCK_PROC_PIDPATH_PID=4242 MOCK_PROC_PIDPATH_VALUE="$framework_other"
failure framework_alias_kernel_different 'ambiguous process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$framework_raw" >"$tmp/ps.before"
export MOCK_PROC_PIDPATH_PID=4242 MOCK_PROC_PIDPATH_FAIL=1
failure framework_alias_kernel_unavailable 'ambiguous process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$framework_raw" >"$tmp/ps.before"
export MOCK_PROC_PIDPATH_PID=4242 MOCK_PROC_PIDPATH_VALUE="$framework_canonical" MOCK_STALE_PIDS=4242
failure framework_alias_vanished 'stale or unverifiable process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$framework_raw" >"$tmp/ps.before"
export MOCK_PROC_PIDPATH_PID=4242 MOCK_PROC_PIDPATH_VALUE="$framework_canonical"
export MOCK_RETARGET_ALIAS_ON_SNAPSHOT="$framework/Resources" MOCK_RETARGET_ALIAS_TARGET=Versions/B/Resources
failure framework_alias_retarget_before_snapshot 'ambiguous process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
/bin/rm "$framework/Resources"; /bin/ln -s Versions/Current/Resources "$framework/Resources"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$framework_raw" >"$tmp/ps.before"
export MOCK_PROC_PIDPATH_PID=4242 MOCK_PROC_PIDPATH_VALUE="$framework_canonical"
export MOCK_RETARGET_ALIAS_ON_LIVE_PID=4242 MOCK_RETARGET_ALIAS_ON_LIVE="$framework/Resources" MOCK_RETARGET_ALIAS_TARGET=Versions/B/Resources
export MOCK_LIVE_OVERRIDE_PID=4242 MOCK_LIVE_OVERRIDE_PATH="$framework_canonical"
failure framework_alias_retarget_after_proof 'ambiguous process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
/bin/rm "$framework/Resources"; /bin/ln -s Versions/Current/Resources "$framework/Resources"
ln -s "$other_app" "$tmp/WritableAlias.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$tmp/WritableAlias.app/Contents/MacOS/unrelated" >"$tmp/ps.before"
failure ordinary_alias_without_kernel_proof 'ambiguous process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
make_embedded_fixture() {
  local root bundle_identifier declared actual binary_identifier team signed_name
  root=$(canonical "$1"); bundle_identifier=$2; declared=$3; actual=$4; binary_identifier=$5; team=$6
  signed_name=$declared
  [[ "$declared" != __missing__ ]] || signed_name=$actual
  make_manual_bundle "$root" "$bundle_identifier" "$signed_name"
  if [[ "$declared" == __missing__ ]]; then
    /usr/libexec/PlistBuddy -c 'Delete :CFBundleExecutable' "$root/Contents/Info.plist"
  elif [[ "$actual" != "$declared" ]]; then
    cp "$root/Contents/MacOS/$declared" "$root/Contents/MacOS/$actual"
  fi
  : >"$root/Contents/MacOS/$actual.mock-signed"
  printf 'Executable=%s\nIdentifier=%s\nTeamIdentifier=%s\n' \
    "$root/Contents/MacOS/$signed_name" "$bundle_identifier" "$team" >"$root/.mock-display"
  printf 'Executable=%s\nIdentifier=%s\nTeamIdentifier=%s\n' \
    "$root/Contents/MacOS/$actual" "$binary_identifier" "$team" >"$root/Contents/MacOS/$actual.mock-display"
}
reject_vanished_embedded() {
  local name=$1 path=$2
  reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$path" >"$tmp/ps.before"
  printf '101 /usr/bin/true\n' >"$tmp/ps.replacement"
  export MOCK_STALE_PIDS=4242 MOCK_PS_CALL_COUNT="$tmp/ps.calls" MOCK_PS_REPLACE_ON_CALL=2 MOCK_PS_REPLACEMENT="$tmp/ps.replacement"
  failure "$name" 'stale or unverifiable process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
}
helper_app="$tmp/Code Helper.app"
make_embedded_fixture "$helper_app" com.microsoft.VSCode.helper __missing__ 'Code Helper' com.microsoft.VSCode.helper 9BNSXJN65R
helper_exe=$(canonical "$helper_app/Contents/MacOS/Code Helper")
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$helper_exe" >"$tmp/ps.before"
success missing_declared_signed_helper "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
docker_app="$tmp/Docker Helper.app"
make_embedded_fixture "$docker_app" com.docker.docker com.docker.backend com.docker.build com.docker.build 9BNSXJN65R
docker_exe=$(canonical "$docker_app/Contents/MacOS/com.docker.build")
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$docker_exe" >"$tmp/ps.before"
success different_declared_signed_helper "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
multi_helper="$tmp/Multi Helper.app"
make_embedded_fixture "$multi_helper" com.microsoft.VSCode.helper __missing__ 'Code Helper' com.microsoft.VSCode.helper 9BNSXJN65R
cp "$multi_helper/Contents/MacOS/Code Helper" "$multi_helper/Contents/MacOS/Another Helper"
reject_vanished_embedded missing_declared_multiple_executables "$(canonical "$multi_helper/Contents/MacOS/Code Helper")"
bad_app_id="$tmp/Bad App Identifier.app"
make_embedded_fixture "$bad_app_id" com.microsoft.VSCode.helper __missing__ 'Code Helper' com.microsoft.VSCode.helper 9BNSXJN65R
printf 'Identifier=com.example.extra\n' >>"$bad_app_id/.mock-display"
reject_vanished_embedded duplicate_signed_app_identifier "$(canonical "$bad_app_id/Contents/MacOS/Code Helper")"
bad_binary="$tmp/Bad Binary Signature.app"
make_embedded_fixture "$bad_binary" com.microsoft.VSCode.helper __missing__ 'Code Helper' com.microsoft.VSCode.helper 9BNSXJN65R
: >"$bad_binary/Contents/MacOS/Code Helper.mock-invalid"
reject_vanished_embedded invalid_embedded_binary_signature "$(canonical "$bad_binary/Contents/MacOS/Code Helper")"
bad_binary_path="$tmp/Bad Binary Executable.app"
make_embedded_fixture "$bad_binary_path" com.microsoft.VSCode.helper __missing__ 'Code Helper' com.microsoft.VSCode.helper 9BNSXJN65R
sed -i '' 's#Executable=.*#Executable=/wrong/binary/executable#' "$bad_binary_path/Contents/MacOS/Code Helper.mock-display"
reject_vanished_embedded mismatched_signed_binary_executable "$(canonical "$bad_binary_path/Contents/MacOS/Code Helper")"
bad_binary_id="$tmp/Bad Binary Identifier.app"
make_embedded_fixture "$bad_binary_id" com.microsoft.VSCode.helper __missing__ 'Code Helper' local.seethis.fixture 9BNSXJN65R
reject_vanished_embedded candidate_signed_binary_identifier "$(canonical "$bad_binary_id/Contents/MacOS/Code Helper")"
bad_team="$tmp/Bad Team.app"
make_embedded_fixture "$bad_team" com.docker.docker com.docker.backend com.docker.build com.docker.build 9BNSXJN65R
sed -i '' 's/TeamIdentifier=.*/TeamIdentifier=OTHERTEAM1/' "$bad_team/Contents/MacOS/com.docker.build.mock-display"
reject_vanished_embedded mismatched_embedded_team "$(canonical "$bad_team/Contents/MacOS/com.docker.build")"
unset_team="$tmp/Unset Team.app"
make_embedded_fixture "$unset_team" com.microsoft.VSCode.helper __missing__ 'Code Helper' com.microsoft.VSCode.helper 'not set'
reject_vanished_embedded unset_signed_team "$(canonical "$unset_team/Contents/MacOS/Code Helper")"
[[ ! -s "$tmp/open.log" ]]
bad_declared="$tmp/Bad Declared.app"
make_embedded_fixture "$bad_declared" com.docker.docker com.docker.backend com.docker.build com.docker.build 9BNSXJN65R
/usr/libexec/PlistBuddy -c 'Set :CFBundleExecutable invalid/name' "$bad_declared/Contents/Info.plist"
reject_vanished_embedded malformed_declared_executable "$(canonical "$bad_declared/Contents/MacOS/com.docker.build")"
bad_signed_exec="$tmp/Bad Signed Executable.app"
make_embedded_fixture "$bad_signed_exec" com.docker.docker com.docker.backend com.docker.build com.docker.build 9BNSXJN65R
sed -i '' 's#Executable=.*#Executable=/wrong/declared/executable#' "$bad_signed_exec/.mock-display"
reject_vanished_embedded mismatched_signed_declared_executable "$(canonical "$bad_signed_exec/Contents/MacOS/com.docker.build")"
xpc="$tmp/Unrelated.xpc"; make_manual_bundle "$xpc" local.unrelated.xpc authd
xpc_exe=$(canonical "$xpc/Contents/MacOS/authd")
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$xpc_exe" >"$tmp/ps.before"
success unrelated_live_xpc "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$xpc_exe" >"$tmp/ps.before"
export MOCK_STALE_PIDS=4242
failure unrelated_vanished_xpc 'stale or unverifiable process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
xpc_unknown="$tmp/Unknown.xpc"; make_manual_bundle "$xpc_unknown" local.unrelated.xpc authd
/usr/libexec/PlistBuddy -c 'Delete :CFBundleIdentifier' "$xpc_unknown/Contents/Info.plist"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$(canonical "$xpc_unknown/Contents/MacOS/authd")" >"$tmp/ps.before"
failure unknown_live_xpc "unverifiable different-bundle app process: $(canonical "$xpc_unknown")" "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
xpc_same_id="$tmp/SameId.xpc"; make_manual_bundle "$xpc_same_id" local.seethis.fixture authd
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$(canonical "$xpc_same_id/Contents/MacOS/authd")" >"$tmp/ps.before"
failure same_id_live_xpc "unresolved running same-bundle app: $(canonical "$xpc_same_id")" "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
cryptex_base="$tmp/System/Volumes/Preboot/Cryptexes"
cryptex_relative=System/Library/PrivateFrameworks/CryptexFixture.framework/Versions/A/XPCServices/Fixture.xpc
cryptex_xpc="$cryptex_base/OS/$cryptex_relative"
make_manual_bundle "$cryptex_xpc" local.unrelated.cryptex fixture
mkdir -p "$(dirname "$cryptex_base/Incoming/OS/$cryptex_relative")"
cp -R "$cryptex_xpc" "$cryptex_base/Incoming/OS/$cryptex_relative"
ln -s "$cryptex_base/OS/System/Library/PrivateFrameworks/CryptexFixture.framework" \
  "$tmp/System/Library/PrivateFrameworks/CryptexFixture.framework"
cryptex_raw="$tmp/System/Library/PrivateFrameworks/CryptexFixture.framework/Versions/A/XPCServices/Fixture.xpc/Contents/MacOS/fixture"
cryptex_kernel="$cryptex_base/Incoming/OS/$cryptex_relative/Contents/MacOS/fixture"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$cryptex_raw" >"$tmp/ps.before"
: >"$tmp/proc.calls"
export MOCK_PROC_PIDPATH_PID=4242 MOCK_PROC_PIDPATH_VALUE="$cryptex_kernel" MOCK_PROC_PIDPATH_CALL_LOG="$tmp/proc.calls"
success dual_cryptex_live_xpc "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
[[ ! -s "$tmp/proc.calls" && ! -s "$tmp/open.log" ]]
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$cryptex_raw" >"$tmp/ps.before"
: >"$tmp/proc.calls"
export MOCK_STALE_PIDS=4242 MOCK_PROC_PIDPATH_PID=4242 MOCK_PROC_PIDPATH_VALUE="$cryptex_kernel" MOCK_PROC_PIDPATH_CALL_LOG="$tmp/proc.calls"
failure dual_cryptex_vanished_xpc 'stale or unverifiable process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
[[ ! -s "$tmp/proc.calls" && ! -s "$tmp/open.log" ]]
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$cryptex_raw" >"$tmp/ps.before"
: >"$tmp/proc.calls"
export MOCK_LIVE_OVERRIDE_PID=4242 MOCK_LIVE_OVERRIDE_PATH="$candidate_exe" MOCK_PROC_PIDPATH_CALL_LOG="$tmp/proc.calls"
failure dual_cryptex_changed_xpc 'ambiguous process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
[[ ! -s "$tmp/proc.calls" && ! -s "$tmp/open.log" ]]
/usr/libexec/PlistBuddy -c 'Delete :CFBundleIdentifier' "$cryptex_xpc/Contents/Info.plist"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$cryptex_raw" >"$tmp/ps.before"
: >"$tmp/proc.calls"
export MOCK_PROC_PIDPATH_CALL_LOG="$tmp/proc.calls"
failure dual_cryptex_unknown_id_xpc "unverifiable different-bundle app process: $(canonical "$cryptex_xpc")" \
  "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
[[ ! -s "$tmp/proc.calls" && ! -s "$tmp/open.log" ]]
race_candidate="$tmp/RaceCandidate.app"; cp -R "$tmp/bootstrap/SeeThis.app" "$race_candidate"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$(canonical "$race_candidate/Contents/MacOS/seethis")" >"$tmp/ps.before"
export MOCK_PS_MUTATE_APP="$race_candidate" MOCK_PS_MUTATE_ID=local.unrelated.fixture
export MOCK_PS_MUTATE_MARKER="$tmp/race-candidate-mutated" MOCK_PS_MUTATE_INVALIDATE=1
failure candidate_id_changed_during_snapshot 'canonical target is running' "$script" preflight --app "$race_candidate"
same_id_app="$tmp/SameIdOther.app"; make_manual_bundle "$same_id_app" local.seethis.fixture seethis
make_owned_manifest "$same_id_app" "$tmp/same-id-owned.manifest" local.seethis.fixture
reset_state; lsrow local.seethis.fixture "$same_id_app"
printf '101 /usr/bin/true\n4242 %s\n' "$(canonical "$same_id_app/Contents/MacOS/seethis")" >"$tmp/ps.before"
export MOCK_PS_MUTATE_APP="$same_id_app" MOCK_PS_MUTATE_ID=local.unrelated.fixture
export MOCK_PS_MUTATE_MARKER="$tmp/same-id-mutated" MOCK_PS_MUTATE_INVALIDATE=1
failure same_id_other_changed_during_snapshot "unverifiable different-bundle app process: $(canonical "$same_id_app")" \
  "$script" preflight --app "$tmp/bootstrap/SeeThis.app" --owned-manifest "$tmp/same-id-owned.manifest"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$other_exe" >"$tmp/ps.before"
success unrelated_live "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$other_exe" >"$tmp/ps.before"
printf '101 /usr/bin/true\n' >"$tmp/ps.replacement"
export MOCK_STALE_PIDS=4242 MOCK_PS_CALL_COUNT="$tmp/ps.calls" MOCK_PS_REPLACE_ON_CALL=2 MOCK_PS_REPLACEMENT="$tmp/ps.replacement"
success unrelated_vanished "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
has "$log" status=PASS
invalid_other_app="$tmp/InvalidUnrelated.app"; cp -R "$other_app" "$invalid_other_app"
: >"$invalid_other_app/.mock-invalid"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$(canonical "$invalid_other_app/Contents/MacOS/unrelated")" >"$tmp/ps.before"
printf '101 /usr/bin/true\n' >"$tmp/ps.replacement"
export MOCK_STALE_PIDS=4242 MOCK_PS_CALL_COUNT="$tmp/ps.calls" MOCK_PS_REPLACE_ON_CALL=2 MOCK_PS_REPLACEMENT="$tmp/ps.replacement"
failure unrelated_invalid_signature_vanished 'stale or unverifiable process evidence for PID 4242' \
  "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
[[ ! -s "$tmp/open.log" ]]
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$other_exe" >"$tmp/ps.before"
export MOCK_STALE_PIDS=4242
failure unrelated_unconfirmed_absence 'stale or unverifiable process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$other_exe" >"$tmp/ps.before"
export MOCK_LIVE_OVERRIDE_PID=4242 MOCK_LIVE_OVERRIDE_PATH="$candidate_exe"
failure unrelated_reused_same_id 'ambiguous process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$candidate_exe" >"$tmp/ps.before"
export MOCK_STALE_PIDS=4242
failure same_id_vanished 'stale or unverifiable process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$candidate_exe" >"$tmp/ps.before"
failure same_id_running 'canonical target is running' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
unknown_app="$tmp/Unknown.app"; make_manual_bundle "$unknown_app" local.unknown.fixture unknown
/usr/libexec/PlistBuddy -c 'Delete :CFBundleIdentifier' "$unknown_app/Contents/Info.plist"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$(canonical "$unknown_app/Contents/MacOS/unknown")" >"$tmp/ps.before"
export MOCK_STALE_PIDS=4242
failure unrelated_unknown_id 'stale or unverifiable process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$other_app/Contents/MacOS/missing" >"$tmp/ps.before"
export MOCK_STALE_PIDS=4242
failure unrelated_missing_executable 'stale or unverifiable process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
cp "$other_exe" "$other_app/Contents/MacOS/not-declared"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$(canonical "$other_app/Contents/MacOS/not-declared")" >"$tmp/ps.before"
export MOCK_STALE_PIDS=4242
failure unrelated_wrong_executable 'stale or unverifiable process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
ln -s "$other_app" "$tmp/Alias.app"
reset_state; printf '101 /usr/bin/true\n4242 %s\n' "$tmp/Alias.app/Contents/MacOS/unrelated" >"$tmp/ps.before"
export MOCK_STALE_PIDS=4242
failure unrelated_symlink_path 'ambiguous process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\n4242 seethis\n' >"$tmp/ps.before"
export MOCK_STALE_PIDS=4242
failure basename_only_stale 'stale or unverifiable process evidence for PID 4242' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"
reset_state; printf '101 /usr/bin/true\nmalformed-process-record\n' >"$tmp/ps.before"
failure unrelated_malformed_record 'running-process snapshot is malformed or truncated' "$script" preflight --app "$tmp/bootstrap/SeeThis.app"

reset_state; dest="$tmp/install"; mkdir -p "$dest"; cp -R "$tmp/bootstrap/SeeThis.app" "$dest/SeeThis.app"; echo old >"$dest/SeeThis.app/old"; lsrow local.seethis.fixture "$dest/SeeThis.app"
success install "$script" install --app "$tmp/update/SeeThis.app" --destination "$dest" --acknowledge
[[ ! -e "$dest/SeeThis.app/old" ]]; grep -Fqx "$(canonical "$dest/SeeThis.app")" "$MOCK_LS_LOG"
reset_state; dest="$tmp/copyfail"; mkdir -p "$dest"; cp -R "$tmp/bootstrap/SeeThis.app" "$dest/SeeThis.app"; echo preserve >"$dest/SeeThis.app/old"; lsrow local.seethis.fixture "$dest/SeeThis.app"; export MOCK_CP_FAIL=1
failure copy_rollback 'staging copy failed; canonical target was not changed' "$script" install --app "$tmp/update/SeeThis.app" --destination "$dest" --acknowledge; has "$dest/SeeThis.app/old" preserve
[[ ! -s "$MOCK_LS_LOG" ]]
reset_state; dest="$tmp/invalidtarget"; mkdir -p "$dest"; cp -R "$tmp/bootstrap/SeeThis.app" "$dest/SeeThis.app"; echo preserve >"$dest/SeeThis.app/old"; : >"$dest/SeeThis.app/.mock-invalid"
failure invalid_target 'canonical target signature invalid' "$script" install --app "$tmp/update/SeeThis.app" --destination "$dest" --acknowledge
has "$dest/SeeThis.app/old" preserve; [[ ! -s "$MOCK_LS_LOG" ]]
reset_state; dest="$tmp/regfail"; mkdir -p "$dest"; cp -R "$tmp/bootstrap/SeeThis.app" "$dest/SeeThis.app"; echo preserve >"$dest/SeeThis.app/old"; lsrow local.seethis.fixture "$dest/SeeThis.app"; export MOCK_LS_FAIL=1
failure register_rollback 'Launch Services registration failed; previous canonical app was restored' "$script" install --app "$tmp/update/SeeThis.app" --destination "$dest" --acknowledge; has "$dest/SeeThis.app/old" preserve
failure install_ack 'install requires --acknowledge' "$script" install --app "$tmp/update/SeeThis.app" --destination "$tmp/noack"

prepare_launch() {
  reset_state; launch_app="$tmp/$1/SeeThis.app"; mkdir -p "$tmp/$1"; cp -R "$tmp/update/SeeThis.app" "$launch_app"
  lsrow local.seethis.fixture "$launch_app"; launch_exe="$launch_app/Contents/MacOS/seethis"
}
prepare_launch launch_ok; printf '4242 %s\n' "$launch_exe" >"$tmp/ps.after"
success launch "$script" launch --app "$launch_app" --acknowledge; grep -Fqx "$(canonical "$launch_app")" "$tmp/open.log"
prepare_launch launch_unrelated_vanished
printf '101 /usr/bin/true\n4242 %s\n' "$other_exe" >"$tmp/ps.before"
printf '101 /usr/bin/true\n' >"$tmp/ps.replacement"
printf '101 /usr/bin/true\n5151 %s\n' "$launch_exe" >"$tmp/ps.after"
export MOCK_STALE_PIDS=4242 MOCK_PS_CALL_COUNT="$tmp/ps.calls" MOCK_PS_REPLACE_ON_CALL=2 MOCK_PS_REPLACEMENT="$tmp/ps.replacement"
success launch_unrelated_vanished "$script" launch --app "$launch_app" --acknowledge
grep -Fqx "$(canonical "$launch_app")" "$tmp/open.log"
prepare_launch launch_candidate_id_changed
printf '101 /usr/bin/true\n4242 %s\n' "$launch_exe" >"$tmp/ps.before"
export MOCK_PS_MUTATE_APP="$launch_app" MOCK_PS_MUTATE_ID=local.unrelated.fixture
export MOCK_PS_MUTATE_MARKER="$tmp/launch-candidate-id-mutated" MOCK_PS_MUTATE_INVALIDATE=1
failure launch_candidate_id_changed 'canonical target is running' "$script" launch --app "$launch_app" --acknowledge
[[ ! -s "$tmp/open.log" ]]
prepare_launch launch_incomplete_baseline; printf '4242 %s\n' "$launch_exe" >"$tmp/ps.after"
export MOCK_PS_CALL_COUNT="$tmp/ps.calls" MOCK_PS_EMPTY_ON_CALL=2
failure launch_incomplete_baseline 'running-process snapshot is empty or incomplete' "$script" launch --app "$launch_app" --acknowledge
[[ ! -s "$tmp/open.log" ]]
prepare_launch launch_existing_pid
printf '5151 /usr/bin/true\n' >"$tmp/ps.before"
printf '5151 %s\n' "$launch_exe" >"$tmp/ps.after"
failure launch_existing_pid 'launch produced no unique newly observed process' "$script" launch --app "$launch_app" --acknowledge
prepare_launch launch_zero; failure launch_zero 'launch produced no unique newly observed process' "$script" launch --app "$launch_app" --acknowledge
prepare_launch launch_many; printf '4242 %s\n4243 %s\n' "$launch_exe" "$launch_exe" >"$tmp/ps.after"
failure launch_many 'launch produced multiple newly observed processes' "$script" launch --app "$launch_app" --acknowledge
prepare_launch launch_wrong; wrong="$tmp/Other.app/Contents/MacOS/seethis"; mkdir -p "$(dirname "$wrong")"; cp "$launch_exe" "$wrong"; printf '4242 %s\n' "$wrong" >"$tmp/ps.after"
failure launch_wrong 'new process executable path does not match the exact installed app' "$script" launch --app "$launch_app" --acknowledge
prepare_launch launch_mutated; printf '4242 %s\n' "$launch_exe" >"$tmp/ps.after"; export MOCK_MUTATE_EXEC=1
failure launch_mutated 'post-open executable bytes differ from the prelaunch snapshot' "$script" launch --app "$launch_app" --acknowledge
prepare_launch launch_req; printf '4242 %s\n' "$launch_exe" >"$tmp/ps.after"; export MOCK_MUTATE_REQ=1
failure launch_req 'post-open designated requirement differs from the prelaunch snapshot' "$script" launch --app "$launch_app" --acknowledge
prepare_launch launch_stale; printf '4242 %s\n' "$launch_exe" >"$tmp/ps.after"; export MOCK_STALE_PIDS=4242
failure launch_stale 'stale or unverifiable process evidence for PID 4242' "$script" launch --app "$launch_app" --acknowledge
failure launch_ack 'launch requires --acknowledge' "$script" launch --app "$tmp/update/SeeThis.app"
echo 'package_macos_test: PASS (fresh build, stable continuity, fail-closed preflight, rollback install, exact launch)'
