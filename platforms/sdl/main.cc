#include <SDL.h>

#include <chrono>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <iostream>
#include <optional>
#include <fstream>

#include "../../src/gameboy.h"
#include "../../src/util/log.h"

// Funções de utilidade locais
std::vector<u8> read_bytes_from_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Could not open file: " << filename << std::endl;
        return {};
    }
    
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<u8> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return buffer;
    }
    
    std::cerr << "Could not read file: " << filename << std::endl;
    return {};
}

bool file_exists_check(const std::string& filename) {
    std::ifstream file(filename);
    return file.good();
}

void write_bytes_to_file(const std::string& filename, const std::vector<u8>& data) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Could not open file for writing: " << filename << std::endl;
        return;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (!file) {
        std::cerr << "Could not write to file: " << filename << std::endl;
    }
}

// Buffers globais para áudio
std::vector<float> g_left_buffer;
std::vector<float> g_right_buffer;
std::mutex audio_mutex;
std::atomic<bool> audio_callback_called(false);
std::atomic<int> audio_sample_count(0);

// Mutex para sincronização de vídeo
std::mutex video_mutex;
std::vector<uint8_t> current_frame_buffer(160 * 144 * 3, 0);
bool frame_updated = false;

// Flag para indicar se o callback de vídeo foi chamado
std::atomic<bool> video_callback_called(false);
std::atomic<int> frame_count(0);

// Função de callback de áudio para SDL
void audio_callback(void* userdata, Uint8* stream, int len) {
    /*std::cout << "[SDL] Audio callback: requested " << len
              << " bytes (" << (len / (int)sizeof(float)) << " float samples)"
              << std::endl;*/
    // Limpa o stream
    SDL_memset(stream, 0, len);
    
    // Converte o stream para float (o formato que nosso sistema de áudio usa)
    float* float_stream = reinterpret_cast<float*>(stream);
    int samples = len / (sizeof(float) * 2); // Dividido por 2 canais
    
    // Copia as amostras para o stream de saída (intercalando esquerdo/direito)
    std::lock_guard<std::mutex> lock(audio_mutex);
    int copy_samples = std::min(static_cast<int>(g_left_buffer.size()), samples);
    
    for (int i = 0; i < copy_samples; i++) {
        float_stream[i*2] = g_left_buffer[i];     // Canal esquerdo
        float_stream[i*2+1] = g_right_buffer[i];  // Canal direito
    }
    
    // Remove as amostras usadas dos buffers
    if (copy_samples > 0) {
        g_left_buffer.erase(g_left_buffer.begin(), g_left_buffer.begin() + copy_samples);
        g_right_buffer.erase(g_right_buffer.begin(), g_right_buffer.begin() + copy_samples);
        audio_callback_called = true;
        audio_sample_count += copy_samples;
    }
    
    // Se não temos amostras suficientes, gere silêncio
    if (copy_samples < samples) {
        // Preenche o restante do buffer com silêncio
        for (int i = copy_samples; i < samples; i++) {
            float_stream[i*2] = 0.0f;     // Canal esquerdo
            float_stream[i*2+1] = 0.0f;   // Canal direito
        }
    }
}

