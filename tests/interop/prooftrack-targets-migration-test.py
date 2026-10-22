#!/usr/bin/env python3
"""Isolated, non-destructive checks for ProofTrack AUDMAP target migration."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "tools/interop/migrate-prooftrack-targets.py"
spec = importlib.util.spec_from_file_location("athena_prooftrack_target_test_module", SCRIPT)
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)

PROOF = "11111111-1111-4111-8111-111111111111"
PROOF2 = "22222222-2222-4222-8222-222222222222"
PARAGRAPH = "33333333-3333-4333-8333-333333333333"
FRAGMENT = "44444444-4444-4444-8444-444444444444"
NOTE = "55555555-5555-4555-8555-555555555555"


def document(proofs=None):
    proof_nodes = [
        {"tag": "enunciation", "id":proof, "properties": [
            {"name":"kind", "value":{"type":"string", "value":"proof"}}],
         "children":[{"tag":"document", "children":[{"text":"Proof text"}]}]}
        for proof in (proofs or [PROOF])]
    return {"tag":"document", "children":[
        {"tag":"body", "children":[{"tag":"document", "children":proof_nodes}]}]}


class ProofTrackMigrationTests(unittest.TestCase):
    def test_batch_is_metadata_only_and_idempotent(self):
        original = document([PROOF, PROOF2])
        cfg = {"proofs_by_file":{"Synthetic.ath":{
            PROOF: PARAGRAPH, PROOF2: NOTE}}}
        plan = migration.plan_document(original, "Synthetic.ath", cfg)
        self.assertEqual(len(plan.checks), 2)
        self.assertEqual(len(plan.edits), 2)
        self.assertTrue(all(e["command"] == "update_properties" for e in plan.edits))
        for entry in plan.edits:
            self.assertEqual(entry["parameters"]["remove"], [])
        for idx, entry in enumerate(plan.edits):
            child = original["children"][0]["children"][0]["children"][idx]
            self.assertEqual(entry["path"], [0, 0, idx])
            self.assertEqual(plan.checks[idx]["tree"], child)
            child["properties"].append(entry["parameters"]["set"][0])
        again = migration.plan_document(original, "Synthetic.ath", cfg)
        self.assertEqual([r["status"] for r in again.records],
                         ["already_linked", "already_linked"])
        self.assertEqual(again.edits, [])

    def test_conflicting_target_is_never_overwritten(self):
        doc = document()
        doc["children"][0]["children"][0]["children"][0]["properties"].append(
            {"name":"target", "value":{"type":"reference","value":NOTE}})
        cfg={"proofs_by_file":{"Synthetic.ath":{PROOF: PARAGRAPH}}}
        with self.assertRaisesRegex(ValueError, "Conflicting"):
            migration.plan_document(doc, "Synthetic.ath", cfg)

    def test_preflight_accepts_paragraph_and_note_without_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            (root/"Vaultfile.json").write_text('{"name":"test"}')
            (root/"a.ath").write_text(
                '<a v="3" k="d"><n t="document"><n t="body"><n t="document">'
                f'<n t="concat" i="{PARAGRAPH}">'
                f'<n t="math-block" i="{FRAGMENT}"><t><v>x=y</v></t></n>'
                '</n>'
                f'<n t="enunciation" i="{PROOF}"><x><p n="kind" t="s">proof</p></x>'
                '<n t="document"><t><v>Proof</v></t></n></n>'
                '</n></n></n></a>')
            (root/"b.ath").write_text(
                '<a v="3" k="d"><n t="document"><n t="body"><n t="document">'
                f'<n t="enunciation" i="{NOTE}">'
                '<x><p n="kind" t="s">note</p></x>'
                '<n t="document"><t><v>Statement</v></t></n></n>'
                f'<n t="enunciation" i="{PROOF2}"><x><p n="kind" t="s">proof</p></x>'
                '<n t="document"><t><v>Proof</v></t></n></n>'
                '</n></n></n></a>')
            index={
                PROOF:{"file":"a.ath", "tag":"enunciation", "kind":"proof"},
                PROOF2:{"file":"b.ath", "tag":"enunciation", "kind":"proof"},
                PARAGRAPH:{"file":"a.ath", "tag":"concat","kind":""},
                FRAGMENT:{"file":"a.ath", "tag":"math-block","kind":""},
                NOTE:{"file":"b.ath", "tag":"enunciation","kind":"note"},
            }
            proposals=[
                {"proof_id":PROOF, "target_id":PARAGRAPH, "status":"linked"},
                {"proof_id":PROOF2,"target_id":NOTE,"status":"linked"},
            ]
            manifest=migration.proposal_manifest(proposals,index,root)
            self.assertEqual(manifest["expected_total"],2)
            self.assertEqual(manifest["_files"],["a.ath","b.ath"])
            # A formula fragment inside a concat is not a paragraph target.
            bad=[{"proof_id":PROOF,"target_id":FRAGMENT,"status":"linked"}]
            with self.assertRaisesRegex(ValueError, "non-paragraph"):
                migration.proposal_manifest(bad,index,root)
            # Any no-op retry of the same link is accepted.
            source=(root/"a.ath").read_text()
            source=source.replace('<p n="kind" t="s">proof</p>',
                                  f'<p n="kind" t="s">proof</p><p n="target" t="r" i="{PARAGRAPH}"/>')
            (root/"a.ath").write_text(source)
            retried=migration.proposal_manifest([proposals[0]],index,root)
            self.assertEqual(retried["expected_total"],1)
            # But a different persisted reference is never overwritten.
            (root/"a.ath").write_text(source.replace(PARAGRAPH+'"/>',NOTE+'"/>'))
            with self.assertRaisesRegex(ValueError,"Conflicting"):
                migration.proposal_manifest([proposals[0]],index,root)


if __name__=="__main__":
    unittest.main(verbosity=2)
