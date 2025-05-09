#include "video.h"

#include "color.h"
#include "../gameboy.h"
#include "../cpu/cpu.h"

#include "../util/bitwise.h"
#include "../util/log.h"

using bitwise::check_bit;

Video::Video(Gameboy& inGb, Options& inOptions) :
    gb(inGb),
    buffer(GAMEBOY_WIDTH, GAMEBOY_HEIGHT),
    background_map(BG_MAP_SIZE, BG_MAP_SIZE)
{
    video_ram = std::vector<u8>(0x4000);
}

u8 Video::read(const Address& address) {
    return video_ram.at(address.value());
}

void Video::write(const Address& address, u8 value) {
    video_ram.at(address.value()) = value;
}

void Video::tick(Cycles cycles) {
    cycle_counter += cycles.cycles;

    switch (current_mode) {
        case VideoMode::ACCESS_OAM:
            if (cycle_counter >= CLOCKS_PER_SCANLINE_OAM) {
                cycle_counter = cycle_counter % CLOCKS_PER_SCANLINE_OAM;
                lcd_status.set_bit_to(1, true);
                lcd_status.set_bit_to(0, true);
                current_mode = VideoMode::ACCESS_VRAM;
            }
            break;
        case VideoMode::ACCESS_VRAM:
            if (cycle_counter >= CLOCKS_PER_SCANLINE_VRAM) {
                cycle_counter = cycle_counter % CLOCKS_PER_SCANLINE_VRAM;
                current_mode = VideoMode::HBLANK;

                bool hblank_interrupt = bitwise::check_bit(lcd_status.value(), 3);

                if (hblank_interrupt) {
                    gb.cpu.interrupt_flag.set_bit_to(1, true);
                }

                bool ly_coincidence_interrupt = bitwise::check_bit(lcd_status.value(), 6);
                bool ly_coincidence = ly_compare.value() == line.value();
                if (ly_coincidence_interrupt && ly_coincidence) {
                    gb.cpu.interrupt_flag.set_bit_to(1, true);
                }
                lcd_status.set_bit_to(2, ly_coincidence);

                lcd_status.set_bit_to(1, false);
                lcd_status.set_bit_to(0, false);
            }
            break;
        case VideoMode::HBLANK:
            if (cycle_counter >= CLOCKS_PER_HBLANK) {

                write_scanline(line.value());
                line.increment();

                cycle_counter = cycle_counter % CLOCKS_PER_HBLANK;

                /* Line 145 (index 144) is the first line of VBLANK */
                if (line == 144) {
                    current_mode = VideoMode::VBLANK;
                    lcd_status.set_bit_to(1, false);
                    lcd_status.set_bit_to(0, true);
                    gb.cpu.interrupt_flag.set_bit_to(0, true);
                } else {
                    lcd_status.set_bit_to(1, true);
                    lcd_status.set_bit_to(0, false);
                    current_mode = VideoMode::ACCESS_OAM;
                }
            }
            break;
        case VideoMode::VBLANK:
            if (cycle_counter >= CLOCKS_PER_SCANLINE) {
                line.increment();

                cycle_counter = cycle_counter % CLOCKS_PER_SCANLINE;

                /* Line 155 (index 154) is the last line */
                if (line == 154) {
                    write_sprites();
                    draw();
                    buffer.reset();
                    line.reset();
                    current_mode = VideoMode::ACCESS_OAM;
                    lcd_status.set_bit_to(1, true);
                    lcd_status.set_bit_to(0, false);
                };
            }
            break;
    }
}

auto Video::display_enabled() const -> bool { return check_bit(control_byte, 7); }
auto Video::window_tile_map() const -> bool { return check_bit(control_byte, 6); }
auto Video::window_enabled() const -> bool { return check_bit(control_byte, 5); }
auto Video::bg_window_tile_data() const -> bool { return check_bit(control_byte, 4); }
auto Video::bg_tile_map_display() const -> bool { return check_bit(control_byte, 3); }
auto Video::sprite_size() const -> bool { return check_bit(control_byte, 2); }
auto Video::sprites_enabled() const -> bool { return check_bit(control_byte, 1); }
auto Video::bg_enabled() const -> bool { return check_bit(control_byte, 0); }

void Video::write_scanline(u8 current_line) {
    if (!display_enabled()) { return; }

    if (bg_enabled() && !debug_disable_background) {
        draw_bg_line(current_line);
    }

    if (window_enabled() && !debug_disable_window) {
        draw_window_line(current_line);
    }
}

