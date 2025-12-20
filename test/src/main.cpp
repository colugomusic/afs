#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "afs.hpp"
#include "doctest.h"

static const auto TEST_MP3 = std::filesystem::path{ASSETS_DIR} / "test.mp3";
static const auto TEST_WAV = std::filesystem::path{ASSETS_DIR} / "test.wav";

TEST_CASE("null test") {
	static constexpr auto CHUNK_SIZE = afs::DEFAULT_CHUNK_SIZE;
	using streamer = afs::streamer<audiorw::stream_item_from_fs_path, CHUNK_SIZE>;
	if (const auto format_hint = audiorw::make_format_hint(TEST_WAV, true)) {
		auto stream = audiorw::stream::item::from(TEST_WAV, *format_hint);
		auto test_streamer = afs::make_uptr<streamer>(ez::ui, std::move(stream));
	}
}
