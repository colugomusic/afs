# audio file streamer

This is the file streamer used in Blockhead's file browser. If you like the way Blockhead does things and want to use this but you are having trouble getting it to build because of [my stupid dependency management system](https://github.com/colugomusic/dope) then let me know and i will help you out.

- Supports WAV, MP3, FLAC and WavPack.
- Immediately starts playing back audio files without loading the entire file into memory first.
- The `process` function is realtime-safe. Everything else is not. [ez annoations](https://github.com/colugomusic/ez) are used to clearly denote the realtime-safe part of the API.
- Provides an interface for seeking around in the file.
- A thread is automatically created which does the loading of audio chunks in the background.
- Loaded chunks are kept in memory until the streamer is destroyed (rather than using a rolling window strategy.)
- Provides an interface to get information about which chunks have been loaded.
- There is no "stop" operation. Just delete the streamer and everything will be cleaned up properly. You can implement a "pause" yourself - just stop calling `process` and the playhead will stay where it is until you resume.

## API
See [test/src/main.cpp](test/src/main.cpp) for a usage example.

### `streamer(ez::nort_t, Stream stream)`

`Stream` is anything that provides the `audiorw::concepts::item_input_stream` concept. `audiorw` provides `audiorw::stream_item_from_bytes` and `audiorw::stream_item_from_fs_path`.

### `[[nodiscard]] auto get_chunk_info(ez::nort_t, afs::tmp_alloc& alloc) const -> afs::tmp_vec<bool>`

### `[[nodiscard]] auto get_estimated_frame_count(ez::nort_t) const -> ads::frame_count`

### `[[nodiscard]] auto get_header(ez::nort_t) const -> audiorw::header`

### `[[nodiscard]] auto get_playback_pos(ez::ui_t) -> double`

### `auto process(ez::audio_t, double SR, afs::output_signal stereo_out) -> void`

### `auto request_playback_pos(ez::nort_t) -> void`

### `auto seek(ez::nort_t, ads::frame_idx pos) -> void`

## MP3 caveats

Miniaudio cannot seek within an MP3 file, or tell us how many frames it contains, without loading the entire file, so MP3s will act slightly differently:

- The streamer can provide an estimate of the total frame count based on how many frames have been loaded so far vs how many bytes of the stream have been consumed. For non-MP3s the same function will return the actual frame count instead of an estimate.
- Trying to seek to an unloaded part of an MP3 will cause the streamer to pause playback until that chunk is loaded.
