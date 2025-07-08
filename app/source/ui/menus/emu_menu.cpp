#include "ui/menus/emu_menu.hpp"

#include "ui/sidebar.hpp"
#include "ui/popup_list.hpp"
#include "ui/option_box.hpp"
#include "ui/error_box.hpp"
#include "ui/nvg_util.hpp"

#include "app.hpp"
#include "log.hpp"
#include "defines.hpp"
#include "i18n.hpp"

#include "emu_helpers/rewind_bar.hpp"

#include <cstring>
#include <math.h>
#include <stdlib.h>
#include <mgb.h>
#include <lz4.h>
#include <minIni.h>

#if 0
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
// #define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#endif

namespace sphaira::ui::menu::emu {
namespace {

struct KeyMap {
    u64 key;
    enum SMS_Button button;
};

struct NamedEnum {
    u32 id;
    const char* name;
};

static const struct NamedEnum CONFIG_SCALER[] = {
    { EmuScalerType_NEAREST, "Nearest" },
    { EmuScalerType_LINEAR, "Linear" },
};

static const struct NamedEnum CONFIG_ZOOM[] = {
    { EmuDisplayType_NORMAL, "Normal" },
    { EmuDisplayType_FIT, "Fit" },
    { EmuDisplayType_FULL, "Full" },
};

static const struct NamedEnum CONFIG_REGION[] = {
    { EmuRegionType_NTSC, "NTSC" },
    { EmuRegionType_PAL, "PAL" },
};

static const struct NamedEnum CONFIG_CONSOLE[] = {
    { EmuConsoleType_EXPORT, "Export" },
    { EmuConsoleType_JAPANESE, "Japanese" },
};

static const struct NamedEnum CONFIG_SYSTEM[] = {
    { EmuSystemType_AUTO, "Auto" },
    { EmuSystemType_SMS, "Master System" },
    { EmuSystemType_GG, "Game Gear" },
    { EmuSystemType_SG1000, "SG-1000" },
};

static const struct NamedEnum CONFIG_PAR[] = {
    { EmuParType_AUTO, "Auto" },
    { EmuParType_NONE, "None" },
    { EmuParType_NTSC, "NTSC" },
    { EmuParType_PAL, "PAL" },
    { EmuParType_GG, "Game Gear" },
};

static const struct KeyMap KEY_MAP[2][7] = {
    {
        { HidNpadButton_B, SMS_Button_JOY1_A },
        { HidNpadButton_A, SMS_Button_JOY1_B },
        { HidNpadButton_Plus, SMS_Button_PAUSE },

        { HidNpadButton_Up | HidNpadButton_StickLUp, SMS_Button_JOY1_UP },
        { HidNpadButton_Left | HidNpadButton_StickLLeft, SMS_Button_JOY1_LEFT },
        { HidNpadButton_Down | HidNpadButton_StickLDown, SMS_Button_JOY1_DOWN },
        { HidNpadButton_Right | HidNpadButton_StickLRight, SMS_Button_JOY1_RIGHT },
    },
    {
        { HidNpadButton_B, SMS_Button_JOY2_A },
        { HidNpadButton_A, SMS_Button_JOY2_B },
        { HidNpadButton_Plus, SMS_Button_PAUSE },

        { HidNpadButton_Up | HidNpadButton_StickLUp, SMS_Button_JOY2_UP },
        { HidNpadButton_Left | HidNpadButton_StickLLeft, SMS_Button_JOY2_LEFT },
        { HidNpadButton_Down | HidNpadButton_StickLDown, SMS_Button_JOY2_DOWN },
        { HidNpadButton_Right | HidNpadButton_StickLRight, SMS_Button_JOY2_RIGHT },
    }
};

#define AUDIO_ENTRIES 6
#define SAMPLE_FREQ 48000
#define SAMPLE_COUNT (SAMPLE_FREQ / 10 * 2)

AudioOutBuffer audio_buffers[AUDIO_ENTRIES]{};
bool g_audio_pending{};

struct SDL_Rect {
    int x, y, w, h;
};

static const struct SMS_StateConfig RUNAHEAD_STATE_CONFIG = {
    .fast = true,
    .include_psg_blip = true,
};

static void on_rewind_toggle(Menu* app);
static void on_speed_reset(Menu* app);

static void on_set_pause(Menu* app, bool enable);
static void on_set_rewind(Menu* app, bool enable);
static void on_set_speed(Menu* app, int speed);

static void on_update_sound_playback_state(Menu* app);
static void on_speed_change(Menu* app);
static bool should_emu_run(const Menu* app);

static void runahead_init(Menu* app, unsigned frames);
static void runahead_exit(Menu* app);
static bool runahead_is_enabled(const Menu* app);
static void runahead_run_frame(Menu* app, double delta);

enum { SPEED_DEFAULT_INDEX = 3 };

static const float SPEED_TABLE[] = {
    0.25, 0.50, 0.75,
    1.00, // default.
    1.25, 1.50, 2.00, 3.00, 4.00,
};

static const float RATIO_TABLE[] = {
    [EmuParType_AUTO] = 0.0,
    [EmuParType_NONE] = 1.0,
    [EmuParType_NTSC] = 8.0 / 7.0,
    [EmuParType_PAL] = 2950000.0 / 2128137.0,
    [EmuParType_GG] = 4.0 / 3.0,
};

const char* BIOS_PATH = "/switch/TotalSMS/bios.bin";

enum {
    SMS_BPP = 2,
    GG_BPP = 4,
};

static uint32_t sms_converted_palette[1 << SMS_BPP * 3];
static uint32_t gg_converted_palette[1 << GG_BPP * 3];
static uint32_t sg_converted_palette[1 << 4];

Result audio_init(Menu* app) {
    R_TRY(audoutInitialize());
    audoutStartAudioOut();

    alignas(0x1000) static s16 AUDIO_BUFFER[AUDIO_ENTRIES][SAMPLE_COUNT]{};
    static_assert(std::size(AUDIO_BUFFER) == std::size(audio_buffers));

    std::memset(AUDIO_BUFFER, 0, sizeof(AUDIO_BUFFER));
    std::memset(audio_buffers, 0, sizeof(audio_buffers));

    for (size_t i = 0; i < std::size(AUDIO_BUFFER); i++) {
        auto& buf_in = AUDIO_BUFFER[i];
        auto& buf_out = audio_buffers[i];

        buf_out.buffer = buf_in;
        buf_out.buffer_size = sizeof(buf_in);
    }

    R_SUCCEED();
}

static void input_set(Menu* app, bool down, uint16_t value) {
    if (!should_emu_run(app)) {
        return;
    }

    if (down) {
        app->inputs[0].button |= value;
    } else {
        app->inputs[0].button &= ~value;
    }
}

static bool input_is_dirty(const Menu* app) {
    return app->inputs[0].button != app->inputs[1].button;
}

static void input_apply(Menu* app) {
    if (!should_emu_run(app)) {
        return;
    }

    SMS_set_buttons(&app->sms, app->inputs[0].button, true);
    SMS_set_buttons(&app->sms, ~app->inputs[0].button, false);
    app->inputs[1] = app->inputs[0];
}

static size_t compressor_size_lz4(size_t src_size) {
    return LZ4_compressBound(src_size);
}

static size_t compressor_lz4(const void* src_data, void* dst_data, size_t src_size, size_t dst_size, bool inflate_mode) {
    int result;

    if (inflate_mode) {
        result = LZ4_decompress_safe((const char*)src_data, (char*)dst_data, src_size, dst_size);
    } else {
        result = LZ4_compress_default((const char*)src_data, (char*)dst_data, src_size, dst_size);
    }

    if (result <= 0) {
        return 0;
    }

    return result;
}

static bool update_screen_and_renderer_size(Menu* app) {
    app->emu_w = app->screen_w = SMS_SCREEN_WIDTH;
    app->emu_h = app->screen_h = SMS_SCREEN_HEIGHT;

    double ratio;
    if (app->m_ratio.Get() == EmuParType_AUTO) {
        if (SMS_is_system_type_gg(&app->sms)) {
            ratio = RATIO_TABLE[EmuParType_GG];
        } else {
            if (app->m_region.Get() == EmuRegionType_NTSC) {
                ratio = RATIO_TABLE[EmuParType_NTSC];
            } else {
                ratio = RATIO_TABLE[EmuParType_PAL];
            }
        }
    } else {
        ratio = RATIO_TABLE[app->m_ratio.Get()];
    }

    if (mgb_has_rom() && SMS_is_system_type_gg(&app->sms)) {
        app->emu_w = app->screen_w = 160;
        app->emu_h = app->screen_h = 144;
        ratio = 4.0 / 3.0;
    }

    app->screen_w *= ratio;

    return true;
}

static void on_rom_load(Menu* app) {
    rewind_bar_set_open(app, false);

    // free rewind and rewind buffer.
    if (app->rewind) {
        rewind_close(app->rewind);
    }

    if (app->rewind_buffer) {
        free(app->rewind_buffer);
        app->rewind_pixel_buffer = NULL;
        app->rewind_state_buffer = NULL;
    }

    // reset rewind state and create savestate config.
    app->rewind_counter = 0;
    app->rewind_should_push = false;
    app->rewind_state_config.include_psg_blip = true;
    app->rewind_state_config.fast = false;

    // allocate new rewind buffer.
    app->rewind_buffer_size = app->pixel_buffer_size;
    app->rewind_buffer_size += SMS_get_state_size(&app->sms, &app->rewind_state_config);
    app->rewind_buffer = malloc(app->rewind_buffer_size);

    // setup pointers.
    app->rewind_pixel_buffer = app->rewind_buffer;
    app->rewind_pixel_buffer_size = app->pixel_buffer_size;
    app->rewind_state_buffer = (uint8_t*)app->rewind_buffer + app->rewind_pixel_buffer_size;
    app->rewind_state_buffer_size = app->rewind_buffer_size - app->rewind_pixel_buffer_size;

    // finally, create rewind.
    const size_t count = 60 * app->rewind_num_seconds / app->rewind_keyframe_interval;
    app->rewind = rewind_init(app->rewind_buffer_size, count, compressor_lz4, compressor_size_lz4);

    // we don't want to play left over audio data from the previous game.
    audoutFlushAudioOutBuffers(NULL);

    // clear the frame buffers.
    memset(app->pixel_buffer[0], 0, app->pixel_buffer_size);
    memset(app->pixel_buffer[1], 0, app->pixel_buffer_size);

    // resume emulator when a rom is loaded.
    on_set_pause(app, false);

    // update screen and renderer size as the rom type may have changed.
    update_screen_and_renderer_size(app);
}

static void mgb_on_file_callback(void* user, const char* file_name, enum CallbackType type, bool result) {
    Menu* app = (Menu*)user;

    switch (type) {
        case CallbackType_LOAD_ROM:
            if (result) {
                on_rom_load(app);
            } else {
                // App::Notify("Failed to load rom");
            }
            break;

        case CallbackType_LOAD_BIOS:
            if (result) {
            } else {
                // App::Notify("Failed to load bios");
            }
            break;

        case CallbackType_LOAD_SAVE:
            if (result) {
                // App::Notify("Loaded Save");
            } else {
                // App::Notify("Failed to load save");
            }
            break;

        case CallbackType_LOAD_STATE:
            if (result) {
                on_set_rewind(app, false);
                // App::Notify("Loaded State");
            } else {
                // App::Notify("Failed to load state");
            }
            break;

        case CallbackType_SAVE_SAVE:
            if (result) {
            } else {
                App::Notify("Failed to save save file");
            }
            break;

        case CallbackType_SAVE_STATE:
            if (result) {
                // App::Notify("Saved State");
            } else {
                App::Notify("Failed to save state");
            }
            break;

        case CallbackType_PATCH_ROM:
            if (result) {
                // App::Notify("Patched Rom");
            } else {
                App::Notify("Failed to patch rom");
            }
            break;
    }
}

static void* mgb_on_convert_pixels_to_png_format(void* user, int* out_w, int* out_h, int* out_channels)
{
    Menu* app = (Menu*)user;
    SDL_Rect rect;
    SMS_get_pixel_region(&app->sms, &rect.x, &rect.y, &rect.w, &rect.h);

    const int src_bpp = sizeof(u32);
    const int src_yoff = rect.y * SMS_SCREEN_WIDTH * src_bpp + rect.x * src_bpp;

    *out_w = rect.w;
    *out_h = rect.h;
    *out_channels = 3;
    uint8_t* dst = (uint8_t*)malloc((*out_w) * (*out_h) * (*out_channels));
    if (!dst)
    {
        return NULL;
    }

    auto in = (const u32*)((const u8*)app->pixel_buffer[app->pixel_buffer_index] + src_yoff);
    for (int y = 0; y < rect.h; y++) {
        auto in_ptr = in + (y + rect.y) * SMS_SCREEN_WIDTH + rect.x;
        auto out_ptr = dst + y * rect.w * 3;
        for (int x = 0; x < rect.w; x++) {
            out_ptr[x * 3 + 0] = ((in_ptr[x] >> 0x0) & 0xFF); // R
            out_ptr[x * 3 + 1] = ((in_ptr[x] >> 0x8) & 0xFF); // G
            out_ptr[x * 3 + 2] = ((in_ptr[x] >> 0x10) & 0xFF); // B
        }
    }

    return dst;
}

// generates full colour range based on the bit depth.
// ie, for SMS 2 bit depth will produce 0, 85, 170, 255.
static void generate_palette(Menu* app, uint32_t* palette, uint8_t bpp) {
    const unsigned max_rgb = 1 << bpp;
    const unsigned bit_mask = (1 << bpp) - 1;

    for (unsigned r = 0; r < max_rgb; r++) {
        for (unsigned g = 0; g < max_rgb; g++){
            for (unsigned b = 0; b < max_rgb; b++) {
                const unsigned index = (r << bpp * 0) | (g << bpp * 1) | (b << bpp * 2);

                const uint8_t rout = r * 255U / bit_mask;
                const uint8_t gout = g * 255U / bit_mask;
                const uint8_t bout = b * 255U / bit_mask;

                palette[index] = RGBA8_MAXALPHA(rout ,gout ,bout);
            }
        }
    }
}

static void generate_sg_palette(Menu* app, uint32_t* palette) {
    // https://www.smspower.org/uploads/Development/sg1000.txt
    struct Colour { u32 r,g,b,a; };
    static const Colour SG_COLOUR_TABLE[] = {
        {0x00, 0x00, 0x00, 0x00}, // 0: transparent
        {0x00, 0x00, 0x00, 0xFF}, // 1: black
        {0x20, 0xC0, 0x20, 0xFF}, // 2: green
        {0x60, 0xE0, 0x60, 0xFF}, // 3: bright green
        {0x20, 0x20, 0xE0, 0xFF}, // 4: blue
        {0x40, 0x60, 0xE0, 0xFF}, // 5: bright blue
        {0xA0, 0x20, 0x20, 0xFF}, // 6: dark red
        {0x40, 0xC0, 0xE0, 0xFF}, // 7: cyan (?)
        {0xE0, 0x20, 0x20, 0xFF}, // 8: red
        {0xE0, 0x60, 0x60, 0xFF}, // 9: bright red
        {0xC0, 0xC0, 0x20, 0xFF}, // 10: yellow
        {0xC0, 0xC0, 0x80, 0xFF}, // 11: bright yellow
        {0x20, 0x80, 0x20, 0xFF}, // 12: dark green
        {0xC0, 0x40, 0xA0, 0xFF}, // 13: pink
        {0xA0, 0xA0, 0xA0, 0xFF}, // 14: gray
        {0xE0, 0xE0, 0xE0, 0xFF}, // 15: white
    };

    for (size_t i = 0; i < std::size(SG_COLOUR_TABLE); i++) {
        const Colour c = SG_COLOUR_TABLE[i];
        palette[i] = RGBA8(c.r, c.g, c.b, c.a);
    }
}

static uint32_t core_colour_callback(void* user, uint8_t r, uint8_t g, uint8_t b) {
    Menu* app = (Menu*)user;
    if (SMS_is_system_type_gg(&app->sms)) {
        return gg_converted_palette[r << 0 | g << 4 | b << 8];
    }
    else {
        return sms_converted_palette[r << 0 | g << 2 | b << 4];
    }
}

static void core_vblank_callback(void* user, uint32_t overscan_colour) {
    Menu* app = (Menu*)user;

    g_audio_pending = false;

    if (!app->rewind_counter) {
        app->rewind_counter = app->rewind_keyframe_interval;
        app->rewind_should_push = true;
    } else {
        app->rewind_counter--;
    }

    if (SMS_get_skip_frame(&app->sms)) {
        return;
    }

    app->pending_frame = true;
    app->overscan_colour = overscan_colour;
    app->pixel_buffer_index ^= 1;
    SMS_set_pixels(&app->sms, app->pixel_buffer[app->pixel_buffer_index ^ 1], SMS_SCREEN_WIDTH, sizeof(u32));
}

static void core_audio_callback(void* user, int16_t* samples, uint32_t size) {
    // Menu* app = (Menu*)user;

    const auto push_buf = [&](AudioOutBuffer& buf_out) -> Result {
        buf_out.data_size = size * sizeof(*samples);
        memcpy(buf_out.buffer, samples, buf_out.data_size);

        armDCacheFlush(buf_out.buffer, buf_out.data_size);
        return audoutAppendAudioOutBuffer(&buf_out);
    };

    for (auto& buf_out : audio_buffers) {
        bool contains;
        if (R_SUCCEEDED(audoutContainsAudioOutBuffer(&buf_out, &contains)) && !contains) {
            log_write("[audio] found empty buffer\n");
            push_buf(buf_out);
            return;
        }
    }

    AudioOutBuffer* released{};
    u32 ReleasedBuffersCount{};
    if (R_FAILED(audoutGetReleasedAudioOutBuffer(&released, &ReleasedBuffersCount))) {
        log_write("[audio] failed to get released\n");
        return;
    }

    if (!ReleasedBuffersCount) {
        log_write("[audio] dropping samples...\n");
        return;
    }

    push_buf(released[0]);
}

static void sdl_poll_emu_inputs(Menu* app) {
    for (int i = 0; i < std::size(KEY_MAP); i++) {
        const auto buttons_down = padGetButtonsDown(&app->pad[i]);
        const auto buttons_up = padGetButtonsUp(&app->pad[i]);

        for (auto& p : KEY_MAP[i]) {
            if ((buttons_down & p.key) || (buttons_up & p.key)) {
                const bool down = buttons_down & p.key;
                input_set(app, down, p.button);
            }
        }
    }
}

static void core_input_callback(void* user, int port) {
    Menu* app = (Menu*)user;

    // disabled whilst input is locked, used for catching up frames in runahead.
    if (app->runahead.lock_input) {
        return;
    }

    // https://github.com/higan-emu/emulation-articles/tree/master/input/latency
    static uint64_t last_poll_time = 0;
    const uint64_t new_poll_time = armTicksToNs(armGetSystemTick()) / 1000 / 1000;
    const uint64_t poll_max = 5; // 5ms
    if (new_poll_time - last_poll_time < poll_max) {
        return;
    }

    last_poll_time = new_poll_time;
    padUpdate(&app->pad[0]);
    padUpdate(&app->pad[1]);

    sdl_poll_emu_inputs(app);

    if (input_is_dirty(app)) {
        input_apply(app);
    }
}

static void on_rewind_toggle(Menu* app) {
    on_set_rewind(app, rewind_bar_enabled() ^ 1);
}

static void on_speed_reset(Menu* app) {
    on_set_speed(app, SPEED_DEFAULT_INDEX);
}

static void on_set_pause(Menu* app, bool enable) {
    if (enable != app->paused) {
        if (enable) {
            App::Notify("Paused");
        } else {
            App::Notify("Resumed");
        }

        app->paused = enable;
        on_update_sound_playback_state(app);
    }
}

static void on_set_rewind(Menu* app, bool enable) {
    if (enable != rewind_bar_enabled()) {
        rewind_bar_set_open(app, enable);
        on_update_sound_playback_state(app);
    }
}

static void on_set_speed(Menu* app, int speed) {
    speed = std::clamp<int>(speed, 0, std::size(SPEED_TABLE) - 1);
    if (app->speed_index != speed) {
        app->speed_index = speed;
        on_speed_change(app);
    }
}

static void on_update_sound_playback_state(Menu* app) {
    if (should_emu_run(app)) {
        audoutStartAudioOut();
    } else {
        audoutStopAudioOut();
    }
}

static void on_speed_change(Menu* app) {
    const float speed = SPEED_TABLE[app->speed_index];
    char buf[32];
    snprintf(buf, sizeof(buf), "Speed %.2fx", speed);
    App::Notify(buf);

    // clear audio as we may go 8x -> 1x which would fill the buffers.
    // todo: resample audio for non 1x speed.
    audoutFlushAudioOutBuffers(NULL);
    app->audio_shared_data.speed_index = app->speed_index;
}

static bool should_emu_run(const Menu* app) {
    return mgb_has_rom() && !app->paused && app->focus && !rewind_bar_enabled();
}

static void emulator_render(Menu* app) {
    if (!mgb_has_rom() || rewind_bar_enabled()) {
        return;
    }

    // update texture pixels if we have a new frame pending.
    if (app->pending_frame) {
        app->emulator_update_texture_pixels(app, app->texture_current, app->pixel_buffer[app->pixel_buffer_index]);
    }

    // get the output size of the sms
    SDL_Rect rect;
    SMS_get_pixel_region(&app->sms, &rect.x, &rect.y, &rect.w, &rect.h);

    // center the image (aka, don't stretch to fill screen)
    Vec4 dst_rect;
    if (app->m_display_type.Get() == EmuDisplayType_FULL) {
        dst_rect.w = SCREEN_WIDTH;
        dst_rect.h = SCREEN_HEIGHT;
    } else {
        float scale = std::min(SCREEN_WIDTH / app->screen_w, SCREEN_HEIGHT / app->screen_h);
        if (app->m_display_type.Get() == EmuDisplayType_NORMAL) {
            scale = floor(scale);
        }

        dst_rect.w = app->screen_w * scale;
        dst_rect.h = app->screen_h * scale;

        // log_write("scale: %.2f sw: %.2f sh: %.2f w: %.2f h: %.2f\n", scale, app->screen_w, app->screen_h, dst_rect.w, dst_rect.h);
    }

    // overscale the gg games due to the image being the size of sms.
    const float internal_scale_w = (float)SMS_SCREEN_WIDTH / app->emu_w;
    const float internal_scale_h = (float)SMS_SCREEN_HEIGHT / app->emu_h;

    dst_rect.w *= internal_scale_w;
    dst_rect.h *= internal_scale_h;
    dst_rect.x = (SCREEN_WIDTH - dst_rect.w) / 2;
    dst_rect.y = (SCREEN_HEIGHT - dst_rect.h) / 2;

    // get the output size of the sms
    // const SDL_FRect src_rect = {.x = rect.x, .y = rect.y, .w = rect.w, .h = rect.h};

    if (app->m_frame_blending.Get() && !rewind_bar_enabled()) {
        // render new frame at 100% alpha with the previous frame as 40%
        gfx::drawImage(App::GetVg(), dst_rect, app->texture_current);
        gfx::drawImage(App::GetVg(), dst_rect, app->texture_previous, 0.0, 0.4);

        std::swap(app->texture_current, app->texture_previous);
    } else {
        gfx::drawImage(App::GetVg(), dst_rect, app->texture_current);
    }
}

static void emulator_run(Menu* app, double cycles, bool skip_audio, bool skip_video, bool lock_input) {
    app->runahead.lock_input = lock_input;
    SMS_skip_audio(&app->sms, skip_audio);
    SMS_skip_frame(&app->sms, skip_video);
    SMS_run(&app->sms, cycles * SPEED_TABLE[app->speed_index]);
}

static void runahead_init(Menu* app, unsigned frames) {
    if (!frames) {
        runahead_exit(app);
        return;
    }

    app->runahead.frames = frames;
    app->runahead.count = 0;
    app->runahead.states = (u8**)malloc(frames * sizeof(*app->runahead.states));
    app->runahead.state_size = SMS_get_state_size(&app->sms, &RUNAHEAD_STATE_CONFIG);
    for (unsigned i = 0; i < app->runahead.frames; i++) {
        app->runahead.states[i] = (u8*)malloc(app->runahead.state_size);
    }
}

static void runahead_exit(Menu* app) {
    if (app->runahead.states) {
        for (unsigned i = 0; i < app->runahead.frames; i++) {
            free(app->runahead.states[i]);
        }

        free(app->runahead.states);
    }

    memset(&app->runahead, 0, sizeof(app->runahead));
}

static bool runahead_is_enabled(const Menu* app) {
    return app->runahead.frames > 0 && app->speed_index == SPEED_DEFAULT_INDEX;
}

// clears frame count so that all new frames must be generated.
// this should be called on input change, loadstate and loadrom.
static void runahead_clear_frames(Menu* app) {
    app->runahead.count = 0;
}

// run the emulate for a single frame.
// will exit early if the emulate is paused or no rom etc.
// if runahead is disabled, then it will run a frame as normal.
static void runahead_run_frame(Menu* app, double delta) {
    // don't run if a rom isn't loaded, paused or lost focus.
    if (!should_emu_run(app)) {
        return;
    }

    // just in case something sends the main thread to sleep
    // ie, filedialog, then cap the max delta to something reasonable!
    // maybe keep track of deltas here to get an average?
    // delta = std::min(delta, 1.333333);
    delta = std::min(delta, 3.0);
    const double cycles = (double)SMS_cycles_per_frame(&app->sms) * delta;
    // const size_t cycles = SMS_cycles_per_frame(&app->sms);

    if (!runahead_is_enabled(app)) {
        // run frame as normal.
        emulator_run(app, cycles, false, false, false);
        // clear frames here as speed change may have disable runahead.
        runahead_clear_frames(app);
    } else {
        padUpdate(&app->pad[0]);
        padUpdate(&app->pad[1]);
        sdl_poll_emu_inputs(app);

        if (app->m_runahead_lazy.Get()) {
            if (input_is_dirty(app)) {
                // only loadstate if it's valid
                if (app->runahead.count) {
                    SMS_loadstate(&app->sms, app->runahead.states[0], app->runahead.state_size, &RUNAHEAD_STATE_CONFIG);
                }

                input_apply(app);
                runahead_clear_frames(app);
            }

            // emulate ahead, fill up state array
            if (app->runahead.count < app->runahead.frames) {
                while (app->runahead.count < app->runahead.frames) {
                    emulator_run(app, cycles, true, true, true);
                    SMS_savestate(&app->sms, app->runahead.states[app->runahead.count], app->runahead.state_size, &RUNAHEAD_STATE_CONFIG);
                    app->runahead.count++;
                }
            } else {
                // otherwise, move state array down, over-writting oldest state
                for (unsigned i = 0; i < app->runahead.count - 1; i++) {
                    uint8_t* temp = app->runahead.states[i];
                    app->runahead.states[i] = app->runahead.states[i + 1];
                    app->runahead.states[i + 1] = temp;
                }

                // add new state
                SMS_savestate(&app->sms, app->runahead.states[app->runahead.count - 1], app->runahead.state_size, &RUNAHEAD_STATE_CONFIG);
            }

            emulator_run(app, cycles, false, false, true);
        } else {
            emulator_run(app, cycles, true, true, false);
            SMS_savestate(&app->sms, app->runahead.states[0], app->runahead.state_size, &RUNAHEAD_STATE_CONFIG);

            for (unsigned i = 1; i < app->runahead.frames; i++) {
                emulator_run(app, cycles, true, true, true);
            }

            emulator_run(app, cycles, false, false, true);
            SMS_loadstate(&app->sms, app->runahead.states[0], app->runahead.state_size, &RUNAHEAD_STATE_CONFIG);
        }
    }
}

} // namespace

Menu::Menu(const fs::FsPath& rom_path, bool close_on_exit) : m_rom_path{rom_path}, m_close_on_exit{close_on_exit} {
    auto app = this;

    padInitialize(&pad[0], HidNpadIdType_No1, HidNpadIdType_Handheld);
    padInitialize(&pad[1], HidNpadIdType_No2);

    static const auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto app = static_cast<Menu*>(UserData);

        if (!std::strcmp(Section, INI_SECTION)) {
            if (app->m_system.LoadFrom(Key, Value)) {}
            else if (app->m_region.LoadFrom(Key, Value)) {}
            else if (app->m_console.LoadFrom(Key, Value)) {}
            else if (app->m_load_bios.LoadFrom(Key, Value)) {}
            else if (app->m_frame_blending.LoadFrom(Key, Value)) {}
            else if (app->m_scaler.LoadFrom(Key, Value)) {}
            else if (app->m_display_type.LoadFrom(Key, Value)) {}
            else if (app->m_ratio.LoadFrom(Key, Value)) {}
            else if (app->m_overscan_fill.LoadFrom(Key, Value)) {}
            else if (app->m_runahead.LoadFrom(Key, Value)) {}
            else if (app->m_runahead_lazy.LoadFrom(Key, Value)) {}
            else if (app->m_savestate_on_exit.LoadFrom(Key, Value)) {}
            else if (app->m_loadstate_on_start.LoadFrom(Key, Value)) {}
        }

        return 1;
    };

