#!/usr/bin/env python3
"""Run a command on the phone through runq (phone/tools/runq.c, launchd job org.davinci.runq)
and print its output.

  tools/davinci-run.py [--timeout SECS] PROGRAM [ARG...]
  e.g. tools/davinci-run.py /var/local/bin/openapp com.apple.mobilesafari https://example.com

PROGRAM is an absolute path on the phone. In Git Bash set MSYS_NO_PATHCONV=1, or the paths get
rewritten into Windows ones. The command file goes to /private/var/local/runq/NAME.cmd; runq
answers with NAME.out (stdout+stderr) and NAME.rc (exit status).
"""
import argparse, importlib.util, os, sys, tempfile, time

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('davinci_files', os.path.join(here, 'davinci-files.py'))
ff = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ff)

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--timeout', type=float, default=60)
ap.add_argument('argv', nargs=argparse.REMAINDER)
a = ap.parse_args()
if not a.argv or not a.argv[0].startswith('/'):
    ap.error('PROGRAM must be an absolute path on the phone')

Q = '/private/var/local/runq'
name = 'r%d' % int(time.time() * 1000)
tmp = tempfile.mkdtemp()
cmd = os.path.join(tmp, name + '.cmd')
with open(cmd, 'w', newline='\n') as f:
    f.write('\n'.join(a.argv) + '\n')


def fetch(remote, local):
    try:
        ff.main(['get', remote, local])
        return open(local, 'rb').read()
    except (SystemExit, OSError, Exception):
        return None


# the .cmd must appear whole: write it under another name, then let runq see it
import contextlib, io
with contextlib.redirect_stdout(io.StringIO()):
    ff.main(['put', cmd, Q + '/' + name + '.cmd', '644'])
end = time.time() + a.timeout
rc = None
while time.time() < end:
    time.sleep(1)
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        rc = fetch(Q + '/' + name + '.rc', os.path.join(tmp, 'rc'))
    if rc is not None:
        break
with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
    out = fetch(Q + '/' + name + '.out', os.path.join(tmp, 'out'))
sys.stdout.write((out or b'').decode('utf-8', 'replace'))
if rc is None:
    print('(no exit status after %.0f s: still running, or runq is not)' % a.timeout, file=sys.stderr)
    sys.exit(124)
with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
    for ext in ('.rc', '.out'):
        try:
            ff.main(['rm', Q + '/' + name + ext])
        except (SystemExit, Exception):
            pass
sys.exit(int(rc.strip() or b'1'))
