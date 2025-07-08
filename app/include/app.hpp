#pragma once

#include "nanovg.h"
#include "nanovg/dk_renderer.hpp"
#include "pulsar.h"
#include "ui/widget.hpp"
#include "ui/notification.hpp"
#include "option.hpp"
#include "fs.hpp"
#include "log.hpp"

#ifdef USE_NVJPG
#include <nvjpg.hpp>
#endif
#include <switch.h>
#include <vector>
#include <string>
#include <span>
#include <optional>
#include <utility>

namespace sphaira {

enum SoundEffect {
    SoundEffect_Focus,
    SoundEffect_Scroll,
    SoundEffect_Limit,
    SoundEffect_Startup,
    SoundEffect_Install,
    SoundEffect_Error,
    SoundEffect_MAX,
};

enum class LaunchType {
    Normal,
    Forwader_Unknown,
    Forwader_Sphaira,
};

struct AmsEmummcPaths {
    char file_based_path[0x80];
    char nintendo[0x80];
};

// todo: why is this global???
void DrawElement(float x, float y, float w, float h, ThemeEntryID id);
void DrawElement(const Vec4&, ThemeEntryID id);

class App {
public:
    App(int argc, char** argv);
    ~App();
    void Loop();

    static App* GetApp();

    static void Exit();
    static void ExitRestart();
    static auto GetVg() -> NVGcontext*;

    static void Push(std::unique_ptr<ui::Widget>&&);

    template<ui::DerivedFromWidget T, typename... Args>
    static void Push(Args&&... args) {
        Push(std::make_unique<T>(std::forward<Args>(args)...));
    }

    // pops all widgets above a menu
    static void PopToMenu();

    // this is thread safe
    static void Notify(std::string text, ui::NotifEntry::Side side = ui::NotifEntry::Side::RIGHT);
    static void Notify(ui::NotifEntry entry);
    static void NotifyPop(ui::NotifEntry::Side side = ui::NotifEntry::Side::RIGHT);
    static void NotifyClear(ui::NotifEntry::Side side = ui::NotifEntry::Side::RIGHT);

    // if R_FAILED(rc), pushes error box. returns rc passed in.
    static Result PushErrorBox(Result rc, const std::string& message);

    static auto GetThemeMetaList() -> std::span<ThemeMeta>;
    static void SetTheme(s64 theme_index);
    static auto GetThemeIndex() -> s64;

    // returns argv[0]
    static auto GetExePath() -> fs::FsPath;
    // returns true if we are hbmenu.
    static auto IsHbmenu() -> bool;

    static auto GetLogEnable() -> bool;
    static auto Get12HourTimeEnable() -> bool;
    static auto GetLanguage() -> long;
    static auto GetTextScrollSpeed() -> long;

    static void SetLogEnable(bool enable);
    static void Set12HourTimeEnable(bool enable);
    static void SetLanguage(long index);
    static void SetTextScrollSpeed(long index);

    static void PlaySoundEffect(SoundEffect effect);

    void Draw();
    void Update();
    void Poll();

    // void DrawElement(float x, float y, float w, float h, ui::ThemeEntryID id);
    auto LoadElementImage(std::string_view value) -> ElementEntry;
    auto LoadElementColour(std::string_view value) -> ElementEntry;
    auto LoadElement(std::string_view data, ElementType type) -> ElementEntry;

    void LoadTheme(const ThemeMeta& meta);
    void CloseTheme();
    void ScanThemes(const std::string& path);
    void ScanThemeEntries();

    // helper that converts 1.2.3 to a u32 used for comparisons.
    static auto GetVersionFromString(const char* str) -> u32;
    static auto IsVersionNewer(const char* current, const char* new_version) -> u32;

    static auto IsApplication() -> bool {
        const auto type = appletGetAppletType();
        return type == AppletType_Application || type == AppletType_SystemApplication;
    }

    static auto IsApplet() -> bool {
        return !IsApplication();
    }

