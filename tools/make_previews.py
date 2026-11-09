"""Creates the theme preview images for the settings page (src/config/index.html).

How it works: a copy of the project is built as a special version that paints every color
role of the theme (background, text, selection, ...) in its own marker color, shows two
sample alarms and does not store, schedule or send anything. Screenshots of it are taken in
the emulator (main screen and alarm menu, in German and English). Every pixel is mapped to a
color role (edges as a mix of two roles). The settings page then colors the images with the
selected theme. Finally the normal app is installed again. The alarms in the emulator are
not changed.

Usage (in WSL, emulator running): bash tools/make-previews.sh
"""
import base64
import itertools
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zlib

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = '/tmp/ww_preview'
SHOTS = '/tmp/ww_preview_shots'
RING = False   # set by capture_ring()
LEFT = False   # set by capture_ring(left=True)
QUIET = re.compile(r'SyntaxWarning|:param|Represents|^N/A%|Saved screenshot|PHONESIM')

# Role order = order in the settings page (roleColors in index.html)
ROLES = ['bg', 'fg', 'dim', 'faint', 'hl_bg', 'hl_fg', 'hl_dim', 'hl_faint',
         'header_bg', 'header_fg', 'header_dim', 'bar_bg', 'bar_fg']

# Sample alarms: (name, hour, minute, days, enabled) - slot 1 and slot 2
DEMO = {
    'de': (('Arbeit', 7, 0, 31, 1), ('WOCHENENDE', 9, 30, 96, 0)),
    'en': (('Work', 7, 0, 31, 1), ('WEEKEND', 9, 30, 96, 0)),
}


def pick_markers(n):
    """n colors from the 64-color palette that are as far apart as possible."""
    pal = [(r, g, b) for r in range(4) for g in range(4) for b in range(4)]
    chosen = [(0, 0, 0), (3, 3, 3)]
    while len(chosen) < n:
        best = max(pal, key=lambda c: (min(sum((a - b) ** 2 for a, b in zip(c, d)) for d in chosen), c)
                   if c not in chosen else (-1, c))
        chosen.append(best)
    return chosen


MARKERS = pick_markers(len(ROLES))


def run(cmd, cwd, timeout=180):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    out = '\n'.join(l for l in (p.stdout + p.stderr).splitlines() if not QUIET.search(l))
    return p.returncode, out


def patch(path, old, new):
    with open(path, encoding='utf-8') as f:
        s = f.read()
    assert old in s, '%s: anchor not found: %s' % (path, old[:60])
    with open(path, 'w', encoding='utf-8') as f:
        f.write(s.replace(old, new, 1))


