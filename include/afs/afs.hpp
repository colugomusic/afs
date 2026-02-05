#pragma once

#include <ads-vocab.hpp>
#include <audiorw.hpp>
#include <ez.hpp>
#include <memory>
#include <immer/table.hpp>

namespace audiorw { struct header; }

namespace afs {

static constexpr auto DEFAULT_CHUNK_SIZE = 1 << 16;

template <typename T> using shptr = std::shared_ptr<T>;
template <typename T> using uptr  = std::unique_ptr<T>;
template <typename T, typename... Args> auto make_shptr(Args&&... args) { return std::make_shared<T>(std::forward<Args>(args)...); }
template <typename T, typename... Args> auto make_uptr(Args&&... args)  { return std::make_unique<T>(std::forward<Args>(args)...); }

using output_signal = std::array<float*, 2>;

} // afs

namespace afs::detail {

enum class state {
	playing,
	finished
};

struct target {
	ads::frame_idx seek_pos;
};

template <size_t CHUNK_SIZE>
struct chunk {
	size_t id = 0; // The ID is also the chunk index.
	shptr<const ads::data<float, ads::DYNAMIC_EXTENT, CHUNK_SIZE>> data;
};

template <size_t CHUNK_SIZE>
struct model {
	immer::table<detail::chunk<CHUNK_SIZE>> loaded_chunks;
	audiorw::header header;
	detail::target target;
	ads::frame_count estimated_frame_count;
};

struct servo {
	detail::state state = state::playing;
	ads::frame_idx playback_beg;
	double playback_pos = 0.0;
};

struct shared_atomics {
	std::atomic<bool> request_playback_pos    = false;
	std::atomic<bool> reported_finished       = false;
	std::atomic<double> reported_playback_pos = 0.0;
};

template <size_t CHUNK_SIZE>
struct shared_safe {
	ez::sync<model<CHUNK_SIZE>> model;
	detail::shared_atomics atomics;
};

template <audiorw::concepts::item_input_stream Stream, typename JThread>
struct loader {
	uptr<Stream> stream;
	JThread thread;
};

template <audiorw::concepts::item_input_stream Stream, typename JThread, size_t CHUNK_SIZE>
struct impl {
	detail::shared_safe<CHUNK_SIZE> shared;
	detail::loader<Stream, JThread> loader;
	detail::servo servo;
};

[[nodiscard]] static
auto quantize(auto v, auto step) {
	return v - (v % step);
}

template <size_t CHUNK_SIZE, size_t BUFFER_SIZE> [[nodiscard]] static
auto fn_seek(ads::frame_idx pos) {
	return [pos](model<CHUNK_SIZE> x) {
		x.target.seek_pos = {quantize(pos.value, static_cast<int64_t>(BUFFER_SIZE))};
		return x;
	};
}

template <size_t CHUNK_SIZE> [[nodiscard]] static
auto make_initial_model(audiorw::header header) -> model<CHUNK_SIZE> {
	model<CHUNK_SIZE> out;
	out.header = header;
	return out;
}

template <size_t CHUNK_SIZE> [[nodiscard]] static
auto can_seek(const model<CHUNK_SIZE>& x) -> bool {
	return x.header.frame_count.has_value();
}

template <size_t CHUNK_SIZE> static
auto get_chunk_info(const model<CHUNK_SIZE>& x, auto reserve_fn, auto resize_fn, auto set_fn) -> void {
	reserve_fn(x.loaded_chunks.size() * 2);
	auto size = size_t{0};
	for (const auto& chunk : x.loaded_chunks) {
		if (chunk.id >= size) {
			size = chunk.id + 1;
			resize_fn(size, false);
		}
		set_fn(chunk.id, true);
	}
}

template <size_t CHUNK_SIZE> [[nodiscard]] static
auto get_estimated_frame_count(const model<CHUNK_SIZE>& x) -> ads::frame_count {
	if (x.header.frame_count) {
		return *x.header.frame_count;
	}
	return x.estimated_frame_count;
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, size_t CHUNK_SIZE> [[nodiscard]] static
auto can_seek(ez::nort_t th, const impl<Stream, JThread, CHUNK_SIZE>* x) -> bool {
	return can_seek(x->shared.model.read(th));
}

template <size_t CHUNK_SIZE> [[nodiscard]] static
auto get_chunk_beg(size_t chunk_idx) -> ads::frame_idx {
	return ads::frame_idx{CHUNK_SIZE} * chunk_idx;
}

template <size_t CHUNK_SIZE> [[nodiscard]] static
auto get_chunk_idx(double pos) -> size_t {
	return static_cast<size_t>(std::floor(pos / CHUNK_SIZE));
}

template <size_t CHUNK_SIZE> [[nodiscard]] static
auto get_chunk_idx(ads::frame_idx fr) -> size_t {
	return static_cast<size_t>(fr.value / CHUNK_SIZE);
}

[[nodiscard]] static
auto get_next_chunk_to_load_forward(size_t chunk_just_loaded, std::optional<size_t> end_chunk) -> std::optional<size_t> {
	if (end_chunk && chunk_just_loaded == *end_chunk) {
		return std::nullopt;
	}
	return chunk_just_loaded + 1;
}

template <size_t CHUNK_SIZE> [[nodiscard]] static
auto get_next_chunk_to_load_random(const model<CHUNK_SIZE>& x, const detail::shared_safe<CHUNK_SIZE>& shared, std::optional<size_t> end_chunk) -> std::optional<size_t> {
	const auto playback_pos   = shared.atomics.reported_playback_pos.load(std::memory_order_relaxed);
	const auto playback_chunk = get_chunk_idx<CHUNK_SIZE>(playback_pos);
	auto check_chunk = playback_chunk;
	for (;;) {
		if (!x.loaded_chunks.find(check_chunk)) {
			return check_chunk;
		}
		check_chunk++;
		if (end_chunk && check_chunk == *end_chunk) {
			check_chunk = 0;
			while (check_chunk < playback_chunk) {
				if (!x.loaded_chunks.find(check_chunk)) {
					return check_chunk;
				}
				check_chunk++;
			}
			return std::nullopt;
		}
	}
	return std::nullopt;
}

template <size_t CHUNK_SIZE> [[nodiscard]] static
auto get_next_chunk_to_load(const model<CHUNK_SIZE>& x, const detail::shared_safe<CHUNK_SIZE>& shared, size_t chunk_just_loaded, std::optional<size_t> end_chunk) -> std::optional<size_t> {
	const auto can_random_seek = x.header.format != audiorw::format::mp3;
	if (can_random_seek) { return get_next_chunk_to_load_random(x, shared, end_chunk); }
	else                 { return get_next_chunk_to_load_forward(chunk_just_loaded, end_chunk); }
}

template <size_t CHUNK_SIZE> [[nodiscard]] static
auto calculate_frame_count_from_end_chunk(size_t end_chunk, ads::frame_count frames_in_end_chunk) -> ads::frame_count {
	return frames_in_end_chunk + ads::frame_count{end_chunk * CHUNK_SIZE};
}

[[nodiscard]] static
auto estimate_frame_count(ads::frame_count total_frames_read, size_t total_bytes_read, size_t file_size) -> ads::frame_count {
	const auto byte_progress = static_cast<double>(total_bytes_read) / static_cast<double>(file_size);
	const auto estimate      = static_cast<double>(total_frames_read.value) / byte_progress;
	return {static_cast<uint64_t>(estimate)};
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, typename StopToken, size_t CHUNK_SIZE> static
auto load_proc(StopToken stop, detail::loader<Stream, JThread>* loader, detail::shared_safe<CHUNK_SIZE>* shared) -> void {
	auto th                      = ez::nort;
	auto current_chunk_idx       = size_t{0};
	auto model                   = shared->model.read(th);
	auto channel_count           = model.header.channel_count;
	auto end_chunk               = std::optional<size_t>{};
	auto interleaved             = ads::interleaved<float>{channel_count, {CHUNK_SIZE}};
	auto interleaved_buffer_size = interleaved.get_frame_count().value * interleaved.get_channel_count().value;
	auto total_frames_read       = ads::frame_count{0};
	for (;;) {
		if (stop.stop_requested()) {
			return;
		}
		shared->atomics.request_playback_pos.store(true, std::memory_order_relaxed);
		loader->stream->seek(get_chunk_beg<CHUNK_SIZE>(current_chunk_idx));
		auto span = std::span{interleaved.data(), interleaved_buffer_size};
		const auto frames_read = loader->stream->read_frames(span);
		total_frames_read += frames_read;
		auto just_found_end_chunk = false;
		if (frames_read < ads::frame_count{CHUNK_SIZE}) {
			// Must have found the end of the file.
			end_chunk = current_chunk_idx;
			just_found_end_chunk = true;
		}
		auto chunk_data = make_shptr<ads::data<float, ads::DYNAMIC_EXTENT, CHUNK_SIZE>>(ads::make<float, CHUNK_SIZE>(channel_count));
		ads::deinterleave(interleaved, chunk_data->begin());
		auto chunk = detail::chunk<CHUNK_SIZE>{
			.id   = current_chunk_idx,
			.data = chunk_data
		};
		model = shared->model.update_publish(th, [=](detail::model<CHUNK_SIZE> x) {
			x.loaded_chunks = x.loaded_chunks.insert(chunk);
			if (just_found_end_chunk)  { x.header.frame_count = x.header.frame_count.value_or(calculate_frame_count_from_end_chunk<CHUNK_SIZE>(*end_chunk, frames_read)); }
			if (!x.header.frame_count) { x.estimated_frame_count = estimate_frame_count(total_frames_read, loader->stream->get_total_bytes_read(), x.header.stream_length); }
			return x;
		});
		const auto next_chunk_to_load = get_next_chunk_to_load(model, *shared, current_chunk_idx, end_chunk);
		if (!next_chunk_to_load.has_value()) {
			// Entire file has been loaded
			return;
		}
		current_chunk_idx = *next_chunk_to_load;
	}
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, typename StopToken, size_t CHUNK_SIZE> static
auto init(ez::nort_t th, impl<Stream, JThread, CHUNK_SIZE>* x, Stream stream) -> void {
	x->loader.stream = make_uptr<Stream>(std::move(stream));
	x->loader.thread = JThread{load_proc<Stream, JThread, StopToken, CHUNK_SIZE>, &x->loader, &x->shared};
	x->shared.model.set_publish(th, make_initial_model<CHUNK_SIZE>(x->loader.stream->get_header()));
}

static
auto report_playback_pos_if_requested(ez::audio_t, detail::servo* servo, detail::shared_atomics* atomics, double pos) -> void {
	if (atomics->request_playback_pos.load(std::memory_order_relaxed)) {
		atomics->reported_playback_pos.store(servo->playback_pos, std::memory_order_relaxed);
		atomics->request_playback_pos.store(false, std::memory_order_relaxed);
	}
}

template <size_t CHUNK_SIZE> [[nodiscard]] static
auto get_local_chunk_frame(double fr) -> float {
	return static_cast<float>(std::fmod(fr, static_cast<double>(CHUNK_SIZE)));
}

template <size_t CHUNK_SIZE> [[nodiscard]] static
auto get_local_chunk_frame(ads::frame_idx fr) -> ads::frame_idx {
	return {fr.value % CHUNK_SIZE};
}

template <size_t CHUNK_SIZE> static
auto finish_if_reached_end(ez::audio_t, detail::servo* servo, detail::shared_atomics* atomics, detail::model<CHUNK_SIZE> model) -> void {
	if (servo->playback_pos >= get_estimated_frame_count(model)) {
		servo->state = state::finished;
		atomics->reported_finished.store(true, std::memory_order_relaxed);
	}
}

template <size_t CHUNK_SIZE, size_t BUFFER_SIZE> static
auto playback_single_chunk(ez::audio_t th, detail::servo* servo, detail::shared_atomics* atomics, detail::model<CHUNK_SIZE> model, size_t chunk_idx, double SR, double frame_inc, output_signal signal) -> void {
	if (const auto chunk = model.loaded_chunks.find(chunk_idx)) {
		for (ads::channel_idx ch; ch < std::min(ads::channel_count{2}, model.header.channel_count); ch++) {
			auto& signal_row = signal.at(ch.value);
			auto fr          = servo->playback_pos;
			for (int i = 0; i < BUFFER_SIZE; i++) {
				signal_row[i] = chunk->data->at(ch, get_local_chunk_frame<CHUNK_SIZE>(fr));
				fr += frame_inc;
			}
		}
		servo->playback_pos += BUFFER_SIZE * frame_inc;
		finish_if_reached_end(th, servo, atomics, model);
	}
	if (model.header.channel_count < 2) {
		std::ranges::copy_n(signal.at(0), BUFFER_SIZE, signal.at(1));
	}
}

template <size_t CHUNK_SIZE, size_t BUFFER_SIZE> static
auto playback_chunk_transition(ez::audio_t th, detail::servo* servo, detail::shared_atomics* atomics, detail::model<CHUNK_SIZE> model, size_t chunk_idx, double SR, double frame_inc, output_signal signal) -> void {
	for (ads::channel_idx ch; ch < std::min(ads::channel_count{2}, model.header.channel_count); ch++) {
		auto& signal_row = signal.at(ch.value);
		auto fr          = servo->playback_pos;
		for (int i = 0; i < BUFFER_SIZE; i++) {
			const auto fr_a = ads::frame_idx{static_cast<int64_t>(std::floor(fr))};
			const auto fr_b = ads::frame_idx{static_cast<int64_t>(std::ceil(fr))};
			const auto fr_t = static_cast<float>(fr - std::floor(fr));
			const auto chunk_a = model.loaded_chunks.find(get_chunk_idx<CHUNK_SIZE>(fr_a));
			const auto chunk_b = model.loaded_chunks.find(get_chunk_idx<CHUNK_SIZE>(fr_b));
			const auto value_a = chunk_a ? chunk_a->data->at(ch, get_local_chunk_frame<CHUNK_SIZE>(fr_a)) : 0.0f;
			const auto value_b = chunk_b ? chunk_b->data->at(ch, get_local_chunk_frame<CHUNK_SIZE>(fr_b)) : 0.0f;
			signal_row[i] = std::lerp(value_a, value_b, fr_t);
			fr += frame_inc;
		}
	}
	if (model.header.channel_count < 2) {
		std::ranges::copy_n(signal.at(0), BUFFER_SIZE, signal.at(1));
	}
	servo->playback_pos += BUFFER_SIZE * frame_inc;
	finish_if_reached_end(th, servo, atomics, model);
}

template <size_t CHUNK_SIZE, size_t BUFFER_SIZE> static
auto playback_frames(ez::audio_t th, detail::servo* servo, detail::shared_atomics* atomics, detail::model<CHUNK_SIZE> model, size_t chunk_beg, size_t chunk_end, double SR, double frame_inc, output_signal signal) -> void {
	if (chunk_beg == chunk_end) { return playback_single_chunk<CHUNK_SIZE, BUFFER_SIZE>(th, servo, atomics, model, chunk_beg, SR, frame_inc, signal); }
	else                        { return playback_chunk_transition<CHUNK_SIZE, BUFFER_SIZE>(th, servo, atomics, model, chunk_beg, SR, frame_inc, signal); }
}

template <size_t CHUNK_SIZE, size_t BUFFER_SIZE> static
auto process_playback(ez::audio_t th, detail::servo* servo, detail::shared_atomics* atomics, detail::model<CHUNK_SIZE> model, double SR, output_signal signal) -> void {
	if (model.target.seek_pos != servo->playback_beg) {
		servo->playback_beg   = model.target.seek_pos;
		servo->playback_pos   = static_cast<double>(model.target.seek_pos.value);
	}
	const auto frame_inc = static_cast<double>(model.header.SR) / SR;
	const auto fr_beg = servo->playback_pos;
	const auto fr_end = servo->playback_pos + (64 * frame_inc);
	const auto chunk_beg = get_chunk_idx<CHUNK_SIZE>(fr_beg);
	const auto chunk_end = get_chunk_idx<CHUNK_SIZE>(fr_end);
	playback_frames<CHUNK_SIZE, BUFFER_SIZE>(th, servo, atomics, model, chunk_beg, chunk_end, SR, frame_inc, signal);
	report_playback_pos_if_requested(th, servo, atomics, servo->playback_pos);
}

template <size_t CHUNK_SIZE, size_t BUFFER_SIZE> static
auto process(ez::audio_t th, detail::servo* servo, detail::shared_atomics* atomics, detail::model<CHUNK_SIZE> model, double SR, output_signal signal) -> void {
	switch (servo->state) {
		case state::playing: { return process_playback<CHUNK_SIZE, BUFFER_SIZE>(th, servo, atomics, model, SR, signal); }
		case state::finished:{ return; }
		default:             { assert (false); return; }
	}
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, size_t CHUNK_SIZE, size_t BUFFER_SIZE> static
auto process(ez::audio_t th, impl<Stream, JThread, CHUNK_SIZE>* x, double SR, output_signal signal) -> void {
	return process<CHUNK_SIZE, BUFFER_SIZE>(th, &x->servo, &x->shared.atomics, *x->shared.model.read(th), SR, signal);
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, size_t CHUNK_SIZE> static
auto get_chunk_info(ez::nort_t th, impl<Stream, JThread, CHUNK_SIZE>* x, auto reserve_fn, auto resize_fn, auto set_fn) -> void {
	return get_chunk_info(x->shared.model.read(th), reserve_fn, resize_fn, set_fn);
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, size_t CHUNK_SIZE> [[nodiscard]] static
auto get_estimated_frame_count(ez::nort_t th, impl<Stream, JThread, CHUNK_SIZE>* x) -> ads::frame_count {
	return get_estimated_frame_count(x->shared.model.read(th));
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, size_t CHUNK_SIZE> [[nodiscard]] static
auto is_playing(ez::nort_t th, const impl<Stream, JThread, CHUNK_SIZE>* x) -> bool {
	return !x->shared.atomics.reported_finished.load(std::memory_order_relaxed);
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, size_t CHUNK_SIZE> [[nodiscard]] static
auto get_header(ez::nort_t th, impl<Stream, JThread, CHUNK_SIZE>* x) -> audiorw::header {
	return x->shared.model.read(th).header;
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, size_t CHUNK_SIZE> [[nodiscard]] static
auto get_playback_pos(ez::nort_t th, impl<Stream, JThread, CHUNK_SIZE>* x) -> double {
	return x->shared.atomics.reported_playback_pos.load(std::memory_order_relaxed);
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, size_t CHUNK_SIZE, size_t BUFFER_SIZE> static
auto seek(ez::nort_t th, impl<Stream, JThread, CHUNK_SIZE>* x, ads::frame_idx pos) -> void {
	x->shared.model.update_publish(th, fn_seek<CHUNK_SIZE, BUFFER_SIZE>(pos));
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, size_t CHUNK_SIZE> static
auto request_playback_pos(ez::nort_t th, impl<Stream, JThread, CHUNK_SIZE>* x) -> void {
	x->shared.atomics.request_playback_pos.store(true, std::memory_order_relaxed);
}

} // afs::detail

namespace afs {

template <audiorw::concepts::item_input_stream Stream, typename JThread, typename StopToken, size_t CHUNK_SIZE, size_t BUFFER_SIZE>
struct streamer {
	streamer(ez::nort_t, Stream stream);
	[[nodiscard]] auto get_estimated_frame_count(ez::nort_t) const -> ads::frame_count;
	[[nodiscard]] auto get_header(ez::nort_t) const -> audiorw::header;
	[[nodiscard]] auto get_playback_pos(ez::ui_t) -> double;
	[[nodiscard]] auto is_playing(ez::nort_t) const -> bool;
	auto get_chunk_info(ez::nort_t, auto reserve_fn, auto resize_fn, auto set_fn) const -> void;
	auto process(ez::audio_t, double SR, output_signal stereo_out) -> void;
	auto request_playback_pos(ez::nort_t) -> void;
	auto seek(ez::nort_t, ads::frame_idx pos) -> void;
private:
	uptr<detail::impl<Stream, JThread, CHUNK_SIZE>> impl_;
};

template <audiorw::concepts::item_input_stream Stream, typename JThread, typename StopToken, size_t CHUNK_SIZE, size_t BUFFER_SIZE>
streamer<Stream, JThread, StopToken, CHUNK_SIZE, BUFFER_SIZE>::streamer(ez::nort_t th, Stream stream)
	: impl_{std::make_unique<detail::impl<Stream, JThread, CHUNK_SIZE>>()}
{
	detail::init<Stream, JThread, StopToken>(th, impl_.get(), std::move(stream));
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, typename StopToken, size_t CHUNK_SIZE, size_t BUFFER_SIZE>
auto streamer<Stream, JThread, StopToken, CHUNK_SIZE, BUFFER_SIZE>::process(ez::audio_t th, double SR, output_signal stereo_out) -> void {
	return detail::process<Stream, JThread, CHUNK_SIZE, BUFFER_SIZE>(th, impl_.get(), SR, stereo_out);
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, typename StopToken, size_t CHUNK_SIZE, size_t BUFFER_SIZE>
auto streamer<Stream, JThread, StopToken, CHUNK_SIZE, BUFFER_SIZE>::get_chunk_info(ez::nort_t th, auto reserve_fn, auto resize_fn, auto set_fn) const -> void {
	return detail::get_chunk_info(th, impl_.get(), reserve_fn, resize_fn, set_fn);
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, typename StopToken, size_t CHUNK_SIZE, size_t BUFFER_SIZE>
auto streamer<Stream, JThread, StopToken, CHUNK_SIZE, BUFFER_SIZE>::get_estimated_frame_count(ez::nort_t th) const -> ads::frame_count {
	return detail::get_estimated_frame_count(th, impl_.get());
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, typename StopToken, size_t CHUNK_SIZE, size_t BUFFER_SIZE>
auto streamer<Stream, JThread, StopToken, CHUNK_SIZE, BUFFER_SIZE>::get_header(ez::nort_t th) const -> audiorw::header {
	return detail::get_header(th, impl_.get());
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, typename StopToken, size_t CHUNK_SIZE, size_t BUFFER_SIZE>
auto streamer<Stream, JThread, StopToken, CHUNK_SIZE, BUFFER_SIZE>::get_playback_pos(ez::ui_t) -> double {
	return detail::get_playback_pos(ez::ui, impl_.get());
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, typename StopToken, size_t CHUNK_SIZE, size_t BUFFER_SIZE>
auto streamer<Stream, JThread, StopToken, CHUNK_SIZE, BUFFER_SIZE>::is_playing(ez::nort_t th) const -> bool {
	return detail::is_playing(th, impl_.get());
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, typename StopToken, size_t CHUNK_SIZE, size_t BUFFER_SIZE>
auto streamer<Stream, JThread, StopToken, CHUNK_SIZE, BUFFER_SIZE>::seek(ez::nort_t th, ads::frame_idx pos) -> void {
	return detail::seek<Stream, JThread, CHUNK_SIZE, BUFFER_SIZE>(th, impl_.get(), pos);
}

template <audiorw::concepts::item_input_stream Stream, typename JThread, typename StopToken, size_t CHUNK_SIZE, size_t BUFFER_SIZE>
auto streamer<Stream, JThread, StopToken, CHUNK_SIZE, BUFFER_SIZE>::request_playback_pos(ez::nort_t th) -> void {
	return detail::request_playback_pos(th, impl_.get());
}

} // afs
