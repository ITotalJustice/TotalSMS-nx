#include "ui/menus/intro_menu.hpp"
#include "ui/menus/filebrowser.hpp"

#include "app.hpp"
#include "ui/nvg_util.hpp"

#include <cmath>
#include <lz4frame.h>

namespace sphaira::ui::menu::intro {
namespace {

constexpr const u8 BACKGROUND_IMAGE[]{
    #embed <intro/background.png>
};

constexpr const u8 MUSIC_COMPRESSED[]{
    #embed <intro/music.bfstm.lz4>
};

auto IsSoundDone(PLSR_PlayerSoundId id) -> bool {
    if (auto player = plsrPlayerGetInstance()) {
        static TimeStamp ts;
        if (ts.GetMs() < 500) { // update every 0.5s
            return false;
        }
        ts.Update();

        if (R_SUCCEEDED(audrvUpdate(&player->driver))) {
            for (int i = 0; i < id->channelCount; i++) {
                const PLSR_PlayerSoundChannel* channel = &id->channels[i];
                for (int j = 0; j < id->wavebufCount; j++) {
                    const AudioDriverWaveBuf* buf = &channel->wavebufs[j];
                    if (buf->state != AudioDriverWaveBufState_Done && buf->state != AudioDriverWaveBufState_Free) {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

auto LoadCompressedData(std::span<const u8> data) -> std::vector<u8> {
    LZ4F_dctx* dctx;
    LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
    ON_SCOPE_EXIT(LZ4F_freeDecompressionContext(dctx));

    LZ4F_frameInfo_t info{};
    size_t compressed_size = data.size();
    auto result = LZ4F_getFrameInfo(dctx, &info, data.data(), &compressed_size);
    if (LZ4F_isError(result)) {
        log_write("error getting frame info\n");
        return {};
    }

    const auto offset = compressed_size;
    std::vector<u8> dst(info.contentSize);
    size_t dst_size = dst.size();
    size_t src_size = data.size() - offset;
    result = LZ4F_decompress(dctx, dst.data(), &dst_size, data.data() + offset, &src_size, nullptr);
    if (LZ4F_isError(result)) {
        log_write("error decompressing frame\n");
        return {};
    }

    return dst;
}

} // namespace

Menu::Menu() {
    // disable to skip intro.
    #if 1
    // enable if wanting to take recording of the intro.
    // svcSleepThread(1e+9);

    Result rc = 0;

    if (!App::GetApp()->m_skip_splash_screen_intro.Get()) {
        const auto music_data = LoadCompressedData(MUSIC_COMPRESSED);
        if (!music_data.empty()) {
            PLSR_BFSTM stream;
            if (R_SUCCEEDED(rc = plsrBFSTMOpenMem(music_data.data(), music_data.size(), &stream))) {
                if (R_SUCCEEDED(rc = plsrPlayerLoadStream(&stream, &m_music_id))) {
                    if (R_SUCCEEDED(rc = plsrPlayerPlay(m_music_id))) {
                        m_background = nvgCreateImageMem(App::GetVg(), NVG_IMAGE_NEAREST, BACKGROUND_IMAGE, sizeof(BACKGROUND_IMAGE));
                        if (m_background) {
                            nvgImageSize(App::GetVg(), m_background, &m_background_w, &m_background_h);
                        }
                    }
                }
                plsrBFSTMClose(&stream);
            }
        }
    }

    if (R_FAILED(rc) || !m_background) {
        PushFilebrowser();
    }
    #else
    PushFilebrowser();
    #endif
}

Menu::~Menu() {
    plsrPlayerFree(m_music_id);

    if (m_background) {
        nvgDeleteImage(App::GetVg(), m_background);
    }
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (m_fade_out && m_alpha >= 255) {
        #if 0
        static int counter = 20;
        counter--;
        if (!counter)
        #endif
        {
            PushFilebrowser();
        }
        return;
    }

    if (!m_fade_out && IsSoundDone(m_music_id)) {
        m_fade_out = true;
    }

    if (m_fade_out) {
        m_alpha = std::min(255, m_alpha + 5);
    } else {
        m_alpha = std::max(0, m_alpha - 5);
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    gfx::drawRect(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, nvgRGBA(0, 0, 0, 0xFF));

    // integer scale the image
    Vec4 vec;
    vec.w = m_background_w;
    vec.h = m_background_h;
    // const float scale = floor(std::min(SCREEN_WIDTH / vec.w, SCREEN_HEIGHT / vec.h));
    const float scale = std::min(SCREEN_WIDTH / vec.w, SCREEN_HEIGHT / vec.h);

    // center image.
    vec.w *= scale;
    vec.h *= scale;
    vec.x = (SCREEN_WIDTH - vec.w) / 2;
    vec.y = (SCREEN_HEIGHT - vec.h) / 2;

    gfx::drawImage(vg, vec, m_background);
    gfx::drawRect(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, nvgRGBA(0, 0, 0, m_alpha));
}

void Menu::PushFilebrowser() {
    SetPop();
    App::Push<ui::menu::filebrowser::Menu>(ui::menu::MenuFlag_Tab);
}

} // namespace sphaira::ui::menu::intro
