"""Look up (and optionally submit) package binaries on VirusTotal.

Built to bisect a false positive: when a release is rejected for antivirus hits,
the question is which build first triggered them, and the answer is in the
preserved packages under `artifacts/mod-manager/`.

A LOOKUP sends only a SHA-256 and is always safe. A SUBMISSION sends the FILE to
VirusTotal, which shares it with the security industry and makes it retrievable
by their Intelligence customers -- so `--submit` is opt-in per run and never
implied. Only submit binaries that are published, or that you are content to
publish.

The key comes from `.env` in the repository root, or from a VIRUSTOTAL_API_KEY
environment variable, which wins when both are set. `.env` is already ignored by
Git, and packages are assembled from an explicit file list, so it cannot reach a
release. Get a free key from your VirusTotal account (profile menu -> API key);
the public quota is 4 requests/minute, 500/day, which this respects by pacing.

  # .env
  VIRUSTOTAL_API_KEY=your-key-here

  python scripts/Get-VirusTotalVerdict.py FILE [FILE ...]
  python scripts/Get-VirusTotalVerdict.py --bisect          # every preserved ASI, oldest first
  python scripts/Get-VirusTotalVerdict.py --submit FILE     # upload an unknown file and wait

Exit code is 0 when every file examined is known and undetected, 1 otherwise, so
this can gate a release build.
"""
import argparse
import glob
import hashlib
import io
import json
import os
import sys
import time
import urllib.error
import urllib.request

API = 'https://www.virustotal.com/api/v3'
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, os.pardir))
# The public quota is 4 requests/minute; stay under it without being slower than
# necessary for a handful of files.
PACE_SECONDS = 16.0


def load_dotenv(path):
    """KEY=VALUE pairs from a .env file. Missing file is not an error."""
    values = {}
    try:
        with io.open(path, encoding='utf-8') as stream:
            for line in stream:
                line = line.strip()
                if not line or line.startswith('#') or '=' not in line:
                    continue
                name, _, value = line.partition('=')
                value = value.strip()
                if len(value) > 1 and value[0] == value[-1] and value[0] in '"\'':
                    value = value[1:-1]
                values[name.strip()] = value
    except OSError:
        pass
    return values


def api_key():
    # An explicit environment variable wins, so CI can override the file.
    key = os.environ.get('VIRUSTOTAL_API_KEY', '').strip()
    source = 'VIRUSTOTAL_API_KEY'
    if not key:
        key = load_dotenv(os.path.join(ROOT, '.env')).get('VIRUSTOTAL_API_KEY', '').strip()
        source = '.env'
    if not key or key.startswith('your-key'):
        raise SystemExit(
            'No VirusTotal API key. Put it in %s as\n\n    VIRUSTOTAL_API_KEY=...\n\n'
            'or set the environment variable. Get a free key from your VirusTotal '
            'profile menu -> API key. Never commit it; .env is already git-ignored.'
            % os.path.join(ROOT, '.env'))
    print('key from %s' % source)
    return key


