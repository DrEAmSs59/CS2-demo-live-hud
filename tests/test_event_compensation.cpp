#include "dll/event_compensation.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("kill cash compensation follows competitive weapon awards") {
  using live_hud::event_compensation::kill_cash_reward;

  REQUIRE(kill_cash_reward("ak47") == 300);
  REQUIRE(kill_cash_reward("weapon_awp") == 100);
  REQUIRE(kill_cash_reward("weapon_mp9") == 600);
  REQUIRE(kill_cash_reward("nova") == 900);
  REQUIRE(kill_cash_reward("weapon_knife_karambit") == 1500);
}

TEST_CASE("throw compensation accepts only grenade weapon_fire names") {
  using live_hud::event_compensation::GrenadeKind;
  using live_hud::event_compensation::grenade_kind;
  using live_hud::event_compensation::grenade_localization_token;

  REQUIRE(grenade_kind("weapon_flashbang") == GrenadeKind::flashbang);
  REQUIRE(grenade_kind("smokegrenade") == GrenadeKind::smoke);
  REQUIRE(grenade_kind("weapon_hegrenade") == GrenadeKind::high_explosive);
  REQUIRE(grenade_kind("molotov") == GrenadeKind::incendiary);
  REQUIRE(grenade_kind("weapon_decoy") == GrenadeKind::decoy);
  REQUIRE(grenade_kind("weapon_ak47") == GrenadeKind::none);
  REQUIRE(std::string_view(grenade_localization_token(GrenadeKind::smoke)) ==
          "#SFUI_TitlesTXT_Smoke_in_the_hole");
  REQUIRE(grenade_localization_token(GrenadeKind::none) == nullptr);
}

TEST_CASE("radar sound compensation classifies stable demo events") {
  using live_hud::event_compensation::RadarSoundKind;
  using live_hud::event_compensation::radar_sound_from_event;

  const auto step = radar_sound_from_event("player_footstep");
  REQUIRE(step.kind == RadarSoundKind::footstep);
  REQUIRE(step.radius == 1100);
  REQUIRE(step.duration == 0.5f);
  REQUIRE(step.is_footstep);

  const auto jump = radar_sound_from_event("player_jump");
  REQUIRE(jump.kind == RadarSoundKind::utility);
  REQUIRE(jump.radius == 204);
  REQUIRE(jump.duration == 0.1f);
  REQUIRE_FALSE(jump.is_footstep);

  const auto scope = radar_sound_from_event("weapon_zoom");
  REQUIRE(scope.kind == RadarSoundKind::scope);
  REQUIRE(scope.radius == 597);
  REQUIRE_FALSE(scope.is_footstep);

  REQUIRE(radar_sound_from_event("weapon_fire", "weapon_ak47").radius ==
          1100);
  REQUIRE(radar_sound_from_event("weapon_fire", "weapon_awp").radius ==
          1400);
  REQUIRE(radar_sound_from_event("weapon_fire", "weapon_m4a1_silencer")
              .radius == 800);
  REQUIRE(radar_sound_from_event("weapon_fire", "weapon_m4a1", true).radius ==
          800);

  const auto utility =
      radar_sound_from_event("weapon_fire", "weapon_flashbang");
  REQUIRE(utility.kind == RadarSoundKind::utility);
  REQUIRE(utility.radius == 700);
  REQUIRE(utility.duration == 0.16f);
  REQUIRE_FALSE(radar_sound_from_event("weapon_fire", "weapon_knife"));
  REQUIRE_FALSE(radar_sound_from_event("player_land"));
}

TEST_CASE("native radar sound dedupe is bounded to the event window") {
  using live_hud::event_compensation::native_sound_covers_event;

  REQUIRE(native_sound_covers_event(100, 140, 75));
  REQUIRE(native_sound_covers_event(100, 140, 100));
  REQUIRE(native_sound_covers_event(100, 140, 140));
  REQUIRE_FALSE(native_sound_covers_event(100, 140, 74));
  REQUIRE_FALSE(native_sound_covers_event(100, 140, 141));
}

TEST_CASE("current Demo movement carrier gets a narrow presentation repair") {
  using live_hud::event_compensation::
      native_movement_sound_needs_presentation_repair;

  REQUIRE(native_movement_sound_needs_presentation_repair(548, 0.10f, false));
  REQUIRE(native_movement_sound_needs_presentation_repair(548, 0.08f, false));
  REQUIRE(native_movement_sound_needs_presentation_repair(548, 0.12f, false));
  REQUIRE_FALSE(
      native_movement_sound_needs_presentation_repair(548, 0.10f, true));
  REQUIRE_FALSE(
      native_movement_sound_needs_presentation_repair(548, 0.50f, false));
  REQUIRE_FALSE(
      native_movement_sound_needs_presentation_repair(1100, 0.10f, false));
  REQUIRE_FALSE(
      native_movement_sound_needs_presentation_repair(1400, 0.10f, false));
  REQUIRE_FALSE(
      native_movement_sound_needs_presentation_repair(204, 0.10f, false));
}

TEST_CASE("only reconstructed generic footsteps request native max styling") {
  using live_hud::event_compensation::native_generic_footstep_needs_max;

  REQUIRE(native_generic_footstep_needs_max(1100, 0.50f, false));
  REQUIRE(native_generic_footstep_needs_max(1100, 0.48f, false));
  REQUIRE(native_generic_footstep_needs_max(1100, 0.52f, false));
  REQUIRE_FALSE(native_generic_footstep_needs_max(1100, 0.50f, true));
  REQUIRE_FALSE(native_generic_footstep_needs_max(1100, 0.10f, false));
  REQUIRE_FALSE(native_generic_footstep_needs_max(1400, 0.50f, false));
  REQUIRE_FALSE(native_generic_footstep_needs_max(204, 0.10f, false));
}

TEST_CASE("death banner pairs native damage summaries in either order") {
  using live_hud::event_compensation::DeathBannerResolution;
  using live_hud::event_compensation::resolve_death_banner;

  constexpr std::uint64_t window = 100;
  REQUIRE(resolve_death_banner(1000, 1030, 980, window) ==
          DeathBannerResolution::native_summary);
  REQUIRE(resolve_death_banner(1000, 1060, 1050, window) ==
          DeathBannerResolution::native_summary);
  REQUIRE(resolve_death_banner(1000, 1099, 0, window) ==
          DeathBannerResolution::wait);
  REQUIRE(resolve_death_banner(1000, 1100, 0, window) ==
          DeathBannerResolution::zero_summary);
  REQUIRE(resolve_death_banner(1000, 1120, 899, window) ==
          DeathBannerResolution::zero_summary);
  REQUIRE(resolve_death_banner(0, 1120, 1110, window) ==
          DeathBannerResolution::wait);
}