void Video::write_sprites() {
    if (!sprites_enabled() || debug_disable_sprites) { return; }

    for (uint sprite_n = 0; sprite_n < 40; sprite_n++) {
        draw_sprite(sprite_n);
    }
}

void Video::draw_bg_line(uint current_line) {
    /* Note: tileset two uses signed numbering to share half the tiles with tileset 1 */
    bool use_tile_set_zero = bg_window_tile_data();
    bool use_tile_map_zero = !bg_tile_map_display();

    Palette palette = load_palette(bg_palette);

    Address tile_set_address = use_tile_set_zero
        ? TILE_SET_ZERO_ADDRESS
        : TILE_SET_ONE_ADDRESS;

    Address tile_map_address = use_tile_map_zero
        ? TILE_MAP_ZERO_ADDRESS
        : TILE_MAP_ONE_ADDRESS;

    /* The pixel row we're drawing on the screen is constant since we're only
     * drawing a single line */
    uint screen_y = current_line;

    // Cache para armazenar os dados de linha de cada tile
    struct TileLineData {
        u8 pixels_1;
        u8 pixels_2;
    };
    std::unordered_map<uint, TileLineData> tile_line_cache;

    for (uint screen_x = 0; screen_x < GAMEBOY_WIDTH; screen_x++) {
        /* Work out the position of the pixel in the framebuffer */
        uint scrolled_x = screen_x + scroll_x.value();
        uint scrolled_y = screen_y + scroll_y.value();

        /* Work out the index of the pixel in the full background map */
        uint bg_map_x = scrolled_x % BG_MAP_SIZE;
        uint by_map_y = scrolled_y % BG_MAP_SIZE;

        /* Work out which tile of the bg_map this pixel is in, and the index of that tile
         * in the array of all tiles */
        uint tile_x = bg_map_x / TILE_WIDTH_PX;
        uint tile_y = by_map_y / TILE_HEIGHT_PX;

        /* Work out which specific (x,y) inside that tile we're going to render */
        uint tile_pixel_x = bg_map_x % TILE_WIDTH_PX;
        uint tile_pixel_y = by_map_y % TILE_HEIGHT_PX;

        /* Work out the address of the tile ID from the tile map */
        uint tile_index = tile_y * TILES_PER_LINE + tile_x;
        Address tile_id_address = tile_map_address + tile_index;

        /* Grab the ID of the tile we'll get data from in the tile map */
        u8 tile_id = gb.mmu.read(tile_id_address);

        /* Calculate the offset from the start of the tile data memory where
         * the data for our tile lives */
        uint tile_data_mem_offset = use_tile_set_zero
            ? tile_id * TILE_BYTES
            : (static_cast<s8>(tile_id) + 128) * TILE_BYTES;

        /* Calculate the extra offset to the data for the line of pixels we
         * are rendering from.
         * 2 (bytes per line of pixels) * y (lines) */
        uint tile_data_line_offset = tile_pixel_y * 2;

        Address tile_line_data_start_address = tile_set_address + tile_data_mem_offset + tile_data_line_offset;

        // Criar uma chave única para o cache baseada no endereço da linha do tile
        uint cache_key = tile_line_data_start_address.value();
        
        // Verificar se os dados da linha já estão no cache
        TileLineData line_data;
        if (tile_line_cache.find(cache_key) == tile_line_cache.end()) {
            // Se não estiver no cache, buscar da memória e armazenar
            line_data.pixels_1 = gb.mmu.read(tile_line_data_start_address);
            line_data.pixels_2 = gb.mmu.read(tile_line_data_start_address + 1);
            tile_line_cache[cache_key] = line_data;
        } else {
            // Se estiver no cache, usar os dados armazenados
            line_data = tile_line_cache[cache_key];
        }

        GBColor pixel_color = get_pixel_from_line(line_data.pixels_1, line_data.pixels_2, tile_pixel_x);
        Color screen_color = get_color_from_palette(pixel_color, palette);

        buffer.set_pixel(screen_x, screen_y, screen_color);
    }
}

