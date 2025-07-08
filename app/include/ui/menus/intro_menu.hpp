#pragma once

#include "ui/widget.hpp"
#include <pulsar.h>

namespace sphaira::ui::menu::intro {

struct Menu final : Widget {
    Menu();
    ~Menu();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

    auto IsMenu() const -> bool override {
        return true;
    }

private:
    void PushFilebrowser();

private:
    PLSR_PlayerSoundId m_music_id{};

    int m_background{};
    int m_background_w{};
    int m_background_h{};

    int m_alpha{255};
    bool m_fade_out{};
};

} // namespace sphaira::ui::menu::intro