    // load all configs ahead of time, as this is actually faster than
    // loading each config one by one as it avoids re-opening the file multiple times.
    ini_browse(cb, this, App::CONFIG_PATH);

    // used for testing.
    #if 0
    SetAction(Button::L, Action{[this](){
    }});
    #endif

    app->quit = false;
    app->speed_index = SPEED_DEFAULT_INDEX;
    app->audio_shared_data.speed_index = app->speed_index;
    app->rewind_keyframe_interval = 90; // every 1.5s
    app->rewind_num_seconds = 60 * 30; // 30 minutes

    log_write("creating display now\n");

    if (!update_screen_and_renderer_size(app)) {
        SetPop();
        return;
    }

    app->pixel_buffer_size = sizeof(u32) * SMS_SCREEN_WIDTH * SMS_SCREEN_HEIGHT;
    app->pixel_buffer[0] = calloc(1, app->pixel_buffer_size);
    app->pixel_buffer[1] = calloc(1, app->pixel_buffer_size);
    if (!app->pixel_buffer[0] || !app->pixel_buffer[1]) {
        SetPop();
        return;
    }

    if (!CreateTextures()) {
        SetPop();
        return;
    }

    if (R_FAILED(audio_init(app))) {
        log_write("failed audio init\n");
        SetPop();
        return;
    }

