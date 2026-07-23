#!/usr/bin/env python3
"""Regenerate ATHENA's notable-person name snapshot from Wikidata."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import time
import urllib.parse
import urllib.request
from pathlib import Path


ENDPOINT = "https://query.wikidata.org/sparql"
USER_AGENT = "ATHENA-person-names/0.5 (https://athena.evalisk.org/)"

OCCUPATIONS = {
    "mathematician": "Q170790",
    "physicist": "Q169470",
    "computer scientist": "Q82594",
    "statistician": "Q2732142",
    "logician": "Q14565331",
    "philosopher": "Q4964182",
    "economist": "Q188094",
    "scientist": "Q901",
}

PAGE_SIZE = 5000

CHECKPOINT_SCHEMA = 3

QUERY = """
SELECT DISTINCT ?person ?personLabel ?familyNameLabel WHERE {{
  ?person wdt:P106 wd:{occupation};
          rdfs:label ?personLabel.
  FILTER(LANG(?personLabel) = "en")
  FILTER(STRSTARTS(LCASE(STR(?personLabel)), "{initial}"))
  OPTIONAL {{
    ?person wdt:P734 ?familyName.
    ?familyName rdfs:label ?familyNameLabel.
    FILTER(LANG(?familyNameLabel) = "en")
  }}
}}
ORDER BY ?person ?familyNameLabel
LIMIT {limit}
OFFSET {offset}
"""


def request_json(query: str, retries: int = 6) -> list[dict[str, object]]:
    params = urllib.parse.urlencode({"query": query, "format": "json"})
    url = f"{ENDPOINT}?{params}"
    for attempt in range(retries):
        request = urllib.request.Request(
            url,
            headers={
                "Accept": "application/sparql-results+json",
                "User-Agent": USER_AGENT,
            },
        )
        try:
            with urllib.request.urlopen(request, timeout=90) as response:
                payload = response.read().decode("utf-8")
            result = json.loads(payload)
            return result["results"]["bindings"]
        except Exception:
            if attempt + 1 == retries:
                raise
            time.sleep(min(60, 4 * (2**attempt)))
    return []


def qid(uri: str) -> str:
    return uri.rstrip("/").rsplit("/", 1)[-1]


def normalized_label(value: object) -> str:
    return " ".join(str(value).split())


def fetch_partition(
    occupation_qid: str, initial: str
) -> list[tuple[str, str, str]]:
    rows: dict[tuple[str, str, str], None] = {}
    offset = 0
    while True:
        query = QUERY.format(
            occupation=occupation_qid,
            initial=initial,
            limit=PAGE_SIZE,
            offset=offset,
        )
        page = request_json(query)
        for item in page:
            entity = qid(str(item.get("person", {}).get("value", "")))
            label = normalized_label(
                item.get("personLabel", {}).get("value", "")
            )
            family = normalized_label(
                item.get("familyNameLabel", {}).get("value", "")
            )
            if entity and label:
                rows[(label, "label", entity)] = None
            if entity and family:
                rows[(family, "family", entity)] = None
        if len(page) < PAGE_SIZE:
            break
        offset += PAGE_SIZE
    return list(rows)


def save_checkpoint(
    checkpoint: Path,
    done: set[str],
    rows: dict[tuple[str, str, str], None],
) -> None:
    state = {
        "schema": CHECKPOINT_SCHEMA,
        "done": sorted(done),
        "rows": [
            [name, kind, entity] for name, kind, entity in sorted(rows)
        ],
    }
    temporary = checkpoint.with_suffix(checkpoint.suffix + ".tmp")
    temporary.write_text(
        json.dumps(state, ensure_ascii=False),
        encoding="utf-8",
    )
    temporary.replace(checkpoint)


def collect(
    checkpoint: Path, workers: int
) -> dict[tuple[str, str, str], None]:
    if checkpoint.exists():
        state = json.loads(checkpoint.read_text(encoding="utf-8"))
    else:
        state = {}
    if state.get("schema") != CHECKPOINT_SCHEMA:
        state = {"schema": CHECKPOINT_SCHEMA, "done": [], "rows": []}

    done = set(state["done"])
    rows: dict[tuple[str, str, str], None] = {}
    for name, kind, entity in state["rows"]:
        name = normalized_label(name)
        if name and kind in {"label", "family"}:
            rows[(name, kind, entity)] = None

    pending = [
        (f"{occupation_qid}:{initial}", occupation_qid, initial)
        for occupation_qid in OCCUPATIONS.values()
        for initial in "abcdefghijklmnopqrstuvwxyz"
        if f"{occupation_qid}:{initial}" not in done
    ]
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=max(1, workers)
    ) as executor:
        futures = {
            executor.submit(fetch_partition, occupation_qid, initial): key
            for key, occupation_qid, initial in pending
        }
        try:
            for future in concurrent.futures.as_completed(futures):
                key = futures[future]
                for row in future.result():
                    rows[row] = None
                done.add(key)
                save_checkpoint(checkpoint, done, rows)
                print(f"{key}: {len(rows)} records", flush=True)
        except BaseException:
            for future in futures:
                future.cancel()
            raise
    return rows


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--checkpoint",
        type=Path,
        default=Path("/tmp/athena-person-names-checkpoint.json"),
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=3,
        help="Concurrent Wikidata partitions (default: 3)",
    )
    args = parser.parse_args()

    rows = collect(args.checkpoint, args.workers)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    unique_rows: dict[tuple[str, str], str] = {}
    for name, kind, entity in sorted(rows):
        unique_rows.setdefault((name, kind), entity)
    with args.output.open("w", encoding="utf-8", newline="\n") as output:
        output.write(
            "# Generated from Wikidata person and family-name labels for "
            "selected scholarly occupations.\n"
            "# name\\tkind\\tWikidata entity\n"
        )
        for (name, kind), entity in sorted(
            unique_rows.items(),
            key=lambda row: (row[0][0].casefold(), row[0][1], row[1]),
        ):
            output.write(f"{name}\t{kind}\t{entity}\n")


if __name__ == "__main__":
    main()