def prepare(lang):
    if os.path.exists(WORK):
        shutil.rmtree(WORK)
    os.makedirs(WORK)
    for name in ('src', 'resources'):
        shutil.copytree(os.path.join(ROOT, name), os.path.join(WORK, name))
    for name in ('package.json', 'wscript'):
        shutil.copy(os.path.join(ROOT, name), WORK)
    c = os.path.join(WORK, 'src', 'c')
    marker = ',\n    '.join('.%s = {.argb = 0x%02X}' % (r, 0xC0 | (m[0] << 4) | (m[1] << 2) | m[2])
                           for r, m in zip(ROLES, MARKERS))
    patch(os.path.join(c, 'theme.c'), 'static void prv_apply(ThemeMode mode) {\n',
          'static void prv_build_marker(void) {\n  s_theme = (Theme) {\n    %s,\n  };\n}\n\n'
          'static void prv_apply(ThemeMode mode) {\n  s_active = mode;\n  prv_build_marker();\n  return;\n'
          % marker)
    patch(os.path.join(c, 'i18n.h'), 'g_lang_de = locale && strncmp(locale, "de", 2) == 0;',
          '(void)locale;\n  g_lang_de = %d;' % (1 if lang == 'de' else 0))
    patch(os.path.join(c, 'scheduler.c'), 'void scheduler_update(void) {\n',
          'void scheduler_update(void) {\n  return;\n')
    patch(os.path.join(c, 'comms.c'), 'void comms_send_state(void) {\n',
          'void comms_send_state(void) {\n  return;\n')
    # never write anything to the emulator's storage
    patch(os.path.join(c, 'comms.c'), 'void comms_set_status(AlarmStatus status, int slot) {\n',
          'void comms_set_status(AlarmStatus status, int slot) {\n  return;\n')
    patch(os.path.join(c, 'alarm_store.c'), 'bool store_save(int slot, const Alarm *alarm) {\n',
          'bool store_save(int slot, const Alarm *alarm) {\n  return true;\n')
    patch(os.path.join(c, 'alarm_store.c'), 'bool store_delete(int slot) {\n',
          'bool store_delete(int slot) {\n  return true;\n')
    patch(os.path.join(c, 'alarm_store.c'), 'void store_set_snooze_until(int slot, time_t when) {\n',
          'void store_set_snooze_until(int slot, time_t when) {\n  return;\n')
    if LEFT:
        patch(os.path.join(c, 'theme.c'),
              's_left_handed = persist_exists(KEY_LEFT_HANDED) && persist_read_bool(KEY_LEFT_HANDED);',
              's_left_handed = true;')
    if RING:
        # ringing screen of slot 1 right after start, clock fixed at 7:00, no auto snooze
        patch(os.path.join(c, 'wristwake.c'), '  if (ring_mask) {', '  ring_mask = 0x01;\n  if (ring_mask) {')
        patch(os.path.join(c, 'ring.c'), '  struct tm *lt = localtime(&now);\n',
              '  struct tm *lt = localtime(&now);\n  lt->tm_hour = 7;\n  lt->tm_min = 0;\n')
        patch(os.path.join(c, 'ring.c'), '#define AUTO_SNOOZE_MS (60 * 1000)', '#define AUTO_SNOOZE_MS (600 * 1000)')
    code = '  // Preview: two sample alarms, in memory only\n'
    code += '  for (int i = 0; i < ALARM_SLOTS; i++) prv_empty_slot(&s_alarms[i], 0);\n'
    for i, (name, hour, minute, days, enabled) in enumerate(DEMO[lang]):
        code += ('  s_alarms[%d].used = 1; s_alarms[%d].enabled = %d; s_alarms[%d].hour = %d;\n'
                 '  s_alarms[%d].minute = %d; s_alarms[%d].days = %d; strcpy(s_alarms[%d].name, "%s");\n'
                 % (i, i, enabled, i, hour, i, minute, i, days, i, name))
    patch(os.path.join(c, 'alarm_store.c'), '  APP_LOG(APP_LOG_LEVEL_INFO, "Storage loaded:',
          code + '  APP_LOG(APP_LOG_LEVEL_INFO, "Storage loaded:')


def emu(*args):
    return run(['pebble'] + list(args) + ['--emulator', 'emery'], WORK, timeout=60)


def shoot(name):
    path = os.path.join(SHOTS, name + '.png')
    emu('screenshot', '--no-open', path)
    return path


def capture(lang):
    prepare(lang)
    code, out = run(['pebble', 'build'], WORK, timeout=300)
    if code != 0:
        sys.exit('Build failed:\n' + out)
    os.makedirs(SHOTS, exist_ok=True)
    code, out = run(['pebble', 'install', '--emulator', 'emery'], WORK, timeout=180)
    if code != 0:
        sys.exit('Install failed:\n' + out)
    time.sleep(3)
    for _ in range(3):
        emu('emu-button', 'click', 'up')
    time.sleep(0.5)
    shots = {'list': shoot('list_' + lang)}
    emu('emu-button', 'click', 'select')
    time.sleep(1)
    shots['menu'] = shoot('menu_' + lang)
    emu('emu-button', 'click', 'back')
    return shots


def load_sunlight():
    """Display color correction (SUNLIGHT in index.html): screen color -> Pebble color.
    The emulator screenshots show the corrected colors."""
    with open(os.path.join(ROOT, 'src', 'config', 'index.html'), encoding='utf-8') as f:
        html = f.read()
    block = re.search(r'var SUNLIGHT = \{(.*?)\};', html, re.S).group(1)
    pairs = re.findall(r"'([0-9a-f]{6})':\s*'([0-9a-f]{6})'", block)
    assert len(pairs) == 64, 'SUNLIGHT table incomplete'
    lv = {'00': 0, '55': 1, 'aa': 2, 'ff': 3}
    return [(tuple(int(shown[k:k + 2], 16) for k in (0, 2, 4)),
             tuple(lv[raw[k:k + 2]] for k in (0, 2, 4))) for raw, shown in pairs]


