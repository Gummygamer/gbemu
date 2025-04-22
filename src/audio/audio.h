#pragma once

#include "../definitions.h"
#include "../register.h"
#include "../options.h"

#include <vector>
#include <array>
#include <memory>
#include <functional>

class Gameboy;

// Callback para enviar amostras de áudio para o sistema de saída
using audio_callback_t = std::function<void(const std::vector<float>&, const std::vector<float>&)>;

// Enumeração para os canais de áudio
enum class AudioChannel {
    CHANNEL1, // Tone & Sweep
    CHANNEL2, // Tone
    CHANNEL3, // Wave Output
    CHANNEL4  // Noise
};

// Classe base para todos os canais de áudio
class SoundChannel {
public:
    SoundChannel() = default;
    virtual ~SoundChannel() = default;
    
    virtual void tick(uint cycles) = 0;
    virtual float get_sample() const = 0;
    
    bool is_enabled() const { return enabled; }
    void set_enabled(bool value) { enabled = value; }
    
    void set_volume(u8 vol) { volume = vol; }
    u8 get_volume() const { return volume; }
    
protected:
    bool enabled = false;
    u8 volume = 0;
    uint length_counter = 0;
    bool length_enabled = false;
};

// Canal 1: Tone & Sweep
class ToneSweepChannel : public SoundChannel {
public:
    ToneSweepChannel();
    
    void tick(uint cycles) override;
    float get_sample() const override;
    
    void set_sweep_register(u8 value);
    void set_length_duty_register(u8 value);
    void set_volume_envelope_register(u8 value);
    void set_frequency_lo_register(u8 value);
    void set_frequency_hi_register(u8 value);
    
private:
    u8 sweep_time = 0;
    bool sweep_decrease = false;
    u8 sweep_shift = 0;
    
    u8 duty_pattern = 0;
    u8 duty_position = 0;
    
    u8 envelope_initial_volume = 0;
    bool envelope_increase = false;
    u8 envelope_sweep_pace = 0;
    
    uint frequency = 0;
    uint timer = 0;
    
    void update_frequency();
};

// Canal 2: Tone
class ToneChannel : public SoundChannel {
public:
    ToneChannel();
    
    void tick(uint cycles) override;
    float get_sample() const override;
    
    void set_length_duty_register(u8 value);
    void set_volume_envelope_register(u8 value);
    void set_frequency_lo_register(u8 value);
    void set_frequency_hi_register(u8 value);
    
private:
    u8 duty_pattern = 0;
    u8 duty_position = 0;
    
    u8 envelope_initial_volume = 0;
    bool envelope_increase = false;
    u8 envelope_sweep_pace = 0;
    
    uint frequency = 0;
    uint timer = 0;
};

// Canal 3: Wave Output
class WaveChannel : public SoundChannel {
public:
    WaveChannel();
    
    void tick(uint cycles) override;
    float get_sample() const override;
    
    void set_enable_register(u8 value);
    void set_length_register(u8 value);
    void set_output_level_register(u8 value);
    void set_frequency_lo_register(u8 value);
    void set_frequency_hi_register(u8 value);
    
    void set_wave_pattern(u8 index, u8 value);
    u8 get_wave_pattern(u8 index) const;
    
private:
    std::array<u8, 16> wave_pattern = {};
    u8 position = 0;
    u8 output_level = 0;
    
    uint frequency = 0;
    uint timer = 0;
};

// Canal 4: Noise
class NoiseChannel : public SoundChannel {
public:
    NoiseChannel();
    
    void tick(uint cycles) override;
    float get_sample() const override;
    
    void set_length_register(u8 value);
    void set_volume_envelope_register(u8 value);
    void set_polynomial_register(u8 value);
    void set_counter_register(u8 value);
    
private:
    u8 envelope_initial_volume = 0;
    bool envelope_increase = false;
    u8 envelope_sweep_pace = 0;
    
    u8 shift_clock_frequency = 0;
    bool counter_step_width_mode = false;
    u8 dividing_ratio = 0;
    
    uint timer = 0;
    uint lfsr = 0; // Linear Feedback Shift Register
};

// Classe principal de áudio
class Audio {
public:
    Audio(Gameboy& inGb, Options& inOptions);
    
    void tick(uint cycles);
    void register_audio_callback(const audio_callback_t& callback);
    
    // Registradores de controle
    u8 read_register(u16 address) const;
    void write_register(u16 address, u8 value);
    
    // Registradores específicos
    ByteRegister nr50; // Canal de controle/ON-OFF/Volume
    ByteRegister nr51; // Seleção do terminal de saída de som
    ByteRegister nr52; // Som ligado/desligado
    
private:
    Gameboy& gb;
    Options& options;
    
    std::unique_ptr<ToneSweepChannel> channel1;
    std::unique_ptr<ToneChannel> channel2;
    std::unique_ptr<WaveChannel> channel3;
    std::unique_ptr<NoiseChannel> channel4;
    
    uint sample_counter = 0;
    static constexpr uint CYCLES_PER_SAMPLE = 95; // ~44100Hz com clock de 4.19MHz
    
    std::vector<float> left_buffer;
    std::vector<float> right_buffer;
    
    audio_callback_t audio_callback;
    
    void mix_samples();
    void update_channel_control();
};