void Video::draw_window_line(uint current_line) {
    /* Note: tileset two uses signed numbering to share half the tiles with tileset 1 */
    bool use_tile_set_zero = bg_window_tile_data();
    bool use_tile_map_zero = !window_tile_map();

    Palette palette = load_palette(bg_palette);

    Address tile_set_address = use_tile_set_zero
        ? TILE_SET_ZERO_ADDRESS
        : TILE_SET_ONE_ADDRESS;

    Address tile_map_address = use_tile_map_zero
        ? TILE_MAP_ZERO_ADDRESS
        : TILE_MAP_ONE_ADDRESS;

    uint screen_y = current_line;
    uint scrolled_y = screen_y - window_y.value();

    if (scrolled_y >= GAMEBOY_HEIGHT) { return; }
    // if (!is_on_screen_y(scrolled_y)) { return; }

    // Cache para armazenar os dados de linha de cada tile
    struct TileLineData {
        u8 pixels_1;
        u8 pixels_2;
    };
    std::unordered_map<uint, TileLineData> tile_line_cache;

    for (uint screen_x = 0; screen_x < GAMEBOY_WIDTH; screen_x++) {
        /* Work out the position of the pixel in the framebuffer */
        uint scrolled_x = screen_x + window_x.value() - 7;

        /* Work out which tile of the bg_map this pixel is in, and the index of that tile
         * in the array of all tiles */
        uint tile_x = scrolled_x / TILE_WIDTH_PX;
        uint tile_y = scrolled_y / TILE_HEIGHT_PX;

        /* Work out which specific (x,y) inside that tile we're going to render */
        uint tile_pixel_x = scrolled_x % TILE_WIDTH_PX;
        uint tile_pixel_y = scrolled_y % TILE_HEIGHT_PX;

        /* Work out the address of the tile ID from the tile map */
        uint tile_index = tile_y * TILES_PER_LINE + tile_x;
        Address tile_id_address = tile_map_address + tile_index;

        /* Grab the ID of the tile we'll get data from in the tile map */
        u8 tile_id = gb.mmu.read(tile_id_address);

        /* Calculate the offset from the start of the tile data memory where
         * the data for our tile lives */
        uint tile_data_mem_offset = use_tile_set_zero
            ? tile_id * TILE_BYTES
            : (static_cast<s8>(tile_id) + 128) * TILE_BYTES;

        /* Calculate the extra offset to the data for the line of pixels we
         * are rendering from.
         * 2 (bytes per line of pixels) * y (lines) */
        uint tile_data_line_offset = tile_pixel_y * 2;

        Address tile_line_data_start_address = tile_set_address + tile_data_mem_offset + tile_data_line_offset;

        // Criar uma chave única para o cache baseada no endereço da linha do tile
        uint cache_key = tile_line_data_start_address.value();
        
        // Verificar se os dados da linha já estão no cache
        TileLineData line_data;
        if (tile_line_cache.find(cache_key) == tile_line_cache.end()) {
            // Se não estiver no cache, buscar da memória e armazenar
            line_data.pixels_1 = gb.mmu.read(tile_line_data_start_address);
            line_data.pixels_2 = gb.mmu.read(tile_line_data_start_address + 1);
            tile_line_cache[cache_key] = line_data;
        } else {
            // Se estiver no cache, usar os dados armazenados
            line_data = tile_line_cache[cache_key];
        }

        GBColor pixel_color = get_pixel_from_line(line_data.pixels_1, line_data.pixels_2, tile_pixel_x);
        Color screen_color = get_color_from_palette(pixel_color, palette);

        buffer.set_pixel(screen_x, screen_y, screen_color);
    }
}

