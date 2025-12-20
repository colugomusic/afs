# audio file streamer

This is the file streamer used in Blockhead's file browser. I am not going to spend time documenting it properly unless someone asks me to. If you like the way Blockhead does things and want to use this but you are having trouble getting it to build because of [my stupid dependency management system](https://github.com/colugomusic/dope) then let me know and i will help you out.

- Supports WAV, MP3, FLAC and WavPack.
- Immediately starts playing back audio files without loading the entire file into memory first.
- Provides an interface for seeking around in the file.
- A thread is automatically created which does the loading of audio chunks in the background.
- Loaded chunks are kept in memory until the streamer is destroyed (rather than using a rolling window strategy.)
- Provides an interface to get information about which chunks have been loaded.
- There is no "stop" operation. Just delete the streamer and everything will be cleaned up properly. You can implement a "pause" yourself - just stop calling `process` and the playhead will stay where it is until you resume.

## MP3 caveats

Miniaudio cannot seek within an MP3 file, or tell us how many frames it contains, without loading the entire file, so MP3s will act slightly differently:

- The streamer can provide an estimate of the total frame count based on how many frames have been loaded so far vs how many bytes of the stream have been consumed. For non-MP3s the same function will return the actual frame count instead of an estimate.
- Trying to seek to an unloaded part of an MP3 will cause the streamer to pause playback until that chunk is loaded.
