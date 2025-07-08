#pragma once

#include "ui/widget.hpp"
#include "ui/scrolling_text.hpp"
#include "ui/progress_box.hpp"
#include "ui/list.hpp"
#include "fs.hpp"
#include "option.hpp"
#include <span>

#include <sms.h>
#include "emu_helpers/rewind.h"

namespace sphaira::ui::menu::emu {

struct Runahead {
    uint8_t** states;
    unsigned count;
    unsigned frames;
    size_t state_size;
    bool lock_input; // if true, locks input.
    // bool lazy; // if true, uses more optimised version.
};

struct Input {
    uint16_t button;
};

// data shared between the audio thread should be copied here
// changes should be made whilst SDL_LockAudioStream is in affect.
struct AudioSharedData {
    int speed_index;
};

struct RewindBarTexture {
    int handle{};
    bool used{};
};

struct RewindBarTextures {
    RewindBarTexture textures[8]{};

    auto GetNext() -> const RewindBarTexture* {
        for (auto& e : textures) {
            if (!e.used) {
                e.used = true;
                return &e;
            }
        }
        return nullptr;
    }
};

enum EmuDisplayType {
    EmuDisplayType_NORMAL,
    EmuDisplayType_FIT,
    EmuDisplayType_FULL,
};

enum EmuScalerType {
    EmuScalerType_NEAREST,
    EmuScalerType_LINEAR,
};

enum EmuRegionType {
    EmuRegionType_NTSC,
    EmuRegionType_PAL,
};

enum EmuConsoleType {
    EmuConsoleType_EXPORT,
    EmuConsoleType_JAPANESE,
};

enum EmuSystemType {
    EmuSystemType_AUTO,
    EmuSystemType_SMS,
    EmuSystemType_GG,
    EmuSystemType_SG1000,
};

enum EmuParType {
    EmuParType_AUTO,
    EmuParType_NONE,
    EmuParType_NTSC,
    EmuParType_PAL,
    EmuParType_GG,
};

struct Menu final : Widget {
    Menu(const fs::FsPath& rom_path, bool close_on_exit = false);
    ~Menu();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;
    void OnFocusLost() override;

    auto IsMenu() const -> bool override {
        return true;
    }

    void emulator_update_texture_pixels(Menu* app, int handle, const void* pixel_buffer);
    bool rewind_push_new_frame(Menu* app);

private:
    bool CreateTextures();
    void DestroyTextures();
    void DisplayOptions();

    // sets all buttons set so that only new button presses will be registered.
    void ResetPads() {
        pad[0].buttons_old = pad[0].buttons_cur = ~0;
        pad[1].buttons_old = pad[1].buttons_cur = ~0;
    }

private:
    const fs::FsPath m_rom_path;
    const bool m_close_on_exit;

public:
    static constexpr inline auto INI_SECTION = "emu";

    option::OptionLong m_system{INI_SECTION, "system", EmuSystemType_AUTO, false};
    option::OptionLong m_region{INI_SECTION, "region", EmuRegionType_NTSC};
    option::OptionLong m_console{INI_SECTION, "console", EmuConsoleType_EXPORT};
    option::OptionBool m_load_bios{INI_SECTION, "load_bios", true};

    option::OptionBool m_frame_blending{INI_SECTION, "frame_blending", false};
    option::OptionLong m_scaler{INI_SECTION, "scaler", EmuScalerType_NEAREST};
    option::OptionLong m_display_type{INI_SECTION, "display_type", EmuDisplayType_FIT};
    option::OptionLong m_ratio{INI_SECTION, "ratio", EmuParType_AUTO};
    option::OptionBool m_overscan_fill{INI_SECTION, "overscan_fill", false};

    option::OptionLong m_runahead{INI_SECTION, "runahead", 0};
    option::OptionBool m_runahead_lazy{INI_SECTION, "runahead_lazy", true};

    option::OptionBool m_savestate_on_exit{INI_SECTION, "savestate_on_exit", false};
    option::OptionBool m_loadstate_on_start{INI_SECTION, "loadstate_on_start", false};

    int texture_current{};
    int texture_previous{};
    RewindBarTextures m_rewind_bar_textures{};

    PadState pad[2]{};
    struct AudioSharedData audio_shared_data{};

    // vars
    struct SMS_Core sms{};
    void* pixel_buffer[2]{};
    size_t pixel_buffer_size{};
    bool pixel_buffer_index{};
    bool pending_frame{};

    struct Runahead runahead{};
    struct Input inputs[2]{}; // [0] current [1 previous]
    uint32_t overscan_colour{};

    Rewind* rewind{};
    void* rewind_buffer{};
    size_t rewind_buffer_size{};

    // counts down every vblank.
    size_t rewind_counter{};
    // set to true when counter hits 0.
    bool rewind_should_push{};

    // these point to the above buffer, do not free!
    void* rewind_pixel_buffer{};
    size_t rewind_pixel_buffer_size{};
    void* rewind_state_buffer{};
    size_t rewind_state_buffer_size{};
    struct SMS_StateConfig rewind_state_config{};

    // allocated sample buffer for audio callbacks.
    int16_t* sample_data{};

    // config
    // size of the emulator.
    float emu_w{};
    float emu_h{};
    // sizes of the emulated screen.
    float screen_w{};
    float screen_h{};

    // how often to save a new frame.
    int rewind_keyframe_interval{};
    // how many seconds of frames to store.
    int rewind_num_seconds{};

    int speed_index{};
    bool paused{};
    bool focus{};
    bool quit{};
};

} // namespace sphaira::ui::menu::emu
