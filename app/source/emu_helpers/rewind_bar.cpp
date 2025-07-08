#include "emu_helpers/rewind_bar.hpp"
#include "ui/nvg_util.hpp"
#include "log.hpp"
#include "app.hpp"

namespace sphaira {
namespace {

struct RewindBar {
    int cursor;
    int count;
    bool enable;
};

#define SHOW_TIME 0

static struct RewindBar g_bar;

#if SHOW_TIME
static const float BAR_HEIGHT = 70;
#else
static const float BAR_HEIGHT = 62;
#endif

using namespace sphaira::ui;
using namespace sphaira::ui::menu::emu;

static void rewind_bar_change_direction(int v) {
    const int result = g_bar.cursor + v;

    if (result >= 0 && result < g_bar.count) {
        g_bar.cursor = result;
        App::PlaySoundEffect(SoundEffect_Scroll);
    }
}

static auto render_entry(NVGcontext* vg, Theme* theme, Menu* app, int index, Vec4 rect, const Vec4& bar) -> const RewindBarTexture* {
    const auto texture = app->m_rewind_bar_textures.GetNext();
    if (!texture) {
        return {};
    }

    if (!rewind_get(app->rewind, index, app->rewind_buffer, app->rewind_buffer_size)) {
        log_write("failed to get rewind entry: %d cursor: %d\n", index, g_bar.cursor);
        return {};
    }

    if (index == g_bar.cursor) {
        const float pad = 2;
        rect.x -= pad;
        rect.y -= pad;
        rect.w += pad * 2;
        rect.h += pad * 2;
        gfx::drawRectOutline(vg, theme, 1.f, rect);
    }

#if SHOW_TIME
    const float font_size = 8;
    const float center_x = (rect.x + rect.w / 2);
    const float center_y = bar.y + ((rect.y - bar.y) / 2);
    gfx::drawTextArgs(vg, center_x, center_y, font_size, NVG_ALIGN_MIDDLE | NVG_ALIGN_CENTER, theme->GetColour(ThemeEntryID_TEXT), "%.1fs", (double)(g_bar.count - 1 - index) * (double)app->rewind_keyframe_interval / 60.0);
#endif

    app->emulator_update_texture_pixels(app, texture->handle, app->rewind_pixel_buffer);
    gfx::drawImage(vg, rect, texture->handle);

    return texture;
}

static void rewind_bar_set_open_internal(Menu* app, bool enable, bool pop_last_state) {
    if (!app->rewind) {
        enable = false;
    }

    if (g_bar.enable != enable) {
        g_bar.enable = enable;

        if (g_bar.enable) {
            app->rewind_push_new_frame(app);
            g_bar.count = rewind_get_count(app->rewind);
            g_bar.cursor = g_bar.count - 1;
        } else {
            // rewind pushes the current frame to the buffer when the bar is opened.
            // if we are closing the bar as we did not load a state, then we want to
            // to remove that pushed state, otheriwse, by rapidly toggling the bar, we
            // would quikcly push many states to the buffer!
            if (pop_last_state) {
                rewind_remove_after(app->rewind, rewind_get_count(app->rewind) - 1);
            }

            // restore frame buffer.
            app->emulator_update_texture_pixels(app, app->texture_current, app->pixel_buffer[app->pixel_buffer_index]);
        }
    }
}

} // namespace

void rewind_bar_init(void) {
    memset(&g_bar, 0, sizeof(g_bar));
}

void rewind_bar_set_open(Menu* app, bool enable) {
    rewind_bar_set_open_internal(app, enable, true);
}

bool rewind_bar_enabled(void) {
    return g_bar.enable;
}

void rewind_bar_button(Menu* app, enum RewindBarButton button) {
    if (!rewind_bar_enabled()) {
        return;
    }

    switch (button) {
        case RewindBarButton_OK:
            // load data at given index and remove all savestates after it.
            rewind_get(app->rewind, g_bar.cursor, app->rewind_buffer, app->rewind_buffer_size);
            rewind_remove_after(app->rewind, g_bar.cursor);

            // copy new frame to front and back buffer.
            memcpy(app->pixel_buffer[0], app->rewind_pixel_buffer, app->rewind_pixel_buffer_size);
            memcpy(app->pixel_buffer[1], app->rewind_pixel_buffer, app->rewind_pixel_buffer_size);

            // load savestate and disable the menu bar.
            SMS_loadstate(&app->sms, app->rewind_state_buffer, app->rewind_state_buffer_size, &app->rewind_state_config);
            rewind_bar_set_open_internal(app, false, false);
            break;

        case RewindBarButton_Back:
            rewind_bar_set_open_internal(app, false, true);
            break;

        case RewindBarButton_Left:
            rewind_bar_change_direction(-1);
            break;

        case RewindBarButton_Right:
            rewind_bar_change_direction(+1);
            break;
    }
}

void rewind_bar_render(NVGcontext* vg, Theme* theme, Menu* app) {
    if (!rewind_bar_enabled()) {
        return;
    }

    const Vec4 viewport = { 0, 0, SMS_SCREEN_WIDTH, SMS_SCREEN_HEIGHT };

    nvgSave(vg);
    nvgScale(vg, SCREEN_WIDTH / viewport.w, SCREEN_HEIGHT / viewport.h);

    Vec4 bar;
    bar.x = 0;
    bar.y = viewport.h - BAR_HEIGHT;
    bar.w = viewport.w;
    bar.h = viewport.h - bar.y;
    gfx::drawRect(vg, bar, nvgRGB(0, 0, 0x47));

    const float centerx = (bar.x + bar.w) / 2;
    const float max_num_boxs = 4;
    const float padx = 5;
#if SHOW_TIME
    const float pady_top = 15;
#else
    const float pady_top = 7;
#endif
    const float pady_bottom = 5;
    const float boxw = bar.w / max_num_boxs;
    const float boxh = bar.h - pady_top - pady_bottom;

    Vec4 center_box;
    center_box.w = boxw;
    center_box.h = boxh;
    center_box.x = centerx - center_box.w / 2;
    center_box.y = bar.y + pady_top;

    // draw left
    Vec4 box = center_box;
    for (int i = g_bar.cursor - 1; i >= 0; i--) {
        box.x -= box.w + padx;
        if (box.x + box.w < bar.x) {
            break;
        }

        render_entry(vg, theme, app, i, box, bar);
    }

    // draw right
    box = center_box;
    for (int i = g_bar.cursor + 1; i < g_bar.count; i++) {
        box.x += box.w + padx;
        if (box.x > bar.x + bar.w) {
            break;
        }

        render_entry(vg, theme, app, i, box, bar);
    }

    // draw center
    const auto texture = render_entry(vg, theme, app, g_bar.cursor, center_box, bar);

    Vec4 rr;
    rr.y = 0;
    rr.h = bar.y;

    const float scale = rr.h / (float)app->screen_h;
    rr.w = (float)SMS_SCREEN_WIDTH * scale;
    rr.x = (viewport.w - rr.w) / 2;

    gfx::drawImage(vg, rr, texture->handle);

    nvgRestore(vg);
}
} // namespace sphaira
