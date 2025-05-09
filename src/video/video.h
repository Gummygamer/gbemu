#pragma once

#include "framebuffer.h"
#include "tile.h"

#include "../mmu.h"
#include "../register.h"
#include "../definitions.h"
#include "../options.h"

#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

class Gameboy;

using vblank_callback_t = std::function<void(const FrameBuffer&)>;

enum class VideoMode {
    ACCESS_OAM,
    ACCESS_VRAM,
    HBLANK,
    VBLANK,
};

struct TileInfo {
    u8 line;
    std::vector<u8> pixels;
};

class Video {
public:
    Video(Gameboy& inGb, Options& inOptions);

    void tick(Cycles cycles);
    void register_vblank_callback(const vblank_callback_t& _vblank_callback);

    u8 read(const Address& address);
    void write(const Address& address, u8 byte);

    u8 control_byte;

    ByteRegister lcd_control;
    ByteRegister lcd_status;

    ByteRegister scroll_y;
    ByteRegister scroll_x;

    /* LCDC Y-coordinate */
    ByteRegister line; /* Line y-position: register LY */
    ByteRegister ly_compare;

    ByteRegister window_y;
    ByteRegister window_x; /* Note: x - 7 */

    ByteRegister bg_palette;
    ByteRegister sprite_palette_0; /* OBP0 */
    ByteRegister sprite_palette_1; /* OBP1 */

    /* TODO: LCD Color Palettes (CGB) */
    /* TODO: LCD VRAM Bank (CGB) */

    ByteRegister dma_transfer; /* DMA */

    bool debug_disable_background = false;
    bool debug_disable_sprites = false;
    bool debug_disable_window = false;

private:
    void write_scanline(u8 current_line);
    void write_sprites();
    void draw();
    void draw_bg_line(uint current_line);
    void draw_window_line(uint current_line);
    void draw_sprite(uint sprite_n);
    static auto get_pixel_from_line(u8 byte1, u8 byte2, u8 pixel_index) -> GBColor;

    static auto is_on_screen(u8 x, u8 y) -> bool;
    static auto is_on_screen_x(u8 x) -> bool;
    static auto is_on_screen_y(u8 y) -> bool;

    auto display_enabled() const -> bool;
    auto window_tile_map() const -> bool;
    auto window_enabled() const -> bool;
    auto bg_window_tile_data() const -> bool;
    auto bg_tile_map_display() const -> bool;
    auto sprite_size() const -> bool;
    auto sprites_enabled() const -> bool;
    auto bg_enabled() const -> bool;

    auto get_tile_info(Address tile_set_location, u8 tile_id, u8 tile_line) const -> TileInfo;
    
    // Novo método para obter a cor original (antes da paleta) de um pixel
    auto get_original_color_at(u8 x, u8 y) -> GBColor;

    static auto get_real_color(u8 pixel_value) -> Color;
    static auto load_palette(ByteRegister& palette_register) -> Palette;
    static auto get_color_from_palette(GBColor color, const Palette& palette) -> Color;

    Gameboy& gb;

    FrameBuffer buffer;
    FrameBuffer background_map;

    std::vector<u8> video_ram;

    VideoMode current_mode = VideoMode::ACCESS_OAM;
    uint cycle_counter = 0;

    vblank_callback_t vblank_callback;
    
    // Buffer para armazenar as cores originais dos pixels
    std::vector<GBColor> original_colors;
};

const uint CLOCKS_PER_HBLANK = 204; /* Mode 0 */
const uint CLOCKS_PER_SCANLINE_OAM = 80; /* Mode 2 */
const uint CLOCKS_PER_SCANLINE_VRAM = 172; /* Mode 3 */
const uint CLOCKS_PER_SCANLINE =
    (CLOCKS_PER_SCANLINE_OAM + CLOCKS_PER_SCANLINE_VRAM + CLOCKS_PER_HBLANK);

const uint CLOCKS_PER_VBLANK = 4560; /* Mode 1 */
const uint SCANLINES_PER_FRAME = 144;
const uint CLOCKS_PER_FRAME = (CLOCKS_PER_SCANLINE * SCANLINES_PER_FRAME) + CLOCKS_PER_VBLANK;
