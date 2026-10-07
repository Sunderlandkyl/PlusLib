#!/usr/bin/env python3
"""Record the download counts of the installers of a GitHub release.

The counts are kept in a CSV file stored as an asset of a release (by default
the same one): one row per date, one column per package (e.g. 2.9.0-Win64),
with columns added as packages appear, so that the files of several releases
can be combined.

The row is either today's date, for sampling the counts of a release that keeps
its installers (the value is then the cumulative count, replaced on a re-run),
or the build date in the installer name, for installers that are replaced
every night (the value is then the final count, summed on a re-run).

Requires the gh CLI, authenticated for the repository.
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


def gh(*args, **kwargs):
  return subprocess.run(['gh', *args], check=True, text=True, stdout=subprocess.PIPE, **kwargs).stdout


def main():
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument('--repo', default=os.environ.get('GH_REPO'), help='OWNER/REPO (default: $GH_REPO)')
  parser.add_argument('--release', required=True, help='tag of the release whose installers are counted')
  parser.add_argument('--ledger', help='tag of the release that holds the CSV file (default: --release)')
  parser.add_argument('--file', default='download-stats.csv', help='name of the CSV asset (default: %(default)s)')
  parser.add_argument('--row', choices=['today', 'build'], default='today',
                      help='key the row by today\'s date or by the build date of the installer (default: %(default)s)')
  parser.add_argument('--inherit', action='store_true',
                      help='if the ledger release has no CSV file yet, continue the one of the newest release that has')
  args = parser.parse_args()
  if not args.repo:
    sys.exit('No repository: pass --repo or set GH_REPO')
  ledger = args.ledger or args.release
  repo = ['--repo', args.repo]

  # Find the release to continue the file from.
  releases = gh('api', f'repos/{args.repo}/releases?per_page=100',
                '--jq', f'.[] | select(any(.assets[]; .name == "{args.file}")) | .tag_name').split()
  source = None
  if ledger in releases:
    source = ledger
  elif args.inherit and releases:
    source = releases[0]

  header, table = ['date'], {}
  with tempfile.TemporaryDirectory() as tmp:
    path = os.path.join(tmp, args.file)
    if source:
      print(f'Continuing {args.file} of {source}')
      gh('release', 'download', source, *repo, '--pattern', args.file, '--dir', tmp)
      with open(path, newline='') as f:
        rows = list(csv.reader(f))
      header = rows[0]
      table = {row[0]: dict(zip(header[1:], row[1:])) for row in rows[1:]}

    counts = gh('release', 'view', args.release, *repo, '--json', 'assets',
                '--jq', '.assets[] | select(.name | endswith(".exe")) | "\\(.name) \\(.downloadCount)"')
    today = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d')
    for line in counts.splitlines():
      name, count = line.split()
      m = INSTALLER_NAME.fullmatch(name)
      if not m:
        sys.exit(f'Unexpected installer name: {name}')
      package = f'{m[1]}-{m[5]}'
      if package not in header:
        header.append(package)
      row = table.setdefault(f'{m[2]}-{m[3]}-{m[4]}' if args.row == 'build' else today, {})
      previous = int(row.get(package) or 0) if args.row == 'build' else 0
      row[package] = str(previous + int(count))
      print(f'{package}: {count}')

    with open(path, 'w', newline='') as f:
      writer = csv.writer(f, lineterminator='\n')
      writer.writerow(header)
      for date in sorted(table):
        writer.writerow([date] + [table[date].get(p, '') for p in header[1:]])
    gh('release', 'upload', ledger, *repo, path, '--clobber')
    print(f'Uploaded {args.file} to {ledger}: {len(table)} rows, {len(header) - 1} packages')


if __name__ == '__main__':
  main()
