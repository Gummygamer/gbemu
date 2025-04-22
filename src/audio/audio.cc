#include "audio.h"
#include "../gameboy.h"
#include "../util/log.h" // Make sure log.h is included
#include "../util/bitwise.h"
#include <iostream>

#include <cmath>
#include <iomanip> // Required for std::hex manipulator

using bitwise::check_bit;
using bitwise::set_bit;

// Debug helper for APU register names
/*
static const char* apu_reg_name(u16 addr) {
    switch (addr) {
        case 0xFF10: return "NR10";
        case 0xFF11: return "NR11";
        case 0xFF12: return "NR12";
        case 0xFF13: return "NR13";
        case 0xFF14: return "NR14";
        case 0xFF16: return "NR21";
        case 0xFF17: return "NR22";
        case 0xFF18: return "NR23";
        case 0xFF19: return "NR24";
        case 0xFF1A: return "NR30";
        case 0xFF1B: return "NR31";
        case 0xFF1C: return "NR32";
        case 0xFF1D: return "NR33";
        case 0xFF1E: return "NR34";
        case 0xFF20: return "NR41";
        case 0xFF21: return "NR42";
        case 0xFF22: return "NR43";
        case 0xFF23: return "NR44";
        case 0xFF24: return "NR50";
        case 0xFF25: return "NR51";
        case 0xFF26: return "NR52";
        default: return "APU_REG";
    }
}
*/

// Implementação do canal 1: Tone & Sweep
ToneSweepChannel::ToneSweepChannel() {
    // Inicialização padrão
}

void ToneSweepChannel::tick(uint cycles) {
    if (!enabled) return;

    // Decrementa o timer
    if (timer > cycles) {
        timer -= cycles;
    } else {
        // Gera a próxima amostra quando o timer expira
        duty_position = (duty_position + 1) % 8;

        // Recalcula o timer baseado na frequência
        // Need to handle frequency potentially being 2048 (which would make timer 0)
        uint freq_val = frequency;
        if (freq_val > 2047) freq_val = 2047; // Avoid negative timer values
        timer = (2048 - freq_val) * 4;
        if (timer == 0) timer = 8192 * 4; // Handle frequency 2048 case? Check HW behaviour
    }

    // TODO: Implementar lógica de sweep
    // TODO: Implementar lógica de envelope
    // TODO: Implementar lógica de contador de comprimento (length counter)
}


float ToneSweepChannel::get_sample() const {
    if (!enabled) return 0.0f;

    // Padrões de duty (razão de trabalho)
    static const std::array<std::array<bool, 8>, 4> duty_patterns = {{
        {false, false, false, false, false, false, false, true},  // 12.5%
        {false, false, false, false, false, false, true, true},   // 25%
        {false, false, false, false, true, true, true, true},     // 50%
        {true, true, true, true, true, true, false, false}        // 75%
    }};

    // Retorna a amostra atual baseada no padrão de duty
    bool high = duty_patterns[duty_pattern][duty_position];
    return high ? (float)volume / 15.0f : -((float)volume / 15.0f);
}

void ToneSweepChannel::set_sweep_register(u8 value) {
    sweep_time = (value >> 4) & 0x07;
    sweep_decrease = check_bit(value, 3);
    sweep_shift = value & 0x07;
}

void ToneSweepChannel::set_length_duty_register(u8 value) {
    duty_pattern = (value >> 6) & 0x03;
    length_counter = 64 - (value & 0x3F);
}

void ToneSweepChannel::set_volume_envelope_register(u8 value) {
    envelope_initial_volume = (value >> 4) & 0x0F;
    envelope_increase = check_bit(value, 3);
    envelope_sweep_pace = value & 0x07;

    // Atualiza o volume atual
    volume = envelope_initial_volume;
    // TODO: Initialize envelope timer based on sweep_pace
}

void ToneSweepChannel::set_frequency_lo_register(u8 value) {
    // Bits baixos da frequência (8 bits)
    frequency = (frequency & 0x700) | value;
}

void ToneSweepChannel::set_frequency_hi_register(u8 value) {
    // Bits altos da frequência (3 bits) e flags de controle
    frequency = (frequency & 0xFF) | ((value & 0x07) << 8);

    // Reinicia o canal se o bit 7 estiver definido
    if (check_bit(value, 7)) {
        enabled = true; // Enable channel
        // Reinicia o timer
        uint freq_val = frequency;
        if (freq_val > 2047) freq_val = 2047;
        timer = (2048 - freq_val) * 4;
        if (timer == 0) timer = 8192 * 4; // Handle frequency 2048 case?
        // Reinicia o contador de comprimento se necessário
        if (length_counter == 0) {
            length_counter = 64;
        }
        // Reset duty position
        duty_position = 0;
        // Reset envelope
        volume = envelope_initial_volume;
        // TODO: Reset sweep timer/state
    }

    // Habilita o contador de comprimento se o bit 6 estiver definido
    length_enabled = check_bit(value, 6);
}

