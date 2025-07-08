#include "ui/error_box.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "i18n.hpp"

namespace sphaira::ui {
namespace {

auto GetModule(Result rc) -> const char* {
    switch (R_MODULE(rc)) {
        case Module_Svc: return "Svc";
        case Module_Fs: return "Fs";
        case Module_Os: return "Os";
        case Module_Ncm: return "Ncm";
        case Module_Ns: return "Ns";
        case Module_Spl: return "Spl";
        case Module_Applet: return "Applet";
        case Module_Usb: return "Usb";
        case Module_Irsensor: return "Irsensor";
        case Module_Libnx: return "Libnx";
        case Module_Sphaira: return "Sphaira";
    }

    return nullptr;
}
auto GetCodeMessage(Result rc) -> const char* {
    switch (rc) {
        case SvcError_TimedOut: return "SvcError_TimedOut";
        case SvcError_Cancelled: return "SvcError_Cancelled";

        case FsError_PathNotFound: return "FsError_PathNotFound";
        case FsError_PathAlreadyExists: return "FsError_PathAlreadyExists";
        case FsError_TargetLocked: return "FsError_TargetLocked";
        case FsError_TooLongPath: return "FsError_TooLongPath";
        case FsError_InvalidCharacter: return "FsError_InvalidCharacter";
        case FsError_InvalidOffset: return "FsError_InvalidOffset";
        case FsError_InvalidSize: return "FsError_InvalidSize";

        case Result_TransferCancelled: return "Error_TransferCancelled";
        case Result_FsTooManyEntries: return "Error_FsTooManyEntries";
        case Result_FsNewPathTooLarge: return "Error_FsNewPathTooLarge";
        case Result_FsInvalidType: return "Error_FsInvalidType";
        case Result_FsEmpty: return "Error_FsEmpty";
        case Result_FsAlreadyRoot: return "Error_FsAlreadyRoot";
        case Result_FsNoCurrentPath: return "Error_FsNoCurrentPath";
        case Result_FsBrokenCurrentPath: return "Error_FsBrokenCurrentPath";
        case Result_FsIndexOutOfBounds: return "Error_FsIndexOutOfBounds";
        case Result_FsFsNotActive: return "Error_FsFsNotActive";
        case Result_FsNewPathEmpty: return "Error_FsNewPathEmpty";
        case Result_FsLoadingCancelled: return "Error_FsLoadingCancelled";
        case Result_FsBrokenRoot: return "Error_FsBrokenRoot";
        case Result_FsUnknownStdioError: return "Error_FsUnknownStdioError";
        case Result_FsReadOnly: return "Error_FsReadOnly";
        case Result_FsNotActive: return "Error_FsNotActive";
        case Result_FsFailedStdioStat: return "Error_FsFailedStdioStat";
        case Result_FsFailedStdioOpendir: return "Error_FsFailedStdioOpendir";
        case Result_NroBadMagic: return "Error_NroBadMagic";
        case Result_NroBadSize: return "Error_NroBadSize";
        case Result_UnzOpen2_64: return "Error_UnzOpen2_64";
        case Result_UnzGetGlobalInfo64: return "Error_UnzGetGlobalInfo64";
        case Result_UnzLocateFile: return "Error_UnzLocateFile";
        case Result_UnzGoToFirstFile: return "Error_UnzGoToFirstFile";
        case Result_UnzGoToNextFile: return "Error_UnzGoToNextFile";
        case Result_UnzOpenCurrentFile: return "Error_UnzOpenCurrentFile";
        case Result_UnzGetCurrentFileInfo64: return "Error_UnzGetCurrentFileInfo64";
        case Result_UnzReadCurrentFile: return "Error_UnzReadCurrentFile";
        case Result_ZipOpen2_64: return "Error_ZipOpen2_64";
        case Result_ZipOpenNewFileInZip: return "Error_ZipOpenNewFileInZip";
        case Result_ZipWriteInFileInZip: return "Error_ZipWriteInFileInZip";
        case Result_MmzBadLocalHeaderSig: return "Error_MmzBadLocalHeaderSig";
        case Result_MmzBadLocalHeaderRead: return "Error_MmzBadLocalHeaderRead";
        case Result_EmuLoadSaveState: return "Error_EmuLoadSaveState";
        case Result_EmuCreateSaveState: return "Error_EmuCreateSaveState";
    }

    return "";
}

} // namespace

ErrorBox::ErrorBox(const std::string& message) : m_message{message} {
    log_write("[ERROR] %s\n", m_message.c_str());

    m_pos.w = 770.f;
    m_pos.h = 430.f;
    m_pos.x = 255;
    m_pos.y = 145;

    SetAction(Button::A, Action{[this](){
        SetPop();
    }});

    App::PlaySoundEffect(SoundEffect::SoundEffect_Error);
}

ErrorBox::ErrorBox(Result code, const std::string& message) : ErrorBox{message} {
    m_code = code;
    m_code_message = GetCodeMessage(code);
    m_code_module = std::to_string(R_MODULE(code));
    if (auto str = GetModule(code)) {
        m_code_module += " (" + std::string(str) + ")";
    }
    log_write("[ERROR] Code: 0x%X Module: %s Description: %u\n", R_VALUE(code), m_code_module.c_str(), R_DESCRIPTION(code));
}

auto ErrorBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);
}

auto ErrorBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP));

    const Vec4 box = { 455, 470, 365, 65 };
    const auto center_x = m_pos.x + m_pos.w/2;

    gfx::drawTextArgs(vg, center_x, 180, 63, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_ERROR), "\uE140");
    if (m_code.has_value()) {
        const auto code = m_code.value();
        if (m_code_message.empty()) {
            gfx::drawTextArgs(vg, center_x, 270, 25, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Code: 0x%X Module: %s", R_VALUE(code), m_code_module.c_str());
        } else {
            gfx::drawTextArgs(vg, center_x, 270, 25, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", m_code_message.c_str());
        }
    } else {
        gfx::drawTextArgs(vg, center_x, 270, 25, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "An error occurred"_i18n.c_str());
    }
    gfx::drawTextArgs(vg, center_x, 325, 23, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", m_message.c_str());
    gfx::drawTextArgs(vg, center_x, 380, 20, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "If this message appears repeatedly, please open an issue."_i18n.c_str());
    gfx::drawTextArgs(vg, center_x, 415, 20, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "https://github.com/ITotalJustice/TotalSMS-nx/issues");
    gfx::drawRectOutline(vg, theme, 4.f, box);
    gfx::drawTextArgs(vg, center_x, box.y + box.h/2, 23, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED), "OK"_i18n.c_str());
}

} // namespace sphaira::ui
