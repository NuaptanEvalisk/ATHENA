#!/usr/bin/env python3
"""Opt-in Xvfb smoke for render-level link peeks; never opens a user Vault.

Run with the normal oneAPI runtime environment after building ATHENA.bin and
structured_radioactive_test. Requires Xvfb, xdotool, ImageMagick and Pillow.
"""

import argparse
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import tempfile
import time

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, default=ROOT / 'build_qt6')
args = parser.parse_args()
home = Path(tempfile.mkdtemp(prefix='athena-link-peek-smoke-'))
system = home / 'profile/system'
system.mkdir(parents=True)
(system / 'sys_state.json').write_text(json.dumps({
    'format': 'athena-system-state', 'version': 1, 'compatibility_version': '2.1.4',
    'tex': {'design_dpi': 600, 'kpsepath': False, 'kpsewhich': False,
            'make_pk': False, 'make_tfm': False}}))
vault = home / 'vault'
rd, wr = os.pipe()
xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(wr), '-screen', '0',
                         '1200x800x24', '-nolisten', 'tcp'], pass_fds=[wr],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                        start_new_session=True)
os.close(wr)
app = None
try:
    assert select.select([rd], [], [], 10)[0], 'Xvfb startup timed out'
    display = ':' + os.read(rd, 80).decode().strip()
    os.close(rd)
    env = dict(os.environ, DISPLAY=display, QT_QPA_PLATFORM='xcb', HOME=str(home),
        ATHENA_HOME_PATH=str(home / 'profile'), ATHENA_PATH=str(ROOT / 'ATHENA'),
        XDG_CONFIG_HOME=str(home / 'config'), XDG_DATA_HOME=str(home / 'data'),
        XDG_CACHE_HOME=str(home / 'cache'), GUILE_AUTO_COMPILE='0',
        ATHENA_GUILE_CACHE_PATH=str(home / 'scheme-cache'))
    env['LD_LIBRARY_PATH'] = ':'.join([str(ROOT / 'ATHENA/lib'),
        str(ROOT / 'ATHENA/lib/athena-guile/lib'), os.environ.get('LD_LIBRARY_PATH', '')])
    with (home / 'fixture.log').open('w') as log:
        subprocess.run([str(args.build / 'tests/structured_radioactive_test'),
                        'typesetsMixedNameWithoutChangingGeometry'],
            env=dict(env, ATHENA_TEST_PEEK_FIXTURE=str(vault)),
            stdout=log, stderr=subprocess.STDOUT, check=True, timeout=45)
    (vault / 'target.ath').write_text('''<TeXmacs|2.1.4>

<style|generic>

<\\body>
  <label|peek-start><hlink|Nested preview|tmfs://wikilink/peek-child>

  A sigma field <math|<with|font|cal|E>=2<rsup|E>> retains its mathematical font.

  This is the target, not the source buffer.<label|peek-end>
</body>

<\\initial>
  <\\collection>
    <associate|font|TeX Gyre Pagella>
  </collection>
</initial>
''')
    (vault / 'leaf.ath').write_text('''<TeXmacs|2.1.4>

<style|generic>

<\\body>
  Nested preview content.
</body>

<\\initial>
  <\\collection>
    <associate|font|TeX Gyre Pagella>
  </collection>
</initial>
''')
    expr = '''(begin
      (set-preference "vault welcome page" "off")
      (set-preference "vault preferred font" "TeX Gyre Pagella")
      (exec-global (lambda ()
        (vault-load (system->url VAULT) "Peek test" "vault.sqlite")
        (vault-set-node "peek-node" "target.ath" "peek-start" "peek-end")
        (vault-set-node "peek-child" "leaf.ath" "" "")))
      (init-style "generic")
      (buffer-set-body (current-buffer)
        (stree->tree '(document
          (hlink "Wikilink preview" "tmfs://wikilink/peek-node")
          (concat "A " (math "<sigma>") "-algebra"))))
      (go-to-path '(0 0 0 0))
      (update-current-buffer) (update-forced)
      (display "ATHENA-PEEK-READY\\n"))'''.replace('VAULT', json.dumps(str(vault)))
    with (home / 'app.log').open('w') as log:
        app = subprocess.Popen([str(args.build / 'src/ATHENA.bin'), '-X', '-x', expr],
            cwd=ROOT / 'ATHENA/bin', env=env, stdout=log, stderr=subprocess.STDOUT,
            start_new_session=True)
        deadline = time.monotonic() + 45
        while 'ATHENA-PEEK-READY' not in (home / 'app.log').read_text():
            assert app.poll() is None, f'ATHENA exited {app.returncode}'
            assert time.monotonic() < deadline, 'Startup timed out'
            time.sleep(.2)

        def xdo(*arguments):
            return subprocess.check_output(['xdotool', *arguments], env=env, text=True).strip()

        def screenshot(name):
            path = home / (name + '.png')
            subprocess.run(['import', '-window', 'root', str(path)], env=env, check=True)
            with Image.open(path) as image:
                canvas = image.convert('RGB').crop((230, 300, 970, 650))
                colors = canvas.getcolors(canvas.width * canvas.height)
                return sum(count for count, color in colors
                           if color == (248, 250, 252))

        def expect_overlay(name, visible):
            deadline = time.monotonic() + 20
            while True:
                assert app.poll() is None, f'ATHENA exited {app.returncode}'
                pixels = screenshot(name)
                if (pixels >= 1000) == visible:
                    return
                assert time.monotonic() < deadline, f'{name}: overlay visible={not visible}'
                time.sleep(.2)

        def overlay_bounds(name):
            with Image.open(home / (name + '.png')) as image:
                channels = image.convert('RGB').split()
                masks = [channel.point(lambda value, expected=color:
                                       255 if value == expected else 0)
                         for channel, color in zip(channels, (248, 250, 252))]
                return ImageChops.multiply(ImageChops.multiply(masks[0], masks[1]),
                                           masks[2]).getbbox()

        def same_preview(first, second, bounds):
            with Image.open(home / (first + '.png')) as a, \
                 Image.open(home / (second + '.png')) as b:
                return ImageChops.difference(a.convert('RGB').crop(bounds),
                                            b.convert('RGB').crop(bounds)).getbbox() is None

        time.sleep(3)
        windows = xdo('search', '--onlyvisible', '--pid', str(app.pid)).splitlines()
        xdo('windowfocus', '--sync', windows[0])
        xdo('mousemove', '600', '500', 'click', '1')
        time.sleep(.5)
        xdo('mousemove', '280', '329')
        expect_overlay('before', False)
        xdo('keydown', 'Shift_L')
        expect_overlay('wikilink', True)
        bounds = overlay_bounds('wikilink')
        xdo('keyup', 'Shift_L')
        time.sleep(.3)
        expect_overlay('released', True)
        # Keep the pointer in the small halo, then enter the parent link.
        xdo('mousemove', str(bounds[0] - 12), str(bounds[1] + 20))
        time.sleep(.3)
        expect_overlay('halo', True)
        link = (str(bounds[0] + 30), str(bounds[1] + 16))
        xdo('mousemove', *link)
        xdo('keydown', 'Shift_L')
        deadline = time.monotonic() + 20
        while True:
            expect_overlay('nested', True)
            if not same_preview('wikilink', 'nested', bounds):
                break
            assert time.monotonic() < deadline, 'Nested preview did not open'
            time.sleep(.2)
        xdo('keyup', 'Shift_L')
        xdo('mousemove', str(bounds[0] + 3), str(bounds[1] + 3))
        time.sleep(.3)
        expect_overlay('nested-held', True)
        assert same_preview('nested', 'nested-held', bounds), 'Ancestor hover closed child'
        xdo('key', 'Escape')
        time.sleep(.3)
        expect_overlay('parent', True)
        assert same_preview('wikilink', 'parent', bounds), 'Escape did not restore parent'
        xdo('key', 'Escape')
        expect_overlay('dismissed', False)
        assert xdo('search', '--onlyvisible', '--pid', str(app.pid)).splitlines() == windows, \
            'Escape opened another window instead of only dismissing the peek'
        xdo('mousemove', '290', '360')
        xdo('keydown', 'Shift_L')
        expect_overlay('radioactive', True)
        xdo('mousemove', '1100', '750')
        expect_overlay('left', False)
        xdo('keyup', 'Shift_L')
        xdo('mousemove', '280', '329', 'keydown', 'Shift_L')
        expect_overlay('click-source', True)
        xdo('keyup', 'Shift_L', 'mousemove', *link, 'click', '1')
        deadline = time.monotonic() + 20
        while True:
            assert app.poll() is None, f'ATHENA exited {app.returncode}'
            titles = [xdo('getwindowname', window) for window in
                      xdo('search', '--onlyvisible', '--pid', str(app.pid)).splitlines()]
            if any('leaf.ath' in title for title in titles):
                break
            assert time.monotonic() < deadline, f'Overlay link did not navigate: {titles}'
            time.sleep(.2)
        print('PASS: persistent peeks, halo, nested overlays, Escape priority, leave and link click.')
finally:
    for process in [app, xvfb]:
        if process is not None and process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
    print('Artifacts:', home, flush=True)