void ToneSweepChannel::update_frequency() {
    // Atualiza a frequência baseada nas configurações de sweep
    // TODO: Implement sweep timing and frequency calculation
    if (sweep_time > 0 && sweep_shift > 0) {
        // Simplified placeholder - proper implementation needed
        uint new_freq = frequency >> sweep_shift;

        if (sweep_decrease) {
            frequency -= new_freq;
        } else {
            frequency += new_freq;
        }

        // Desativa o canal se a frequência estiver fora dos limites
        if (frequency > 2047) {
            enabled = false;
        }
    }
}

// Implementação do canal 2: Tone
ToneChannel::ToneChannel() {
    // Inicialização padrão
}

void ToneChannel::tick(uint cycles) {
    if (!enabled) return;

    // Decrementa o timer
    if (timer > cycles) {
        timer -= cycles;
    } else {
        // Gera a próxima amostra quando o timer expira
        duty_position = (duty_position + 1) % 8;

        // Recalcula o timer baseado na frequência
        uint freq_val = frequency;
        if (freq_val > 2047) freq_val = 2047;
        timer = (2048 - freq_val) * 4;
        if (timer == 0) timer = 8192 * 4; // Handle frequency 2048 case?
    }
    // TODO: Implementar lógica de envelope
    // TODO: Implementar lógica de contador de comprimento (length counter)
}

float ToneChannel::get_sample() const {
    if (!enabled) return 0.0f;

    // Padrões de duty (razão de trabalho)
    static const std::array<std::array<bool, 8>, 4> duty_patterns = {{
        {false, false, false, false, false, false, false, true},  // 12.5%
        {false, false, false, false, false, false, true, true},   // 25%
        {false, false, false, false, true, true, true, true},     // 50%
        {true, true, true, true, true, true, false, false}        // 75%
    }};

    // Retorna a amostra atual baseada no padrão de duty
    bool high = duty_patterns[duty_pattern][duty_position];
    return high ? (float)volume / 15.0f : -((float)volume / 15.0f);
}

void ToneChannel::set_length_duty_register(u8 value) {
    duty_pattern = (value >> 6) & 0x03;
    length_counter = 64 - (value & 0x3F);
}

void ToneChannel::set_volume_envelope_register(u8 value) {
    envelope_initial_volume = (value >> 4) & 0x0F;
    envelope_increase = check_bit(value, 3);
    envelope_sweep_pace = value & 0x07;

    // Atualiza o volume atual
    volume = envelope_initial_volume;
    // TODO: Initialize envelope timer based on sweep_pace
}

void ToneChannel::set_frequency_lo_register(u8 value) {
    // Bits baixos da frequência (8 bits)
    frequency = (frequency & 0x700) | value;
}

void ToneChannel::set_frequency_hi_register(u8 value) {
    // Bits altos da frequência (3 bits) e flags de controle
    frequency = (frequency & 0xFF) | ((value & 0x07) << 8);

    // Reinicia o canal se o bit 7 estiver definido
    if (check_bit(value, 7)) {
        enabled = true; // Enable channel
        // Reinicia o timer
        uint freq_val = frequency;
        if (freq_val > 2047) freq_val = 2047;
        timer = (2048 - freq_val) * 4;
        if (timer == 0) timer = 8192 * 4; // Handle frequency 2048 case?
        // Reinicia o contador de comprimento se necessário
        if (length_counter == 0) {
            length_counter = 64;
        }
        // Reset duty position
        duty_position = 0;
        // Reset envelope
        volume = envelope_initial_volume;
    }

    // Habilita o contador de comprimento se o bit 6 estiver definido
    length_enabled = check_bit(value, 6);
}

// Implementação do canal 3: Wave Output
WaveChannel::WaveChannel() {
    // Inicialização padrão
    wave_pattern.fill(0);
}

void WaveChannel::tick(uint cycles) {
    if (!enabled) return;

    // Decrementa o timer
    if (timer > cycles) {
        timer -= cycles;
    } else {
        // Avança para a próxima posição na forma de onda
        position = (position + 1) % 32;

        // Recalcula o timer baseado na frequência
        uint freq_val = frequency;
        if (freq_val > 2047) freq_val = 2047;
        timer = (2048 - freq_val) * 2; // Wave timer is different
        if (timer == 0) timer = 8192 * 2; // Handle frequency 2048 case?
    }
    // TODO: Implementar lógica de contador de comprimento (length counter)
}


