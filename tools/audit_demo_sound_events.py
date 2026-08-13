#!/usr/bin/env python3
"""Read-only summary of demo carriers usable for native-style radar sound rings."""

from __future__ import annotations

import argparse
from collections import Counter
from bisect import bisect_left
from pathlib import Path
from typing import Any, Mapping

from demoparser2 import DemoParser


def columns(rows: Any) -> Mapping[str, list[Any]]:
    if isinstance(rows, Mapping):
        return rows
    if hasattr(rows, "to_dict"):
        return rows.to_dict(orient="list")
    return {}


def row_count(rows: Mapping[str, list[Any]]) -> int:
    return max((len(value) for value in rows.values() if isinstance(value, list)), default=0)


def main() -> None:
    cli = argparse.ArgumentParser()
    cli.add_argument("demo", type=Path)
    args = cli.parse_args()
    parser = DemoParser(str(args.demo.resolve()))

    sound = columns(parser.parse_event("player_sound"))
    print(f"player_sound rows={row_count(sound)} columns={sorted(sound)}")
    radii = sound.get("radius", [])
    steps = sound.get("step", [])
    print(
        "player_sound step="
        f"{sum(bool(value) for value in steps)} "
        f"nonstep={sum(not bool(value) for value in steps)}"
    )
    radius_bins: Counter[int] = Counter()
    for value in radii:
        try:
            radius_bins[int(round(float(value) / 50.0) * 50)] += 1
        except (TypeError, ValueError, OverflowError):
            continue
    print(f"player_sound radius_bins={radius_bins.most_common(16)}")

    footsteps = columns(parser.parse_event("player_footstep"))
    print(f"player_footstep rows={row_count(footsteps)} columns={sorted(footsteps)}")

    jump_rows: Mapping[str, list[Any]] = {}
    for event_name in ("player_jump", "player_land", "player_falldamage"):
        rows = columns(parser.parse_event(event_name))
        print(f"{event_name} rows={row_count(rows)} columns={sorted(rows)}")
        if event_name == "player_jump":
            jump_rows = rows

    foot_ticks_by_player: dict[str, list[int]] = {}
    for player, tick in zip(
        footsteps.get("user_steamid", []), footsteps.get("tick", [])
    ):
        foot_ticks_by_player.setdefault(str(player), []).append(int(tick))
    for values in foot_ticks_by_player.values():
        values.sort()
    jump_to_footstep: Counter[str] = Counter()
    for player, tick in zip(
        jump_rows.get("user_steamid", []), jump_rows.get("tick", [])
    ):
        values = foot_ticks_by_player.get(str(player), [])
        jump_tick = int(tick)
        index = bisect_left(values, jump_tick + 1)
        if index >= len(values):
            jump_to_footstep["none"] += 1
            continue
        delta = values[index] - jump_tick
        if delta <= 16:
            jump_to_footstep["1-16"] += 1
        elif delta <= 32:
            jump_to_footstep["17-32"] += 1
        elif delta <= 64:
            jump_to_footstep["33-64"] += 1
        elif delta <= 128:
            jump_to_footstep["65-128"] += 1
        else:
            jump_to_footstep[">128"] += 1
    print(f"jump_to_next_footstep_ticks={dict(jump_to_footstep)}")

    fires = columns(parser.parse_event("weapon_fire"))
    weapons = Counter(str(value) for value in fires.get("weapon", []))
    print(f"weapon_fire rows={row_count(fires)} columns={sorted(fires)}")
    print(f"weapon_fire weapons={weapons.most_common(24)}")


if __name__ == "__main__":
    main()
