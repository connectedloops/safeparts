#!/bin/sh
set -eu

case_name=${1:-maximum-words}
case "$case_name" in
  maximum-words) test_name=maximum_words_split_keeps_every_share_exportable; runner=qt ;;
  maximum-valid) test_name=maximum_valid_workload_remains_bounded_and_resettable; runner=qt ;;
  maximum-recovery) test_name=maximum_base64_replacement_recovers_exact_bytes; runner=rust ;;
  maximum-retained) test_name=exact_retained_recovery_limit_is_transactional_and_recovers_maximum_secret; runner=rust ;;
  maximum-protected) test_name=maximum_policy_argon2_recovery_executes_exactly; runner=rust ;;
  maximum-base58-codec) test_name=base58check::tests::maximum_codec_vector_round_trips_with_pinned_shape; runner=core ;;
  maximum-accessibility) test_name=maximum_accessibility_handoff_remains_bounded_and_responsive; runner=qt ;;
  matrix-base64) test_name=maximum_base64url_text_unprotected_recovers_exact_bytes; runner=rust ;;
  matrix-base58) test_name=maximum_base58check_binary_protected_recovers_exact_bytes; runner=rust ;;
  matrix-words) test_name=maximum_words_text_protected_recovers_exact_bytes; runner=rust ;;
  matrix-bip39) test_name=maximum_bip39_binary_unprotected_recovers_exact_bytes; runner=rust ;;
  *) echo "unknown capacity case: $case_name" >&2; exit 64 ;;
esac
[ "$(uname -s)" = Darwin ] || { echo 'capacity evidence requires macOS' >&2; exit 1; }

repo=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
cd "$repo"
out="target/desktop-evidence/issue-147/capacity/$case_name"
rm -rf "$out" && mkdir -p "$out"
printf '{"phase":"build","case":"%s"}\n' "$case_name"

if [ "$runner" = qt ]; then
  cmake -S apps/desktop -B target/desktop-build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON >"$out/build.stdout" 2>"$out/build.stderr"
  cmake --build target/desktop-build --target desktop-actions -j2 >>"$out/build.stdout" 2>>"$out/build.stderr"
  executable="$repo/target/desktop-build/desktop-actions"
else
  if [ "$runner" = core ]; then
    cargo_args='--release -p safeparts_core --lib --no-run --message-format=json'
    expected_name=safeparts_core
  else
    cargo_args='--release -p safeparts_desktop_bridge --test operation --no-run --message-format=json'
    expected_name=operation
  fi
  # shellcheck disable=SC2086
  mise exec -- cargo test $cargo_args >"$out/build.jsonl" 2>"$out/build.stderr"
  executable=$(python3 - "$out/build.jsonl" "$expected_name" <<'PY'
import json, os, sys
matches=[]
for line in open(sys.argv[1], encoding="utf-8"):
    try: item=json.loads(line)
    except json.JSONDecodeError: continue
    exe=item.get("executable")
    target=item.get("target", {})
    if exe and target.get("name")==sys.argv[2] and item.get("profile", {}).get("test"):
        matches.append(os.path.realpath(exe))
if len(set(matches)) != 1:
    raise SystemExit(f"expected one exact test executable, got {matches}")
print(matches[0])
PY
)
fi
[ -x "$executable" ] || { echo "resolved executable is not executable: $executable" >&2; exit 1; }
exe_hash=$(shasum -a 256 "$executable" | awk '{print $1}')
printf '%s\n' "$executable" >"$out/executable.path"
printf '%s\n' "$exe_hash" >"$out/executable.sha256"
printf '{"phase":"run","case":"%s","test":"%s","executable":"%s","sha256":"%s"}\n' "$case_name" "$test_name" "$executable" "$exe_hash"