float WaveChannel::get_sample() const {
    if (!enabled) return 0.0f;

    // Obtém o valor da forma de onda atual
    u8 wave_byte = wave_pattern[position / 2];
    // If position is even, use high nibble, otherwise use low nibble
    u8 wave_nibble = (position % 2 == 0) ? (wave_byte >> 4) : (wave_byte & 0x0F);

    // Aplica o nível de saída (shift right)
    switch (output_level) {
        case 0: return 0.0f; // Mudo
        case 1: return (((float)wave_nibble / 7.5f) - 1.0f); // 100% (Range: [-1, 1])
        case 2: return (((float)wave_nibble / 7.5f) - 1.0f) * 0.5f; // 50% (Shift right 1)
        case 3: return (((float)wave_nibble / 7.5f) - 1.0f) * 0.25f; // 25% (Shift right 2)
        default: return 0.0f; // Should not happen
    }
}


void WaveChannel::set_enable_register(u8 value) {
    // Bit 7: DAC power
    enabled = check_bit(value, 7);
}

void WaveChannel::set_length_register(u8 value) {
    length_counter = 256 - value;
}

void WaveChannel::set_output_level_register(u8 value) {
    output_level = (value >> 5) & 0x03;
}

void WaveChannel::set_frequency_lo_register(u8 value) {
    // Bits baixos da frequência (8 bits)
    frequency = (frequency & 0x700) | value;
}

void WaveChannel::set_frequency_hi_register(u8 value) {
    // Bits altos da frequência (3 bits) e flags de controle
    frequency = (frequency & 0xFF) | ((value & 0x07) << 8);

    // Reinicia o canal se o bit 7 estiver definido
    if (check_bit(value, 7)) {
        enabled = true; // Needs DAC enable too (NR30)
        // Reinicia o timer
        uint freq_val = frequency;
        if (freq_val > 2047) freq_val = 2047;
        timer = (2048 - freq_val) * 2;
        if (timer == 0) timer = 8192 * 2; // Handle frequency 2048 case?
        // Reinicia a posição
        position = 0;
        // Reinicia o contador de comprimento se necessário
        if (length_counter == 0) {
            length_counter = 256;
        }
    }

    // Habilita o contador de comprimento se o bit 6 estiver definido
    length_enabled = check_bit(value, 6);
}

void WaveChannel::set_wave_pattern(u8 index, u8 value) {
    if (index < 16) {
        wave_pattern[index] = value;
    }
}

u8 WaveChannel::get_wave_pattern(u8 index) const {
    if (index < 16) {
        // TODO: Reading Wave RAM might be weird if accessed while channel is playing
        return wave_pattern[index];
    }
    return 0xFF; // Return FF for out-of-bounds reads? Check HW.
}

// Implementação do canal 4: Noise
NoiseChannel::NoiseChannel() {
    // Inicialização padrão
    lfsr = 0x7FFF; // Valor inicial do registrador de deslocamento
}

void NoiseChannel::tick(uint cycles) {
    if (!enabled) return;

    // Decrementa o timer
    if (timer > cycles) {
        timer -= cycles;
    } else {
         // Calculate divisor based on NR43
        static const int divisors[] = {8, 16, 32, 48, 64, 80, 96, 112};
        int divisor = divisors[dividing_ratio & 0x07];
        int clock_shift = shift_clock_frequency & 0x0F;

        // Recalcula o timer baseado na frequência
        uint new_timer_period = divisor << clock_shift;
        if (new_timer_period == 0) new_timer_period = 8; // Avoid zero period? Check HW
        timer = new_timer_period;

        // Calcula o próximo valor do LFSR
        // XOR bit 0 and bit 1
        bool xor_result = ((lfsr & 0x1) ^ ((lfsr >> 1) & 0x1)) != 0;
        // Shift LFSR right
        lfsr >>= 1;
        // Set bit 14 to the XOR result
        lfsr = bitwise::set_bit_to(lfsr, 14, xor_result);

        // Se estiver no modo de 7 bits (width mode = 1), também define o bit 6
        if (counter_step_width_mode) {
            lfsr = bitwise::set_bit_to(lfsr, 6, xor_result);
        }
    }
    // TODO: Implementar lógica de envelope
    // TODO: Implementar lógica de contador de comprimento (length counter)
}