    const size_t sample_data_size = SAMPLE_COUNT;
    app->sample_data = (int16_t*)malloc(sample_data_size * sizeof(*app->sample_data));
    if (!app->sample_data) {
        SetPop();
        return;
    }

    generate_palette(app, sms_converted_palette, SMS_BPP);
    generate_palette(app, gg_converted_palette, GG_BPP);
    generate_sg_palette(app, sg_converted_palette);

    SMS_init(&app->sms);
    SMS_set_userdata(&app->sms, app);
    SMS_set_colour_callback(&app->sms, core_colour_callback);
    SMS_set_vblank_callback(&app->sms, core_vblank_callback);
    SMS_set_apu_callback(&app->sms, core_audio_callback, app->sample_data, sample_data_size, SAMPLE_FREQ);
    SMS_set_input_callback(&app->sms, core_input_callback);
    SMS_set_pixels(&app->sms, app->pixel_buffer[app->pixel_buffer_index ^ 1], SMS_SCREEN_WIDTH, sizeof(u32));
    SMS_set_builtin_palette(&app->sms, sg_converted_palette);

    mgb_init(&app->sms);
    mgb_set_userdata(app);
    mgb_set_on_file_callback(mgb_on_file_callback);
    mgb_set_on_convert_pixels_to_png_format(mgb_on_convert_pixels_to_png_format);