SUNLIGHT = load_sunlight()


def to_levels(rgb):
    """Closest Pebble color (2 bits per channel) to a corrected screen color."""
    return min(SUNLIGHT, key=lambda e: sum((a - b) ** 2 for a, b in zip(e[0], rgb)))[1]


def capture_ring(lang, left=False):
    """Screenshot of the ringing screen (slot 1, clock fixed at 7:00)."""
    global RING, LEFT
    RING, LEFT = True, left
    try:
        prepare(lang)
    finally:
        RING, LEFT = False, False
    code, out = run(['pebble', 'build'], WORK, timeout=300)
    if code != 0:
        sys.exit('Build failed:\n' + out)
    os.makedirs(SHOTS, exist_ok=True)
    code, out = run(['pebble', 'install', '--emulator', 'emery'], WORK, timeout=180)
    if code != 0:
        sys.exit('Install failed:\n' + out)
    time.sleep(3)
    return shoot(('ring_left_' if left else 'ring_') + lang)


def fit(rgb, cache):
    """Maps a screen color to a role (or to a mix of two roles)."""
    if rgb in cache:
        return cache[rgb]
    lv = to_levels(rgb)
    best = None
    for i, m in enumerate(MARKERS):
        if m == lv:
            best = (0, i, i, 4)
    if best is None:
        for (i, a), (j, b) in itertools.permutations(enumerate(MARKERS), 2):
            for w in (1, 2, 3):
                mix = tuple((x * w + y * (4 - w)) / 4 for x, y in zip(a, b))
                err = sum((p - q) ** 2 for p, q in zip(mix, lv))
                if best is None or err < best[0]:
                    best = (err, i, j, w)
        if best[0] > 0.6:   # matches no role: keep the color as is
            best = (0, -1, 0xC0 | (lv[0] << 4) | (lv[1] << 2) | lv[2], 0)
    cache[rgb] = best[1:]
    return cache[rgb]


def encode(paths):
    symbols, index, cache, images = [], {}, {}, {}
    for key, path in paths.items():
        img = Image.open(path).convert('RGB')
        w, h = img.size
        data = bytearray()
        for rgb in img.getdata():
            sym = fit(rgb, cache)
            if sym not in index:
                index[sym] = len(symbols)
                symbols.append(list(sym))
            data.append(index[sym])
        images[key] = base64.b64encode(zlib.compress(bytes(data), 9)).decode('ascii')
    assert len(symbols) <= 256
    return {'w': w, 'h': h, 'sym': symbols, 'img': images}


def main():
    # --encode-only: re-encode existing screenshots without using the emulator
    encode_only = '--encode-only' in sys.argv
    paths = {}
    for lang in ('de', 'en'):
        if encode_only:
            shots = {k: os.path.join(SHOTS, '%s_%s.png' % (k, lang)) for k in ('list', 'menu')}
        else:
            shots = capture(lang)
        paths[lang + '_list'] = shots['list']
        paths[lang + '_menu'] = shots['menu']
    data = encode(paths)
    literal = [s for s in data['sym'] if s[0] == -1]
    print('Symbols: %d, fixed colors: %d' % (len(data['sym']), len(literal)))
    html_path = os.path.join(ROOT, 'src', 'config', 'index.html')
    with open(html_path, encoding='utf-8') as f:
        html = f.read()
    block = ('/*PREVIEW_DATA_START*/\n  var PREVIEW = %s;\n  /*PREVIEW_DATA_END*/'
             % json.dumps(data, separators=(',', ':')))
    html, n = re.subn(r'/\*PREVIEW_DATA_START\*/.*?/\*PREVIEW_DATA_END\*/', lambda m: block, html,
                      flags=re.S)
    assert n == 1, 'placeholder PREVIEW_DATA missing in index.html'
    with open(html_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(html)
    print('Preview data: %d characters' % len(block))
    code, out = run(['pebble', 'build'], ROOT, timeout=300)
    if encode_only:
        print('Built' if code == 0 else 'ERROR:\n' + out)
        return
    # install the normal app again
    code2, out2 = run(['pebble', 'install', '--emulator', 'emery'], ROOT, timeout=180)
    print('Normal app installed again' if code == 0 and code2 == 0 else 'ERROR:\n' + out + out2)


if __name__ == '__main__':
    main()
