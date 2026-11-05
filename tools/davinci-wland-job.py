#!/usr/bin/env python3
"""Add (or remove) the wland boot job in the phone's launchd job cache.

  davinci-wland-job.py add | remove

Reads /System/Library/xpc/launchd.plist from the running phone over the
kernel file server (tools/davinci-files.py), keeps the first original it sees
as launchd.plist.orig next to it, and writes the cache back with the job
org.davinci.wland = `wland auto` (once per phone boot, log in
/var/local/wland.log; same job as ios-flame's org.iosonpixel.wland).
launchd reads the cache at boot: reboot the phone afterwards.
"""
import os, plistlib, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, '..', 'research', 'launchd')
REMOTE = '/System/Library/xpc/launchd.plist'
KEY = '/System/Library/LaunchDaemons/org.davinci.wland.plist'
JOB = {
    'Label': 'org.davinci.wland',
    'ProgramArguments': ['/var/local/bin/wland', 'auto'],
    'RunAtLoad': True,
    'KeepAlive': False,
    'UserName': 'root',
    'EnvironmentVariables': {'PATH': '/var/local/bin:/usr/bin:/bin:/usr/sbin:/sbin'},
    'StandardOutPath': '/var/local/wland.out',
    'StandardErrorPath': '/var/local/wland.out',
}


def files(*args):
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    subprocess.run([sys.executable, '-I', os.path.join(HERE, 'davinci-files.py'), *args], check=True, env=env)


def main(what):
    os.makedirs(WORK, exist_ok=True)
    cur = os.path.join(WORK, 'launchd.plist.cur')
    files('get', REMOTE, cur)
    orig = os.path.join(WORK, 'launchd.plist.orig')
    if not os.path.exists(orig):
        with open(cur, 'rb') as f, open(orig, 'wb') as o:
            o.write(f.read())
        files('put', orig, REMOTE + '.orig', '644')
    p = plistlib.load(open(cur, 'rb'))
    jobs = p['LaunchDaemons']
    if what == 'add':
        jobs[KEY] = JOB
    else:
        jobs.pop(KEY, None)
    new = os.path.join(WORK, 'launchd.plist.new')
    plistlib.dump(p, open(new, 'wb'), fmt=plistlib.FMT_BINARY)
    files('put', new, REMOTE, '644')
    print(f'{what}: {KEY in jobs and "job present" or "job absent"}, {len(jobs)} launchd jobs')


if __name__ == '__main__':
    if len(sys.argv) != 2 or sys.argv[1] not in ('add', 'remove'):
        raise SystemExit(__doc__)
    main(sys.argv[1])