// Função para gerar amostras de áudio de teste
void generate_test_audio() {
    // Gera um tom simples para testar o áudio
    const int sample_rate = 44100;
    const float frequency = 440.0f; // 440 Hz (nota A4)
    const float amplitude = 0.5f;
    const float duration = 0.5f; // 0.5 segundos
    const int num_samples = static_cast<int>(sample_rate * duration);
    
    std::vector<float> left_samples(num_samples);
    std::vector<float> right_samples(num_samples);
    
    for (int i = 0; i < num_samples; i++) {
        float t = static_cast<float>(i) / sample_rate;
        float sample = amplitude * std::sin(2.0f * M_PI * frequency * t);
        left_samples[i] = sample;
        right_samples[i] = sample;
    }
    
    std::lock_guard<std::mutex> lock(audio_mutex);
    g_left_buffer.insert(g_left_buffer.end(), left_samples.begin(), left_samples.end());
    g_right_buffer.insert(g_right_buffer.end(), right_samples.begin(), right_samples.end());
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <rom_file>" << std::endl;
        return 1;
    }

    std::cout << "Starting gbemu with ROM: " << argv[1] << std::endl;

    // Inicializa o SDL com vídeo e áudio
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    std::cout << "SDL initialized successfully" << std::endl;

    // Cria a janela
    SDL_Window* window = SDL_CreateWindow(
        "gbemu",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        160 * 3, 144 * 3,
        SDL_WINDOW_SHOWN
    );

    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    std::cout << "SDL window created successfully" << std::endl;

    // Cria o renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (renderer == nullptr) {
        SDL_DestroyWindow(window);
        std::cerr << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    std::cout << "SDL renderer created successfully" << std::endl;

    // Define o tamanho lógico do renderer
    SDL_RenderSetLogicalSize(renderer, 160, 144);

    // Cria a textura para renderização
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        160, 144
    );

    std::cout << "SDL texture created successfully" << std::endl;

    // Configuração do áudio
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = 44100;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_callback;

    SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_device == 0) {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
    } else {
        SDL_PauseAudioDevice(audio_device, 0); // Inicia a reprodução de áudio
        std::cout << "SDL audio device opened successfully" << std::endl;
        std::cout << "Audio format: " << have.format << " (expected " << AUDIO_F32 << ")" << std::endl;
        std::cout << "Audio channels: " << (int)have.channels << std::endl;
        std::cout << "Audio frequency: " << have.freq << " Hz" << std::endl;
        std::cout << "Audio buffer size: " << have.samples << " samples" << std::endl;
    }

    // Gera um tom de teste para verificar se o áudio está funcionando
    // generate_test_audio();
    // std::cout << "Generated test audio tone" << std::endl;

    // Carrega a ROM
    std::cout << "Loading ROM file: " << argv[1] << std::endl;
    std::vector<u8> rom_data = read_bytes_from_file(argv[1]);
    if (rom_data.empty()) {
        std::cerr << "Failed to load ROM file: " << argv[1] << std::endl;
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::cout << "ROM loaded successfully, size: " << rom_data.size() << " bytes" << std::endl;

    // Carrega os dados de save, se existirem
    std::vector<u8> save_data;
    std::string save_filename = std::string(argv[1]) + ".sav";
    if (file_exists_check(save_filename)) {
        save_data = read_bytes_from_file(save_filename);
        std::cout << "Save data loaded, size: " << save_data.size() << " bytes" << std::endl;
    } else {
        std::cout << "No save data found" << std::endl;
    }

    // Configura as opções do emulador
    Options options;
    for (int i = 2; i < argc; i++) {
        std::string arg(argv[i]);

        if (arg == "--debug") {
            options.debugger = true;
            std::cout << "Debug mode enabled" << std::endl;
        } else if (arg == "--trace") {
            options.trace = true;
            std::cout << "Trace mode enabled" << std::endl;
        } else if (arg == "--silent") {
            options.disable_logs = true;
            std::cout << "Silent mode enabled" << std::endl;
        } else if (arg == "--exit-on-infinite-jr") {
            options.exit_on_infinite_jr = true;
            std::cout << "Exit on infinite JR enabled" << std::endl;
        } else if (arg == "--print-serial-output") {
            options.print_serial = true;
            std::cout << "Print serial output enabled" << std::endl;
        }
    }

    std::cout << "Creating Gameboy instance..." << std::endl;

    // Cria a instância do Gameboy
    Gameboy gameboy(rom_data, options, save_data);

    std::cout << "Gameboy instance created successfully" << std::endl;

    // Flag para controlar o loop principal
    std::atomic<bool> running(true);

    // Evento SDL
    SDL_Event e;

    // Função para converter teclas SDL em botões do Game Boy
    auto key_to_button = [](SDL_Keycode key) -> std::optional<GbButton> {
        switch (key) {
            case SDLK_UP:
                return GbButton::Up;
            case SDLK_DOWN:
                return GbButton::Down;
            case SDLK_LEFT:
                return GbButton::Left;
            case SDLK_RIGHT:
                return GbButton::Right;
            case SDLK_z:
                return GbButton::A;
            case SDLK_x:
                return GbButton::B;
            case SDLK_BACKSPACE:
                return GbButton::Select;
            case SDLK_RETURN:
                return GbButton::Start;
            default:
                return std::nullopt;
        }
    };

    std::cout << "Starting emulator thread..." << std::endl;

    // Thread do emulador
    std::thread emulator_thread([&]() {
        try {
            std::cout << "Emulator thread started" << std::endl;
            gameboy.run(
                [&running]() { 
                    return !running; 
                },
                [](const FrameBuffer& buffer) {
                    // Marca que o callback de vídeo foi chamado
                    video_callback_called = true;
                    frame_count++;
                    
                    if (frame_count % 60 == 0) {
                        std::cout << "Rendered frame " << frame_count << std::endl;
                    }
                    
                    // Buffer para os dados RGB
                    std::vector<uint8_t> rgb_buffer(160 * 144 * 3, 0);
                    
                    // Convertemos os pixels da FrameBuffer para o formato RGB
                    for (int y = 0; y < 144; y++) {
                        for (int x = 0; x < 160; x++) {
                            Color pixel = buffer.get_pixel(x, y);
                            int idx = (y * 160 + x) * 3;
                            
                            // Definimos os valores RGB com base na cor
                            switch (pixel) {
                                case Color::White:
                                    rgb_buffer[idx] = 255;
                                    rgb_buffer[idx+1] = 255;
                                    rgb_buffer[idx+2] = 255;
                                    break;
                                case Color::LightGray:
                                    rgb_buffer[idx] = 192;
                                    rgb_buffer[idx+1] = 192;
                                    rgb_buffer[idx+2] = 192;
                                    break;
                                case Color::DarkGray:
                                    rgb_buffer[idx] = 96;
                                    rgb_buffer[idx+1] = 96;
                                    rgb_buffer[idx+2] = 96;
                                    break;
                                case Color::Black:
                                default:
                                    rgb_buffer[idx] = 0;
                                    rgb_buffer[idx+1] = 0;
                                    rgb_buffer[idx+2] = 0;
                                    break;
                            }
                        }
                    }
                    
                    // Atualiza o buffer de frame atual
                    std::lock_guard<std::mutex> lock(video_mutex);
                    current_frame_buffer = rgb_buffer;
                    frame_updated = true;
                },
                [](const std::vector<float>& left, const std::vector<float>& right) {
                    // Este callback será chamado quando novas amostras de áudio estiverem disponíveis
                    std::lock_guard<std::mutex> lock(audio_mutex);
                    
                    // Verifica se recebemos amostras válidas
                    if (!left.empty() && !right.empty()) {
                        // Adiciona as amostras aos buffers globais
                        g_left_buffer.insert(g_left_buffer.end(), left.begin(), left.end());
                        g_right_buffer.insert(g_right_buffer.end(), right.begin(), right.end());
                        
                        // Limita o tamanho dos buffers para evitar uso excessivo de memória
                        const size_t max_buffer_size = 44100 * 2; // 2 segundos de áudio
                        if (g_left_buffer.size() > max_buffer_size) {
                            g_left_buffer.erase(g_left_buffer.begin(), g_left_buffer.begin() + (g_left_buffer.size() - max_buffer_size));
                        }
                        if (g_right_buffer.size() > max_buffer_size) {
                            g_right_buffer.erase(g_right_buffer.begin(), g_right_buffer.begin() + (g_right_buffer.size() - max_buffer_size));
                        }
                        
                        if (audio_sample_count % 44100 == 0) {
                            std::cout << "Audio samples received: " << left.size() << " (total: " << audio_sample_count << ")" << std::endl;
                        }
                    }
                }
            );
            std::cout << "Emulator run completed" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Exception in emulator thread: " << e.what() << std::endl;
            running = false;
        } catch (...) {
            std::cerr << "Unknown exception in emulator thread" << std::endl;
            running = false;
        }
    });

    std::cout << "Emulator thread started, entering main loop" << std::endl;

    // Contador para verificar se o callback de vídeo foi chamado
    int check_counter = 0;

    // Loop principal
    while (running) {
        // Frame timing start
        auto frame_start = std::chrono::high_resolution_clock::now();

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                std::cout << "Quit event received" << std::endl;
                running = false;
            } else if (e.type == SDL_KEYDOWN) {
                auto button = key_to_button(e.key.keysym.sym);
                if (button) {
                    gameboy.button_pressed(*button);
                }

                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    std::cout << "Escape key pressed" << std::endl;
                    running = false;
                } else if (e.key.keysym.sym == SDLK_1) {
                    gameboy.debug_toggle_background();
                } else if (e.key.keysym.sym == SDLK_2) {
                    gameboy.debug_toggle_sprites();
                } else if (e.key.keysym.sym == SDLK_3) {
                    gameboy.debug_toggle_window();
                } else if (e.key.keysym.sym == SDLK_t) {
                    // Gera um tom de teste quando a tecla T é pressionada
                    generate_test_audio();
                    std::cout << "Generated test audio tone" << std::endl;
                }
            } else if (e.type == SDL_KEYUP) {
                auto button = key_to_button(e.key.keysym.sym);
                if (button) {
                    gameboy.button_released(*button);
                }
            }
        }

        // Verifica se o callback de vídeo foi chamado
        check_counter++;
        if (check_counter % 100 == 0) {
            if (!video_callback_called) {
                std::cout << "Warning: Video callback has not been called yet after " << check_counter / 100 << " seconds" << std::endl;
            } else {
                std::cout << "Video callback has been called, frames rendered: " << frame_count << std::endl;
            }
            
            if (!audio_callback_called) {
                std::cout << "Warning: Audio callback has not been called yet after " << check_counter / 100 << " seconds" << std::endl;
            } else {
                std::cout << "Audio callback has been called, samples processed: " << audio_sample_count << std::endl;
            }
        }

        // Renderiza o frame atual
        {
            std::lock_guard<std::mutex> lock(video_mutex);
            if (frame_updated) {
                SDL_UpdateTexture(
                    texture,
                    NULL,
                    current_frame_buffer.data(),
                    160 * 3
                );

                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);
                
                frame_updated = false;
            }
        }

        // Frame timing end and sleep to cap at ~59.73 FPS
        constexpr double target_fps = 59.73;
        constexpr double target_frame_time_ms = 1000.0 / target_fps;
        auto frame_end = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        if (elapsed_ms < target_frame_time_ms) {
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(target_frame_time_ms - elapsed_ms));
        }
    }

    std::cout << "Main loop exited, joining emulator thread" << std::endl;

    // Aguarda a thread do emulador terminar
    emulator_thread.join();

    std::cout << "Emulator thread joined" << std::endl;

    // Salva o RAM do cartucho
    if (!rom_data.empty()) {
        std::cout << "Saving cartridge RAM" << std::endl;
        write_bytes_to_file(save_filename, gameboy.get_cartridge_ram());
    }

    // Limpa os recursos do SDL
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    
    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
    }
    
    SDL_Quit();

    std::cout << "SDL resources cleaned up, exiting" << std::endl;

    return 0;
}

