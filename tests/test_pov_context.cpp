#include "dll/pov_context.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

TEST_CASE("POV scopes compose and restore transaction domains") {
  using live_hud::pov::Domain;

  REQUIRE_FALSE(live_hud::pov::active());
  {
    live_hud::pov::Scope radar(Domain::radar);
    REQUIRE(live_hud::pov::active(Domain::radar));
    REQUIRE_FALSE(live_hud::pov::active(Domain::voice));
    {
      live_hud::pov::Scope voice(Domain::voice);
      REQUIRE(live_hud::pov::active(Domain::radar));
      REQUIRE(live_hud::pov::active(Domain::voice));
    }
    REQUIRE(live_hud::pov::active(Domain::radar));
    REQUIRE_FALSE(live_hud::pov::active(Domain::voice));
  }
  REQUIRE_FALSE(live_hud::pov::active());

  {
    live_hud::pov::Scope presentation(Domain::hud_presentation);
    REQUIRE(live_hud::pov::active(Domain::hud_presentation));
    REQUIRE_FALSE(live_hud::pov::active(Domain::view_effects));
  }
  REQUIRE_FALSE(live_hud::pov::active());

  {
    live_hud::pov::Scope combat(Domain::combat_feedback);
    REQUIRE(live_hud::pov::active(Domain::combat_feedback));
    REQUIRE_FALSE(live_hud::pov::active(Domain::player_sound));
  }
  REQUIRE_FALSE(live_hud::pov::active());
}

TEST_CASE("POV snapshot publishes one followed identity atomically") {
  live_hud::pov::Snapshot expected{};
  expected.pawn = reinterpret_cast<void*>(std::uintptr_t{0x1234});
  expected.controller = reinterpret_cast<void*>(std::uintptr_t{0x5678});
  expected.slot = 7;
  expected.team = 3;

  live_hud::pov::publish(expected);
  const auto actual = live_hud::pov::snapshot();
  REQUIRE(actual.pawn == expected.pawn);
  REQUIRE(actual.controller == expected.controller);
  REQUIRE(actual.slot == expected.slot);
  REQUIRE(actual.team == expected.team);
  REQUIRE((actual.generation & 1U) == 0);

  live_hud::pov::invalidate();
  const auto empty = live_hud::pov::snapshot();
  REQUIRE(empty.pawn == nullptr);
  REQUIRE(empty.controller == nullptr);
  REQUIRE(empty.slot == -1);
  REQUIRE(empty.team == 0);
}

TEST_CASE("POV pin keeps one death identity while camera publication advances") {
  live_hud::pov::Snapshot victim{};
  victim.pawn = reinterpret_cast<void*>(std::uintptr_t{0x1111});
  victim.controller = reinterpret_cast<void*>(std::uintptr_t{0x2222});
  victim.slot = 4;
  victim.team = 2;
  live_hud::pov::publish(victim);
  victim = live_hud::pov::snapshot();

  live_hud::pov::pin(victim);
  REQUIRE(live_hud::pov::pinned());

  live_hud::pov::Snapshot killer{};
  killer.pawn = reinterpret_cast<void*>(std::uintptr_t{0x3333});
  killer.controller = reinterpret_cast<void*>(std::uintptr_t{0x4444});
  killer.slot = 9;
  killer.team = 3;
  live_hud::pov::publish(killer);

  const auto during_death = live_hud::pov::snapshot();
  REQUIRE(during_death.pawn == victim.pawn);
  REQUIRE(during_death.controller == victim.controller);
  REQUIRE(during_death.generation == victim.generation);

  live_hud::pov::unpin();
  REQUIRE_FALSE(live_hud::pov::pinned());
  const auto after_death = live_hud::pov::snapshot();
  REQUIRE(after_death.pawn == killer.pawn);
  REQUIRE(after_death.controller == killer.controller);

  live_hud::pov::invalidate();
}
