#!/usr/bin/env bash
set -euo pipefail
umask 077

die() { printf 'ERROR: %s\n' "$*" >&2; exit 2; }
note() { printf '%s\n' "$*"; }
usage() {
  cat <<'EOF'
Usage:
  package_macos.sh package [--source DIR] [--commit SHA] [--output DIR] [--mode development] [--configuration Debug|Release]
  package_macos.sh package [--source DIR] [--commit SHA] [--output DIR]
                           --mode stable --identity ID
                           --stable-action bootstrap|update [--prior-app PATH]
  package_macos.sh preflight --app PATH [--canonical-target PATH] [--owned-manifest FILE ...]
                             [--historical-audit-index FILE --historical-audit-index-sha256 SHA256]
  package_macos.sh install --app PATH --destination DIR --acknowledge [--owned-manifest FILE ...]
                           [--historical-audit-index FILE --historical-audit-index-sha256 SHA256]
  package_macos.sh launch --app PATH --acknowledge [--owned-manifest FILE ...]
                          [--historical-audit-index FILE --historical-audit-index-sha256 SHA256]

Packaging always performs a fresh build from an archive of the exact clean Git
commit. Prebuilt bundle input and caller-asserted provenance are not accepted.
EOF
}

real() { python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$1"; }
sha256_file() {
  local output hash
  output=$(shasum -a 256 "$1") || return 1
  hash=${output%%[[:space:]]*}
  [[ "$hash" =~ ^[0-9a-f]{64}$ ]] || return 1
  printf '%s' "$hash"
}
sha256_text() {
  local output hash
  output=$(printf '%s' "$1" | shasum -a 256) || return 1
  hash=${output%%[[:space:]]*}
  [[ "$hash" =~ ^[0-9a-f]{64}$ ]] || return 1
  printf '%s' "$hash"
}
plist() {
  local value
  if value=$(/usr/libexec/PlistBuddy -c "Print :$2" "$1/Contents/Info.plist" 2>/dev/null); then
    printf '%s' "$value"
  elif value=$(defaults read "$1/Contents/Info" "$2" 2>/dev/null); then
    printf '%s' "$value"
  else
    return 1
  fi
}
bundle_id() { plist "$1" CFBundleIdentifier; }
executable_name() { plist "$1" CFBundleExecutable; }
executable() {
  local name path
  name=$(executable_name "$1")
  [[ -n "$name" && "$name" != */* ]] || die 'bundle lacks a valid CFBundleExecutable'
  path="$1/Contents/MacOS/$name"
  [[ -f "$path" ]] || die "bundle executable missing: $path"
  real "$path"
}
designated_requirement() {
  local raw line value
  local -a candidates=()
  raw=$(codesign -d -r- "$1" 2>&1) || return 1
  while IFS= read -r line; do
    if [[ "$line" == '# designated => '* ]]; then
      value=${line#'# designated => '}
    elif [[ "$line" == 'designated => '* ]]; then
      value=${line#'designated => '}
    elif [[ "$line" == *'designated =>'* ]]; then
      return 1
    else
      continue
    fi
    [[ "$value" =~ [^[:space:]] && "$value" != *$'\n'* &&
       "$value" != *$'\r'* ]] || return 1
    candidates+=("$value")
  done <<<"$raw"
  [[ "${#candidates[@]}" == 1 ]] || return 1
  printf '%s' "${candidates[0]}"
}
cdhash() {
  local raw value count
  raw=$(codesign -d --verbose=4 "$1" 2>&1) || return 1
  count=$(printf '%s\n' "$raw" | grep -c '^CDHash=' || true)
  [[ "$count" == 1 ]] || return 1
  value=$(printf '%s\n' "$raw" | sed -n 's/^CDHash=//p')
  [[ "$value" =~ ^[0-9A-Fa-f]{40,64}$ ]] || return 1
  printf '%s' "$value"
}
strict_verify() { codesign --verify --deep --strict "$1" >/dev/null 2>&1; }

bundle_tree_hash() {
  local root=$1 temp paths listing item mode value result
  temp=$(mktemp -d "${TMPDIR:-/tmp}/seethis-tree-hash.XXXXXX") || return 1
  paths="$temp/paths"
  listing="$temp/listing"
  if ! (cd "$root" && LC_ALL=C find . \( -type f -o -type l \) -print0 | LC_ALL=C sort -z) >"$paths"; then
    rm -rf "$temp"
    return 1
  fi
  : >"$listing"
  while IFS= read -r -d '' item; do
    item=${item#./}
    mode=$(cd "$root" && stat -f '%Lp' "$item") || { rm -rf "$temp"; return 1; }
    [[ "$mode" =~ ^[0-7]+$ ]] || { rm -rf "$temp"; return 1; }
    if [[ -L "$root/$item" ]]; then
      value=$(cd "$root" && readlink "$item") || { rm -rf "$temp"; return 1; }
      [[ -n "$value" ]] || { rm -rf "$temp"; return 1; }
      printf 'link\t%s\t%s\t%s\n' "$item" "$mode" "$value" >>"$listing"
    else
      value=$(sha256_file "$root/$item") || { rm -rf "$temp"; return 1; }
      printf 'file\t%s\t%s\t%s\n' "$item" "$mode" "$value" >>"$listing"
    fi
  done <"$paths"
  result=$(sha256_file "$listing") || { rm -rf "$temp"; return 1; }
  rm -rf "$temp"
  printf '%s' "$result"
}

provenance_file() { printf '%s/Contents/Resources/seethis-delivery-provenance.v1' "$1"; }
provenance_value() {
  local file=$1 key=$2 count value
  [[ -f "$file" ]] || return 1
  count=$(grep -c "^${key}=" "$file" || true)
  [[ "$count" == 1 ]] || return 1
  value=$(sed -n "s/^${key}=//p" "$file") || return 1
  [[ -n "$value" && "$value" != *$'\n'* ]] || return 1
  printf '%s' "$value"
}
validate_signed_provenance() {
  local app=$1 file schema recorded_id recorded_executable mode identity source_commit source_tree actual_id actual_executable
  file=$(provenance_file "$app")
  schema=$(provenance_value "$file" schema) || return 1
  recorded_id=$(provenance_value "$file" bundle_id) || return 1
  recorded_executable=$(provenance_value "$file" executable) || return 1
  mode=$(provenance_value "$file" mode) || return 1
  identity=$(provenance_value "$file" signing_identity_sha1) || return 1
  source_commit=$(provenance_value "$file" source_commit) || return 1
  source_tree=$(provenance_value "$file" source_tree) || return 1
  actual_id=$(bundle_id "$app") || return 1
  actual_executable=$(executable_name "$app") || return 1
  [[ -n "$actual_id" && -n "$actual_executable" ]] || return 1
  [[ "$schema" == seethis-delivery-provenance-v1 ]] || return 1
  [[ "$recorded_id" == "$actual_id" ]] || return 1
  [[ "$recorded_executable" == "$actual_executable" ]] || return 1
  [[ "$mode" == development || "$mode" == stable ]] || return 1
  [[ "$identity" == adhoc || "$identity" =~ ^[0-9A-Fa-f]{40}$ ]] || return 1
  if [[ "$mode" == development ]]; then
    [[ "$identity" == adhoc ]] || return 1
  else
    [[ "$identity" != adhoc ]] || return 1
  fi
  [[ "$source_commit" =~ ^[0-9a-f]{40}$ && "$source_tree" =~ ^[0-9a-f]{40}$ ]] || return 1
  return 0
}

resolve_identity_sha1() {
  local requested=$1 output line hash name found=''
  [[ "$requested" != *$'\n'* ]] || die 'stable identity must be one line'
  output=$(security find-identity -v -p codesigning 2>/dev/null) || die 'unable to query code-signing identities'
  while IFS= read -r line; do
    hash=$(printf '%s\n' "$line" | sed -n 's/^[[:space:]]*[0-9][0-9]*)[[:space:]]*\([0-9A-Fa-f]\{40\}\)[[:space:]].*/\1/p')
    name=$(printf '%s\n' "$line" | sed -n 's/^[^"]*"\(.*\)"[[:space:]]*$/\1/p')
    [[ -n "$hash" ]] || continue
    if [[ "$requested" == "$hash" || "$requested" == "$name" ]]; then
      [[ -z "$found" || "$found" == "$hash" ]] || die 'stable identity selection is ambiguous'
      found=$hash
    fi
  done <<<"$output"
  [[ -n "$found" ]] || die 'stable identity is unavailable'
  printf '%s' "$found"
}

lsregister_command() {
  if command -v lsregister >/dev/null 2>&1; then
    command -v lsregister
  elif [[ -x /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister ]]; then
    printf '%s' /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
  else
    return 1
  fi
}

registered_apps() {
  local wanted=$1 command dump records row checked=''
  command=$(lsregister_command) || die 'Launch Services registry is unavailable'
  dump=$("$command" -dump 2>/dev/null) || die 'Launch Services registry query failed'
  printf '%s\n' "$dump" | awk '/[^[:space:]]/ { nonblank=1 } END { exit !nonblank }' ||
    die 'Launch Services snapshot is empty or incomplete'
  records=$(printf '%s\n' "$dump" | awk -v wanted="$wanted" '
    function trim(s) { sub(/^[[:space:]]+/, "", s); sub(/[[:space:]]+$/, "", s); return s }
    function set_field(field, value) {
      if (field == "identifier") {
        if (identifier_seen && identifier != value) malformed=1
        identifier=value; identifier_seen=1
      } else if (field == "legacy_id") {
        if (legacy_seen && legacy_id != value) malformed=1
        legacy_id=value; legacy_seen=1
      } else if (field == "bundle_id") {
        if (bundle_seen && bundle_id != value) malformed=1
        bundle_id=value; bundle_seen=1
      } else if (field == "path") {
        if (path_seen && path != value) malformed=1
        path=value; path_seen=1
      }
    }
    function emit() {
      if (identifier_seen || legacy_seen || bundle_seen || path_seen) {
        id=identifier_seen ? identifier : (legacy_seen ? legacy_id : bundle_id)
        display_label=(bundle_seen && bundle_id ~ / \(0x[0-9A-Fa-f]+\)$/)
        if (display_label && !identifier_seen && !legacy_seen) id=""
        app_class=(record_type == "application" || bundle_class ~ /^kLSBundleClassApplication([[:space:]]|$)/ || item_flags ~ /(^|[[:space:]])application([[:space:]]|$)/)
        other_class=(record_type != "" && record_type != "application") || (bundle_class != "" && bundle_class !~ /^kLSBundleClassApplication([[:space:]]|$)/)
        known_nonbundle=(bundle_flags ~ /(^|[[:space:]])no-info\.plist([[:space:]]|$)/ || item_flags ~ /(^|[[:space:]])unsupported-format([[:space:]]|$)/)
        possible_app=(path ~ /\.app$/ || id == wanted || (app_class && (path == "" || path !~ /\.service$/)))
        if (identifier_seen && identifier == wanted && other_class) malformed=1
        if (!other_class && id == "" && path ~ /^\/.*\.app$/ && known_nonbundle) {
          if (has_info_dictionary) malformed=1
          else print "?" path
        }
        if (!other_class && (id == wanted || (possible_app && !known_nonbundle))) {
          if ((display_label && id == "") ||
              (identifier_seen && legacy_seen && identifier != legacy_id) ||
              (bundle_seen && !display_label && bundle_id != id) ||
              id == "" || path == "" || path !~ /^\// || path !~ /\.app$/) malformed=1
          else {
            complete++
            print_candidate=(id == wanted)
            if (print_candidate) print path
          }
        }
      }
      identifier=""; legacy_id=""; bundle_id=""; path=""; record_type=""
      bundle_class=""; item_flags=""; bundle_flags=""; has_info_dictionary=0
      identifier_seen=0; legacy_seen=0; bundle_seen=0; path_seen=0
    }
    /^[[:space:]]*[-]{8,}[[:space:]]*$/ { emit(); next }
    /^[[:space:]]*$/ { emit(); next }
    {
      line=$0
      if (line ~ /^[[:space:]]*(record type|type):[[:space:]]*/) {
        sub(/^[[:space:]]*(record type|type):[[:space:]]*/, "", line)
        line=trim(line)
        if (record_type != "" && record_type != line) malformed=1
        record_type=line
      } else if (line ~ /^[[:space:]]*class:[[:space:]]*/) {
        sub(/^[[:space:]]*class:[[:space:]]*/, "", line); bundle_class=trim(line)
      } else if (line ~ /^[[:space:]]*item flags:[[:space:]]*/) {
        sub(/^[[:space:]]*item flags:[[:space:]]*/, "", line); item_flags=trim(line)
      } else if (line ~ /^[[:space:]]*bundle flags:[[:space:]]*/) {
        sub(/^[[:space:]]*bundle flags:[[:space:]]*/, "", line); bundle_flags=trim(line)
      } else if (line ~ /^[[:space:]]*infoDictionary:[[:space:]]*/) {
        has_info_dictionary=1
      } else if (line ~ /^[[:space:]]*identifier:[[:space:]]*/) {
        sub(/^[[:space:]]*identifier:[[:space:]]*/, "", line); set_field("identifier", trim(line))
      } else if (line ~ /^[[:space:]]*bundle identifier:[[:space:]]*/) {
        sub(/^[[:space:]]*bundle identifier:[[:space:]]*/, "", line); set_field("legacy_id", trim(line))
      } else if (line ~ /^[[:space:]]*bundle id:[[:space:]]*/) {
        sub(/^[[:space:]]*bundle id:[[:space:]]*/, "", line); set_field("bundle_id", trim(line))
      } else if (line ~ /^[[:space:]]*(path|bundle path):[[:space:]]*/) {
        sub(/^[[:space:]]*(path|bundle path):[[:space:]]*/, "", line); line=trim(line)
        sub(/[[:space:]]+\(0x[0-9A-Fa-f]+\)$/, "", line)
        if (line ~ /^".*"$/) { sub(/^"/, "", line); sub(/"$/, "", line) }
        set_field("path", line)
      }
    }
    END {
      emit()
      if (malformed || complete == 0) exit 42
    }
  ') || die 'Launch Services snapshot is malformed or truncated'
  while IFS= read -r row; do
    [[ -n "$row" ]] || continue
    if [[ "$row" == \?* ]]; then
      row=${row#?}
      [[ ! -e "$row/Contents/Info.plist" && ! -L "$row/Contents/Info.plist" ]] ||
        die 'Launch Services snapshot has an unidentified app bundle'
      continue
    fi
    checked=${checked:+$checked$'\n'}$row
  done <<<"$records"
  printf '%s\n' "$checked" | sed '/^$/d' | LC_ALL=C sort -u
}

capture_process_snapshot() {
  local destination=$1 include_raw=${2:-0} raw line parsed='' pid path observed
  raw=$(ps -axo pid=,comm= 2>/dev/null) || die 'running-process snapshot is unavailable'
  printf '%s\n' "$raw" | awk '/[^[:space:]]/ { nonblank=1 } END { exit !nonblank }' ||
    die 'running-process snapshot is empty or incomplete'
  while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    if [[ "$line" =~ ^[[:space:]]*([0-9]+)[[:space:]]+(.+)$ ]]; then
      pid=${BASH_REMATCH[1]}
      path=${BASH_REMATCH[2]}
      observed=$path
      if [[ "$path" = /* && -e "$path" ]]; then path=$(real "$path"); fi
      parsed=${parsed:+$parsed$'\n'}$pid$'\t'$path
      if ((include_raw)); then parsed+=$'\t'$observed; fi
    else
      die 'running-process snapshot is malformed or truncated'
    fi
  done <<<"$raw"
  printf -v "$destination" '%s' "$parsed"
}

codesign_display_value() {
  local output=$1 key=$2 line value='' count=0
  while IFS= read -r line; do
    [[ "$line" == "$key="* ]] || continue
    value=${line#"$key="}
    [[ -n "$value" && "$value" != *$'\r'* && "$value" != *$'\t'* ]] || return 1
    count=$((count + 1))
  done <<<"$output"
  [[ "$count" == 1 ]] || return 1
  printf '%s' "$value"
}

only_macos_executable() {
  python3 - "$1" "$2" <<'PY'
import os, stat, sys
root, observed = sys.argv[1:]
try:
    executables = [entry.path for entry in os.scandir(root)
                   if stat.S_ISREG(entry.stat(follow_symlinks=False).st_mode)
                   and os.access(entry.path, os.X_OK)]
except OSError:
    sys.exit(1)
sys.exit(0 if executables == [observed] else 1)
PY
}

unrelated_app_snapshot() {
  local path=$1 raw_path=$2 candidate_id=$3 candidate_app=$4 candidate_exec=$5 app_root name='' snapshot_id
  local declared=0 app_display binary_display app_display_id app_team binary_exec binary_id binary_team signed_exec
  [[ "$raw_path" = /* && "$raw_path" == "$path" && -f "$path" && -r "$path" && ! -L "$path" ]] || return 1
  [[ "$path" == */Contents/MacOS/* ]] || return 1
  app_root=${path%/Contents/MacOS/*}
  [[ "$path" != "$candidate_exec" && "$app_root" != "$candidate_app" ]] || return 1
  [[ "$app_root" = /*.app && -d "$app_root" && ! -L "$app_root" &&
     -d "$app_root/Contents/MacOS" && ! -L "$app_root/Contents" && ! -L "$app_root/Contents/MacOS" &&
     -f "$app_root/Contents/Info.plist" && -r "$app_root/Contents/Info.plist" && ! -L "$app_root/Contents/Info.plist" ]] || return 1
  if name=$(executable_name "$app_root"); then
    declared=1
    [[ "$name" =~ [^[:space:]] && "$name" != */* && "$name" != . && "$name" != .. &&
       "$name" != *$'\n'* && "$name" != *$'\r'* && "$name" != *$'\t'* ]] || return 1
  fi
  snapshot_id=$(bundle_id "$app_root") || return 1
  [[ -n "$snapshot_id" && "$snapshot_id" != "$candidate_id" ]] || return 1
  strict_verify "$app_root" || return 1
  [[ "$(bundle_id "$app_root")" == "$snapshot_id" ]] || return 1
  if ((declared)); then
    [[ "$(executable_name "$app_root")" == "$name" ]] || return 1
    if [[ "$path" == "$app_root/Contents/MacOS/$name" ]]; then return 0; fi
  else
    executable_name "$app_root" >/dev/null 2>&1 && return 1
    only_macos_executable "$app_root/Contents/MacOS" "$path" || return 1
  fi

  # An embedded signed executable needs independent identity evidence from both signatures.
  [[ -x "$path" ]] || return 1
  strict_verify "$path" || return 1
  app_display=$(codesign -d --verbose=4 "$app_root" 2>&1) || return 1
  binary_display=$(codesign -d --verbose=4 "$path" 2>&1) || return 1
  app_display_id=$(codesign_display_value "$app_display" Identifier) || return 1
  app_team=$(codesign_display_value "$app_display" TeamIdentifier) || return 1
  binary_exec=$(codesign_display_value "$binary_display" Executable) || return 1
  binary_id=$(codesign_display_value "$binary_display" Identifier) || return 1
  binary_team=$(codesign_display_value "$binary_display" TeamIdentifier) || return 1
  [[ "$app_display_id" == "$snapshot_id" && "$binary_exec" == "$path" &&
     "$binary_id" != "$candidate_id" && "$app_team" =~ ^[A-Za-z0-9]{10}$ &&
     "$app_team" == "$binary_team" ]] || return 1
  if ((declared)); then
    signed_exec=$(codesign_display_value "$app_display" Executable) || return 1
    [[ "$signed_exec" == "$app_root/Contents/MacOS/$name" &&
       -f "$signed_exec" && -r "$signed_exec" && -x "$signed_exec" && ! -L "$signed_exec" ]] || return 1
  fi
  [[ "$(bundle_id "$app_root")" == "$snapshot_id" ]] || return 1
  if ((declared)); then
    [[ "$(executable_name "$app_root")" == "$name" ]]
  else
    executable_name "$app_root" >/dev/null 2>&1 && return 1
    only_macos_executable "$app_root/Contents/MacOS" "$path"
  fi
}

pid_absent_from_complete_snapshot() {
  local wanted=$1 snapshot pid observed
  capture_process_snapshot snapshot
  while IFS=$'\t' read -r pid observed; do
    [[ "$pid" == "$wanted" ]] && return 1
  done <<<"$snapshot"
  return 0
}
proc_pidpath_exact() {
  python3 - "$1" seethis-proc-pidpath-proof-v1 <<'PY'
import ctypes, os, sys
try:
    pid = int(sys.argv[1])
    libproc = ctypes.CDLL('/usr/lib/libproc.dylib')
    libproc.proc_pidpath.argtypes = (ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32)
    libproc.proc_pidpath.restype = ctypes.c_int
    path_buffer = ctypes.create_string_buffer(4096)
    length = libproc.proc_pidpath(pid, path_buffer, len(path_buffer))
    path = os.fsdecode(path_buffer.value)
    if length <= 0 or length >= len(path_buffer) - 1 or len(path_buffer.value) != length:
        sys.exit(1)
    if not os.path.isabs(path) or '\n' in path or '\r' in path or '\t' in path:
        sys.exit(1)
    print(path)
except (AttributeError, OSError, ValueError, OverflowError):
    sys.exit(1)
PY
}
verified_process_alias() {
  local pid=$1 raw=$2 canonical=$3 kernel_path
  [[ "$raw" = /* && "$raw" != "$canonical" && -e "$raw" ]] || return 1
  [[ "$(real "$raw")" == "$canonical" ]] || return 1
  kernel_path=$(proc_pidpath_exact "$pid") || return 1
  [[ "$kernel_path" == "$canonical" ]]
}
live_process_path() {
  local pid=$1 path
  path=$(ps -p "$pid" -o comm= 2>/dev/null | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' | head -1)
  [[ -n "$path" ]] || return 1
  if [[ "$path" = /* && -e "$path" ]]; then path=$(real "$path"); fi
  printf '%s' "$path"
}

owned_manifest_matches() {
  local manifest=$1 app=$2 schema path expected_tree expected_id expected_req_hash
  local actual_tree actual_id actual_req actual_req_hash
  [[ -f "$manifest" ]] || return 1
  schema=$(provenance_value "$manifest" schema) || return 1
  path=$(provenance_value "$manifest" app_path) || return 1
  expected_tree=$(provenance_value "$manifest" bundle_tree_sha256) || return 1
  expected_id=$(provenance_value "$manifest" bundle_id) || return 1
  expected_req_hash=$(provenance_value "$manifest" designated_requirement_sha256) || return 1
  [[ "$schema" == seethis-owned-artifacts-v1 ]] || return 1
  [[ "$expected_tree" =~ ^[0-9a-f]{64}$ ]] || return 1
  [[ "$expected_req_hash" =~ ^[0-9a-f]{64}$ ]] || return 1
  [[ -n "$expected_id" ]] || return 1
  [[ -d "$app" ]] || return 1
  [[ "$(real "$path")" == "$app" ]] || return 1
  strict_verify "$app" || return 1
  validate_signed_provenance "$app" || return 1
  actual_tree=$(bundle_tree_hash "$app") || return 1
  actual_id=$(bundle_id "$app") || return 1
  actual_req=$(designated_requirement "$app") || return 1
  actual_req_hash=$(sha256_text "$actual_req") || return 1
  [[ "$actual_tree" =~ ^[0-9a-f]{64}$ && -n "$actual_id" && -n "$actual_req" ]] || return 1
  [[ "$actual_tree" == "$expected_tree" ]] || return 1
  [[ "$actual_id" == "$expected_id" ]] || return 1
  [[ "$actual_req_hash" == "$expected_req_hash" ]] || return 1
  return 0
}
is_owned_app() {
  local app=$1 manifest
  shift
  for manifest in "$@"; do owned_manifest_matches "$manifest" "$app" && return 0; done
  return 1
}

historical_audit_evidence() {
  local index=$1 app=$2 expected_index_sha=$3
  [[ -f "$index" && "$expected_index_sha" =~ ^[0-9a-f]{64}$ ]] || return 1
  [[ "$(sha256_file "$index")" == "$expected_index_sha" ]] || return 1
  python3 - "$index" "$app" <<'PY'
import json, sys
try:
    rows = json.load(open(sys.argv[1]))
    if rows.get('schema') != 'seethis_legacy_audit_bundle_proof.v2': raise ValueError()
    if not isinstance(rows.get('derived_from_sha256'), str) or not __import__('re').fullmatch(r'[0-9a-f]{64}', rows['derived_from_sha256']): raise ValueError()
    matches = [r for r in rows['rows'] if r.get('path') == sys.argv[2]]
    if len(matches) != 1: raise ValueError()
    r = matches[0]
    if r.get('exists') is not True or r.get('strict_signature_valid') is not True or r.get('signed_provenance_exists') is not False: raise ValueError()
    if r.get('exact_archive_match') is True and r.get('extracted_signature_valid') is True:
        archive = r['exact_archive_path']
        candidates = [x['sha256'] for x in r.get('artifact_zip_candidates', []) if x['path'] == archive]
        if not candidates and r.get('exact_archive_sha256'): candidates = [r['exact_archive_sha256']]
        if len(candidates) != 1: raise ValueError()
        print('zip\t%s\t%s' % (archive, candidates[0]))
    elif r.get('original_file_seal_verified') is True:
        print('seal\t%s\t%s' % (r['original_file_seal_path'], r['original_file_seal_sha256']))
    elif r.get('original_build_copy_exact') is True:
        print('build_copy\t%s\t%s\t%s\t%s' % (r['original_build_copy_path'], r['original_build_copy_tree_sha256'], r['original_qa_manifest_path'], r['original_qa_manifest_sha256']))
    else: raise ValueError()
except (OSError, KeyError, ValueError, TypeError, StopIteration):
    sys.exit(1)
PY
}
historical_audit_matches() {
  local app=$1 index=$2 expected_index_sha=$3 record kind evidence expected_sha manifest manifest_sha temp extracted path count=0 actual_sha
  [[ -d "$app" && ! -L "$app" ]] || return 1
  strict_verify "$app" || return 1
  record=$(historical_audit_evidence "$index" "$app" "$expected_index_sha") || return 1
  IFS=$'\t' read -r kind evidence expected_sha manifest manifest_sha <<<"$record"
  [[ "$evidence" = /* && -e "$evidence" && ! -L "$evidence" ]] || return 1
  case "$kind" in
    zip)
      [[ "$expected_sha" =~ ^[0-9a-f]{64}$ ]] || return 1
      actual_sha=$(sha256_file "$evidence") || return 1
      [[ "$actual_sha" == "$expected_sha" ]] || return 1
      temp=$(mktemp -d "${TMPDIR:-/tmp}/seethis-audit-verify.XXXXXX") || return 1
      if ! ditto -x -k "$evidence" "$temp"; then rm -rf "$temp"; return 1; fi
      extracted=''
      while IFS= read -r path; do extracted=$path; count=$((count + 1)); done < <(find "$temp" -type d -name '*.app' -prune | LC_ALL=C sort)
      if [[ "$count" != 1 ]] || ! strict_verify "$extracted" ||
         ! compare_complete_bundles "$app" "$extracted" 'historical audit' 'original archive' retained_archive; then
        rm -rf "$temp"; return 1
      fi
      rm -rf "$temp"
      ;;
    seal)
      [[ "$expected_sha" =~ ^[0-9a-f]{64}$ ]] || return 1
      actual_sha=$(sha256_file "$evidence") || return 1
      [[ "$actual_sha" == "$expected_sha" ]] || return 1
      python3 - "$app" "$evidence" <<'PY' || return 1
import hashlib, pathlib, re, sys
app, seal = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
root = app.parent
expected = {}
for line in seal.read_text().splitlines():
    m = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
    if not m: sys.exit(1)
    p = pathlib.PurePosixPath(m[2])
    if p.is_absolute() or '..' in p.parts or p.parts[0] != app.name or p in expected: sys.exit(1)
    expected[p] = m[1]
actual = {p.relative_to(root) for p in app.rglob('*') if p.is_file() or p.is_symlink()}
if not expected or actual != set(expected) or any((root / p).is_symlink() for p in actual): sys.exit(1)
for p, digest in expected.items():
    if hashlib.sha256((root / p).read_bytes()).hexdigest() != digest: sys.exit(1)
PY
      ;;
    build_copy)
      [[ "$expected_sha" =~ ^[0-9a-f]{64}$ && "$manifest_sha" =~ ^[0-9a-f]{64}$ ]] || return 1
      [[ "$manifest" = /* && -f "$manifest" && ! -L "$manifest" ]] || return 1
      [[ "$(sha256_file "$manifest")" == "$manifest_sha" ]] || return 1
      [[ -d "$evidence" ]] && strict_verify "$evidence" || return 1
      python3 - "$evidence" "$manifest" <<'PY' || return 1
import hashlib, pathlib, re, sys
copy, manifest = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
try:
    prefix = './' + copy.relative_to(manifest.parent).as_posix() + '/'
except ValueError:
    sys.exit(1)
expected = {}
for line in manifest.read_text().splitlines():
    match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
    if not match: sys.exit(1)
    if match[2].startswith(prefix):
        name = match[2][len(prefix):]
        relative = pathlib.PurePosixPath(name)
        if name in expected or relative.is_absolute() or not relative.parts or '..' in relative.parts: sys.exit(1)
        expected[name] = match[1]
actual = {p.relative_to(copy).as_posix() for p in copy.rglob('*') if p.is_file() or p.is_symlink()}
if not expected or actual != set(expected) or any((copy / p).is_symlink() for p in actual): sys.exit(1)
for name, digest in expected.items():
    if hashlib.sha256((copy / name).read_bytes()).hexdigest() != digest: sys.exit(1)
PY
      actual_sha=$(bundle_tree_hash "$evidence") || return 1
      [[ "$actual_sha" == "$expected_sha" ]] || return 1
      compare_complete_bundles "$app" "$evidence" 'historical audit' 'original build copy' retained_archive || return 1
      ;;
    *) return 1 ;;
  esac
}

identity_continuity_note=''
check_prior_identity() {
  local prior=$1 candidate=$2 candidate_req=$3 label=$4
  local prior_req prior_mode candidate_mode prior_identity candidate_identity prior_cd candidate_cd
  strict_verify "$prior" || die "$label signature invalid"
  validate_signed_provenance "$prior" || die "$label signed provenance invalid"
  prior_mode=$(provenance_value "$(provenance_file "$prior")" mode)
  candidate_mode=$(provenance_value "$(provenance_file "$candidate")" mode)
  [[ "$prior_mode" == "$candidate_mode" ]] || die "$label delivery mode is incompatible"
  prior_identity=$(provenance_value "$(provenance_file "$prior")" signing_identity_sha1)
  candidate_identity=$(provenance_value "$(provenance_file "$candidate")" signing_identity_sha1)
  prior_req=$(designated_requirement "$prior") || die "$label designated requirement is unavailable"
  prior_cd=$(cdhash "$prior") || die "$label CDHash is unavailable"
  candidate_cd=$(cdhash "$candidate") || die 'candidate CDHash is unavailable'
  if [[ "$candidate_mode" == stable ]]; then
    [[ "$prior_identity" == "$candidate_identity" && "$prior_req" == "$candidate_req" ]] ||
      die "$label designated requirement is incompatible"
  elif [[ "$prior_req" != "$candidate_req" || "$prior_cd" != "$candidate_cd" ]]; then
    identity_continuity_note='development ad hoc identity changed; no TCC grant continuity is claimed; exact-path reauthorization may be required'
  fi
}

preflight_action() {
  local app='' canonical_target='' audit_index='' audit_index_sha='' id requirement cd candidate_exec candidate_name candidate_hash
  local -a owned_manifests=()
  local owned_count=0
  while (($#)); do
    case "$1" in
      --app) app=$2; shift 2 ;;
      --canonical-target) canonical_target=$2; shift 2 ;;
      --owned-manifest) owned_manifests+=("$2"); owned_count=$((owned_count + 1)); shift 2 ;;
      --historical-audit-index) audit_index=$2; shift 2 ;;
      --historical-audit-index-sha256) audit_index_sha=$2; shift 2 ;;
      *) die "unknown preflight option: $1" ;;
    esac
  done
  [[ -z "$audit_index" && -z "$audit_index_sha" || -n "$audit_index" && "$audit_index_sha" =~ ^[0-9a-f]{64}$ ]] ||
    die 'historical audit index requires its exact expected SHA-256'
  if [[ -n "$audit_index" ]]; then
    [[ -f "$audit_index" && "$(sha256_file "$audit_index")" == "$audit_index_sha" ]] ||
      die 'historical audit index does not match the expected SHA-256'
  fi
  [[ -n "$app" && -d "$app" ]] || die '--app must name a bundle'
  app=$(real "$app")
  [[ -f "$app/Contents/Info.plist" ]] || die 'candidate bundle lacks Info.plist'
  strict_verify "$app" || die 'candidate signature invalid'
  validate_signed_provenance "$app" || die 'candidate signed provenance invalid'
  id=$(bundle_id "$app")
  [[ -n "$id" ]] || die 'candidate bundle identifier is empty'
  candidate_exec=$(executable "$app")
  candidate_name=$(basename "$candidate_exec")
  requirement=$(designated_requirement "$app") || die 'candidate designated requirement is unavailable'
  cd=$(cdhash "$app") || die 'candidate CDHash is unavailable'
  candidate_hash=$(sha256_file "$candidate_exec") || die 'candidate executable hash is unavailable'
  [[ -z "$canonical_target" || ! -L "$canonical_target" ]] || die 'canonical target must not be a symbolic link'
  [[ -z "$canonical_target" ]] || canonical_target=$(real "$canonical_target")
  if [[ -n "$canonical_target" && -e "$canonical_target" && "$canonical_target" != "$app" ]]; then
    [[ -d "$canonical_target" ]] || die 'canonical target is not an app bundle'
    local canonical_id
    canonical_id=$(bundle_id "$canonical_target")
    [[ -n "$canonical_id" && "$canonical_id" == "$id" ]] || die 'canonical target bundle identifier is incompatible'
    check_prior_identity "$canonical_target" "$app" "$requirement" 'canonical target'
  fi

  local registered='' missing_registered='' missing_registered_hash='' known_audits='' raw_registered path
  raw_registered=$(registered_apps "$id")
  while IFS= read -r path; do
    [[ -n "$path" ]] || continue
    [[ ! -L "$path" ]] || die "ambiguous registered same-bundle path: $path"
    path=$(real "$path")
    case $'\n'"$registered"$'\n' in *$'\n'"$path"$'\n'*) continue ;; esac
    registered=${registered:+$registered$'\n'}$path
    if [[ ! -e "$path" ]]; then
      [[ ! -L "$path" ]] || die "ambiguous registered same-bundle path: $path"
      missing_registered=${missing_registered:+$missing_registered$'\n'}$path
      continue
    fi
    [[ "$path" == "$app" ]] && continue
    if [[ -n "$canonical_target" && "$path" == "$canonical_target" ]]; then
      [[ -d "$path" ]] || die 'registered canonical target is missing'
      local registered_id
      registered_id=$(bundle_id "$path")
      [[ -n "$registered_id" && "$registered_id" == "$id" ]] || die 'registered canonical target bundle identifier changed'
      check_prior_identity "$path" "$app" "$requirement" 'registered canonical target'
      continue
    fi
    if ((owned_count > 0)) && is_owned_app "$path" "${owned_manifests[@]}"; then continue; fi
    if [[ -n "$audit_index" ]] && historical_audit_matches "$path" "$audit_index" "$audit_index_sha"; then
      known_audits=${known_audits:+$known_audits$'\n'}$path
      continue
    fi
    die "unresolved registered same-bundle app: $path"
  done <<<"$raw_registered"

  local process_output='' process_snapshot pid observed raw_observed live app_root live_id alias_proven proof_raw
  capture_process_snapshot process_snapshot 1
  while IFS=$'\t' read -r pid observed raw_observed; do
    [[ -n "$pid" && -n "$observed" ]] || continue
    if [[ "$observed" == */Contents/MacOS/* ]]; then
      app_root=${observed%/Contents/MacOS/*}
      alias_proven=0; proof_raw=$raw_observed
      if [[ "$app_root" == *.app && "$raw_observed" != "$observed" ]]; then
        verified_process_alias "$pid" "$raw_observed" "$observed" || die "ambiguous process evidence for PID $pid"
        alias_proven=1; proof_raw=$observed
      fi
      if unrelated_app_snapshot "$observed" "$proof_raw" "$id" "$app" "$candidate_exec"; then
        if live=$(live_process_path "$pid"); then
          [[ "$live" == "$observed" ]] || die "ambiguous process evidence for PID $pid"
        else
          ((alias_proven == 0)) || die "stale or unverifiable process evidence for PID $pid"
          pid_absent_from_complete_snapshot "$pid" || die "stale or unverifiable process evidence for PID $pid"
        fi
        if ((alias_proven)); then
          [[ -e "$raw_observed" && "$(real "$raw_observed")" == "$observed" ]] || die "ambiguous process evidence for PID $pid"
        fi
        continue
      fi
      live=$(live_process_path "$pid") || die "stale or unverifiable process evidence for PID $pid"
      [[ "$live" == "$observed" ]] || die "ambiguous process evidence for PID $pid"
      if ((alias_proven)); then
        [[ -e "$raw_observed" && "$(real "$raw_observed")" == "$observed" ]] || die "ambiguous process evidence for PID $pid"
      fi
      [[ "$live" = /* ]] || die "ambiguous process executable path for PID $pid"
      app_root=${live%/Contents/MacOS/*}
      [[ "$app_root" != "$live" && -d "$app_root" ]] || die "stale or unresolved app process evidence for PID $pid"
      [[ "$app_root" != "$app" && "$live" != "$candidate_exec" ]] || die 'canonical target is running'
      live_id=$(bundle_id "$app_root") || die "unverifiable different-bundle app process: $app_root"
      if [[ "$app_root" != *.app && -f "$app_root/Contents/Info.plist" &&
            -r "$app_root/Contents/Info.plist" && -n "$live_id" && "$live_id" != "$id" ]]; then
        continue
      fi
      [[ "$live_id" == "$id" ]] || die "unverifiable different-bundle app process: $app_root"
      process_output=${process_output:+$process_output,}$pid:$live
      die "unresolved running same-bundle app: $app_root"
    elif [[ "$observed" == "$candidate_name" ]]; then
      live=$(live_process_path "$pid") || die "stale or unverifiable process evidence for PID $pid"
      [[ "$live" == "$observed" ]] || die "ambiguous process evidence for PID $pid"
      die "ambiguous process executable path for PID $pid"
    fi
  done <<<"$process_snapshot"

  while IFS= read -r path; do
    [[ -n "$path" ]] || continue
    [[ ! -e "$path" && ! -L "$path" ]] || die "missing registration reappeared: $path"
  done <<<"$missing_registered"
  missing_registered_hash=$(sha256_text "$missing_registered") || die 'missing registration set hash is unavailable'

  note "canonical_path=$app"
  note "bundle_id=$id"
  note "executable=$candidate_exec"
  note "candidate_executable_sha256=$candidate_hash"
  note "candidate_cdhash=$cd"
  note "candidate_designated_requirement=$requirement"
  note "continuity_note=${identity_continuity_note:-identity continuity is not established by this preflight}"
  note "registered_same_bundle_apps=$(printf '%s' "$registered" | paste -sd, -)"
  note "missing_registered_same_bundle_apps=$(printf '%s' "$missing_registered" | paste -sd, -)"
  note "missing_registered_set_sha256=$missing_registered_hash"
  note "known_historical_audit_apps=$(printf '%s' "$known_audits" | paste -sd, -)"
  note "running_same_bundle_processes=$process_output"
  note 'status=PASS'
  note 'policy=read-only; exact canonical targets, signed owned artifacts and original-evidence-verified historical audits only are classified as known'
}

discover_built_app() {
  local build=$1 exact="$build/seethis.app" found='' count=0 path
  if [[ -d "$exact" ]]; then printf '%s' "$exact"; return; fi
  while IFS= read -r path; do found=$path; count=$((count + 1)); done < <(find "$build" -type d -name '*.app' -prune | LC_ALL=C sort)
  [[ "$count" == 1 ]] || die 'clean build must produce exactly one app bundle'
  printf '%s' "$found"
}

validate_prior_stable_app() {
  local prior=$1 selected_sha=$2 id_var=$3 req_var=$4 tree_var=$5 cd_var=$6 commit_var=$7
  local value_id value_req value_tree value_cd prior_mode prior_identity value_commit
  [[ -d "$prior" ]] || die 'stable update requires --prior-app naming an accepted prior stable app'
  prior=$(real "$prior")
  strict_verify "$prior" || die 'prior stable app signature invalid'
  validate_signed_provenance "$prior" || die 'prior stable app lacks valid signed provenance'
  prior_mode=$(provenance_value "$(provenance_file "$prior")" mode)
  [[ "$prior_mode" == stable ]] || die 'prior app is not a stable package'
  prior_identity=$(provenance_value "$(provenance_file "$prior")" signing_identity_sha1)
  [[ "$prior_identity" == "$selected_sha" ]] || die 'prior stable signing identity is incompatible'
  value_id=$(bundle_id "$prior")
  value_req=$(designated_requirement "$prior") || die 'prior stable app designated requirement is unavailable'
  value_tree=$(bundle_tree_hash "$prior") || die 'prior stable app tree hash is unavailable'
  value_cd=$(cdhash "$prior") || die 'prior stable app CDHash is unavailable'
  value_commit=$(provenance_value "$(provenance_file "$prior")" source_commit) || die 'prior stable app source provenance is unavailable'
  [[ -n "$value_id" && -n "$value_req" && -n "$value_cd" ]] || die 'prior stable app signing evidence is incomplete'
  printf -v "$id_var" '%s' "$value_id"
  printf -v "$req_var" '%s' "$value_req"
  printf -v "$tree_var" '%s' "$value_tree"
  printf -v "$cd_var" '%s' "$value_cd"
  printf -v "$commit_var" '%s' "$value_commit"
}

bundle_measurement_error=''
bundle_comparison_error=''
bundle_comparison_left_tree=''
bundle_comparison_left_id=''
bundle_comparison_left_name=''
bundle_comparison_left_requirement=''
bundle_comparison_left_executable_hash=''
bundle_comparison_left_cdhash=''
measure_bundle() {
  local app=$1 prefix=$2 label=$3 value name path
  bundle_measurement_error=''
  value=$(bundle_tree_hash "$app") || {
    bundle_measurement_error="$label tree hash is unavailable or invalid"
    return 1
  }
  [[ "$value" =~ ^[0-9a-f]{64}$ ]] || {
    bundle_measurement_error="$label tree hash is unavailable or invalid"
    return 1
  }
  printf -v "${prefix}_tree" '%s' "$value"

  value=$(bundle_id "$app") || {
    bundle_measurement_error="$label bundle identifier is unavailable or invalid"
    return 1
  }
  [[ "$value" =~ ^[A-Za-z0-9][A-Za-z0-9.-]*$ ]] || {
    bundle_measurement_error="$label bundle identifier is unavailable or invalid"
    return 1
  }
  printf -v "${prefix}_id" '%s' "$value"

  name=$(executable_name "$app") || {
    bundle_measurement_error="$label executable name is unavailable or invalid"
    return 1
  }
  [[ -n "$name" && "$name" != */* && "$name" != *$'\n'* ]] || {
    bundle_measurement_error="$label executable name is unavailable or invalid"
    return 1
  }
  printf -v "${prefix}_name" '%s' "$name"
  path="$app/Contents/MacOS/$name"
  [[ -f "$path" ]] || {
    bundle_measurement_error="$label executable is unavailable"
    return 1
  }
  value=$(sha256_file "$path") || {
    bundle_measurement_error="$label executable hash is unavailable or invalid"
    return 1
  }
  [[ "$value" =~ ^[0-9a-f]{64}$ ]] || {
    bundle_measurement_error="$label executable hash is unavailable or invalid"
    return 1
  }
  printf -v "${prefix}_executable_hash" '%s' "$value"

  value=$(designated_requirement "$app") || {
    bundle_measurement_error="$label designated requirement is unavailable or invalid"
    return 1
  }
  [[ -n "$value" && "$value" != *$'\n'* ]] || {
    bundle_measurement_error="$label designated requirement is unavailable or invalid"
    return 1
  }
  printf -v "${prefix}_requirement" '%s' "$value"

  value=$(cdhash "$app") || {
    bundle_measurement_error="$label CDHash is unavailable or invalid"
    return 1
  }
  [[ "$value" =~ ^[0-9A-Fa-f]{40,64}$ ]] || {
    bundle_measurement_error="$label CDHash is unavailable or invalid"
    return 1
  }
  printf -v "${prefix}_cdhash" '%s' "$value"
}

compare_complete_bundles() {
  local left=$1 right=$2 left_label=$3 right_label=$4 context=$5
  local left_tree='' left_id='' left_name='' left_executable_hash='' left_requirement='' left_cdhash=''
  local right_tree='' right_id='' right_name='' right_executable_hash='' right_requirement='' right_cdhash=''
  local left_error='' right_error='' comparison_label content_verb
  bundle_comparison_error=''
  if ! measure_bundle "$left" left "$left_label"; then left_error=$bundle_measurement_error; fi
  if ! measure_bundle "$right" right "$right_label"; then right_error=$bundle_measurement_error; fi
  if [[ -n "$left_error" || -n "$right_error" ]]; then
    bundle_comparison_error=$left_error
    [[ -z "$right_error" ]] || bundle_comparison_error=${bundle_comparison_error:+$bundle_comparison_error'; '}$right_error
    return 1
  fi

  case "$context" in
    build_retained) comparison_label='build and retained bundle'; content_verb=differ ;;
    retained_archive) comparison_label='retained and archive bundle'; content_verb=differ ;;
    staged_candidate) comparison_label='staged candidate'; content_verb=differs ;;
    installed_candidate) comparison_label='installed candidate'; content_verb=differs ;;
    *) bundle_comparison_error='internal bundle-comparison context is invalid'; return 1 ;;
  esac
  [[ "$left_tree" == "$right_tree" ]] || bundle_comparison_error="$comparison_label content $content_verb"
  [[ -n "$bundle_comparison_error" || "$left_id" == "$right_id" ]] || bundle_comparison_error="$comparison_label identifiers differ"
  [[ -n "$bundle_comparison_error" || "$left_name" == "$right_name" ]] || bundle_comparison_error="$comparison_label executable names differ"
  [[ -n "$bundle_comparison_error" || "$left_executable_hash" == "$right_executable_hash" ]] || bundle_comparison_error="$comparison_label executable bytes differ"
  [[ -n "$bundle_comparison_error" || "$left_requirement" == "$right_requirement" ]] || bundle_comparison_error="$comparison_label designated requirements differ"
  [[ -n "$bundle_comparison_error" || "$left_cdhash" == "$right_cdhash" ]] || bundle_comparison_error="$comparison_label CDHashes differ"
  [[ -z "$bundle_comparison_error" ]] || return 1
  bundle_comparison_left_tree=$left_tree
  bundle_comparison_left_id=$left_id
  bundle_comparison_left_name=$left_name
  bundle_comparison_left_requirement=$left_requirement
  bundle_comparison_left_executable_hash=$left_executable_hash
  bundle_comparison_left_cdhash=$left_cdhash
}

verify_archive_against_retained() {
  local retained=$1 archive=$2 extract_root extracted='' path count=0
  extract_root=$(mktemp -d "${TMPDIR:-/tmp}/seethis-archive-verify.XXXXXX")
  ditto -x -k "$archive" "$extract_root" || { rm -rf "$extract_root"; die 'archive extraction failed'; }
  while IFS= read -r path; do extracted=$path; count=$((count + 1)); done < <(find "$extract_root" -type d -name '*.app' -prune | LC_ALL=C sort)
  [[ "$count" == 1 ]] || { rm -rf "$extract_root"; die 'archive must contain exactly one app bundle'; }
  strict_verify "$extracted" || { rm -rf "$extract_root"; die 'archive-extracted bundle failed strict signature verification'; }
  if ! compare_complete_bundles "$retained" "$extracted" 'retained bundle' 'archive-extracted bundle' retained_archive; then
    local comparison_error=$bundle_comparison_error
    rm -rf "$extract_root"
    die "$comparison_error"
  fi
  rm -rf "$extract_root"
}

package_action() {
  local source='.' requested_commit='' output='' mode=development identity='' stable_action='' prior_app='' configuration=Debug
  while (($#)); do
    case "$1" in
      --source) source=$2; shift 2 ;;
      --commit) requested_commit=$2; shift 2 ;;
      --output) output=$2; shift 2 ;;
      --mode) mode=$2; shift 2 ;;
      --configuration) configuration=$2; shift 2 ;;
      --identity) identity=$2; shift 2 ;;
      --stable-action) stable_action=$2; shift 2 ;;
      --prior-app) prior_app=$2; shift 2 ;;
      -h|--help) usage; return ;;
      *) die "unknown package option: $1" ;;
    esac
  done
  [[ "$mode" == development || "$mode" == stable ]] || die 'mode must be development or stable'
  [[ "$configuration" == Debug || "$configuration" == Release ]] || die 'configuration must be Debug or Release'
  source=$(real "$source")
  git -C "$source" rev-parse --is-inside-work-tree >/dev/null 2>&1 || die 'source is not a git checkout'
  [[ -z "$(git -C "$source" status --porcelain)" ]] || die 'source checkout is dirty'
  local commit tree
  if [[ -n "$requested_commit" ]]; then
    commit=$(git -C "$source" rev-parse --verify "$requested_commit^{commit}" 2>/dev/null) || die 'requested source commit is invalid'
    [[ "$commit" == "$(git -C "$source" rev-parse HEAD)" ]] || die 'requested source commit is not the clean checkout HEAD'
  else
    commit=$(git -C "$source" rev-parse HEAD)
  fi
  [[ "$commit" =~ ^[0-9a-f]{40}$ ]] || die 'source commit is not a full Git object identity'
  tree=$(git -C "$source" rev-parse "$commit^{tree}")

  local identity_sha=adhoc prior_id='' prior_req='' prior_tree='' prior_cd='' prior_commit=''
  if [[ "$mode" == stable ]]; then
    [[ -n "$identity" ]] || die 'stable mode requires --identity'
    [[ "$stable_action" == bootstrap || "$stable_action" == update ]] || die 'stable mode requires --stable-action bootstrap or update'
    identity_sha=$(resolve_identity_sha1 "$identity")
    if [[ "$stable_action" == bootstrap ]]; then
      [[ -z "$prior_app" ]] || die 'stable bootstrap must not supply --prior-app'
    else
      [[ -n "$prior_app" ]] || die 'stable update requires --prior-app naming an accepted prior stable app'
      validate_prior_stable_app "$prior_app" "$identity_sha" prior_id prior_req prior_tree prior_cd prior_commit
    fi
  else
    [[ -z "$identity" && -z "$stable_action" && -z "$prior_app" ]] || die 'development mode does not accept stable identity or update options'
  fi

  [[ -n "$output" ]] || output="$PWD/.seethis-delivery"
  [[ ! -e "$output" ]] || die 'output already exists; choose a new task-owned output directory'
  mkdir -p "$output"
  output=$(real "$output")
  local work source_archive clean_source build bundle app archive cmake_path compiler_path cmake_version compiler_version
  local source_archive_sha cmake_version_hash compiler_version_hash prior_req_hash requirement_hash archive_hash
  local entitlements_sha sdk_path sdk_version developer_dir preset architectures minimum_version bundle_version bundle_build icon_file icon_sha
  work=$(mktemp -d "$output/work.XXXXXX")
  source_archive="$work/source.tar"
  clean_source="$work/source"
  build="$work/build"
  mkdir -p "$clean_source"
  git -C "$source" archive --format=tar --output="$source_archive" "$commit"
  source_archive_sha=$(sha256_file "$source_archive") || die 'source archive hash is unavailable'
  tar -xf "$source_archive" -C "$clean_source"
  entitlements_sha=$(sha256_file "$clean_source/src/platform/mac/seethis.entitlements") || die 'Apple Events entitlement source is unavailable'
  cmake_path=$(command -v cmake) || die 'cmake is required'
  compiler_path=$(xcrun --sdk macosx --find clang++) || die 'selected Apple C++ compiler is unavailable'
  sdk_path=$(xcrun --sdk macosx --show-sdk-path) || die 'selected macOS SDK is unavailable'
  sdk_version=$(xcrun --sdk macosx --show-sdk-version) || die 'selected macOS SDK version is unavailable'
  developer_dir=${DEVELOPER_DIR:-$(xcode-select -p)} || die 'selected developer directory is unavailable'
  [[ -x "$compiler_path" && -d "$sdk_path" ]] || die 'selected Apple toolchain paths are invalid'
  cmake_version=$("$cmake_path" --version | head -1)
  compiler_version=$("$compiler_path" --version | head -1)
  cmake_version_hash=$(sha256_text "$cmake_version") || die 'CMake version hash is unavailable'
  compiler_version_hash=$(sha256_text "$compiler_version") || die 'compiler version hash is unavailable'
  prior_req_hash=$(sha256_text "$prior_req") || die 'prior designated-requirement hash is unavailable'
  preset=macos-clang
  [[ "$configuration" != Release ]] || preset=macos-release
  "$cmake_path" --preset "$preset" -S "$clean_source" -B "$build" \
    -DCMAKE_BUILD_TYPE="$configuration" -DCMAKE_CXX_COMPILER="$compiler_path" \
    -DCMAKE_OBJCXX_COMPILER="$compiler_path" -DCMAKE_OSX_SYSROOT="$sdk_path" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.2
  "$cmake_path" --build "$build" --config "$configuration"
  bundle=$(discover_built_app "$build")
  [[ -f "$bundle/Contents/Info.plist" ]] || die 'clean build did not produce a bundle Info.plist'
  local built_id built_executable
  built_id=$(bundle_id "$bundle")
  built_executable=$(executable_name "$bundle")
  [[ -n "$built_id" ]] || die 'bundle identifier is empty'
  [[ -n "$built_executable" && "$built_executable" != */* ]] || die 'bundle has an invalid CFBundleExecutable'
  [[ -f "$bundle/Contents/MacOS/$built_executable" ]] || die 'bundle executable declared by Info.plist is missing'
  architectures=$(xcrun lipo -archs "$bundle/Contents/MacOS/$built_executable") || die 'built executable architecture is unavailable'
  [[ "$architectures" == arm64 ]] || die 'built executable must contain arm64 only'
  minimum_version=$(xcrun otool -l "$bundle/Contents/MacOS/$built_executable" | awk '
    $1 == "cmd" { build_version = ($2 == "LC_BUILD_VERSION") }
    build_version && $1 == "minos" { print $2 }') || die 'built executable deployment target is unavailable'
  [[ "$minimum_version" == 14.2 ]] || die 'built executable minimum macOS version must be 14.2'
  [[ "$(plist "$bundle" LSMinimumSystemVersion)" == "$minimum_version" ]] || die 'bundle and executable minimum macOS versions differ'
  bundle_version=$(plist "$bundle" CFBundleShortVersionString) || die 'bundle version is unavailable'
  bundle_build=$(plist "$bundle" CFBundleVersion) || die 'bundle build version is unavailable'
  icon_file=$(plist "$bundle" CFBundleIconFile) || die 'bundle icon declaration is unavailable'
  [[ "$icon_file" == SeeThis.icns ]] || die 'bundle icon must be SeeThis.icns'
  icon_sha=$(sha256_file "$bundle/Contents/Resources/$icon_file") || die 'bundle icon resource is unavailable'
  [[ "$icon_sha" == "$(sha256_file "$clean_source/assets/app-icon/SeeThis.icns")" ]] || die 'bundle icon differs from the exact source icon'
  if [[ "$mode" == stable && "$stable_action" == update ]]; then
    [[ "$built_id" == "$prior_id" ]] || die 'stable update bundle identifier differs from prior stable app'
  fi

  mkdir -p "$bundle/Contents/Resources"
  local provenance
  provenance=$(provenance_file "$bundle")
  {
    printf 'schema=seethis-delivery-provenance-v1\n'
    printf 'source_commit=%s\nsource_tree=%s\nsource_archive_sha256=%s\n' "$commit" "$tree" "$source_archive_sha"
    printf 'cmake_path=%s\ncmake_version_sha256=%s\n' "$cmake_path" "$cmake_version_hash"
    printf 'compiler_path=%s\ncompiler_version_sha256=%s\n' "$compiler_path" "$compiler_version_hash"
    printf 'build_configuration=%s\nconfigure_preset=%s\narchitectures=%s\nminimum_macos=%s\n' "$configuration" "$preset" "$architectures" "$minimum_version"
    printf 'cmake_version=%s\ncompiler_version=%s\n' "$cmake_version" "$compiler_version"
    printf 'developer_dir=%s\nsdk_path=%s\nsdk_version=%s\n' "$developer_dir" "$sdk_path" "$sdk_version"
    printf 'bundle_version=%s\nbundle_build=%s\nicon_file=%s\nicon_sha256=%s\n' "$bundle_version" "$bundle_build" "$icon_file" "$icon_sha"
    printf 'notarized=false\n'
    [[ "$mode" != development ]] || printf 'developer_id_signed=false\nsignature_kind=adhoc\n'
    printf 'mode=%s\nstable_action=%s\nsigning_identity_sha1=%s\n' "$mode" "${stable_action:-none}" "$identity_sha"
    printf 'bundle_id=%s\nexecutable=%s\n' "$built_id" "$built_executable"
    printf 'prior_source_commit=%s\nprior_bundle_tree_sha256=%s\nprior_cdhash=%s\nprior_designated_requirement_sha256=%s\n' \
      "$prior_commit" "$prior_tree" "$prior_cd" "$prior_req_hash"
  } >"$provenance"
  if [[ "$mode" == stable ]]; then
    codesign --force --deep --options runtime --entitlements "$clean_source/src/platform/mac/seethis.entitlements" --sign "$identity_sha" "$bundle"
  else
    codesign --force --deep --entitlements "$clean_source/src/platform/mac/seethis.entitlements" --sign - "$bundle"
  fi
  strict_verify "$bundle" || die 'freshly signed build failed strict signature verification'
  validate_signed_provenance "$bundle" || die 'freshly signed build provenance validation failed'
  local requirement current_cd
  requirement=$(designated_requirement "$bundle") || die 'freshly signed build designated requirement is unavailable'
  current_cd=$(cdhash "$bundle") || die 'freshly signed build CDHash is unavailable'
  requirement_hash=$(sha256_text "$requirement") || die 'designated-requirement hash is unavailable'
  if [[ "$mode" == stable && "$stable_action" == update ]]; then
    [[ "$requirement" == "$prior_req" ]] || die 'stable update designated requirement is incompatible with prior stable app'
  fi

  app="$output/SeeThis.app"
  cp -a "$bundle" "$app"
  strict_verify "$app" || die 'retained bundle failed strict signature verification'
  compare_complete_bundles "$bundle" "$app" 'fresh build bundle' 'retained bundle' build_retained || die "$bundle_comparison_error"
  archive="$output/SeeThis.zip"
  [[ "$configuration" != Release ]] || archive="$output/SeeThis.app.zip"
  ditto -c -k --sequesterRsrc --keepParent "$app" "$archive"
  verify_archive_against_retained "$app" "$archive"
  local executable_path executable_hash bundle_hash archive_extract archive_tree note_text extracted_app
  executable_path=$(executable "$app")
  executable_hash=$(sha256_file "$executable_path") || die 'retained executable hash is unavailable'
  bundle_hash=$(bundle_tree_hash "$app") || die 'retained bundle tree hash is unavailable'
  archive_extract=$(mktemp -d "$output/archive-check.XXXXXX")
  ditto -x -k "$archive" "$archive_extract"
  extracted_app=$(find "$archive_extract" -type d -name '*.app' -prune -print -quit)
  archive_tree=$(bundle_tree_hash "$extracted_app") || die 'archive bundle tree hash is unavailable'
  archive_hash=$(sha256_file "$archive") || die 'archive hash is unavailable'
  printf '%s  %s\n' "$archive_hash" "${archive##*/}" >"$output/SHA256SUMS"
  rm -rf "$archive_extract"
  case "$mode:$stable_action" in
    development:*) note_text='development identity may change on rebuild; exact-bundle authorization may be required' ;;
    stable:bootstrap) note_text='no inherited TCC continuity is claimed; field authorization may be required' ;;
    stable:update) note_text='stable identity compatibility verified against prior signed artifact; real TCC continuity remains unproven' ;;
  esac
  {
    printf 'schema=seethis-delivery-evidence-v1\n'
    printf 'build_configuration=%s\nconfigure_preset=%s\narchitectures=%s\nminimum_macos=%s\n' "$configuration" "$preset" "$architectures" "$minimum_version"
    printf 'cmake_version=%s\ncompiler_version=%s\n' "$cmake_version" "$compiler_version"
    printf 'developer_dir=%s\nsdk_path=%s\nsdk_version=%s\n' "$developer_dir" "$sdk_path" "$sdk_version"
    printf 'bundle_version=%s\nbundle_build=%s\nicon_file=%s\nicon_sha256=%s\n' "$bundle_version" "$bundle_build" "$icon_file" "$icon_sha"
    printf 'notarized=false\n'
    [[ "$mode" != development ]] || printf 'developer_id_signed=false\nsignature_kind=adhoc\n'
    printf 'commit=%s\nsource_tree=%s\nsource_archive_sha256=%s\n' "$commit" "$tree" "$source_archive_sha"
    printf 'mode=%s\nstable_action=%s\nidentity_sha1=%s\n' "$mode" "${stable_action:-none}" "$identity_sha"
    printf 'continuity_note=%s\n' "$note_text"
    printf 'NSAppleEventsUsageDescription=present\napple_events_entitlement=com.apple.security.automation.apple-events\napple_events_entitlements_source_sha256=%s\n' "$entitlements_sha"
    printf 'bundle_id=%s\nCFBundleExecutable=%s\n' "$built_id" "$built_executable"
    printf 'cmake_path=%s\ncmake_version_sha256=%s\ncompiler_path=%s\ncompiler_version_sha256=%s\n' \
      "$cmake_path" "$cmake_version_hash" "$compiler_path" "$compiler_version_hash"
    printf 'executable_sha256=%s\nbundle_tree_sha256=%s\narchive_bundle_tree_sha256=%s\narchive_sha256=%s\n' \
      "$executable_hash" "$bundle_hash" "$archive_tree" "$archive_hash"
    printf 'cdhash=%s\ndesignated_requirement=%s\n' "$current_cd" "$requirement"
    printf 'prior_source_commit=%s\nprior_bundle_tree_sha256=%s\nprior_cdhash=%s\nprior_designated_requirement_sha256=%s\n' \
      "$prior_commit" "$prior_tree" "$prior_cd" "$prior_req_hash"
  } >"$output/evidence.txt"
  {
    printf 'schema=seethis-owned-artifacts-v1\n'
    printf 'app_path=%s\n' "$(real "$app")"
    printf 'bundle_tree_sha256=%s\nbundle_id=%s\ndesignated_requirement_sha256=%s\n' \
      "$bundle_hash" "$built_id" "$requirement_hash"
  } >"$output/owned-artifacts.txt"
  rm -rf "$work"
  note "PACKAGED app=$(real "$app") zip=$(real "$archive") evidence=$(real "$output/evidence.txt") owned_manifest=$(real "$output/owned-artifacts.txt")"
}

