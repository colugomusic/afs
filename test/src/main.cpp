#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "afs.hpp"
#include "doctest.h"

static const auto TEST_MP3 = std::filesystem::path{ASSETS_DIR} / "test.mp3";
static const auto TEST_WAV = std::filesystem::path{ASSETS_DIR} / "test.wav";

TEST_CASE("null test") {
}
