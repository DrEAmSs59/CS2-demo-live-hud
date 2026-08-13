#pragma once

#include <cstdint>
#include <string_view>

namespace live_hud::event_compensation {

enum class GrenadeKind {
  none,
  flashbang,
  smoke,
  high_explosive,
  incendiary,
  decoy,
};

enum class RadarSoundKind {
  none,
  footstep,
  weapon,
  scope,
  utility,
};

enum class DeathBannerResolution {
  wait,
  native_summary,
  zero_summary,
};

struct RadarSoundSpec {
  RadarSoundKind kind = RadarSoundKind::none;
  int radius = 0;
  float duration = 0.0f;
  bool is_footstep = false;

  explicit operator bool() const noexcept {
    return kind != RadarSoundKind::none && radius > 0 && duration > 0.0f;
  }
};

// Demo files retain the weapon name on player_death even though they do not
// retain the live cash-award user message. This is the standard competitive
// kill award for that weapon family.
int kill_cash_reward(std::string_view weapon) noexcept;

// weapon_fire is the recoverable source for throw notices that are absent as
// RadioText messages in a .dem file.
GrenadeKind grenade_kind(std::string_view weapon) noexcept;
const char* grenade_localization_token(GrenadeKind kind) noexcept;

// Third-party GOTV demos omit player_sound but retain these client events.
// The returned parameters are submitted to CS2's native radar-sound ingress;
// Panorama never draws a replacement circle. Native EmitSound metadata, when
// present at runtime, remains authoritative and dedupes this approximation.
RadarSoundSpec radar_sound_from_event(std::string_view event_name,
                                      std::string_view weapon = {},
                                      bool silenced = false) noexcept;

// The current client exposes followed-player movement as a stable
// 548-unit / 100-ms pawn sound. That pulse is too small and short to present
// the normal running/landing radar cue in Demo POV. Keep the predicate narrow;
// the caller restores only its presentation radius/lifetime while retaining
// the generic native slot, and leaves the paired 204-unit jump pulse intact.
bool native_movement_sound_needs_presentation_repair(
    int radius, float duration, bool is_footstep) noexcept;

// The reconstructed movement profile stays in the generic native slot. Mark
// only that exact profile for one native max-presentation trigger so HudRadar
// restarts its own player-sound-max animation; weapon and jump rows must not
// inherit the stronger style.
bool native_generic_footstep_needs_max(int radius, float duration,
                                       bool is_footstep) noexcept;

// A reconstructed event is held for a short native-priority window. Suppress
// it when the same sound family reached the native audio ingress just before
// or after the event; entity audio and game events need not run in one frame.
bool native_sound_covers_event(std::uint64_t queued_stamp,
                               std::uint64_t current_stamp,
                               std::uint64_t last_native_stamp) noexcept;

// player_death and SendLastKillerDamageToClient are independent native inputs
// and either one can arrive first. Resolve them in one short, symmetric window:
// replay a captured native summary when it pairs with the death event, wait for
// a late native message, and use a zero summary only after that wait expires.
DeathBannerResolution resolve_death_banner(
    std::uint64_t death_stamp, std::uint64_t current_stamp,
    std::uint64_t last_killer_stamp, std::uint64_t pair_window) noexcept;

}  // namespace live_hud::event_compensation