python3 - "$out" "$executable" "$test_name" "$runner" "${CAPACITY_TIMEOUT_SECONDS:-300}" <<'PY'
import hashlib, json, os, re, signal, subprocess, sys, time
out, executable, test_name, runner, timeout_text = sys.argv[1:]
timeout=int(timeout_text); pid_path=os.path.join(out,"test.pid")
args=[executable, test_name]
if runner != "qt": args += ["--ignored", "--exact", "--nocapture"]
script='printf "%s\\n" "$$" > "$1"; shift; exec "$@"'
command=["/usr/bin/time","-l","/bin/sh","-c",script,"capacity-exec",pid_path,*args]
env=os.environ.copy()
if runner=="qt": env["QT_QPA_PLATFORM"]="cocoa"
samples=[]; errors=[]; latest_phase="startup"; last_sample=None; max_gap=0; sampling_complete=False
required=["below_retained_complete","exact_retained_begin","exact_retained_recovered","above_limit","preserved","above_limit_preserved"]
started=time.monotonic()
with open(os.path.join(out,"test.stdout"),"w",encoding="utf-8") as stdout, open(os.path.join(out,"test.time"),"w",encoding="utf-8") as stderr:
    process=subprocess.Popen(command,stdout=stdout,stderr=stderr,env=env,start_new_session=True)
    pid=None
    while process.poll() is None:
        if time.monotonic()-started > timeout:
            os.killpg(process.pid,signal.SIGTERM)
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGKILL); process.wait()
            raise SystemExit(124)
        if pid is None and os.path.exists(pid_path):
            text=open(pid_path).read().strip()
            if text: pid=int(text)
        if runner=="rust" and test_name.startswith("exact_retained") and pid and not sampling_complete:
            stderr.flush()
            lines=open(os.path.join(out,"test.time"),encoding="utf-8",errors="replace").read().splitlines()
            for line in lines:
                match=re.search(r"capacity_phase=([a-z_]+)",line)
                if match: latest_phase=match.group(1)
            try:
                command_identity=subprocess.check_output(["ps","-p",str(pid),"-o","command="],text=True).strip()
                if os.path.realpath(command_identity.split()[0]) != os.path.realpath(executable):
                    raise RuntimeError(f"PID identity mismatch: {command_identity}")
                rss=int(subprocess.check_output(["ps","-o","rss=","-p",str(pid)],text=True).strip())*1024
                footprint=subprocess.check_output(["/usr/bin/footprint","-p",str(pid),"-f","bytes"],text=True,stderr=subprocess.STDOUT)
                match=re.search(r"Footprint: ([0-9]+) B",footprint)
                if not match: raise RuntimeError("footprint total missing")
                now=int((time.monotonic()-started)*1000)
                if last_sample is not None: max_gap=max(max_gap,now-last_sample)
                last_sample=now
                samples.append({"timestampMs":now,"pid":pid,"executable":os.path.realpath(executable),"phase":latest_phase,"rssBytes":rss,"privateFootprintBytes":int(match.group(1))})
                sampling_complete=all(phase in {sample["phase"] for sample in samples} for phase in required)
            except Exception as error:
                errors.append({"timestampMs":int((time.monotonic()-started)*1000),"error":str(error)})
        time.sleep(0.1)
    code=process.wait()
if code: raise SystemExit(code)
if pid is None: raise SystemExit("test PID was not recorded")
identity={"pid":pid,"executable":os.path.realpath(executable),"sha256":hashlib.sha256(open(executable,"rb").read()).hexdigest()}
json.dump(identity,open(os.path.join(out,"identity.json"),"w"),indent=2)
if samples:
    with open(os.path.join(out,"samples.jsonl"),"w") as stream:
        for sample in samples: stream.write(json.dumps(sample,sort_keys=True)+"\n")
    json.dump(errors,open(os.path.join(out,"sampling-errors.json"),"w"),indent=2)
    phases={sample["phase"] for sample in samples}
    if errors or len({sample["pid"] for sample in samples}) != 1 or len({sample["executable"] for sample in samples}) != 1:
        raise SystemExit("ambiguous or failed time-series sampling")
    if not all(phase in phases for phase in required):
        raise SystemExit(f"samples do not span every phase: {sorted(phases)}")
    if max_gap > 1000: raise SystemExit(f"sampling gap too large: {max_gap}ms")
    json.dump({"sampleCount":len(samples),"maximumGapMs":max_gap,"phases":sorted(phases),"errors":errors},open(os.path.join(out,"sampling.json"),"w"),indent=2)
PY