void Video::draw_sprite(const uint sprite_n) {
    using bitwise::check_bit;

    /* Each sprite is represented by 4 bytes, or 8 bytes in 8x16 mode */
    Address offset_in_oam = sprite_n * SPRITE_BYTES;

    Address oam_start = 0xFE00 + offset_in_oam.value();
    u8 sprite_y = gb.mmu.read(oam_start);
    u8 sprite_x = gb.mmu.read(oam_start + 1);

    /* If the sprite would be drawn offscreen, don't draw it */
    if (sprite_y == 0 || sprite_y >= 160) { return; }
    if (sprite_x == 0 || sprite_x >= 168) { return; }

    uint sprite_size_multiplier = sprite_size()
        ? 2 : 1;

    /* Sprites are always taken from the first tileset */
    Address tile_set_location = TILE_SET_ZERO_ADDRESS;

    u8 pattern_n = gb.mmu.read(oam_start + 2);
    u8 sprite_attrs = gb.mmu.read(oam_start + 3);

    /* Bits 0-3 are used only for CGB */
    bool use_palette_1 = check_bit(sprite_attrs, 4);
    bool flip_x = check_bit(sprite_attrs, 5);
    bool flip_y = check_bit(sprite_attrs, 6);
    bool obj_behind_bg = check_bit(sprite_attrs, 7);

    Palette palette = use_palette_1
        ? load_palette(sprite_palette_1)
        : load_palette(sprite_palette_0);

    uint tile_offset = pattern_n * TILE_BYTES;

    Address pattern_address = tile_set_location + tile_offset;

    Tile tile(pattern_address, gb.mmu, sprite_size_multiplier);
    int start_y = sprite_y - 16;
    int start_x = sprite_x - 8;

    for (uint y = 0; y < TILE_HEIGHT_PX * sprite_size_multiplier; y++) {
        for (uint x = 0; x < TILE_WIDTH_PX; x++) {
            uint maybe_flipped_y = !flip_y ? y : (TILE_HEIGHT_PX * sprite_size_multiplier) - y - 1;
            uint maybe_flipped_x = !flip_x ? x : TILE_WIDTH_PX - x - 1;

            GBColor gb_color = tile.get_pixel(maybe_flipped_x, maybe_flipped_y);

            // Color 0 is transparent
            if (gb_color == GBColor::Color0) { continue; }

            int screen_x = start_x + x;
            int screen_y = start_y + y;

            if (!is_on_screen(screen_x, screen_y)) { continue; }

            // Armazenar a cor original do fundo antes de aplicar a paleta
            GBColor bg_gb_color = get_original_color_at(screen_x, screen_y);
            
            // Se o objeto deve ficar atrás do fundo e o fundo não é transparente (Color0)
            if (obj_behind_bg && bg_gb_color != GBColor::Color0) { continue; }

            Color screen_color = get_color_from_palette(gb_color, palette);
            buffer.set_pixel(screen_x, screen_y, screen_color);
        }
    }
}

auto Video::get_pixel_from_line(u8 byte1, u8 byte2, u8 pixel_index) -> GBColor {
    using bitwise::bit_value;

    u8 color_u8 = static_cast<u8>((bit_value(byte2, 7-pixel_index) << 1) | bit_value(byte1, 7-pixel_index));
    return get_color(color_u8);
}

auto Video::is_on_screen_x(u8 x) -> bool { return x < GAMEBOY_WIDTH; }

auto Video::is_on_screen_y(u8 y) -> bool { return y < GAMEBOY_HEIGHT; }

auto Video::is_on_screen(u8 x, u8 y) -> bool { return is_on_screen_x(x) && is_on_screen_y(y); }

// Novo método para armazenar a cor original (antes da paleta) de cada pixel
auto Video::get_original_color_at(u8 x, u8 y) -> GBColor {
    // Implementação simplificada - em uma implementação completa, 
    // seria necessário armazenar as cores originais em um buffer separado
    // Esta é uma aproximação baseada na cor atual
    Color current_color = buffer.get_pixel(x, y);
    
    if (current_color == Color::White) return GBColor::Color0;
    if (current_color == Color::LightGray) return GBColor::Color1;
    if (current_color == Color::DarkGray) return GBColor::Color2;
    if (current_color == Color::Black) return GBColor::Color3;
    
    // Fallback
    return GBColor::Color0;
}

auto Video::load_palette(ByteRegister& palette_register) -> Palette {
    using bitwise::compose_bits;
    using bitwise::bit_value;

    // Implementação mais eficiente usando bitmasking
    u8 palette_value = palette_register.value();
    u8 color0 = palette_value & 0x03;
    u8 color1 = (palette_value >> 2) & 0x03;
    u8 color2 = (palette_value >> 4) & 0x03;
    u8 color3 = (palette_value >> 6) & 0x03;

    Color real_color_0 = get_real_color(color0);
    Color real_color_1 = get_real_color(color1);
    Color real_color_2 = get_real_color(color2);
    Color real_color_3 = get_real_color(color3);

    return { real_color_0, real_color_1, real_color_2, real_color_3 };
}

auto Video::get_color_from_palette(GBColor color, const Palette& palette) -> Color {
    switch (color) {
        case GBColor::Color0: return palette.color0;
        case GBColor::Color1: return palette.color1;
        case GBColor::Color2: return palette.color2;
        case GBColor::Color3: return palette.color3;
    }
}


auto Video::get_real_color(u8 pixel_value) -> Color {
    switch (pixel_value) {
        case 0: return Color::White;
        case 1: return Color::LightGray;
        case 2: return Color::DarkGray;
        case 3: return Color::Black;
        default:
            fatal_error("Invalid color value");
    }
}

void Video::register_vblank_callback(const vblank_callback_t& _vblank_callback) {
    vblank_callback = _vblank_callback;
}

void Video::draw() {
    vblank_callback(buffer);
}
