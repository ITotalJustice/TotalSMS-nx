#pragma once

#include <stddef.h>
#include "ui/menus/emu_menu.hpp"

namespace sphaira {

enum RewindBarButton {
    RewindBarButton_OK,
    RewindBarButton_Back,
    RewindBarButton_Left,
    RewindBarButton_Right,
    // RewindBarButton_Export,
};

using Menu = sphaira::ui::menu::emu::Menu;

void rewind_bar_init(void);
void rewind_bar_set_open(Menu* app, bool enable);
bool rewind_bar_enabled(void);
void rewind_bar_button(Menu* app, enum RewindBarButton button);
void rewind_bar_render(NVGcontext* vg, Theme* theme, Menu* app);

} // namespace sphaira
