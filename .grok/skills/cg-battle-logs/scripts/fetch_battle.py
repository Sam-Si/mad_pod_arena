#!/usr/bin/env python3
"""Fetch one CodinGame CSB/Mad Pod battle JSON by id or share-replay URL."""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

API = "https://www.codingame.com/services/gameResult/findByGameId"
ID_RE = re.compile(
    r"(?:share-replay|replay)/(\d+)|battle_(\d+)\.json|^(\d+)$"
)


def parse_game_id(s: str) -> int:
    s = s.strip()
    m = ID_RE.search(s)
    if not m:
        raise SystemExit(f"Could not parse game id from: {s!r}")
    for g in m.groups():
        if g:
            return int(g)
    raise SystemExit(f"Could not parse game id from: {s!r}")


def fetch(game_id: int, viewer_user_id: int | None, cookie: str | None) -> dict:
    # null 2nd arg works for public share-replay without auth
    body_obj: list = [game_id, viewer_user_id]
    data = json.dumps(body_obj).encode("utf-8")
    headers = {
        "Content-Type": "application/json;charset=UTF-8",
        "Accept": "application/json, text/plain, */*",
        "Origin": "https://www.codingame.com",
        "Referer": f"https://www.codingame.com/replay/{game_id}",
        "User-Agent": "mad_pod_arena/cg-battle-logs (physics research)",
    }
    if cookie:
        headers["Cookie"] = cookie
    req = urllib.request.Request(API, data=data, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        err_body = e.read().decode("utf-8", errors="replace")
        raise SystemExit(f"HTTP {e.code}: {err_body}") from e


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("target", help="game id, share-replay URL, or battle_*.json name")
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="output path (default: battles/copy_pasted_battles/battle_<id>.json)",
    )
    ap.add_argument(
        "--user-id",
        type=int,
        default=None,
        help="viewer userId (only if you are that user + CG_COOKIE). Default: null (public).",
    )
    ap.add_argument(
        "--cookie",
        default=os.environ.get("CG_COOKIE"),
        help="Cookie header (or set CG_COOKIE). Never commit secrets.",
    )
    ap.add_argument("--pretty", action="store_true", help="indent JSON")
    args = ap.parse_args()

    game_id = parse_game_id(args.target)
    out = args.output or Path("battles/copy_pasted_battles") / f"battle_{game_id}.json"
    out.parent.mkdir(parents=True, exist_ok=True)

    payload = args.user_id  # None → JSON null
    data = fetch(game_id, payload, args.cookie)

    if not isinstance(data, dict) or "frames" not in data:
        raise SystemExit(f"Unexpected response (no frames): {data!r}"[:500])

    with out.open("w", encoding="utf-8") as f:
        if args.pretty:
            json.dump(data, f, indent=2, ensure_ascii=False)
        else:
            json.dump(data, f, ensure_ascii=False)
        f.write("\n")

    n = len(data.get("frames") or [])
    print(f"wrote {out} gameId={data.get('gameId')} frames={n} ranks={data.get('ranks')}")


if __name__ == "__main__":
    main()
