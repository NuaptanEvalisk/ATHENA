#!/usr/bin/env python3
"""Prepare and review proof targets without writing to the source vault.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import collections
import json
import os
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET

ALLOWED = {'theorem', 'proposition', 'lemma', 'corollary', 'question', 'example'}
EXCLUDED = {'.athena', '.backup', '.cache', '.git'}
WRAPPERS = {'with', 'style-with', 'style-with*'}


def children(node):
    return [child for child in node if child.tag != 'x']


def tag(node):
    return node.get('t') if node.tag == 'n' else node.tag


def field(node, name):
    return node.find('./x/p[@n="' + name + '"]')


def value(node, name):
    prop = field(node, name)
    return '' if prop is None else ''.join(prop.itertext())


def text(node):
    """Readable projection only; node --xml provides the exact source."""
    if node.tag == 't':
        return node.findtext('v', '')
    if node.tag in {'o', 's'}:
        return node.get('k', '')
    parts = children(node)
    kind = tag(node)
    if kind in WRAPPERS and parts:
        return text(parts[-1])
    if kind == 'hlink' and parts:
        return text(parts[0]) + ' [LINK: ' + text(parts[-1]) + ']'
    if kind == 'scripts' and len(parts) == 5:
        base, left_sub, left_sup, sub, sup = map(text, parts)
        return (('_{' + left_sub + '}' if left_sub else '') +
                ('^{' + left_sup + '}' if left_sup else '') + base +
                ('_{' + sub + '}' if sub else '') + ('^{' + sup + '}' if sup else ''))
    if kind == 'frac' and len(parts) == 2:
        return '(' + text(parts[0]) + ')/(' + text(parts[1]) + ')'
    result = ('\n' if kind in {'document', 'row'} else '').join(map(text, parts))
    if node.tag == 'd':
        result = node.get('l', '') + result + node.get('r', '')
    return result


def blank(node):
    return tag(node) in {'t', 'concat', 'document'} and not text(node).strip()


def unwrap(node):
    while True:
        parts = children(node)
        if tag(node) in WRAPPERS and parts:
            node = parts[-1]
        elif tag(node) in {'concat', 'document'}:
            parts = [p for p in parts if not blank(p)]
            if len(parts) != 1:
                return node
            node = parts[0]
        else:
            return node


def describe(node, limit=700):
    prop = field(node, 'target')
    result = dict(id=node.get('i', ''), tag=tag(node), kind=value(node, 'kind'),
                  variant=value(node, 'variant'), name=value(node, 'name'),
                  target=prop.get('i', '') if prop is not None else '')
    rendered = text(node).strip()
    result['text'] = rendered if limit is None else rendered[:limit]
    result['truncated'] = limit is not None and len(rendered) > limit
    if tag(node) == 'transclude':
        parts = children(node)
        result['refs'] = ([text(p) for p in children(parts[0])]
                          if len(parts) == 1 and tag(parts[0]) == 'tuple' else [])
    return result


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')


def read_json(path):
    return json.loads(path.read_text())


def separator(p):
    if re.match(r'^(Hint\b|提示|Requirement\b)', p['text'].lstrip(), re.I):
        return 'hint'
    if p['kind'] == 'note' and re.fullmatch(r'Proof\s+\d+', p['text'].strip(), re.I):
        return 'proof label'
    return ''


def predecessor_records(previous):
    result = []
    for node in reversed(previous):
        entry = describe(unwrap(node))
        result.append(entry)
        if not separator(entry):
            break
    return list(reversed(result))


def body(path):
    root = ET.parse(path).getroot()
    if root.tag != 'a' or root.get('v') != '3':
        raise ValueError(f'Expected XML v3: {path}')
    result = root.find('./n[@t="document"]/n[@t="body"]')
    if result is None:
        raise ValueError(f'Missing document body: {path}')
    return result


def prepare(vault, work):
    vault, work = vault.resolve(), work.resolve()
    if work == vault or vault in work.parents:
        raise ValueError('The review workspace must be outside the source vault')
    work.mkdir(parents=True, exist_ok=False)
    (work / 'cases').mkdir()
    (work / 'decisions').mkdir()
    index, predecessors, cases, duplicates = {}, {}, {}, set()
    files = []
    for directory, dirs, names in os.walk(vault):
        dirs[:] = sorted(d for d in dirs if d not in EXCLUDED)
        files.extend(Path(directory) / n for n in sorted(names) if n.endswith('.ath'))

    def walk(node, relative, path=(), previous=(), enclosing=()):
        if tag(node) in {'quote', 'quasiquote', 'macro', 'xmacro'}:
            return
        identity = node.get('i', '')
        kind = value(node, 'kind') if tag(node) == 'enunciation' else ''
        if identity:
            entry = dict(id=identity, file=relative, path=list(path), tag=tag(node), kind=kind)
            if kind:
                entry.update(describe(node, 400))
            if identity in index:
                duplicates.add(identity)
            index[identity] = entry
        if kind in {'proof', 'solution'}:
            predecessors[identity] = predecessor_records(previous)
        if kind == 'proof':
            cases[identity] = dict(proof={**describe(node, 4000), 'file': relative},
                                   enclosing=list(enclosing),
                                   before=[describe(p, 1000) for p in previous[-10:]])
        if kind:
            enclosing = (*enclosing, dict(id=identity, kind=kind))
        parts = children(node)
        if tag(node) in {'document', 'concat'}:
            preceding = list(previous)
            for i, child in enumerate(parts):
                walk(child, relative, path + (i,), preceding, enclosing)
                child_id = unwrap(child).get('i', '')
                if child_id in cases:
                    cases[child_id]['after'] = [describe(p, 600) for p in parts[i+1:i+4]]
                if not blank(child):
                    preceding.append(child)
        elif tag(node) in WRAPPERS | {'surround'} and parts:
            walk(parts[-1], relative, path + (len(parts)-1,), previous, enclosing)
        else:
            for i, child in enumerate(parts):
                walk(child, relative, path + (i,), (), enclosing)

    for i, path in enumerate(files, 1):
        walk(body(path), str(path.relative_to(vault)))
        if i % 100 == 0 or i == len(files):
            print(f'Scanning {i}/{len(files)}', flush=True)

    resolved = {}

    def eligible(identity):
        return identity not in duplicates and index.get(identity, {}).get('kind') in ALLOWED

    def through(p, seen):
        if eligible(p['id']):
            return p['id'], ['adjacent ' + p['kind']]
        if p['kind'] == 'proof':
            target, trace = infer(p['id'], seen)
            return target, ['inherit proof ' + p['id']] + trace
        if p['tag'] == 'transclude' and len(p['refs']) == 1:
            source = p['refs'][0]
            if eligible(source):
                return source, ['transcluded statement ' + source]
            if index.get(source, {}).get('kind') == 'proof':
                target, trace = infer(source, seen)
                return target, ['inherit transcluded proof ' + source] + trace
        return '', []

    def infer(identity, seen):
        if identity in resolved:
            return resolved[identity]['target'], resolved[identity]['trace']
        if identity in seen or identity in duplicates:
            return '', []
        seen = seen | {identity}
        own = index[identity]
        if own.get('target'):
            if not eligible(own['target']):
                return '', ['existing target requires review']
            return own['target'], ['existing proof.target']
        prior = predecessors[identity]
        trace = []
        pos = len(prior)-1
        while pos >= 0 and separator(prior[pos]):
            trace.append('skip ' + separator(prior[pos]) + ' ' + prior[pos]['id'])
            pos -= 1
        if pos < 0:
            return '', []
        prev = prior[pos]
        if prev['kind'] == 'solution':
            before_solution = predecessors.get(prev['id'], [])
            if not before_solution:
                return '', []
            p = before_solution[-1]
            if p['kind'] != 'question' and not (p['tag'] == 'transclude' and
                    len(p['refs']) == 1 and eligible(p['refs'][0])):
                return '', []
            target, tail = through(p, seen)
            trace.append('before solution ' + prev['id'])
        else:
            target, tail = through(prev, seen)
        return target, trace + tail

    for identity in cases:
        target, trace = infer(identity, set())
        if target:
            resolved[identity] = dict(target=target, trace=trace, file=index[identity]['file'])
    pending = [identity for identity in cases if identity not in resolved]
    variants = collections.Counter(cases[i]['proof']['variant'] or 'plain' for i in pending)
    config = dict(vault=str(vault), files=len(files), proofs=len(cases),
                  automatic=len(resolved), pending=len(pending), pending_variants=dict(variants),
                  duplicate_uuids=sorted(duplicates), allowed_target_kinds=sorted(ALLOWED))
    write_json(work / 'workspace.json', config)
    write_json(work / 'index.json', index)
    write_json(work / 'automatic.json', resolved)
    write_json(work / 'queue.json', pending)
    for identity in pending:
        record = cases[identity]
        references = set()
        for p in [record['proof'], *record['before']]:
            references.update(p.get('refs', []))
            references.update(re.findall(r'tmfs://(?:wikilink|transclude)/([0-9a-fA-F-]{36})', p['text']))
        record['referenced_nodes'] = [index[i] for i in sorted(references) if i in index]
        record['previous_proof_targets'] = {
            p['id']: resolved[p['id']] for p in record['before'] if p['id'] in resolved}
        write_json(work / 'cases' / (identity + '.json'), record)
    shutil.copy2(__file__, work / 'review.py')
    prompt = Path(__file__).with_name('prooftrack-luna-prompt.md').read_text()
    (work / 'PROMPT.md').write_text(
        f'Workspace: `{work}`\n\nSource vault (read-only): `{vault}`\n\n' + prompt)
    (work / 'README.md').write_text(
        '# ProofTrack / Luna\n\n' + json.dumps(config, ensure_ascii=False, indent=2) +
        '\n\nRead PROMPT.md. Run commands in this directory.\n\n'
        '```sh\npython3 review.py status\npython3 review.py next\n'
        'python3 review.py case PROOF_UUID\npython3 review.py context NODE_UUID --before 20 --after 5\n'
        'python3 review.py node NODE_UUID\npython3 review.py node NODE_UUID --xml\n'
        'python3 review.py search "keyword"\npython3 review.py validate\n```\n\n'
        'automatic.json and decisions/ are proposals only. No command applies changes to Notes.\n')
    print(json.dumps(config, ensure_ascii=False, indent=2))
    print(work / 'PROMPT.md')


def validate(work, decision, index, queue):
    identity = decision['proof_id']
    if identity not in queue:
        raise ValueError('Proof is not in the review queue: ' + identity)
    if not decision.get('reason', '').strip() or not decision.get('evidence'):
        raise ValueError('Reason and evidence are required')
    if decision['status'] == 'linked':
        target = decision['target_id']
        if index.get(target, {}).get('kind') not in ALLOWED:
            raise ValueError('Target must be an outer provable/question/example UUID: ' + target)
        if target in read_json(work / 'workspace.json')['duplicate_uuids']:
            raise ValueError('Ambiguous target UUID: ' + target)
    elif decision['status'] in {'needs_statement', 'ambiguous'}:
        if decision.get('target_id') is not None:
            raise ValueError('Unresolved decisions must have target_id=null')
    else:
        raise ValueError('Unknown decision status')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, default=Path.cwd())
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('prepare')
    p.add_argument('--vault', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    for name in ('status', 'next', 'validate'):
        commands.add_parser(name)
    for name in ('case', 'node', 'context'):
        p = commands.add_parser(name)
        p.add_argument('uuid')
        if name == 'node':
            p.add_argument('--xml', action='store_true')
        if name == 'context':
            p.add_argument('--before', type=int, default=12)
            p.add_argument('--after', type=int, default=3)
    p = commands.add_parser('search')
    p.add_argument('query')
    p.add_argument('--limit', type=int, default=20)
    p = commands.add_parser('decide')
    p.add_argument('uuid')
    choice = p.add_mutually_exclusive_group(required=True)
    choice.add_argument('--target')
    choice.add_argument('--unresolved', choices=['needs_statement', 'ambiguous'])
    p.add_argument('--reason', required=True)
    p.add_argument('--evidence', action='append', required=True)
    p.add_argument('--candidate', action='append', default=[])
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args.vault, args.output)
        return
    work = args.workspace.resolve()
    config, queue = read_json(work / 'workspace.json'), read_json(work / 'queue.json')
    decisions = {p.stem: read_json(p) for p in (work / 'decisions').glob('*.json')}
    if args.command == 'status':
        print(json.dumps(dict(config, reviewed=len(decisions), remaining=len(set(queue)-decisions.keys()),
                              decisions=dict(collections.Counter(d['status'] for d in decisions.values()))),
                         ensure_ascii=False, indent=2))
        return
    if args.command in {'case', 'next'}:
        identity = args.uuid if args.command == 'case' else next((i for i in queue if i not in decisions), None)
        print((work / 'cases' / (identity + '.json')).read_text() if identity else 'Review complete.')
        return
    index = read_json(work / 'index.json')
    if args.command == 'decide':
        decision = dict(proof_id=args.uuid, status='linked' if args.target else args.unresolved,
                        target_id=args.target, reason=args.reason, evidence=args.evidence,
                        candidates=args.candidate)
        validate(work, decision, index, queue)
        write_json(work / 'decisions' / (args.uuid + '.json'), decision)
        print('Recorded ' + args.uuid)
    elif args.command == 'validate':
        errors = []
        for identity, decision in decisions.items():
            try:
                if identity != decision['proof_id']:
                    raise ValueError('Decision filename and proof_id differ')
                validate(work, decision, index, queue)
            except (ValueError, KeyError) as exc:
                errors.append(f'{identity}: {exc}')
        missing = sorted(set(queue)-decisions.keys())
        write_json(work / 'validation.json', dict(errors=errors, missing=missing))
        if not errors:
            combined = {i: dict(proof_id=i, status='linked', target_id=r['target'],
                                reason='Deterministic rules', evidence=r['trace'])
                        for i, r in read_json(work / 'automatic.json').items()}
            combined.update(decisions)
            write_json(work / 'proposed-targets.json', list(combined.values()))
        print(json.dumps(dict(errors=errors, reviewed=len(decisions), missing=len(missing)), indent=2))
        if errors or missing:
            raise SystemExit(1)
    elif args.command == 'search':
        matches = [r for r in index.values() if r.get('kind') and
                   args.query.casefold() in (r.get('name', '') + ' ' + r.get('text', '') + ' ' + r['file']).casefold()]
        print(json.dumps(dict(total=len(matches), matches=matches[:args.limit]), ensure_ascii=False, indent=2))
    else:
        record = index[args.uuid]
        source = body(Path(config['vault']) / record['file'])
        parents = {child: parent for parent in source.iter() for child in children(parent)}
        node = next((n for n in source.iter() if n.tag != 'p' and n.get('i') == args.uuid), None)
        if node is None:
            raise ValueError('UUID is no longer present in its indexed source file: ' + args.uuid)
        ancestors = [node]
        while ancestors[-1] in parents:
            ancestors.append(parents[ancestors[-1]])
        ancestors.reverse()
        if args.command == 'node':
            print(ET.tostring(node, encoding='unicode') if args.xml else
                  json.dumps(dict(file=record['file'], **describe(node, None)), ensure_ascii=False, indent=2))
        else:
            siblings = children(ancestors[-2])
            position = siblings.index(node)
            print(json.dumps(dict(file=record['file'],
                                  enclosing=[describe(n, 200) for n in ancestors[:-1] if tag(n) == 'enunciation'],
                                  before=[describe(n, 1200) for n in siblings[max(0, position-args.before):position]],
                                  node=describe(node, 4000),
                                  after=[describe(n, 1200) for n in siblings[position+1:position+1+args.after]]),
                             ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
