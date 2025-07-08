#include "app.hpp"
#include "log.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"

namespace sphaira::ui::menu {

auto MenuBase::GetPolledData(bool force_refresh) -> PolledData {
    static PolledData data{};
    static TimeStamp timestamp{};
    static bool has_init = false;

    if (!has_init) {
        has_init = true;
        force_refresh = true;
    }

    // update every second, do this in Draw because Update() isn't called if it
    // doesn't have focus.
    if (force_refresh || timestamp.GetSeconds() >= 1) {
        data.tm = {};
        data.battery_percetange = {};
        data.charger_type = {};
        // data.type = {};
        // data.status = {};
        // data.strength = {};
        // data.ip = {};

        const auto t = std::time(NULL);
        localtime_r(&t, &data.tm);
        psmGetBatteryChargePercentage(&data.battery_percetange);
        psmGetChargerType(&data.charger_type);
        // nifmGetInternetConnectionStatus(&data.type, &data.strength, &data.status);
        // nifmGetCurrentIpAddress(&data.ip);

        timestamp.Update();
    }

    return data;
}

MenuBase::MenuBase(const std::string& title, u32 flags) : m_title{title}, m_flags{flags} {
    // this->SetParent(this);
    this->SetPos(30, 87, 1220 - 30, 646 - 87);
    SetAction(Button::START, Action{App::Exit});
}

MenuBase::~MenuBase() {
}

void MenuBase::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);
}

void MenuBase::Draw(NVGcontext* vg, Theme* theme) {
    DrawElement(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ThemeEntryID_BACKGROUND);
    Widget::Draw(vg, theme);

    // const auto pdata = GetPolledData();

    const float start_y = 70;
    const float font_size = 22;
    const float spacing = 30;

    float start_x = 1220;
    float bounds[4];

    #define draw(colour, size, spacing, fixed, ...) \
        nvgFontSize(vg, size); \
        gfx::drawTextArgs(vg, start_x, start_y, size, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(colour), __VA_ARGS__); \
        if (fixed) { \
            start_x -= fixed; \
        } else { \
            gfx::textBoundsArgs(vg, 0, 0, bounds, __VA_ARGS__); \
            start_x -= spacing + (bounds[2] - bounds[0]); \
        }

    draw(ThemeEntryID_TEXT_INFO, 16, 10, 0, "v" APP_VERSION_HASH);
    draw(ThemeEntryID_TEXT_INFO, font_size, spacing, 0, "TotalSMS");

    if (!App::IsApplication()) {
        draw(ThemeEntryID_ERROR, font_size, spacing, 0, ("[Applet Mode]"_i18n).c_str());
    }

    #undef draw

    gfx::drawRect(vg, 30.f, 86.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
    gfx::drawRect(vg, 30.f, 646.0f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));

    nvgFontSize(vg, 28);
    gfx::textBounds(vg, 0, 0, bounds, m_title.c_str());

    const auto text_w = SCREEN_WIDTH / 2 - 30;
    const auto title_sub_x = 80 + (bounds[2] - bounds[0]) + 10;

    gfx::drawTextArgs(vg, 80, start_y, 28.f, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT), m_title.c_str());
    m_scroll_title_sub_heading.Draw(vg, true, title_sub_x, start_y, text_w - title_sub_x, 16, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO), m_title_sub_heading.c_str());
    m_scroll_sub_heading.Draw(vg, true, 80, 685, text_w - 160, 18, NVG_ALIGN_LEFT, theme->GetColour(ThemeEntryID_TEXT), m_sub_heading.c_str());
}

void MenuBase::SetTitle(std::string title) {
    m_title = title;
}

void MenuBase::SetTitleSubHeading(std::string sub_heading) {
    m_title_sub_heading = sub_heading;
}

void MenuBase::SetSubHeading(std::string sub_heading) {
    m_sub_heading = sub_heading;
}

} // namespace sphaira::ui::menu
