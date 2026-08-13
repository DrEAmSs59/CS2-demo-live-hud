#!/usr/bin/env python3
"""Audit pinned client code signatures against the currently installed DLL.

The nearest exact match is a rebase candidate, not an automatic offset update.
Data globals and signatures containing changed relative displacements still need
manual disassembly review before changing the client fingerprint pin.
"""

from __future__ import annotations

import struct
from pathlib import Path


CLIENT = Path(
    r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive"
    r"\game\csgo\bin\win64\client.dll"
)

# Code bytes already documented in offsets/current.h.
SITES = {
    "IsObserverOrDead": (0x899A80, "8b 91 d0 13 00 00 83 fa ff"),
    "PovSlotPawn": (0x927FA0, "48 83 ec 28 83 f9 ff"),
    "PovSlotController": (0x927F60, "48 83 ec 28 83 f9 ff"),
    "PovGetHudPlayer": (0xC11F70, "40 53 48 83 ec 20 33 c9"),
    "PovGetHudAlive": (0xC12520, "40 53 48 83 ec 20 33 c9"),
    "PovSpectatorTools": (0xC78600, "48 83 ec 28 ba ff ff ff ff"),
    "PovGameplayEvent": (0xC81720, "48 89 4c 24 08"),
    "PovDeathPostProcess": (0xCA59C0, "40 55 53 56 57 41 56"),
    "PovAttackerFeedback": (
        0x847DB0,
        "48 89 5c 24 08 48 89 6c 24 18 48 89 74 24 20 57 "
        "48 81 ec 80 00 00 00",
    ),
    "EventFieldHash": (0x224F30, "48 83 ec 28 45 8b"),
    "FilterPlayerEntity": (0x7F84B0, "40 53 48 83 ec 20"),
    "PawnGetPlayerSlot": (0x900910, "40 53 48 83 ec 20"),
    "EntityPlayerId": (0x1513EF0, "48 83 ec 08"),
    "EntityAbsOrigin": (0x219F80, "40 53 48 83 ec 20"),
    "PovDamageMessage": (0xE011F0, "48 89 5c 24 10"),
    "DamageDirection": (0xDF6CA0, "48 89 5c 24 08"),
    "DamageDirectionCall": (0xE012FB, "e8 a0 59 ff ff"),
    "DamageIndicatorVisible": (0xE085B0, "48 89 5c 24 08"),
    "PovDeathPanelEvent": (0xE04210, "48 89 4c 24 08"),
    "PovLastKillerDamage": (0xC216C0, "48 89 5c 24 08"),
    "DeathPanelDamageSummary": (0xE08AD0, "40 53 48 83 ec 20"),
    "DeathPanelShow": (0xE04E40, "40 57 41 56 48 81 ec a8 00 00 00"),
    "DeathPanelHide": (0xE01BF0, "48 89 5c 24 08"),
    "RadarSoundEmitCall": (0xBA4F1A, "e8 81 11 29 00"),
    "RadarSoundSubmit": (
        0xE360A0,
        "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 40",
    ),
    "RadarSoundCreateCall": (0xE28336, "e8 b5 bf ff ff"),
    "RadarSoundSnippetUpdateCall": (0xE4A72F, "e8 1c fe fe ff"),
    "GameEventDispatch": (0x998070, "40 53 41 54 41 56 48 83 ec 30"),
    "GameEventDispatchVtable": (
        0x1AD4A28,
        "70 80 99 80 01 00 00 00",
    ),
    "HudTeamRelationship": (0x899980, "48 89 5c 24 10 57"),
    "BuyZonePredicate": (0x899440, "48 89 5c 24 08 57"),
    "PovRadarTransactionVtable": (
        0x1B76E68,
        "80 82 e2 80 01 00 00 00",
    ),
    "PovRadarModeUpdate": (0xE21D30, "48 89 5c 24 18"),
    "PovRadarUpdate": (0xE355D0, "48 89 4c 24 08"),
    "PovRadarLocalTransform": (0xE36100, "48 8b c4 55 53"),
    "PovPlayerOverhead": (0xE28810, "41 56 48 81 ec 90 00 00 00"),
    "PovTeamCounter": (0xE289F0, "40 53 48 83 ec 20"),
    "PovBroadcastMode": (
        0x732610,
        "48 83 ec 28 48 8b 0d 3d 76 be 01",
    ),
    "PovVoiceUpdate": (0xE28A20, "40 53 48 83 ec 20"),
    "PovMoneyUpdate": (0xE29470, "40 57 48 83 ec 20"),
    "PovRadioText": (0x1110360, "48 89 5c 24 08"),
    "PovSayText2": (0x1110CA0, "48 89 4c 24 08"),
    "PovHudRootUpdate": (0xE0D430, "40 55 53 41 54"),
    "PovSpecPlayerUpdate": (0xE0C590, "40 53 56 57 41 54"),
    "PovLiveFlashSubmit": (0x1132230, "48 89 6c 24 10"),
    "PovRenderGraph": (
        0x11405E0,
        "48 89 5c 24 18 48 89 4c 24 08",
    ),
    "HudRadarMarkDirty": (0xE4DF20, "48 83 ec 28 48 8d 0d"),
    "IconStyleObsJne": (0xE3C085, "0f 85 07 01 00 00"),
    "IconStyleHltvJne": (0xE3C09D, "0f 85 ef 00 00 00"),
    "IconPaintObsJne": (0xE3E33C, "75 40"),
    "IconPaintHltvJne": (0xE3E350, "75 2c"),
    "RadarEnemyHide": (0xE35B4D, "48 8b 85 80 00 00 00"),
    "KillSoundCmpJe": (0xC83047, "48 3b f0 74 10"),
    "PushNotice": (
        0xE36B50,
        "48 89 5c 24 10 48 89 74 24 18 48 89 7c 24 20",
    ),
    "FindHudElement": (0xDFC710, "40 53 48 83 ec 20"),
    "RadioMuteJne": (0x1110957, "0f 85 f0 01 00 00"),
    "ChatPrintfDemoJne": (0x110DB37, "0f 85 c4 00 00 00"),
    "SayText2DemoJne": (0x1110CD7, "0f 85 22 07 00 00"),
    "VoiceShouldDraw": (0xE3EDA0, "40 53 48 83 ec 20"),
    "VoiceUpdateDispatch": (0xE28A3A, "e9 11 35 02 00"),
    "VoiceUpdate": (
        0xE4BF50,
        "4c 8b dc 55 53 56 57 41 55 41 56 41 57",
    ),
    "VoiceModeCall": (0xE4C03B, "e8 a0 e4 ca ff"),
    "VoiceSpeakingCall": (0xE4C062, "e8 a9 08 d6 ff"),
    "VoiceActivity": (0xAE5500, "48 83 ec 28 89 4c 24 30"),
    "ServerVoiceSubmit": (0xAED960, "48 89 4c 24 08"),
    "VoicePacketSpeakerCall": (0x1110C77, "e8 e4 7d aa ff"),
    "HudMoneyDispatch": (0xE29552, "e9 49 8c 01 00"),
    "HudMoneyUpdate": (0xE421A0, "40 56 57 48 83 ec 48"),
    "GetHudAlivePawn": (0xC12520, "40 53 48 83 ec 20"),
    "TeamCounterBroadcastCall": (0xE4572E, "e8 dd ce 8e ff"),
    "TeamCounterPlayerDataCall": (0xE31B1C, "e8 0f 0e 01 00"),
    "TeamCounterResolvePawn": (
        0xA74A20,
        "48 83 ec 28 48 8b 05 0d 03 62 01 48 85 c0 74 29",
    ),
    "PlayerPawnEventVtable": (
        0x1B2A0C0,
        "f0 85 bf 80 01 00 00 00 40 be c0 80 01 00 00 00",
    ),
    "GrenadePipGate": (0x7A6F90, "40 56 48 83 ec 50"),
    "FlashAmountCvt": (0xCC1144, "f3 0f 2c 93 1c 14 00 00"),
    "FlashOverlayLoad": (0x8A3B3C, "f3 0f 10 86 1c 14 00 00"),
    "FlashSpectatorCompositeTest": (0x1146D14, "84 c0"),
    "KillSoundCvarJne": (0xC830C4, "0f 85 db 08 00 00"),
    "KillSoundModeJne": (0xC830D8, "75 46"),
    "KillSoundModeJe": (0xC830E8, "74 3d"),
    "KillSoundFallbackJe": (0xC83101, "74 1d"),
}


