#pragma once

#include <cstdint>

namespace live_hud::pov {

enum class Domain : std::uint32_t {
  none = 0,
  radar = 1u << 0,
  team_counter = 1u << 1,
  money = 1u << 2,
  voice = 1u << 3,
  communications = 1u << 4,
  view_effects = 1u << 5,
  hud_presentation = 1u << 6,
  player_overhead = 1u << 7,
  player_sound = 1u << 8,
  combat_feedback = 1u << 9,
};

constexpr Domain operator|(Domain lhs, Domain rhs) {
  return static_cast<Domain>(static_cast<std::uint32_t>(lhs) |
                             static_cast<std::uint32_t>(rhs));
}

constexpr Domain operator&(Domain lhs, Domain rhs) {
  return static_cast<Domain>(static_cast<std::uint32_t>(lhs) &
                             static_cast<std::uint32_t>(rhs));
}

struct Snapshot {
  void* pawn = nullptr;
  void* controller = nullptr;
  std::int32_t slot = -1;
  std::int32_t team = 0;
  std::uint64_t generation = 0;
};

// A native HUD/message/event callback owns a Scope. Identity and mode adapters
// are allowed to expose live-POV semantics only while one of these scopes is
// active. This replaces return-address windows and stack inspection.
class Scope final {
 public:
  explicit Scope(Domain domain) noexcept;
  ~Scope() noexcept;

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  std::uint32_t previous_domains_ = 0;
};

bool active() noexcept;
bool active(Domain domains) noexcept;
Domain domains() noexcept;

void publish(const Snapshot& snapshot) noexcept;
Snapshot snapshot() noexcept;
// Temporarily expose one immutable identity while camera-follow publication may
// continue underneath it.  Death feedback uses this to prevent a Demo camera
// cut to the killer from changing local identity halfway through one native
// death/postprocess transaction.
void pin(const Snapshot& snapshot) noexcept;
void unpin() noexcept;
bool pinned() noexcept;
void invalidate() noexcept;

}  // namespace live_hud::pov
