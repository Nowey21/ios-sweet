#!/usr/bin/env python3
"""Add or remove a one-shot boot job in the phone's launchd job cache.

  davinci-launchd-job.py add NAME PROGRAM [ARG ...]
  davinci-launchd-job.py remove NAME

There is no shell on the phone: a job is how a test program (vktest,
v2test, ...) runs. The job org.davinci.NAME runs PROGRAM once at boot as
root, output in /var/local/NAME.out (read it with tools/davinci-files.py).
Same mechanism as tools/davinci-wland-job.py; the first original cache is
kept as launchd.plist.orig on the phone and in research/launchd/.
Reboot the phone afterwards: launchd reads the cache at boot.
"""
import os, plistlib, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, '..', 'research', 'launchd')
REMOTE = '/System/Library/xpc/launchd.plist'


def files(*args):
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    subprocess.run([sys.executable, '-I', os.path.join(HERE, 'davinci-files.py'), *args], check=True, env=env)


def main(argv):
    if len(argv) < 2 or argv[0] not in ('add', 'remove') or (argv[0] == 'add' and len(argv) < 3):
        raise SystemExit(__doc__)
    what, name = argv[0], argv[1]
    label = 'org.davinci.' + name
    key = f'/System/Library/LaunchDaemons/{label}.plist'
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
        jobs[key] = {
            'Label': label,
            'ProgramArguments': argv[2:],
            'RunAtLoad': True,
            'KeepAlive': False,
            'UserName': 'root',
            'EnvironmentVariables': {'PATH': '/var/local/bin:/usr/bin:/bin:/usr/sbin:/sbin',
                                     'DYLD_LIBRARY_PATH': '/var/local/lib'},
            'StandardOutPath': f'/var/local/{name}.out',
            'StandardErrorPath': f'/var/local/{name}.out',
        }
    else:
        jobs.pop(key, None)
    new = os.path.join(WORK, 'launchd.plist.new')
    plistlib.dump(p, open(new, 'wb'), fmt=plistlib.FMT_BINARY)
    files('put', new, REMOTE, '644')
    print(f'{what} {label}: {"present" if key in jobs else "absent"}, {len(jobs)} launchd jobs')


if __name__ == '__main__':
    main(sys.argv[1:])