def request(path, key, data=None, content_type=None, method=None):
    headers = {'x-apikey': key, 'accept': 'application/json'}
    if content_type:
        headers['content-type'] = content_type
    call = urllib.request.Request(API + path, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(call, timeout=120) as response:
            return response.status, json.loads(response.read().decode('utf-8'))
    except urllib.error.HTTPError as error:
        body = error.read().decode('utf-8', 'replace')
        try:
            return error.code, json.loads(body)
        except ValueError:
            return error.code, {'error': {'message': body[:400]}}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def verdicts(attributes):
    """Only the engines that flagged it, as (vendor, label) pairs."""
    results = attributes.get('last_analysis_results', {})
    flagged = [(vendor, entry.get('result') or entry.get('category'))
               for vendor, entry in sorted(results.items())
               if entry.get('category') in ('malicious', 'suspicious')]
    stats = attributes.get('last_analysis_stats', {})
    total = sum(stats.get(name, 0) for name in
                ('harmless', 'malicious', 'suspicious', 'undetected', 'timeout'))
    return flagged, stats.get('malicious', 0) + stats.get('suspicious', 0), total


def report(label, digest, status, body, show_vendors):
    if status == 404:
        print('  %-44s %s  NOT ON VIRUSTOTAL' % (label, digest[:16]))
        return None
    if status != 200:
        message = body.get('error', {}).get('message', 'unknown error')
        print('  %-44s %s  API %s: %s' % (label, digest[:16], status, message))
        return None
    attributes = body.get('data', {}).get('attributes', {})
    flagged, count, total = verdicts(attributes)
    print('  %-44s %s  %d/%d' % (label, digest[:16], count, total))
    if show_vendors and flagged:
        for vendor, result in flagged:
            print('       %-22s %s' % (vendor, result))
    return count


def submit(path, key):
    """Upload a file and wait for its analysis. This publishes the file."""
    boundary = '----CrimsonDesertTelemetry'
    name = os.path.basename(path)
    with open(path, 'rb') as stream:
        payload = stream.read()
    body = (('--%s\r\nContent-Disposition: form-data; name="file"; filename="%s"\r\n'
             'Content-Type: application/octet-stream\r\n\r\n' % (boundary, name)).encode()
            + payload + ('\r\n--%s--\r\n' % boundary).encode())
    status, response = request('/files', key, body,
                               'multipart/form-data; boundary=' + boundary)
    if status not in (200, 201):
        print('  submission failed: API %s %s' % (status,
              response.get('error', {}).get('message', '')))
        return False
    analysis = response.get('data', {}).get('id')
    print('  submitted %s, analysis %s' % (name, analysis))
    for attempt in range(40):
        time.sleep(PACE_SECONDS)
        status, body = request('/analyses/' + analysis, key)
        state = body.get('data', {}).get('attributes', {}).get('status')
        print('    [%2d] %s' % (attempt + 1, state or 'no status'))
        if state == 'completed':
            return True
    print('  analysis did not complete in time; look it up again later')
    return False


def preserved_binaries():
    """Every preserved ASI, oldest first, one per package directory."""
    found = []
    for path in glob.glob(os.path.join(ROOT, 'artifacts', 'mod-manager', '*',
                                       'CrimsonDesertTelemetry', 'CrimsonDesertTelemetry.asi')):
        found.append((os.path.getmtime(path), path))
    return [path for _, path in sorted(found)]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('files', nargs='*')
    parser.add_argument('--bisect', action='store_true',
                        help='look up every preserved ASI, oldest first')
    parser.add_argument('--submit', action='store_true',
                        help='UPLOAD files VirusTotal does not know. This publishes them.')
    parser.add_argument('--vendors', action='store_true',
                        help='name the engines that flagged each file')
    arguments = parser.parse_args()

    paths = list(arguments.files)
    if arguments.bisect:
        paths.extend(preserved_binaries())
    if not paths:
        parser.error('give at least one file, or --bisect')

    key = api_key()
    print('%d file(s); lookups send a hash only%s'
          % (len(paths), ', --submit will upload unknown ones' if arguments.submit else ''))
    worst = 0
    unknown = []
    for index, path in enumerate(paths):
        if index:
            time.sleep(PACE_SECONDS)
        if not os.path.exists(path):
            print('  %-44s missing' % os.path.basename(path))
            worst = max(worst, 1)
            continue
        digest = sha256(path)
        label = os.path.relpath(path, ROOT).replace('\\', '/')
        label = label[-44:] if len(label) > 44 else label
        status, body = request('/files/' + digest, key)
        count = report(label, digest, status, body, arguments.vendors)
        if count is None:
            unknown.append(path)
            worst = max(worst, 1)
        elif count:
            worst = max(worst, 1)

    if unknown and arguments.submit:
        print('\nsubmitting %d unknown file(s) -- this publishes them' % len(unknown))
        for path in unknown:
            print('  %s' % os.path.basename(path))
            submit(path, key)
        print('\nRe-run without --submit to read the finished verdicts.')
    elif unknown:
        print('\n%d file(s) are not on VirusTotal. Re-run with --submit to upload them, '
              'which publishes them.' % len(unknown))
    return worst


if __name__ == '__main__':
    sys.exit(main())