rss=$(awk '/maximum resident set size/{print $1}' "$out/test.time")
footprint=$(awk '/peak memory footprint/{print $1}' "$out/test.time")
[ -n "$rss" ] && [ -n "$footprint" ] || { echo 'direct test memory metrics missing' >&2; exit 1; }
python3 - "$out" "$case_name" "$test_name" "$rss" "$footprint" "$executable" "$exe_hash" <<'PY'
import json, os, re, sys
out,case,test,rss,private,exe,sha=sys.argv[1:]; rss=int(rss); private=int(private); limit=1024*1048576
summary={"schemaVersion":2,"case":case,"test":test,"measurementScope":"direct-test-process","executablePath":exe,"executableSha256":sha,"maximumResidentSetBytes":rss,"peakMemoryFootprintBytes":private,"directTestPeakRssBytes":rss,"directTestPeakPrivateFootprintBytes":private,"limitBytes":limit,"rssPassed":rss<limit,"footprintPassed":private<limit}
if os.path.exists(os.path.join(out,"samples.jsonl")):
    samples=[json.loads(line) for line in open(os.path.join(out,"samples.jsonl"))]
    summary.update(sampledTestMaximumRssBytes=max(x["rssBytes"] for x in samples),sampledTestMaximumPrivateFootprintBytes=max(x["privateFootprintBytes"] for x in samples))
    text=open(os.path.join(out,"test.time"),encoding="utf-8").read()
    facts=dict(re.findall(r"(raw_source_shares_bytes|encoded_non_whitespace_bytes|generated_packet_bytes|decoded_packet_bytes|secret_bytes|supplied_unique_shares|retained_limit_bytes)=([0-9]+)",text))
    facts={key:int(value) for key,value in facts.items()}
    required=set(["raw_source_shares_bytes","encoded_non_whitespace_bytes","generated_packet_bytes","decoded_packet_bytes","secret_bytes","supplied_unique_shares","retained_limit_bytes"])
    if set(facts)!=required: raise SystemExit(f"capacity facts incomplete: {facts}")
    concurrent=facts["raw_source_shares_bytes"]+facts["retained_limit_bytes"]+facts["generated_packet_bytes"]+facts["decoded_packet_bytes"]+facts["secret_bytes"]*2
    ledger={"facts":facts,"entries":[
      {"owner":"test source shares","phase":"all retained phases","bytes":facts["raw_source_shares_bytes"],"formula":"16 * (RETAINED_LIMIT / 16)","lifetime":"concurrent"},
      {"owner":"non-whitespace encoded bytes","phase":"all retained phases","bytes":facts["encoded_non_whitespace_bytes"],"formula":"sum(split_whitespace token lengths)","lifetime":"subset of test source shares; non-additive"},
      {"owner":"Operation accepted raw recovery","phase":"exact/recovered/above/preserved","bytes":facts["retained_limit_bytes"],"formula":"Operation retained_input_bytes", "lifetime":"concurrent; below-limit Operation is destroyed before exact Operation"},
      {"owner":"test generated Share packets","phase":"all retained phases","bytes":facts["generated_packet_bytes"],"formula":"sum(SharePacket::encode_binary lengths)","lifetime":"concurrent"},
      {"owner":"Operation decoded Share packets","phase":"exact/recovered/above/preserved","bytes":facts["decoded_packet_bytes"],"formula":"same packet bytes decoded from retained tokens","lifetime":"concurrent"},
      {"owner":"test Secret plus recovered bytes","phase":"recovered/preserved","bytes":facts["secret_bytes"]*2,"formula":"2 * secret_bytes","lifetime":"concurrent"}],
      "derivedConcurrentBytes":concurrent,"observedDirectPeakPrivateFootprintBytes":private,"observedMinusDerivedBytes":private-concurrent,"visibilityLimit":"allocator metadata/capacity, framework/runtime mappings, transient parser allocations, and ibig internals are not directly attributable; admission caps are not counted as allocations"}
    json.dump(ledger,open(os.path.join(out,"ownership-ledger.json"),"w"),indent=2)
    summary["ownershipLedgerPath"]="ownership-ledger.json"
json.dump(summary,open(os.path.join(out,"summary.json"),"w"),sort_keys=True); open(os.path.join(out,"summary.json"),"a").write("\n")
if rss>=limit or private>=limit: raise SystemExit("direct-test-process memory gate failed")
print(json.dumps(summary,sort_keys=True))
PY
printf '{"phase":"complete","case":"%s"}\n' "$case_name"
