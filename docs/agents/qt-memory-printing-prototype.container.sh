#!/usr/bin/env bash
# THROWAWAY. Run only in the isolated image: edits that container's CUPS configuration.
set -euo pipefail
mkdir -p /evidence/backend /evidence/traces /run/cups /run/probe
chmod 1777 /evidence /evidence/traces
chown probe:probe /run/probe
chmod 700 /run/probe
cat > /etc/cups/cupsd.conf <<'CUPS'
LogLevel warn
Listen /run/cups/cups.sock
Browsing Off
WebInterface No
DefaultAuthType None
PreserveJobHistory Yes
PreserveJobFiles Yes
<Location />
  Order allow,deny
  Allow all
</Location>
<Policy default>
  <Limit All>
    Order allow,deny
    Allow all
  </Limit>
</Policy>
CUPS
cat > /usr/lib/cups/backend/capture <<'BACKEND'
#!/bin/sh
# This is the simulated OS print destination, not part of the application.
[ "$#" -gt 0 ] || { echo 'direct capture "Synthetic capture" "Synthetic capture"'; exit 0; }
if [ "$#" -ge 6 ]; then
    cat "$6" > "/evidence/backend/job-$1.pdf"
else
    cat > "/evidence/backend/job-$1.pdf"
fi
BACKEND
chmod 700 /usr/lib/cups/backend/capture
cupsd -f &
cups_pid=$!
trap 'cp /var/log/cups/error_log /evidence/cups-errors.txt 2>/dev/null || true; kill "$cups_pid" 2>/dev/null || true' EXIT
export CUPS_SERVER=/run/cups/cups.sock
for attempt in $(seq 1 100); do
    if lpstat -r 2>/dev/null | grep -Fxq 'scheduler is running'; then break; fi
    sleep 0.1
done
lpstat -r
lpadmin -p probe -E -v capture:/probe -m raw
{
    uname -a
    dpkg-query -W qt6-base-dev libcups2t64 cups-daemon strace poppler-utils
    printf 'QT_QPA_PLATFORM=offscreen; Docker network=none; destination=synthetic raw CUPS backend\n'
} > /evidence/environment.txt

for mode in memory cancel failure control; do
    runuser -u probe -- env QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/run/probe \
        CUPS_SERVER=/run/cups/cups.sock \
        strace -ff -yy -s 160 -e trace=%file,%network,write,writev,pwrite64,close \
        -o "/evidence/traces/$mode" /opt/probe "$mode" \
        > "/evidence/$mode.log" 2> "/evidence/$mode.stderr"
    if [[ "$mode" == memory || "$mode" == control ]]; then
        # A bounded wait for the local simulated printer, not a background-agent wait.
        expected=1
        [[ "$mode" == control ]] && expected=2
        for attempt in $(seq 1 100); do
            count=$(find /evidence/backend -name '*.pdf' | wc -l)
            [[ "$count" -ge "$expected" ]] && break
            sleep 0.1
        done
    fi
done
lpstat -W all -o > /evidence/jobs.txt
cp /var/log/cups/error_log /evidence/cups-errors.txt 2>/dev/null || true
for pdf in /evidence/backend/*.pdf; do
    pdfinfo "$pdf" > "$pdf.info"
    pdftotext -layout "$pdf" "$pdf.txt"
done
python3 - <<'PY'
import hashlib, json, re
from pathlib import Path
root = Path('/evidence')
logs = {mode: (root / f'{mode}.log').read_text() for mode in ['memory', 'cancel', 'failure', 'control']}
job = int(re.search(r'job_id=(\d+)', logs['memory'])[1])
expected_hash = re.search(r'sha256=([0-9a-f]+)', logs['memory'])[1]
pdfs = sorted((root / 'backend').glob('*.pdf'))
assert len(pdfs) == 2, f'Expected successful memory/control jobs only, got {pdfs}'
assert hashlib.sha256((root / 'backend' / f'job-{job}.pdf').read_bytes()).hexdigest() == expected_hash
for pdf in pdfs:
    info = pdf.with_suffix('.pdf.info').read_text()
    assert re.search(r'^Pages:\s+3$', info, re.M), info
    text = pdf.with_suffix('.pdf.txt').read_text()
    for page in range(1, 4):
        assert f'SYNTHETIC_PAGE_{page}_OF_3' in text
        for row in range(1, 41):
            assert f'SYNTHETIC_P{page}_ROW_{row} ' in text
assert 'cancelled_during_upload=1 server_state=cancelled' in logs['cancel']
assert 'expected_failure=missing_printer' in logs['failure']

summary = {'pdf_hash_matches_submitted_memory': True, 'captured_jobs': len(pdfs),
           'pages_per_successful_job': 3, 'synthetic_rows_per_job': 120,
           'partial_upload_cancelled': True, 'missing_printer_failed_without_output': True,
           'trace_candidates': {}}
# Collect syscall evidence; the parent must inspect it, not mistake this regex for a security proof.
for mode in logs:
    traces = '\n'.join(p.read_text() for p in (root / 'traces').glob(mode + '*'))
    writes = [line for line in traces.splitlines()
              if re.search(r'\b(open|openat|creat)\(', line) and
              re.search(r'O_WRONLY|O_RDWR|O_CREAT|O_TMPFILE', line)]
    endpoints = [line for line in traces.splitlines()
                 if re.search(r'\b(connect|sendto)\(', line)]
    summary['trace_candidates'][mode] = {'write_opens': writes, 'connections': endpoints}
(root / 'summary.json').write_text(json.dumps(summary, indent=2))
print(json.dumps({k: v for k, v in summary.items() if k != 'trace_candidates'}, indent=2))
PY