float NoiseChannel::get_sample() const {
    if (!enabled) return 0.0f;

    // A saída é o inverso do bit 0 do LFSR
    bool high = (lfsr & 0x1) == 0;
    return high ? (float)volume / 15.0f : -((float)volume / 15.0f);
}

void NoiseChannel::set_length_register(u8 value) {
    // Only bits 0-5 are used for length
    length_counter = 64 - (value & 0x3F);
}

void NoiseChannel::set_volume_envelope_register(u8 value) {
    envelope_initial_volume = (value >> 4) & 0x0F;
    envelope_increase = check_bit(value, 3);
    envelope_sweep_pace = value & 0x07;

    // Atualiza o volume atual
    volume = envelope_initial_volume;
    // TODO: Initialize envelope timer based on sweep_pace
}

void NoiseChannel::set_polynomial_register(u8 value) {
    shift_clock_frequency = (value >> 4) & 0x0F;
    counter_step_width_mode = check_bit(value, 3);
    dividing_ratio = value & 0x07;
}

void NoiseChannel::set_counter_register(u8 value) {
    // Reinicia o canal se o bit 7 estiver definido
    if (check_bit(value, 7)) {
        enabled = true; // Enable channel
        // Reinicia o LFSR
        lfsr = 0x7FFF;
         // Recalcula o timer baseado na frequência
        static const int divisors[] = {8, 16, 32, 48, 64, 80, 96, 112};
        int divisor = divisors[dividing_ratio & 0x07];
        int clock_shift = shift_clock_frequency & 0x0F;
        uint new_timer_period = divisor << clock_shift;
        if (new_timer_period == 0) new_timer_period = 8; // Avoid zero period?
        timer = new_timer_period;
        // Reinicia o contador de comprimento se necessário
        if (length_counter == 0) {
            length_counter = 64;
        }
        // Reset envelope
        volume = envelope_initial_volume;
    }

    // Habilita o contador de comprimento se o bit 6 estiver definido
    length_enabled = check_bit(value, 6);
}


// Implementação da classe principal de áudio
Audio::Audio(Gameboy& inGb, Options& inOptions) :
    gb(inGb),
    options(inOptions),
    channel1(std::make_unique<ToneSweepChannel>()),
    channel2(std::make_unique<ToneChannel>()),
    channel3(std::make_unique<WaveChannel>()),
    channel4(std::make_unique<NoiseChannel>())
{
    // Inicializa os buffers de áudio
    left_buffer.reserve(2048); // Increase buffer capacity slightly
    right_buffer.reserve(2048);
}

void Audio::tick(uint cycles) {
    // TODO: Implement Frame Sequencer to clock length, envelope, sweep units
    // Frame sequencer runs at 512 Hz. Clocks length, envelope, sweep on specific steps.

    // For now, just tick the timer/frequency part of each channel
    channel1->tick(cycles);
    channel2->tick(cycles);
    channel3->tick(cycles);
    channel4->tick(cycles);

    // Incrementa o contador de amostras based on main GB clock cycles
    sample_counter += cycles;

    // Generate a new sample when enough *main clock* cycles have passed
    // Adjust CYCLES_PER_SAMPLE based on your main clock rate (CLOCK_RATE in definitions.h)
    // CYCLES_PER_SAMPLE = CLOCK_RATE / SAMPLE_RATE;
    // Example: 4194304 / 44100 = ~95.1
    static constexpr uint CYCLES_PER_SAMPLE = CLOCK_RATE / 44100; // Target 44100 Hz

    while (sample_counter >= CYCLES_PER_SAMPLE) {
        sample_counter -= CYCLES_PER_SAMPLE;

        // Mistura as amostras de todos os canais
        mix_samples();

        // Envia as amostras para o callback de áudio se houver amostras suficientes
        // Should be triggered more often, maybe smaller threshold like 512 or 1024
        if (left_buffer.size() >= 1024) {
            if (audio_callback) {
                 // Copy buffers before calling callback to avoid deadlocks if callback modifies them
                 // Although our current SDL callback doesn't modify, it's safer
                 std::vector<float> left_copy = left_buffer;
                 std::vector<float> right_copy = right_buffer;
                 audio_callback(left_copy, right_copy);
            }
            // Clear the *internal* buffers after sending
            left_buffer.clear();
            right_buffer.clear();
        }
    }
}

void Audio::register_audio_callback(const audio_callback_t& callback) {
    audio_callback = callback;
}