    // returns true if launched in applet mode with a title suspended in the background.
    static auto IsAppletWithSuspendedApp() -> bool {
        R_UNLESS(IsApplet(), false);
        R_TRY_RESULT(pmdmntInitialize(), false);
        ON_SCOPE_EXIT(pmdmntExit());

        u64 pid;
        return R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid));
    }

    static auto IsEmummc() -> bool;
    static auto IsParitionBaseEmummc() -> bool;
    static auto IsFileBaseEmummc() -> bool;

    static void SetAutoSleepDisabled(bool enable) {
        static Mutex mutex{};
        static int ref_count{};

        mutexLock(&mutex);
        ON_SCOPE_EXIT(mutexUnlock(&mutex));

        if (enable) {
            appletSetAutoSleepDisabled(true);
            ref_count++;
        } else {
            if (ref_count) {
                ref_count--;
            }

            if (!ref_count) {
                appletSetAutoSleepDisabled(false);
            }
        }
    }

    static void SetBoostMode(bool enable, bool force = false) {
        static Mutex mutex{};
        static int ref_count{};

        mutexLock(&mutex);
        ON_SCOPE_EXIT(mutexUnlock(&mutex));

        if (enable) {
            ref_count++;
            appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
        } else {
            if (ref_count) {
                ref_count--;
            }
        }

        if (!ref_count || force) {
            ref_count = 0;
            appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
        }
    }

// private:
    static constexpr inline auto CONFIG_PATH = "/switch/TotalSMS/config.ini";
    static constexpr inline auto INI_SECTION = "config";
    static constexpr inline auto DEFAULT_THEME_PATH = "romfs:/themes/default_theme.ini";

    fs::FsPath m_app_path;

    bool m_is_launched_via_sphaira_forwader{};

    NVGcontext* vg{};
    PadState m_pad{};
    TouchInfo m_touch_info{};
    Controller m_controller{};
    std::vector<ThemeMeta> m_theme_meta_entries;

    Vec2 m_scale{1, 1};

    std::vector<std::unique_ptr<ui::Widget>> m_widgets;
    u32 m_pop_count{};
    ui::NotifMananger m_notif_manager{};

    AppletHookCookie m_appletHookCookie{};

    Theme m_theme{};
    fs::FsPath theme_path{};
    s64 m_theme_index{};

    AmsEmummcPaths m_emummc_paths{};
    bool m_quit{};

    option::OptionBool m_log_enabled{INI_SECTION, "log_enabled", false, false};
    option::OptionString m_theme_path{INI_SECTION, "theme", DEFAULT_THEME_PATH, false};
    option::OptionBool m_12hour_time{INI_SECTION, "12hour_time", false, false};
    option::OptionLong m_language{INI_SECTION, "language", 0, false}; // auto
    option::OptionLong m_text_scroll_speed{"accessibility", "text_scroll_speed", 1, false}; // normal
    option::OptionBool m_skip_splash_screen_intro{INI_SECTION, "skip_splash_screen_intro", false};

    PLSR_PlayerSoundId m_sound_ids[SoundEffect_MAX]{};

#ifdef USE_NVJPG
    nj::Decoder m_decoder;
#endif

// private: // from nanovg decko3d example by adubbz
    static constexpr unsigned NumFramebuffers = 2;
    static constexpr unsigned StaticCmdSize = 0x1000;
    unsigned s_width{1280};
    unsigned s_height{720};
    dk::UniqueDevice device;
    dk::UniqueQueue queue;
    std::optional<CMemPool> pool_images;
    std::optional<CMemPool> pool_code;
    std::optional<CMemPool> pool_data;
    dk::UniqueCmdBuf cmdbuf;
    CMemPool::Handle depthBuffer_mem;
    CMemPool::Handle framebuffers_mem[NumFramebuffers];
    dk::Image depthBuffer;
    dk::Image framebuffers[NumFramebuffers];
    DkCmdList framebuffer_cmdlists[NumFramebuffers];
    dk::UniqueSwapchain swapchain;
    DkCmdList render_cmdlist;
    std::optional<nvg::DkRenderer> renderer;
    void createFramebufferResources();
    void destroyFramebufferResources();
    void recordStaticCommands();
};

} // namespace sphaira
