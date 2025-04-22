#include "gameboy.h"
#include <chrono>
#include <thread>

Gameboy::Gameboy(const std::vector<u8>& cartridge_data, Options& options,
                 const std::vector<u8>& save_data)
    : cartridge(get_cartridge(cartridge_data, save_data)),
      cpu(*this, options),
      video(*this, options),
      audio(*this, options),
      mmu(*this, options),
      timer(*this),
      serial(options),
      debugger(*this, options)
{
    if (options.disable_logs) log_set_level(LogLevel::Error);

    log_set_level(options.trace
        ? LogLevel::Trace
        : LogLevel::Info
    );
}

void Gameboy::button_pressed(GbButton button) {
    input.button_pressed(button);
}

void Gameboy::button_released(GbButton button) {
    input.button_released(button);
}

void Gameboy::debug_toggle_background() {
    video.debug_disable_background = !video.debug_disable_background;
}

void Gameboy::debug_toggle_sprites() {
    video.debug_disable_sprites = !video.debug_disable_sprites;
}

void Gameboy::debug_toggle_window() {
    video.debug_disable_window = !video.debug_disable_window;
}

void Gameboy::run(
    const should_close_callback_t& _should_close_callback,
    const vblank_callback_t& _vblank_callback,
    const audio_callback_t& _audio_callback
) {
    should_close_callback = _should_close_callback;

    video.register_vblank_callback(_vblank_callback);
    
    // Registra o callback de áudio se fornecido
    if (_audio_callback) {
        audio.register_audio_callback(_audio_callback);
    }

    // Timing constants
    constexpr double target_fps = 59.73;
    constexpr double target_frame_time_ms = 1000.0 / target_fps; // ~16.74 ms
    constexpr uint32_t cycles_per_frame = 70224; // 4194304 Hz / 59.73 FPS

    while (!should_close_callback()) {
        auto frame_start = std::chrono::high_resolution_clock::now();

        uint32_t cycles_this_frame = 0;
        while (cycles_this_frame < cycles_per_frame && !should_close_callback()) {
            // tick() returns void, but we need to know cycles used per tick
            // So we must modify tick() to return the cycles used, or accumulate them here
            // For now, let's use the elapsed_cycles variable
            uint32_t cycles_before = elapsed_cycles;
            tick();
            uint32_t cycles_after = elapsed_cycles;
            cycles_this_frame += (cycles_after - cycles_before);
        }

        // VBlank callback should be triggered by the video system, but we can ensure frame pacing here
        auto frame_end = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        if (elapsed_ms < target_frame_time_ms) {
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(target_frame_time_ms - elapsed_ms));
        }
    }

    debugger.set_enabled(false);
}

void Gameboy::tick() {
    debugger.cycle();

    auto cycles = cpu.tick();
    elapsed_cycles += cycles.cycles;

    video.tick(cycles);
    audio.tick(cycles.cycles);
    timer.tick(cycles.cycles);
}

auto Gameboy::get_cartridge_ram() const -> const std::vector<u8>& {
    return cartridge->get_cartridge_ram();
}