u8 Audio::read_register(u16 address) const {
     // Check if APU is powered off via NR52
    // Reading registers might return specific values when APU is off
    //bool audio_enabled = check_bit(nr52.value(), 7);
    // if (!audio_enabled && address != 0xFF26) {
        // Hardware might return 0xFF for most registers when off, need verification
    //    return 0xFF;
    // }
    if (address >= 0xFF10 && address <= 0xFF3F) {
        // std::cout << "[APU] Read " << apu_reg_name(address)
        //   << " (0x" << std::hex << address << ")" << std::dec << std::endl;
    }

    // Canal 1: Tone & Sweep (0xFF10-0xFF14)
    if (address >= 0xFF10 && address <= 0xFF14) {
        // Placeholder read values - consult pandocs for exact read masks/values
        switch (address) {
            case 0xFF10: return 0x80; // NR10 - Sweep (read mask unknown, often returns high bits)
            case 0xFF11: return 0x3F; // NR11 - Length/Duty (only duty readable? mask 0xC0?)
            case 0xFF12: return channel1->get_volume() << 4; // NR12 - Vol/Env (readable?)
            case 0xFF13: return 0xFF; // NR13 - Frequency Lo (write-only)
            case 0xFF14: return 0xBF; // NR14 - Frequency Hi (only length enable readable? mask 0x40?)
            default: return 0xFF;
        }
    }

    // Canal 2: Tone (0xFF16-0xFF19)
    if (address >= 0xFF16 && address <= 0xFF19) {
        // Placeholder read values
        switch (address) {
            case 0xFF16: return 0x3F; // NR21 - Length/Duty (only duty readable? mask 0xC0?)
            case 0xFF17: return channel2->get_volume() << 4; // NR22 - Volume Envelope (readable?)
            case 0xFF18: return 0xFF; // NR23 - Frequency Lo (write-only)
            case 0xFF19: return 0xBF; // NR24 - Frequency Hi (only length enable readable? mask 0x40?)
            default: return 0xFF;
        }
    }

    // Canal 3: Wave Output (0xFF1A-0xFF1E)
    if (address >= 0xFF1A && address <= 0xFF1E) {
        // Placeholder read values
        switch (address) {
            case 0xFF1A: return channel3->is_enabled() ? 0xFF : 0x7F; // NR30 - Enable (mask 0x80?)
            case 0xFF1B: return 0xFF; // NR31 - Length (write-only?)
            case 0xFF1C: return 0x9F; // NR32 - Output Level (mask 0x60?)
            case 0xFF1D: return 0xFF; // NR33 - Frequency Lo (write-only)
            case 0xFF1E: return 0xBF; // NR34 - Frequency Hi (only length enable readable? mask 0x40?)
            default: return 0xFF;
        }
    }

    // Canal 4: Noise (0xFF20-0xFF23)
    if (address >= 0xFF20 && address <= 0xFF23) {
        // Placeholder read values
        switch (address) {
            case 0xFF20: return 0xFF; // NR41 - Length (write-only?)
            case 0xFF21: return channel4->get_volume() << 4; // NR42 - Volume Envelope (readable?)
            case 0xFF22: return 0x00; // NR43 - Polynomial Counter (readable?)
            case 0xFF23: return 0xBF; // NR44 - Counter (only length enable readable? mask 0x40?)
            default: return 0xFF;
        }
    }

    // Controle de áudio (0xFF24-0xFF26)
    if (address >= 0xFF24 && address <= 0xFF26) {
        switch (address) {
            case 0xFF24: return nr50.value(); // NR50 - Channel control/ON-OFF/Volume
            case 0xFF25: return nr51.value(); // NR51 - Selection of sound output terminal
            case 0xFF26: {
                // NR52 - Sound on/off + Status flags
                u8 value = (nr52.value() & 0x80) | 0x70; // Bit 7 readable, bits 4-6 unused, usually read 1

                // Bits 0-3: Status of channels (1 if active)
                if (channel1->is_enabled()) value |= 0x01;
                if (channel2->is_enabled()) value |= 0x02;
                if (channel3->is_enabled()) value |= 0x04; // Needs DAC enable too
                if (channel4->is_enabled()) value |= 0x08;

                return value;
            }
            default: return 0xFF;
        }
    }

    // Wave Pattern RAM (0xFF30-0xFF3F)
    if (address >= 0xFF30 && address <= 0xFF3F) {
        // Reading wave RAM might be tricky if channel 3 is active
        // For now, just return the value. Hardware might return 0xFF sometimes.
        return channel3->get_wave_pattern(address - 0xFF30);
    }

    log_warn("Unmapped audio read: 0x%04X", address);
    return 0xFF; // Default for unmapped audio reads
}