    // try and load bios.
    if (m_load_bios.Get()) {
        log_write("loading bios\n");
        if (!mgb_load_bios_file(BIOS_PATH)) {
            log_write("failed to load bios\n");
        }
    }

    mgb_set_region(m_region.Get());
    mgb_set_console(m_console.Get());

    if (!mgb_load_rom_file(m_rom_path)) {
        SetPop();
        return;
    }

    if (m_loadstate_on_start.Get()) {
        mgb_load_state_file(NULL);
    }

    runahead_init(app, m_runahead.Get());
    rewind_bar_init();
    ResetPads();
    // on_set_speed(app, 4);
}

Menu::~Menu() {
    auto app = this;

    audoutStopAudioOut();
    audoutExit();

    if (mgb_has_rom()) {
        if (m_savestate_on_exit.Get()) {
            mgb_save_state_file(NULL);
        }
    }

    runahead_exit(app);
    mgb_exit();
    SMS_quit(&app->sms);

    DestroyTextures();

    #if 0
    stbi_write_png("/background_256x192.png", SMS_SCREEN_WIDTH, SMS_SCREEN_HEIGHT, 4, app->pixel_buffer[app->pixel_buffer_index], SMS_SCREEN_WIDTH * 4);
    #endif

    if (app->rewind) {
        rewind_close(app->rewind);
    }
    if (app->rewind_buffer) {
        free(app->rewind_buffer);
    }
    if (app->sample_data) {
        free(app->sample_data);
    }
    if (app->pixel_buffer[0]) {
        free(app->pixel_buffer[0]);
    }
    if (app->pixel_buffer[1]) {
        free(app->pixel_buffer[1]);
    }
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);
    auto app = this;

    static TimeStamp pause_ts;
    static bool pending_pause{};

    if (controller->GotDown(Button::R2 | Button::SR_ANY)) {
        pause_ts.Update();
        pending_pause = true;
    } else if (pending_pause) {
        if (controller->GotHeld(Button::R2 | Button::SR_ANY)) {
            if (pause_ts.GetMs() >= 400) {
                pending_pause = false;
                ResetPads();
                on_rewind_toggle(this);
            }
        } else if (controller->GotUp(Button::R2 | Button::SR_ANY)) {
            pending_pause = false;
            ResetPads();
            DisplayOptions();
        }
    }

    #if 0
    if (controller->GotDown(Button::L2 | Button::SL_ANY) | controller->GotHeld(Button::L2 | Button::SL_ANY)) {
        on_set_speed(app, SPEED_DEFAULT_INDEX + 2);
    } else if (controller->GotUp(Button::L2 | Button::SL_ANY)) {
        on_speed_reset(app);
    }
    #endif

    static u64 start = 0;
    if (start == 0) {
        start = armTicksToNs(armGetSystemTick());
    }

    const u64 now = armTicksToNs(armGetSystemTick());
    const double delta = (double)(now - start) / (double)(1e+9);
    // const double delta = 1.0 / 60.0;// (double)(now - start) / (double)(1e+9);
    start = now;

    on_update_sound_playback_state(app);

    if (should_emu_run(app)) {
        const double TARGET_FRAME_TIME = 1.0 / SMS_target_fps(&app->sms);
        runahead_run_frame(app, delta / TARGET_FRAME_TIME);

        if (app->rewind_should_push) {
            rewind_push_new_frame(app);
            app->rewind_should_push = false;
        }
    } else if (rewind_bar_enabled()) {
        if (controller->GotDown(Button::ANY_LEFT)) {
            rewind_bar_button(this, RewindBarButton_Left);
        }
        if (controller->GotDown(Button::ANY_RIGHT)) {
            rewind_bar_button(this, RewindBarButton_Right);
        }
        if (controller->GotDown(Button::B)) {
            rewind_bar_button(this, RewindBarButton_Back);
        }
        if (controller->GotDown(Button::A)) {
            rewind_bar_button(this, RewindBarButton_OK);
        }
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    auto app = this;

    // set overscan colour if enabled and the system is NOT game gear.
    NVGcolor overscan = nvgRGB(0, 0, 0);
    if (app->m_overscan_fill.Get() && mgb_has_rom() && !SMS_is_system_type_gg(&app->sms)) {
        const auto c = std::byteswap(app->overscan_colour);
        overscan = nvgRGB((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF);
    }

    gfx::drawRect(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, overscan);
    emulator_render(app);
    rewind_bar_render(vg, theme, app);

    for (auto& e : m_rewind_bar_textures.textures) {
        e.used = false;
    }
}

void Menu::OnFocusGained() {
    Widget::OnFocusGained();
    focus = true;
}

void Menu::OnFocusLost() {
    Widget::OnFocusLost();
    focus = false;
}

void Menu::emulator_update_texture_pixels(Menu* app, int handle, const void* pixel_buffer) {
    app->pending_frame = false;
    // TimeStamp ts;
    nvgUpdateImage(App::GetVg(), handle, (const u8*)pixel_buffer);
    // log_write("nvgUpdateImage(1), time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
}

bool Menu::rewind_push_new_frame(Menu* app) {
    memcpy(app->rewind_pixel_buffer, app->pixel_buffer[app->pixel_buffer_index], app->pixel_buffer_size);

    if (!SMS_savestate(&app->sms, app->rewind_state_buffer, app->rewind_state_buffer_size, &app->rewind_state_config)) {
        return false;
    }

    size_t compressed_size;
    if (!rewind_push(app->rewind, app->rewind_buffer, app->rewind_buffer_size, &compressed_size)) {
        return false;
    }

    // enable to see compression ratio.
#if 0
    const size_t count = rewind_get_count(app->rewind);
    const double total = rewind_get_allocated_size(app->rewind, false);
    log_write("old_size: %.2f KiB new_size: %.2f KiB compression %.2f%% count: %zu total: %.2f MiB avg: %.2f KiB\n", app->rewind_buffer_size / 1024.0, compressed_size / 1024.0, ((double)compressed_size / (double)app->rewind_buffer_size) * 100.0, count, total / 1024.0 / 1024.0, total / (double)count / 1024.0);
#endif

    return true;
}

bool Menu::CreateTextures() {
    auto app = this;
    auto vg = App::GetVg();
    DestroyTextures();

    TimeStamp ts;
    const auto image_flags = m_scaler.Get() == EmuScalerType_NEAREST ? NVG_IMAGE_NEAREST : 0;
    app->texture_current = nvgCreateImageRGBA(vg, SMS_SCREEN_WIDTH, SMS_SCREEN_HEIGHT, image_flags, (const u8*)app->pixel_buffer[0]);
    log_write("CreateTextures(0), time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
    app->texture_previous = nvgCreateImageRGBA(vg, SMS_SCREEN_WIDTH, SMS_SCREEN_HEIGHT, image_flags, (const u8*)app->pixel_buffer[1]);
    log_write("CreateTextures(1), time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
    if (!app->texture_current || !app->texture_previous) {
        return false;
    }

    for (auto& e : m_rewind_bar_textures.textures) {
        e.handle = nvgCreateImageRGBA(vg, SMS_SCREEN_WIDTH, SMS_SCREEN_HEIGHT, image_flags, NULL);
        log_write("\tCreateTextures(x), time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
        if (!e.handle) {
            return false;
        }
    }

    // log_write("CreateTextures(), time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());

    return true;
}

void Menu::DestroyTextures() {
    auto app = this;
    auto vg = App::GetVg();

    if (app->texture_current) {
        nvgDeleteImage(vg, app->texture_current);
        app->texture_current = 0;
    }

    if (app->texture_previous) {
        nvgDeleteImage(vg, app->texture_previous);
        app->texture_previous = 0;
    }

    for (auto& e : m_rewind_bar_textures.textures) {
        if (e.handle) {
            nvgDeleteImage(vg, e.handle);
            e.handle = 0;
        }
    }
}

void Menu::DisplayOptions() {
    auto options = std::make_unique<Sidebar>("Pause Menu"_i18n, Sidebar::Side::LEFT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryCallback>("Options"_i18n, [this](){
        auto options = std::make_unique<Sidebar>("Options"_i18n, Sidebar::Side::LEFT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        options->Add<SidebarEntryCallback>("System Options"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("System Options"_i18n, Sidebar::Side::LEFT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            SidebarEntryArray::Items region_items;
            for (auto& e : CONFIG_REGION) {
                region_items.emplace_back(i18n::get(e.name));
            }

            SidebarEntryArray::Items console_items;
            for (auto& e : CONFIG_CONSOLE) {
                console_items.emplace_back(i18n::get(e.name));
            }

            SidebarEntryArray::Items system_items;
            for (auto& e : CONFIG_SYSTEM) {
                system_items.emplace_back(i18n::get(e.name));
            }

            options->Add<SidebarEntryArray>("Region"_i18n, region_items, [this](s64& index_out){
                m_region.Set(index_out);
                update_screen_and_renderer_size(this);
            }, m_region.Get(),
                "[NTSC]: 60hz.\n"\
                "[PAL]: 50hz."_i18n
            );

            options->Add<SidebarEntryArray>("Location"_i18n, console_items, [this](s64& index_out){
                m_console.Set(index_out);
            }, m_console.Get(),
                "[Export]: Europe, Australia, USA, Brazil.\n"\
                "[Japanese]: Japan, Korea."_i18n
            );

            options->Add<SidebarEntryBool>(
                "Load Bios"_i18n, m_load_bios,
                "Load the bios located at /switch/TotalSMS/bios.bin.\n\n"\
                "A bios is not required, it is only useful to show the Sega intro when loading a game."_i18n
            );

            // stubbed as it may cause problems when loading roms invalid for that system.
            #if 0
            options->Add<SidebarEntryArray>("System"_i18n, system_items, [this](s64& index_out){
                m_system.Set(index_out);
                update_screen_and_renderer_size(this);
            }, m_system.Get(),
                "[Auto]: Set based on the file extension and rom database.\n"\
                "[Master System]: System is set to the Master System.\n"\
                "[Game Gear]: System is set to the Game Gear.\n"\
                "[SG-1000]: System is set to the SG-1000."_i18n
            );
            #endif
        }, "Change the system options."_i18n);

        options->Add<SidebarEntryCallback>("Display Options"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("Display Options"_i18n, Sidebar::Side::LEFT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            SidebarEntryArray::Items zoom_items;
            for (auto& e : CONFIG_ZOOM) {
                zoom_items.emplace_back(i18n::get(e.name));
            }

            SidebarEntryArray::Items scaler_items;
            for (auto& e : CONFIG_SCALER) {
                scaler_items.emplace_back(i18n::get(e.name));
            }

            SidebarEntryArray::Items ratio_items;
            for (auto& e : CONFIG_PAR) {
                ratio_items.emplace_back(i18n::get(e.name));
            }

            options->Add<SidebarEntryArray>("Zoom"_i18n, zoom_items, [this](s64& index_out){
                m_display_type.Set(index_out);
            }, m_display_type.Get(),
                "[Normal]: Pixel perfect intager scaling.\n"\
                "[Fit]: Fills the horizontal screen.\n"\
                "[Full]: Fills the entire screen, stretches the image."
            );

            options->Add<SidebarEntryArray>("Scaler"_i18n, scaler_items, [this](s64& index_out){
                m_scaler.Set(index_out);
                CreateTextures();
            }, m_scaler.Get(),
                "[Nearest]: Uses nearest neighbour, keeps sharp pixels.\n"\
                "[Linear]: Uses linear interpolation, smooths image."_i18n
            );

            options->Add<SidebarEntryArray>("Pixel aspect ratio (PAR)"_i18n, ratio_items, [this](s64& index_out){
                m_ratio.Set(index_out);
                update_screen_and_renderer_size(this);
            }, m_ratio.Get(),
                "[None]: Disabled (may result in narrow image).\n"\
                "[Auto]: Set based on the system type.\n"\
                "[NTSC]: PAR 8:7.\n"\
                "[PAL]: PAR 2950000:2128137.\n"\
                "[Game Gear]: PAR 4:3."_i18n
            );

            options->Add<SidebarEntryBool>(
                "Fill border with overscan"_i18n, m_overscan_fill,
                "Fills the black borders with the overscan colour."_i18n
            );

            options->Add<SidebarEntryBool>("Frame blending"_i18n, m_frame_blending, [this](bool& v_out){
                if (v_out) {
                    // copy front buffer to previous texture.
                    emulator_update_texture_pixels(this, texture_previous, pixel_buffer[pixel_buffer_index]);
                }
            }, "Blends the current and previous frame togther to emulate screen ghosting."_i18n);

        }, "Change the display options."_i18n);

        options->Add<SidebarEntryCallback>("Change controller order"_i18n, [this](){
            HidLaControllerSupportArg arg;
            hidLaCreateControllerSupportArg(&arg);
            arg.hdr.enable_take_over_connection = false;
            arg.hdr.player_count_max = 2;

            HidLaControllerSupportResultInfo info;
            hidLaShowControllerSupportForSystem(&info, &arg, false);
        }, "Change the controller order, useful for getting ready for 2 player games."_i18n);

        options->Add<SidebarEntryCallback>("Advanced Options"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("Advanced Options"_i18n, Sidebar::Side::LEFT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            options->Add<SidebarEntryBool>(
                "Create savestate on exit"_i18n, m_savestate_on_exit,
                "WARNING: The savestate created will always overwrite the previous savestate."_i18n
            );

            options->Add<SidebarEntryBool>("Load savestate on start"_i18n, m_loadstate_on_start);

            options->Add<SidebarEntryBool>(
                "Skip splash screen intro"_i18n, App::GetApp()->m_skip_splash_screen_intro,
                "Skips the Sega intro when launching the app, loading straight into the filebrowser if enabled."_i18n
            );
        });

    }, "Change the emulator options."_i18n);

    options->Add<SidebarEntryCallback>("Load Savestate"_i18n, [this](){
        auto info = mgb_load_state_info_file(NULL, true);
        if (!info) {
            App::PushErrorBox(Result_EmuLoadSaveState, "Failed to load savestate");
            return;
        }
        ON_SCOPE_EXIT(mgb_free_state_info(info));

        const time_t timestamp = info->meta.timestamp;
        const struct tm* tm = localtime(&timestamp);
        char buf[256];
        std::snprintf(buf, sizeof(buf), "Load savestate?\n\n%02u/%02u/%04u - %02u:%02u:%02u", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900, tm->tm_hour, tm->tm_min, tm->tm_sec);

        const auto image = nvgCreateImageMem(App::GetVg(), 0, info->png, info->png_size);

        App::Push<ui::OptionBox>(
            buf, "No"_i18n, "Yes"_i18n, 1, [this](auto op_index){
                if (op_index && *op_index) {
                    if (!mgb_load_state_file(NULL)) {
                        App::PushErrorBox(Result_EmuLoadSaveState, "Failed to load savestate");
                    } else {
                        App::PopToMenu();
                    }
                }
            }, image, true
        );
    }, "Loads a savestate."_i18n);

    options->Add<SidebarEntryCallback>("Create Savestate"_i18n, [this](){
        const auto func = [this](){
            if (!mgb_save_state_file(NULL)) {
                App::PushErrorBox(Result_EmuCreateSaveState, "Failed to create savestate");
            } else {
                App::PopToMenu();
            }
        };

        auto info = mgb_load_state_info_file(NULL, true);
        if (info) {
            ON_SCOPE_EXIT(mgb_free_state_info(info));

            const time_t timestamp = info->meta.timestamp;
            const struct tm* tm = localtime(&timestamp);
            char buf[256];
            std::snprintf(buf, sizeof(buf), "Overwrite savestate?\n\n%02u/%02u/%04u - %02u:%02u:%02u", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900, tm->tm_hour, tm->tm_min, tm->tm_sec);

            const auto image = nvgCreateImageMem(App::GetVg(), 0, info->png, info->png_size);

            App::Push<ui::OptionBox>(
                buf, "No"_i18n, "Yes"_i18n, 1, [this, func](auto op_index){
                    if (op_index && *op_index) {
                        func();
                    }
                }, image, true
            );
        } else {
            func();
        }
    }, "Creates a savestate."_i18n);

    options->Add<SidebarEntryCallback>("Reset Game"_i18n, [this](){
        App::Push<ui::OptionBox>(
            "Reset Game?"_i18n,
            "No"_i18n, "Yes"_i18n, 1, [this](auto op_index){
                if (op_index && *op_index) {
                    mgb_load_rom_file(m_rom_path);
                    App::PopToMenu();
                }
            }
        );
    }, "Resets the game from the beginning.\n\n"\
       "Please save before selecting this option or data will be lost."_i18n);

    options->Add<SidebarEntryCallback>("Exit Game"_i18n, [this](){
        App::Push<ui::OptionBox>(
            "Are you sure you want to exit?"_i18n,
            "No"_i18n, "Yes"_i18n, 1, [this](auto op_index){
                if (op_index && *op_index) {
                    App::PopToMenu();
                    if (m_close_on_exit) {
                        App::Exit();
                    } else {
                        this->SetPop();
                    }
                }
            }
        );
    }, "Closes the game, opens the file browser.\n\n"\
       "Please save before selecting this option or data will be lost."_i18n);
}

} // namespace sphaira::ui::menu::emu