install_action() {
  local app='' destination='' audit_index='' audit_index_sha='' ack=0 manifest
  local -a owned_manifests=()
  local owned_count=0
  while (($#)); do
    case "$1" in
      --app) app=$2; shift 2 ;;
      --destination) destination=$2; shift 2 ;;
      --acknowledge) ack=1; shift ;;
      --owned-manifest) owned_manifests+=("$2"); owned_count=$((owned_count + 1)); shift 2 ;;
      --historical-audit-index) audit_index=$2; shift 2 ;;
      --historical-audit-index-sha256) audit_index_sha=$2; shift 2 ;;
      *) die "unknown install option: $1" ;;
    esac
  done
  ((ack)) || die 'install requires --acknowledge'
  [[ -n "$app" && -d "$app" && -n "$destination" ]] || die 'install requires --app and --destination'
  app=$(real "$app")
  strict_verify "$app" || die 'candidate signature invalid'
  [[ -d "$destination" ]] || mkdir -p "$destination"
  destination=$(real "$destination")
  local target="$destination/$(basename "$app")" lsreg
  [[ ! -L "$target" ]] || die 'canonical target must not be a symbolic link'
  lsreg=$(lsregister_command) || die 'Launch Services registry is unavailable'
  local -a preflight_args=(--app "$app" --canonical-target "$target")
  [[ -z "$audit_index" && -z "$audit_index_sha" ]] ||
    preflight_args+=(--historical-audit-index "$audit_index" --historical-audit-index-sha256 "$audit_index_sha")
  if ((owned_count > 0)); then
    for manifest in "${owned_manifests[@]}"; do preflight_args+=(--owned-manifest "$manifest"); done
  fi
  local preflight_report continuity_report missing_hash_before missing_hash_after
  preflight_report=$(preflight_action "${preflight_args[@]}")
  continuity_report=$(printf '%s\n' "$preflight_report" | sed -n 's/^continuity_note=//p')
  missing_hash_before=$(printf '%s\n' "$preflight_report" | sed -n 's/^missing_registered_set_sha256=//p')
  [[ "$missing_hash_before" =~ ^[0-9a-f]{64}$ ]] || die 'initial missing registration set is unavailable'
  local target_existed_before=0 target_inode_before='' target_tree_before=''
  if [[ -e "$target" ]]; then
    target_existed_before=1
    target_inode_before=$(stat -f '%d:%i' "$target") || die 'canonical target identity is unavailable'
    target_tree_before=$(bundle_tree_hash "$target") || die 'canonical target tree hash is unavailable'
  fi

  local stage_root staged candidate_tree candidate_id candidate_name candidate_req candidate_hash candidate_cdhash
  stage_root=$(mktemp -d "$destination/.seethis-install.XXXXXX")
  staged="$stage_root/candidate.app"
  if ! cp -a "$app" "$staged"; then rm -rf "$stage_root"; die 'staging copy failed; canonical target was not changed'; fi
  strict_verify "$staged" || { rm -rf "$stage_root"; die 'staged candidate signature invalid; canonical target was not changed'; }
  if ! compare_complete_bundles "$app" "$staged" 'source candidate' 'staged candidate' staged_candidate; then
    local comparison_error=$bundle_comparison_error
    rm -rf "$stage_root"
    die "$comparison_error; canonical target was not changed"
  fi
  candidate_tree=$bundle_comparison_left_tree
  candidate_id=$bundle_comparison_left_id
  candidate_name=$bundle_comparison_left_name
  candidate_req=$bundle_comparison_left_requirement
  candidate_hash=$bundle_comparison_left_executable_hash
  candidate_cdhash=$bundle_comparison_left_cdhash

  if ((target_existed_before)); then
    [[ -d "$target" && ! -L "$target" ]] || { rm -rf "$stage_root"; die 'canonical target changed during staging'; }
    [[ "$(stat -f '%d:%i' "$target")" == "$target_inode_before" &&
       "$(bundle_tree_hash "$target")" == "$target_tree_before" ]] ||
      { rm -rf "$stage_root"; die 'canonical target changed during staging'; }
  else
    [[ ! -e "$target" && ! -L "$target" ]] ||
      { rm -rf "$stage_root"; die 'canonical target appeared during staging'; }
  fi
  if ! preflight_report=$(preflight_action "${preflight_args[@]}" 2>&1); then
    rm -rf "$stage_root"
    printf '%s\n' "$preflight_report" >&2
    exit 2
  fi
  missing_hash_after=$(printf '%s\n' "$preflight_report" | sed -n 's/^missing_registered_set_sha256=//p')
  [[ "$missing_hash_after" == "$missing_hash_before" ]] ||
    { rm -rf "$stage_root"; die 'missing registered app set changed during staging'; }

  local backup="$stage_root/previous.app" displaced="$stage_root/failed-new.app" had_previous=0
  if [[ -e "$target" ]]; then
    had_previous=1
    if ! mv "$target" "$backup"; then rm -rf "$stage_root"; die 'unable to stage previous canonical app; target was not changed'; fi
  fi
  if ! mv "$staged" "$target"; then
    if ((had_previous)) && ! mv "$backup" "$target"; then die "installation swap failed and previous app could not be restored; preserved at $backup"; fi
    rm -rf "$stage_root"
    die 'installation swap failed; previous canonical app was restored'
  fi
  local validation_ok=1 validation_error='installed candidate signature validation failed'
  local installed_tree='' installed_id='' installed_name='' installed_executable_hash='' installed_requirement='' installed_cdhash=''
  strict_verify "$target" || validation_ok=0
  if ((validation_ok)); then
    if ! measure_bundle "$target" installed 'installed candidate'; then
      validation_ok=0
      validation_error=$bundle_measurement_error
    elif [[ "$installed_tree" != "$candidate_tree" ]]; then
      validation_ok=0; validation_error='installed candidate content differs'
    elif [[ "$installed_id" != "$candidate_id" ]]; then
      validation_ok=0; validation_error='installed candidate bundle identifier differs'
    elif [[ "$installed_name" != "$candidate_name" ]]; then
      validation_ok=0; validation_error='installed candidate executable name differs'
    elif [[ "$installed_executable_hash" != "$candidate_hash" ]]; then
      validation_ok=0; validation_error='installed candidate executable bytes differ'
    elif [[ "$installed_requirement" != "$candidate_req" ]]; then
      validation_ok=0; validation_error='installed candidate designated requirement differs'
    elif [[ "$installed_cdhash" != "$candidate_cdhash" ]]; then
      validation_ok=0; validation_error='installed candidate CDHash differs'
    fi
  fi
  if ((validation_ok == 0)); then
    mv "$target" "$displaced" || true
    if ((had_previous)) && ! mv "$backup" "$target"; then die "installed candidate validation failed and previous app could not be restored; preserved at $backup"; fi
    rm -rf "$stage_root"
    die "$validation_error; previous canonical app was restored"
  fi
  if ! "$lsreg" -f "$target" >/dev/null; then
    mv "$target" "$displaced" || true
    if ((had_previous)); then
      if ! mv "$backup" "$target"; then die "Launch Services registration failed and previous app could not be restored; preserved at $backup"; fi
      "$lsreg" -f "$target" >/dev/null 2>&1 || true
    fi
    rm -rf "$stage_root"
    die 'Launch Services registration failed; previous canonical app was restored'
  fi
  rm -rf "$stage_root"
  note "INSTALLED app=$(real "$target") bundle_id=$candidate_id executable_sha256=$candidate_hash designated_requirement=$candidate_req"
  note "continuity_note=$continuity_report"
}