void Audio::write_register(u16 address, u8 value) {
    // std::cout << "[DEBUG] Entered Audio::write_register, address=0x" << std::hex << address << ", value=0x" << (int)value << std::dec << std::endl;
    if (address >= 0xFF10 && address <= 0xFF3F) {
        // std::cout << "[APU] Write " << apu_reg_name(address)
        //   << " (0x" << std::hex << address << "): 0x"
        //   << std::hex << (int)value << std::dec << std::endl;
        if (address == 0xFF26) {
            // std::cout << "[APU] NR52 global sound "
            //   << ((value & 0x80) ? "ENABLED" : "DISABLED")
            //   << std::endl;
        }
    }
    // --- ADDED LOGGING START ---
    // Only log writes within the relevant APU register range
    if (address >= 0xFF10 && address <= 0xFF3F) {
        // Use log_debug or log_trace depending on desired verbosity
        log_debug("Audio Register Write: Address=0x%04X, Value=0x%02X", address, value);

        // Add specific logs for important registers
        switch (address) {
            // Channel 1
            case 0xFF10: log_debug("  NR10 (Sweep): 0x%02X", value); break;
            case 0xFF11: log_debug("  NR11 (Len/Duty): 0x%02X (Duty: %d, Len: %d)", value, (value >> 6) & 0x03, value & 0x3F); break;
            case 0xFF12: log_debug("  NR12 (Vol/Env): 0x%02X (Vol: %d, Inc: %d, Pace: %d)", value, (value >> 4) & 0x0F, check_bit(value, 3), value & 0x07); break;
            case 0xFF13: log_debug("  NR13 (Freq Lo): 0x%02X", value); break;
            case 0xFF14: log_debug("  NR14 (Freq Hi/Ctrl): 0x%02X (Trigger: %d, LenEn: %d)", value, check_bit(value, 7), check_bit(value, 6)); break;

            // Channel 2
            case 0xFF16: log_debug("  NR21 (Len/Duty): 0x%02X (Duty: %d, Len: %d)", value, (value >> 6) & 0x03, value & 0x3F); break;
            case 0xFF17: log_debug("  NR22 (Vol/Env): 0x%02X (Vol: %d, Inc: %d, Pace: %d)", value, (value >> 4) & 0x0F, check_bit(value, 3), value & 0x07); break;
            case 0xFF18: log_debug("  NR23 (Freq Lo): 0x%02X", value); break;
            case 0xFF19: log_debug("  NR24 (Freq Hi/Ctrl): 0x%02X (Trigger: %d, LenEn: %d)", value, check_bit(value, 7), check_bit(value, 6)); break;

            // Channel 3
            case 0xFF1A: log_debug("  NR30 (DAC Enable): 0x%02X (Enabled: %d)", value, check_bit(value, 7)); break;
            case 0xFF1B: log_debug("  NR31 (Length): 0x%02X (Len: %d)", value, 256 - value); break;
            case 0xFF1C: log_debug("  NR32 (Out Level): 0x%02X (Level: %d)", value, (value >> 5) & 0x03); break;
            case 0xFF1D: log_debug("  NR33 (Freq Lo): 0x%02X", value); break;
            case 0xFF1E: log_debug("  NR34 (Freq Hi/Ctrl): 0x%02X (Trigger: %d, LenEn: %d)", value, check_bit(value, 7), check_bit(value, 6)); break;

            // Channel 4
            case 0xFF20: log_debug("  NR41 (Length): 0x%02X (Len: %d)", value, 64 - (value & 0x3F)); break;
            case 0xFF21: log_debug("  NR42 (Vol/Env): 0x%02X (Vol: %d, Inc: %d, Pace: %d)", value, (value >> 4) & 0x0F, check_bit(value, 3), value & 0x07); break;
            case 0xFF22: log_debug("  NR43 (Poly): 0x%02X", value); break;
            case 0xFF23: log_debug("  NR44 (Counter/Ctrl): 0x%02X (Trigger: %d, LenEn: %d)", value, check_bit(value, 7), check_bit(value, 6)); break;

            // Control Registers
            case 0xFF24: log_debug("  NR50 (Volume Ctrl): 0x%02X (VinL: %d, LVol: %d, VinR: %d, RVol: %d)", value, check_bit(value, 7), (value >> 4) & 0x7, check_bit(value, 3), value & 0x7); break;
            case 0xFF25: log_debug("  NR51 (Panning): 0x%02X", value); break;
            case 0xFF26: log_debug("  NR52 (Master Ctrl): 0x%02X (Master En: %d)", value, check_bit(value, 7)); break;

             // Wave RAM
            case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
            case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
            case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
            case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
                 log_trace("  Wave RAM [0x%04X]: 0x%02X", address, value); // Use trace for wave ram potentially
                 break;
        }
    }
    // --- ADDED LOGGING END ---


    // Original write logic starts here...
    bool audio_enabled = check_bit(nr52.value(), 7);

    // If the audio system is completely off, only allow writes to NR52 (the master control)
    if (!audio_enabled && address != 0xFF26) {
        // Still allow writing to Wave RAM even if APU is off
        if (address >= 0xFF30 && address <= 0xFF3F) {
             channel3->set_wave_pattern(address - 0xFF30, value);
        }
        // Otherwise, ignore writes to other APU registers
        return;
    }

    // Channel 1: Tone & Sweep (0xFF10-0xFF14)
    if (address >= 0xFF10 && address <= 0xFF14) {
        switch (address) {
            case 0xFF10: channel1->set_sweep_register(value); break;
            case 0xFF11: channel1->set_length_duty_register(value); break;
            case 0xFF12: channel1->set_volume_envelope_register(value); break;
            case 0xFF13: channel1->set_frequency_lo_register(value); break;
            case 0xFF14: channel1->set_frequency_hi_register(value); break;
        }
        return;
    }

    // Channel 2: Tone (0xFF16-0xFF19)
    if (address >= 0xFF16 && address <= 0xFF19) {
        switch (address) {
            // 0xFF15 is unused
            case 0xFF16: channel2->set_length_duty_register(value); break;
            case 0xFF17: channel2->set_volume_envelope_register(value); break;
            case 0xFF18: channel2->set_frequency_lo_register(value); break;
            case 0xFF19: channel2->set_frequency_hi_register(value); break;
        }
        return;
    }

    // Channel 3: Wave Output (0xFF1A-0xFF1E)
    if (address >= 0xFF1A && address <= 0xFF1E) {
        switch (address) {
            case 0xFF1A: channel3->set_enable_register(value); break;
            case 0xFF1B: channel3->set_length_register(value); break;
            case 0xFF1C: channel3->set_output_level_register(value); break;
            case 0xFF1D: channel3->set_frequency_lo_register(value); break;
            case 0xFF1E: channel3->set_frequency_hi_register(value); break;
             // 0xFF1F is unused
        }
        return;
    }

    // Channel 4: Noise (0xFF20-0xFF23)
    if (address >= 0xFF20 && address <= 0xFF23) {
         // 0xFF1F is unused
        switch (address) {
            case 0xFF20: channel4->set_length_register(value); break;
            case 0xFF21: channel4->set_volume_envelope_register(value); break;
            case 0xFF22: channel4->set_polynomial_register(value); break;
            case 0xFF23: channel4->set_counter_register(value); break;
        }
        return;
    }

    // Controle de áudio (0xFF24-0xFF26)
    if (address >= 0xFF24 && address <= 0xFF26) {
        switch (address) {
            case 0xFF24: nr50.set(value); break; // NR50 - Channel control/ON-OFF/Volume
            case 0xFF25: nr51.set(value); break; // NR51 - Selection of sound output terminal
            case 0xFF26: {
                // NR52 - Sound on/off
                bool was_enabled = check_bit(nr52.value(), 7);
                // Only bit 7 is writable. Preserve read-only status bits 0-3.
                u8 current_status = nr52.value() & 0x0F; // Keep current status bits
                nr52.set((value & 0x80) | current_status | 0x70); // Set master enable, keep status, set unused bits high

                bool now_enabled = check_bit(nr52.value(), 7);

                // If the audio was just disabled, reset most APU state.
                if (was_enabled && !now_enabled) {
                    // Reset all registers except NR52 itself (according to pandocs)
                    for(u16 reg_addr = 0xFF10; reg_addr <= 0xFF25; ++reg_addr) {
                        // Wave RAM isn't cleared
                         if (reg_addr < 0xFF30 || reg_addr > 0xFF3F) {
                              // Length counters might behave differently? Need check.
                              // For simplicity, let's try resetting them via write_register
                              // (Need default/reset values for each register)
                              // write_register(reg_addr, 0x00); // Simplified reset - need accurate defaults!
                         }
                    }
                    // Maybe disable channels explicitly too
                    channel1->set_enabled(false);
                    channel2->set_enabled(false);
                    channel3->set_enabled(false); // This might just disable DAC
                    channel4->set_enabled(false);
                    // Reset internal state like timers, envelopes etc. (Needs implementation in channel classes)
                }
                break;
            }
        }
        return;
    }

    // Wave Pattern RAM (0xFF30-0xFF3F)
    // (Handled even if APU is off)
    if (address >= 0xFF30 && address <= 0xFF3F) {
        // Writing to Wave RAM while Channel 3 is active might have timing issues/artefacts.
        channel3->set_wave_pattern(address - 0xFF30, value);
        return;
    }

    // If we reach here, it's an unhandled audio-related write
    log_warn("Unhandled audio register write: Address=0x%04X, Value=0x%02X", address, value);

}


