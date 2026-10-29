"""Read-only Android capture of pstore and its init-created console copy."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import shlex
import uuid


def unpack_capture(stdout, token):
    """Require a completion trailer; transport success alone is insufficient."""
    prefix = b'\n' + token.encode('ascii') + b':'
    body, found, trailer = stdout.rpartition(prefix)
    if not found or not trailer.endswith(b'\n'):
        return None, stdout
    status = trailer[:-1]
    if not status.isdigit() or int(status) > 255:
        return None, stdout
    return int(status), body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    # Refuse reuse: every capture retains its own metadata and timestamps.
    args.output.mkdir(parents=True, exist_ok=False)
    commands = {
        'state.txt': 'id; getprop sys.boot_completed; uptime; ls -la /sys/fs/pstore /data/debuglogger/console-ramoops-0',
        'console-ramoops-0': 'cat /sys/fs/pstore/console-ramoops-0',
        'debuglogger-console-ramoops-0': 'cat /data/debuglogger/console-ramoops-0',
        'pstore.tar': 'tar -cf - -C /sys/fs/pstore .',
        'dmesg.txt': 'dmesg',
    }
    records = []
    for name, command in commands.items():
        token = 'DEGAS_CAPTURE_' + uuid.uuid4().hex
        wrapped = '(' + command + '); task_rc=$?; printf "\\n' + token + ':%s\\n" "$task_rc"'
        result = subprocess.run(['adb', '-s', args.serial, 'exec-out',
                                 'su -c ' + shlex.quote(wrapped)],
                                capture_output=True, timeout=30)
        remote_status, content = unpack_capture(result.stdout, token)
        valid = result.returncode == 0 and remote_status == 0
        destination = name if valid else name+'.failed-output'
        (args.output/destination).write_bytes(content)
        (args.output/(name+'.stderr')).write_bytes(result.stderr)
        records.append(dict(file=destination, command=command, returncode=result.returncode,
                            remote_returncode=remote_status, complete=valid,
                            bytes=len(content), sha256=hashlib.sha256(content).hexdigest()))
    (args.output/'manifest.json').write_text(json.dumps(dict(
        utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), serial=args.serial,
        records=records), indent=2)+'\n')
    print(json.dumps(records, indent=2))


if __name__ == '__main__':
    main()
