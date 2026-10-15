#!/usr/bin/env python3
"""Exercise canonical enunciations and UTF-8 MathJax in an isolated website."""

import argparse
from html import unescape
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile
from xml.etree import ElementTree


def fixture() -> str:
    return (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<athena-document version="2" text-model="utf-8">'
        '<node tag="document"><node tag="style"><text><value>generic</value>'
        '</text></node><node tag="body"><node tag="document">'
        '<node tag="math"><text><value>π∈ℝ</value></text></node>'
        '<node tag="math"><text><value>0&lt;∞ and x&lt;0</value></text></node>'
        '<text><value>Literal &lt;alpha&gt;, &lt;less&gt;, &lt;gtr&gt; '
        'and &lt;0&gt; markers.</value></text>'
        '<node tag="math"><node tag="concat">'
        '<node tag="named-symbol"><text><value>texmacs:⟨</value></text></node>'
        '<text><value>x</value></text>'
        '<node tag="named-symbol"><text><value>texmacs:⟩</value></text></node>'
        '<text><value> and </value></text>'
        '<node tag="named-symbol"><text><value>texmacs:∞</value></text></node>'
        '</node></node>'
        '<node tag="enunciation"><properties>'
        '<property name="kind" type="string">definition</property>'
        '<property name="name" type="rich_tree"><text><value>Named result</value>'
        '</text></property><property name="numbered" type="boolean">true</property>'
        '</properties><node tag="document">'
        '<text><value>Enunciation body survives.</value></text>'
        '<node tag="equation*"><node tag="document"><node tag="concat">'
        '<node tag="big"><text><value>∑</value></text></node>'
        '<node tag="rsub"><text><value>i∈I</value></text></node>'
        '</node></node></node></node></node>'
        '<node tag="enunciation"><properties>'
        '<property name="kind" type="string">proof</property>'
        '<property name="name" type="rich_tree"><text><value /></text></property>'
        '<property name="numbered" type="boolean">false</property>'
        '</properties><node tag="document"><text><value>Proof body survives.'
        '</value></text></node></node>'
        '</node></node></node></athena-document>'
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--document', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='athena-website-math-') as tmp:
        home = Path(tmp)
        vault = home / 'vault'
        vault.mkdir()
        (vault / 'Vaultfile.json').write_text(json.dumps({'name': 'Math HTML test'}))
        source = vault / 'source.ath'
        if args.document:
            shutil.copyfile(args.document, source)
        else:
            source.write_text(fixture(), encoding='utf-8')
        site = home / 'site'
        (vault / 'websites.json').write_text(json.dumps({
            'version': 1, 'websites': [{
                'id': 'math', 'name': 'Math export',
                'selector': {'kind': 'path', 'path': 'source.ath'},
                'destination': str(site), 'generatePdfs': False,
                'entrypoint': {'kind': 'file', 'path': 'source.ath'},
            }],
        }))
        system = home / 'profile/system'
        system.mkdir(parents=True)
        (system / 'sys_state.json').write_text(json.dumps({
            'format': 'athena-system-state', 'version': 3,
        }))
        env = dict(os.environ)
        runtime = args.runtime.resolve()
        resources = args.resources.resolve()
        env.update({
            'HOME': str(home), 'ATHENA_HOME_PATH': str(home / 'profile'),
            'XDG_CONFIG_HOME': str(home / 'config'),
            'XDG_CACHE_HOME': str(home / 'cache'),
            'XDG_DATA_HOME': str(home / 'data'),
            'ATHENA_PATH': str(resources), 'QT_QPA_PLATFORM': 'offscreen',
            'GUILE_AUTO_COMPILE': '0',
            'GUILE_LOAD_PATH': str(runtime / 'share/guile/3.0'),
            'GUILE_LOAD_COMPILED_PATH': str(runtime / 'lib/guile/3.0/ccache'),
            'LD_LIBRARY_PATH': ':'.join((str(runtime / 'lib'),
                str(resources / 'lib'), env.get('LD_LIBRARY_PATH', ''))),
        })
        with (home / 'runtime.log').open('w+') as log:
            process = subprocess.Popen([
                str(args.binary.resolve()), '-H', '-X', '--generate-website',
                str(vault), 'math'], cwd=home, env=env, start_new_session=True,
                stdout=log, stderr=subprocess.STDOUT)
            try:
                process.wait(timeout=90)
            finally:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
            log.seek(0)
            output = log.read()
        if process.returncode:
            raise AssertionError(output[-12000:])
        html = (site / 'source.html').read_text(encoding='utf-8')
        document = ElementTree.fromstring(source.read_bytes())
        required = sum(1 for node in document.iter('node')
                       if node.get('tag') == 'enunciation')
        equations = sum(1 for node in document.iter('node')
                        if node.get('tag') == 'equation*')
        actual = len(re.findall(r'<div\b[^>]*\bclass="enunciation enunciation-', html))
        assert actual == required, (
            f'Expected {required} enunciations, got {actual}; '
            f'body={html[html.find("<body"):][:1600]!r}')
        if not args.document:
            for needle in ('Named result', 'Enunciation body survives.',
                           'Proof body survives.', 'π∈ℝ',
                           'Literal <alpha>, <less>, <gtr> and <0> markers.'):
                assert needle in unescape(html), f'Missing {needle}'
            assert re.search(r'0\s*<\s*∞', html), 'Literal 0 < ∞ comparison was lost'
            assert re.search(r'x\s*<\s*0', html), 'Literal x < 0 comparison was lost'
            assert '\\sum' in html, 'Unicode sum was not converted'
            assert '\\langle' in html, 'Unicode opening angle was not converted'
            assert '\\rangle' in html, 'Unicode closing angle was not converted'
            assert '\\infty' in html, 'Unicode infinity token was not converted'
            assert '\\alpha' not in html, 'Literal <alpha> was parsed as a symbol'
            assert r'i∈I' in html, 'UTF-8 formula corrupted ASCII subscript'
            assert '`' * 8 not in html, 'Formula letters expanded into invalid filler'
        elif b'Orientation of Linear Spaces' in source.read_bytes():
            assert 'Orientation of Linear Spaces' in html, 'Named enunciation lost'
        assert '\\big∑' not in html and '\\big∫' not in html
        assert '\\nonconverted' not in html, 'Math symbols became nonconverted'
        assert 'non converted symbol: 0' not in output
        assert 'non converted symbol: ⟨' not in output
        assert 'non converted symbol: ⟩' not in output
        assert 'non converted symbol: ∞' not in output
        assert 'âĹĹ' not in html, 'Math content was decoded as Cork'
        assert html.count('\\[') >= equations, 'Display equation lost in HTML'
        print(f'ATHENA-WEBSITE-MATH-PASS enunciations={actual} '
              f'display={html.count(chr(92) + "[")}')


if __name__ == '__main__':
    main()