void Audio::mix_samples() {
    // std::cout << "[DEBUG] Entered Audio::mix_samples" << std::endl;
    // Verifica se o áudio está habilitado via NR52 bit 7
    bool audio_enabled = check_bit(nr52.value(), 7);
    if (!audio_enabled) {
        // If APU is off, output silence
        left_buffer.push_back(0.0f);
        right_buffer.push_back(0.0f);
        return;
    }

    // Obtém as amostras de cada canal
    float sample1 = channel1->get_sample();
    float sample2 = channel2->get_sample();
    float sample3 = channel3->get_sample();
    float sample4 = channel4->get_sample();

    // Get master volume levels (0-7, add 1 to get 1-8 range?) - Pandocs says 0-7 directly scales
    u8 left_vol = (nr50.value() >> 4) & 0x7;
    u8 right_vol = nr50.value() & 0x7;

    // Calculate final left sample based on NR51 panning and master volume
    float left_final = 0.0f;
    if (check_bit(nr51.value(), 7)) left_final += sample4; // CH4 left
    if (check_bit(nr51.value(), 6)) left_final += sample3; // CH3 left
    if (check_bit(nr51.value(), 5)) left_final += sample2; // CH2 left
    if (check_bit(nr51.value(), 4)) left_final += sample1; // CH1 left
    // Scale by master volume (0-7 means multiply by 0/8 to 7/8?) -> No, Pandocs says adds 1. So 1-8 range?
    // Let's try direct scaling 0-7 first.
    left_final *= (float)(left_vol) / 7.0f; // Scale by master volume (0.0 to 1.0)

    // Calculate final right sample based on NR51 panning and master volume
    float right_final = 0.0f;
    if (check_bit(nr51.value(), 3)) right_final += sample4; // CH4 right
    if (check_bit(nr51.value(), 2)) right_final += sample3; // CH3 right
    if (check_bit(nr51.value(), 1)) right_final += sample2; // CH2 right
    if (check_bit(nr51.value(), 0)) right_final += sample1; // CH1 right
    right_final *= (float)(right_vol) / 7.0f; // Scale by master volume

    // The samples from get_sample() are already ~[-1.0, 1.0].
    // Mixing involves summing, so we need to scale down to avoid clipping.
    // Dividing by 4 (number of channels) is a simple approach.
    left_final /= 4.0f;
    right_final /= 4.0f;

    // Clamp the final samples to [-1.0, 1.0] just in case
    left_final = std::max(-1.0f, std::min(1.0f, left_final));
    right_final = std::max(-1.0f, std::min(1.0f, right_final));

    // Adiciona as amostras aos buffers internos da classe Audio
    left_buffer.push_back(left_final);
    right_buffer.push_back(right_final);

    if (!left_buffer.empty()) {
        float min = left_buffer[0];
        float max = left_buffer[0];
        float sum = 0.0f;
        for (float v : left_buffer) {
            if (v < min) min = v;
            if (v > max) max = v;
            sum += v;
        }
        // std::cout << "[APU] Left buffer: min=" << min
        //   << ", max=" << max
        //   << ", mean=" << (sum / left_buffer.size())
        //   << std::endl;
    }

    if (!right_buffer.empty()) {
        float min = right_buffer[0];
        float max = right_buffer[0];
        float sum = 0.0f;
        for (float v : right_buffer) {
            if (v < min) min = v;
            if (v > max) max = v;
            sum += v;
        }
        // std::cout << "[APU] Right buffer: min=" << min
        //   << ", max=" << max
        //   << ", mean=" << (sum / right_buffer.size())
        //   << std::endl;
    }
}


void Audio::update_channel_control() {
    // This function seems redundant now as the status bits are updated
    // directly in the read_register function for NR52.
    // We might need it if channel enable flags change outside of register writes (e.g., length counter expiry).
    // For now, let's comment it out or simplify it.

    // u8 value = nr52.value() & 0x80; // Mantém apenas o bit 7 (master enable)
    // value |= 0x70; // Set unused bits high

    // if (channel1->is_enabled()) value |= 0x01;
    // if (channel2->is_enabled()) value |= 0x02;
    // if (channel3->is_enabled()) value |= 0x04; // Needs DAC check too
    // if (channel4->is_enabled()) value |= 0x08;

    // nr52.set(value);
}