named_processes_from_snapshot() {
  local name=$1 snapshot=$2 pid observed live
  while IFS=$'\t' read -r pid observed; do
    [[ -n "$pid" && -n "$observed" ]] || continue
    if [[ "$observed" == "$name" || "$observed" == */"$name" ]]; then
      live=$(live_process_path "$pid") || die "stale or unverifiable process evidence for PID $pid"
      [[ "$live" == "$observed" ]] || die "ambiguous process evidence for PID $pid"
      printf '%s\t%s\n' "$pid" "$live"
    fi
  done <<<"$snapshot"
}

launch_action() {
  local app='' audit_index='' audit_index_sha='' ack=0 manifest
  local -a owned_manifests=()
  local owned_count=0
  while (($#)); do
    case "$1" in
      --app) app=$2; shift 2 ;;
      --acknowledge) ack=1; shift ;;
      --owned-manifest) owned_manifests+=("$2"); owned_count=$((owned_count + 1)); shift 2 ;;
      --historical-audit-index) audit_index=$2; shift 2 ;;
      --historical-audit-index-sha256) audit_index_sha=$2; shift 2 ;;
      *) die "unknown launch option: $1" ;;
    esac
  done
  ((ack)) || die 'launch requires --acknowledge'
  [[ "$app" = /* && -d "$app" ]] || die 'launch requires an existing full canonical app path'
  app=$(real "$app")
  strict_verify "$app" || die 'candidate signature invalid'
  local -a preflight_args=(--app "$app")
  [[ -z "$audit_index" && -z "$audit_index_sha" ]] ||
    preflight_args+=(--historical-audit-index "$audit_index" --historical-audit-index-sha256 "$audit_index_sha")
  if ((owned_count > 0)); then
    for manifest in "${owned_manifests[@]}"; do preflight_args+=(--owned-manifest "$manifest"); done
  fi
  preflight_action "${preflight_args[@]}" >/dev/null
  local expected_exec expected_name expected_hash expected_req expected_cd expected_tree
  local before_inventory after_inventory after_candidates new_rows='' row pid path count=0
  local post_hash post_req post_cd post_tree
  expected_exec=$(executable "$app")
  expected_name=$(basename "$expected_exec")
  expected_hash=$(sha256_file "$expected_exec") || die 'prelaunch executable hash is unavailable'
  expected_req=$(designated_requirement "$app") || die 'prelaunch designated requirement is unavailable'
  expected_cd=$(cdhash "$app") || die 'prelaunch CDHash is unavailable'
  expected_tree=$(bundle_tree_hash "$app") || die 'prelaunch app tree hash is unavailable'
  capture_process_snapshot before_inventory
  open "$app"
  sleep "${SEETHIS_LAUNCH_WAIT_SECONDS:-1}"
  capture_process_snapshot after_inventory
  after_candidates=$(named_processes_from_snapshot "$expected_name" "$after_inventory")
  while IFS= read -r row; do
    [[ -n "$row" ]] || continue
    pid=${row%%$'\t'*}
    case $'\n'"$before_inventory"$'\n' in *$'\n'"$pid"$'\t'*) continue ;; esac
    new_rows=${new_rows:+$new_rows$'\n'}$row
    count=$((count + 1))
  done <<<"$after_candidates"
  [[ "$count" -gt 0 ]] || die 'launch produced no unique newly observed process'
  [[ "$count" == 1 ]] || die 'launch produced multiple newly observed processes'
  pid=${new_rows%%$'\t'*}
  path=${new_rows#*$'\t'}
  [[ "$path" == "$expected_exec" ]] || die 'new process executable path does not match the exact installed app'
  post_hash=$(sha256_file "$path") || die 'post-open executable hash is unavailable'
  post_req=$(designated_requirement "$app") || die 'post-open designated requirement is unavailable'
  post_cd=$(cdhash "$app") || die 'post-open CDHash is unavailable'
  post_tree=$(bundle_tree_hash "$app") || die 'post-open app tree hash is unavailable'
  [[ "$post_hash" == "$expected_hash" ]] || die 'post-open executable bytes differ from the prelaunch snapshot'
  [[ "$post_req" == "$expected_req" ]] || die 'post-open designated requirement differs from the prelaunch snapshot'
  [[ "$post_cd" == "$expected_cd" ]] || die 'post-open CDHash differs from the prelaunch snapshot'
  [[ "$post_tree" == "$expected_tree" ]] || die 'post-open app content differs from the prelaunch snapshot'
  strict_verify "$app" || die 'post-open app signature validation failed'
  note "LAUNCHED pid=$pid app=$app executable=$expected_exec executable_sha256=$expected_hash cdhash=$expected_cd designated_requirement=$expected_req"
}

[[ $# -gt 0 ]] || { usage; exit 2; }
action=$1
shift
case "$action" in
  package) package_action "$@" ;;
  preflight) preflight_action "$@" ;;
  install) install_action "$@" ;;
  launch) launch_action "$@" ;;
  -h|--help) usage ;;
  *) die "unknown action: $action" ;;
esac
