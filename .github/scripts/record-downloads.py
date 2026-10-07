#!/usr/bin/env python3
"""Record the download counts of the installers of a GitHub release.

The counts are kept in a CSV file stored as an asset of a release, by default
<release>.csv on the download-stats release of PlusToolkit/PlusLibData: one row
per date, one column per package (e.g. 2.9.0-Win64), with columns added as
packages appear, so that the files of several releases can be combined.

The row is either today's date, for sampling the counts of a release that keeps
its installers (the value is then the cumulative count, replaced on a re-run),
or the build date in the installer name, for installers that are replaced
every night (the value is then the final count, summed on a re-run).

Nothing is written when there is nothing new: no count changed since the last
row (today), or no installer was downloaded (build).

Requires the gh CLI. The ledger release is accessed with the token in
LEDGER_TOKEN when it is set, otherwise with the same token as the release.
"""

import argparse
import csv
import datetime
import os
import re
import subprocess
import sys
import tempfile

INSTALLER_NAME = re.compile(r'PlusApp-([0-9.]+)\.(\d{4})(\d{2})(\d{2})-(.*)\.exe')


def gh(*args, token=None):
  env = dict(os.environ, GH_TOKEN=token) if token else None
  return subprocess.run(['gh', *args], check=True, text=True, stdout=subprocess.PIPE, env=env).stdout


def main():
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument('--repo', default=os.environ.get('GH_REPO'), help='OWNER/REPO of the release (default: $GH_REPO)')
  parser.add_argument('--release', required=True, help='tag of the release whose installers are counted')
  parser.add_argument('--ledger-repo', default='PlusToolkit/PlusLibData', help='OWNER/REPO of the ledger release (default: %(default)s)')
  parser.add_argument('--ledger-release', default='download-stats', help='tag of the release that holds the CSV files (default: %(default)s)')
  parser.add_argument('--file', help='name of the CSV asset (default: <release>.csv)')
  parser.add_argument('--row', choices=['today', 'build'], default='today',
                      help='key the row by today\'s date or by the build date of the installers (default: %(default)s)')
  parser.add_argument('--installer', nargs='*', metavar='NAME',
                      help='record only these installers (default: all of the release)')
  args = parser.parse_args()
  if not args.repo:
    sys.exit('No repository: pass --repo or set GH_REPO')
  file = args.file or f'{args.release}.csv'
  ledger = ['--repo', args.ledger_repo, args.ledger_release]
  token = os.environ.get('LEDGER_TOKEN')

  counts = gh('release', 'view', args.release, '--repo', args.repo, '--json', 'assets',
              '--jq', '.assets[] | select(.name | endswith(".exe")) | "\\(.name) \\(.downloadCount)"')
  if args.installer is not None:
    counts = '\n'.join(line for line in counts.splitlines() if line.split()[0] in args.installer)
  if not counts:
    print('No installers to record')
    return

  header, table = ['date'], {}
  with tempfile.TemporaryDirectory() as tmp:
    path = os.path.join(tmp, file)
    if gh('release', 'view', *ledger, '--json', 'assets', '--jq', f'.assets[] | select(.name == "{file}") | .name', token=token):
      gh('release', 'download', *ledger, '--pattern', file, '--dir', tmp, token=token)
      with open(path, newline='') as f:
        rows = list(csv.reader(f))
      header = rows[0]
      table = {row[0]: dict(zip(header[1:], row[1:])) for row in rows[1:]}
    last = table[max(table)] if table else {}

    today = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d')
    changed = False
    for line in counts.splitlines():
      name, count = line.split()
      m = INSTALLER_NAME.fullmatch(name)
      if not m:
        sys.exit(f'Unexpected installer name: {name}')
      package = f'{m[1]}-{m[5]}'
      print(f'{package}: {count}')
      if package not in header:
        header.append(package)
      if args.row == 'build':
        row = table.setdefault(f'{m[2]}-{m[3]}-{m[4]}', {})
        row[package] = str(int(row.get(package) or 0) + int(count))
        changed |= int(count) > 0
      else:
        row = table.setdefault(today, {})
        row[package] = count
        changed |= count != last.get(package)
    if not changed:
      print(f'Nothing new for {file}')
      return

    with open(path, 'w', newline='') as f:
      writer = csv.writer(f, lineterminator='\n')
      writer.writerow(header)
      for date in sorted(table):
        writer.writerow([date] + [table[date].get(p, '') for p in header[1:]])
    gh('release', 'upload', *ledger, path, '--clobber', token=token)
    print(f'Uploaded {file} to {args.ledger_repo} {args.ledger_release}: {len(table)} rows, {len(header) - 1} packages')


if __name__ == '__main__':
  main()
