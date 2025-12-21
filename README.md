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

`streamer(ez::nort_t, Stream stream)`

`Stream` is anything that satisfies the `audiorw::concepts::item_input_stream` concept. `audiorw` provides `audiorw::stream_item_from_bytes` and `audiorw::stream_item_from_fs_path`.

`[[nodiscard]] auto get_chunk_info(ez::nort_t, afs::tmp_alloc& alloc) const -> afs::tmp_vec<bool>`

Returns a list of chunks, true or false, depending on if they are loaded or not. The list may be less than the total number of chunks. The remaining chunks are not loaded. For example if there are 5 chunks and this function returns `[true, false, true]` then the final two chunks are implicitly `[false, false]`. The total number of chunks is `get_estimated_frame_count() * CHUNK_SIZE`.

`[[nodiscard]] auto get_estimated_frame_count(ez::nort_t) const -> ads::frame_count`

For non-MP3 files, returns the exact number of audio frames. For MP3 files, see the caveats below.

`[[nodiscard]] auto get_header(ez::nort_t) const -> audiorw::header`

Returns the audiorw header.

`[[nodiscard]] auto get_playback_pos(ez::ui_t) -> double`

Returns the last reported playback position (see  `request_playback_pos` below.)

`[[nodiscard]] auto is_playing(ez::nort_t) const -> bool`

Returns false if the playback got to the end. The playback automatically stops in this case. (Further calls to `process` will produce silence.)

`auto process(ez::audio_t, double SR, afs::output_signal stereo_out) -> void`

This is the realtime-safe audio processing function. `afs::output_signal` is `std::array<float*, 2>` for your two channels of audio data. If the input stream is mono then only the first buffer is written to. If you feel like forking the library, it would be pretty easy to support a dynamic number of channels. I just don't need it myself, yet.

`auto request_playback_pos(ez::nort_t) -> void`

Requests the realtime audio thread to report the playback position, which can then be queried later by other threads using `get_playback_pos()`. Note that `process()` needs to be running for this to have any effect.

`auto seek(ez::nort_t, ads::frame_idx pos) -> void`

Seek to the given position within the stream.

## MP3 caveats

Miniaudio cannot seek within an MP3 file, or tell us how many frames it contains, without loading the entire file, so MP3s will act slightly differently:

- `get_estimated_frame_count` returns an estimate of the total frame count based on how many frames have been loaded so far, vs how many bytes of the stream have been consumed. For non-MP3s the same function will return the actual frame count instead of an estimate.
- Trying to seek to an unloaded part of an MP3 will cause the streamer to pause playback until that chunk is loaded.