def pe_sections(data: bytes):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    timestamp = struct.unpack_from("<I", data, pe + 8)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    image_size = struct.unpack_from("<I", data, pe + 24 + 56)[0]
    table = pe + 24 + optional_size
    sections = []
    for index in range(count):
        off = table + index * 40
        name = data[off : off + 8].split(b"\0", 1)[0].decode("ascii")
        virtual_size, rva, raw_size, raw_at = struct.unpack_from(
            "<IIII", data, off + 8
        )
        sections.append((name, rva, virtual_size, raw_at, raw_size))
    return timestamp, image_size, sections


def rva_to_offset(sections, rva: int) -> int:
    for _name, base, virtual_size, raw_at, raw_size in sections:
        if base <= rva < base + max(virtual_size, raw_size):
            return raw_at + rva - base
    raise ValueError(f"RVA outside sections: {rva:#x}")


def offset_to_rva(sections, offset: int) -> int:
    for _name, base, virtual_size, raw_at, raw_size in sections:
        if raw_at <= offset < raw_at + max(virtual_size, raw_size):
            return base + offset - raw_at
    raise ValueError(f"file offset outside sections: {offset:#x}")


def find_all(data: bytes, needle: bytes) -> list[int]:
    hits = []
    at = 0
    while True:
        at = data.find(needle, at)
        if at < 0:
            return hits
        hits.append(at)
        at += 1


def main() -> None:
    data = CLIENT.read_bytes()
    timestamp, image_size, sections = pe_sections(data)
    print(f"client timestamp=0x{timestamp:08X} image=0x{image_size:08X}")
    for name, (old_rva, hex_bytes) in SITES.items():
        expected = bytes.fromhex(hex_bytes)
        actual_at_old = data[
            rva_to_offset(sections, old_rva) : rva_to_offset(sections, old_rva)
            + len(expected)
        ]
        if actual_at_old == expected:
            print(f"{name:24} old=0x{old_rva:08X} status=SAME")
            continue
        candidates = []
        for file_at in find_all(data, expected):
            try:
                candidate = offset_to_rva(sections, file_at)
            except ValueError:
                continue
            if abs(candidate - old_rva) <= 0x40000:
                candidates.append(candidate)
        candidates.sort(key=lambda value: abs(value - old_rva))
        if not candidates:
            print(
                f"{name:24} old=0x{old_rva:08X} status=NO_EXACT_CANDIDATE "
                f"actual={actual_at_old.hex(' ')}"
            )
            continue
        nearest = candidates[0]
        suffix = "" if len(candidates) == 1 else f" nearby_candidates={len(candidates)}"
        print(
            f"{name:24} old=0x{old_rva:08X} candidate=0x{nearest:08X} "
            f"delta={nearest - old_rva:+#x}{suffix}"
        )


if __name__ == "__main__":
    main()
