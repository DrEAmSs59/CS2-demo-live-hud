#include <catch2/catch_test_macros.hpp>
#include "common/build_id.hpp"

#include <windows.h>

TEST_CASE("read_pe_fingerprint reads this test EXE") {
  wchar_t buf[MAX_PATH]{};
  REQUIRE(GetModuleFileNameW(nullptr, buf, MAX_PATH) > 0);
  auto fp = live_hud::read_pe_fingerprint(buf);
  REQUIRE(fp.has_value());
  REQUIRE(fp->size_of_image > 0);
  REQUIRE(fp->time_date_stamp > 0);
}

TEST_CASE("fingerprint_matches rejects zero expected as unconfigured") {
  live_hud::PeFingerprint a{1, 2};
  REQUIRE_FALSE(live_hud::fingerprint_matches(a, 0, 0));
  REQUIRE_FALSE(live_hud::fingerprint_matches(a, 1, 0));
  REQUIRE(live_hud::fingerprint_matches(a, 1, 2));
}
