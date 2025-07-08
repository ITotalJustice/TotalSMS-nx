#include "ui/menus/filebrowser.hpp"
#include "ui/menus/emu_menu.hpp"

#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"

#include "log.hpp"
#include "app.hpp"
#include "ui/nvg_util.hpp"
#include "fs.hpp"
#include "defines.hpp"
#include "image.hpp"
#include "swkbd.hpp"
#include "i18n.hpp"
#include "threaded_file_transfer.hpp"
#include "minizip_helper.hpp"

#include <minIni.h>
#include <minizip/zip.h>
#include <minizip/unzip.h>
#include <dirent.h>
#include <cstring>
#include <cassert>
#include <string>
#include <string_view>
#include <ctime>
#include <span>
#include <utility>
#include <ranges>

namespace sphaira::ui::menu::filebrowser {
namespace {

constinit UEvent g_change_uevent;

constexpr FsEntry FS_ENTRY_DEFAULT{
    "microSD card", "/", FsType::Sd, FsEntryFlag_Assoc,
};

constexpr FsEntry FS_ENTRIES[]{
    FS_ENTRY_DEFAULT,
};

struct ExtDbEntry {
    std::string_view db_name;
    std::span<const std::string_view> ext;
};

constexpr std::string_view ROM_EXTENSIONS[] = {
    "sms", "gg", "bin", "sg",
};
constexpr std::string_view ZIP_EXTENSIONS[] = {
    "zip",
};
constexpr std::string_view FILTER_EXTENSIONS[] = {
    "sms", "gg", "bin", "sg", "zip"
};

auto IsExtension(std::string_view ext, std::span<const std::string_view> list) -> bool {
    for (auto e : list) {
        if (e.length() == ext.length() && !strncasecmp(ext.data(), e.data(), ext.length())) {
            return true;
        }
    }
    return false;
}

auto IsExtension(std::string_view ext1, std::string_view ext2) -> bool {
    return ext1.length() == ext2.length() && !strncasecmp(ext1.data(), ext2.data(), ext1.length());
}

} // namespace

void SignalChange() {
    ueventSignal(&g_change_uevent);
}

FsView::FsView(Menu* menu, const fs::FsPath& path, const FsEntry& entry, ViewSide side) : m_menu{menu}, m_side{side} {
    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            if (m_entries_current.empty()) {
                return;
            }

            const auto& entry = GetEntry();

            if (entry.type == FsDirEntryType_Dir) {
                Scan(GetNewPathCurrent());
            } else {
                if (IsExtension(entry.GetInternalExtension(), ROM_EXTENSIONS)) {
                    App::Push<menu::emu::Menu>(GetNewPathCurrent());
                }
            }
        }}),

        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            if (!m_menu->IsTab() && App::GetApp()->m_controller.GotHeld(Button::R2)) {
                m_menu->PromptIfShouldExit();
                return;
            }

            std::string_view view{m_path};
            if (view != m_fs->Root()) {
                const auto end = view.find_last_of('/');
                assert(end != view.npos);

                if (end == 0) {
                    Scan(m_fs->Root(), true);
                } else {
                    Scan(view.substr(0, end), true);
                }
            } else {
                if (!m_menu->IsTab()) {
                    m_menu->PromptIfShouldExit();
                }
            }
        }}),

        std::make_pair(Button::START, Action{"Exit"_i18n, [this](){
            App::Exit();
        }})
    );

    SetSide(m_side);

    auto buf = path;
    if (path.empty()) {
        ini_gets("paths", "last_path", entry.root, buf, sizeof(buf), App::CONFIG_PATH);
    }

    SetFs(buf, entry);
}

FsView::FsView(Menu* menu, ViewSide side) : FsView{menu, "", FS_ENTRY_DEFAULT, side} {

}

FsView::~FsView() {
    // don't store mount points for non-sd card paths.
    if (IsSd()) {
        ini_puts("paths", "last_path", m_path, App::CONFIG_PATH);

        // save last selected file.
        if (!m_entries.empty()) {
            ini_puts("paths", "last_file", GetEntry().name, App::CONFIG_PATH);
        }
    }
}

void FsView::Update(Controller* controller, TouchInfo* touch) {
    m_list->OnUpdate(controller, touch, m_index, m_entries_current.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    });
}

void FsView::Draw(NVGcontext* vg, Theme* theme) {
    const auto& text_col = theme->GetColour(ThemeEntryID_TEXT);

    if (m_entries_current.empty()) {
        gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty..."_i18n.c_str());
        return;
    }

    constexpr float text_xoffset{15.f};
    bool got_dir_count = false;

    m_list->Draw(vg, theme, m_entries_current.size(), [this, text_col, &got_dir_count](auto* vg, auto* theme, auto v, auto i) {
        const auto& [x, y, w, h] = v;
        auto& e = GetEntry(i);

        if (e.IsDir()) {
            // NOTE: make this native only if hdd dir scan is too slow.
            // if (m_fs->IsNative() && e.file_count == -1 && e.dir_count == -1) {
            // NOTE: this takes longer than 16ms when opening a new folder due to it
            // checking all 9 folders at once.
            if (!got_dir_count && e.file_count == -1 && e.dir_count == -1) {
                got_dir_count = true;
                m_fs->DirGetEntryCount(GetNewPath(e), &e.file_count, &e.dir_count);
            }
        } else if (!e.checked_extension) {
            e.checked_extension = true;
            if (auto ext = std::strrchr(e.name, '.')) {
                e.extension = ext+1;
            }
        }

        auto text_id = ThemeEntryID_TEXT;
        const auto selected = m_index == i;
        if (selected) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_entries_current.size() - 1) {
                gfx::drawRect(vg, Vec4{x, y + h, w, 1.f}, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        if (e.IsDir()) {
            DrawElement(x + text_xoffset, y + 5, 50, 50, ThemeEntryID_ICON_FOLDER);
        } else {
            auto icon = ThemeEntryID_ICON_FILE;
            const auto ext = e.GetExtension();
            if (IsExtension(ext, ROM_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_NRO;
            } else if (IsExtension(ext, ZIP_EXTENSIONS)) {
                icon = ThemeEntryID_ICON_ZIP;
            }

            DrawElement(x + text_xoffset, y + 5, 50, 50, icon);
        }

        m_scroll_name.Draw(vg, selected, x + text_xoffset+65, y + (h / 2.f), w-(75+text_xoffset+65+50), 20, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), e.name);

        if (e.IsDir()) {
            if (e.file_count != -1) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) - 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(text_id), "%zd files"_i18n.c_str(), e.file_count);
            }
            if (e.dir_count != -1) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) + 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(text_id), "%zd dirs"_i18n.c_str(), e.dir_count);
            }
        } else if (e.IsFile()) {
            if (!e.time_stamp.is_valid) {
                const auto path = GetNewPath(e);
                if (m_fs->IsNative()) {
                    m_fs->GetFileTimeStampRaw(path, &e.time_stamp);
                } else {
                    m_fs->FileGetSizeAndTimestamp(path, &e.time_stamp, &e.file_size);
                }
            }

            const auto t = (time_t)(e.time_stamp.modified);
            struct tm tm{};
            localtime_r(&t, &tm);
            gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) + 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(text_id), "%02u/%02u/%u", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
            if ((double)e.file_size / 1024.0 / 1024.0 <= 0.009) {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) - 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(text_id), "%.2f KiB", (double)e.file_size / 1024.0);
            } else {
                gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f) - 3, 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, theme->GetColour(text_id), "%.2f MiB", (double)e.file_size / 1024.0 / 1024.0);
            }
        }
    });
}

void FsView::OnFocusGained() {
    Widget::OnFocusGained();
    if (m_entries.empty()) {
        if (m_path.empty()) {
            Scan(m_fs->Root());
        } else {
            Scan(m_path);
        }

        if (IsSd() && !m_entries.empty()) {
            LastFile last_file{};
            if (ini_gets("paths", "last_file", "", last_file.name, sizeof(last_file.name), App::CONFIG_PATH)) {
                SetIndexFromLastFile(last_file);
            }
        }
    }
}

void FsView::SetSide(ViewSide side) {
    m_side = side;

    const auto pos = m_menu->GetPos();
    this->SetPos(pos);
    Vec4 v{75, GetY() + 1.f + 42.f, 1220.f - 45.f * 2, 60};

    m_list = std::make_unique<List>(1, 8, m_pos, v);

    // reset scroll position.
    m_scroll_name.Reset();
}

void FsView::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        log_write("setting y off\n");
        m_list->SetYoff();
    }

    if (!m_entries_current.empty() && !GetEntry().checked_internal_extension && GetEntry().GetExtension() == "zip") {
        GetEntry().checked_internal_extension = true;

        TimeStamp ts;
        fs::FsPath filename_inzip{};
        if (R_SUCCEEDED(mz::PeekFirstFileName(GetFs(), GetNewPathCurrent(), filename_inzip))) {
            if (auto ext = std::strrchr(filename_inzip, '.')) {
                GetEntry().internal_name = filename_inzip.toString();
                GetEntry().internal_extension = ext+1;
            }
            log_write("\tzip, time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
        }
    }

    m_menu->UpdateSubheading();
}

auto FsView::Scan(const fs::FsPath& new_path, bool is_walk_up) -> Result {
    TimeStamp ts;
    ON_SCOPE_EXIT(log_write("\tscan final, time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs()));

    App::SetBoostMode(true);
    ON_SCOPE_EXIT(App::SetBoostMode(false));

    log_write("new scan path: %s\n", new_path.s);
    if (!is_walk_up && !m_path.empty() && !m_entries_current.empty()) {
        const LastFile f(GetEntry().name, m_index, m_list->GetYoff(), m_entries_current.size());
        m_previous_highlighted_file.emplace_back(f);
    }

    m_path = new_path;
    m_entries.clear();
    m_index = 0;
    m_list->SetYoff(0);
    m_menu->SetTitleSubHeading(m_path);

    fs::Dir d;
    R_TRY(m_fs->OpenDirectory(new_path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d));

    // we won't run out of memory here (tm)
    std::vector<FsDirectoryEntry> dir_entries;
    R_TRY(d.ReadAll(dir_entries));

    const auto count = dir_entries.size();
    m_entries.reserve(count);

    m_entries_index.clear();
    m_entries_index_hidden.clear();
    m_entries_index_search.clear();

    m_entries_index.reserve(count);
    m_entries_index_hidden.reserve(count);

    u32 i = 0;
    for (const auto& e : dir_entries) {
        if (e.type == FsDirEntryType_File) {
            const auto ext = std::strrchr(e.name, '.');
            if (!ext || !IsExtension(ext + 1, FILTER_EXTENSIONS)) {
                continue;
            }
        }

        m_entries_index_hidden.emplace_back(i);

        if (e.name[0] != '.') {
            m_entries_index.emplace_back(i);
        }

        m_entries.emplace_back(e);
        i++;
    }

    Sort();

    SetIndex(0);

    // find previous entry
    if (is_walk_up && !m_previous_highlighted_file.empty()) {
        ON_SCOPE_EXIT(m_previous_highlighted_file.pop_back());
        SetIndexFromLastFile(m_previous_highlighted_file.back());
    }

    R_SUCCEED();
}

void FsView::Sort() {
    // returns true if lhs should be before rhs
    const auto sort = m_menu->m_sort.Get();
    const auto order = m_menu->m_order.Get();
    const auto folders_first = m_menu->m_folders_first.Get();
    const auto hidden_last = m_menu->m_hidden_last.Get();

    const auto sorter = [this, sort, order, folders_first, hidden_last](u32 _lhs, u32 _rhs) -> bool {
        const auto& lhs = m_entries[_lhs];
        const auto& rhs = m_entries[_rhs];

        if (hidden_last) {
            if (lhs.IsHidden() && !rhs.IsHidden()) {
                return false;
            } else if (!lhs.IsHidden() && rhs.IsHidden()) {
                return true;
            }
        }

        if (folders_first) {
            if (lhs.type == FsDirEntryType_Dir && !(rhs.type == FsDirEntryType_Dir)) { // left is folder
                return true;
            } else if (!(lhs.type == FsDirEntryType_Dir) && rhs.type == FsDirEntryType_Dir) { // right is folder
                return false;
            }
        }

        switch (sort) {
            case SortType_Size: {
                if (lhs.file_size == rhs.file_size) {
                    return strncasecmp(lhs.name, rhs.name, sizeof(lhs.name)) < 0;
                } else if (order == OrderType_Descending) {
                    return lhs.file_size > rhs.file_size;
                } else {
                    return lhs.file_size < rhs.file_size;
                }
            } break;
            case SortType_Alphabetical: {
                if (order == OrderType_Descending) {
                    return strncasecmp(lhs.name, rhs.name, sizeof(lhs.name)) < 0;
                } else {
                    return strncasecmp(lhs.name, rhs.name, sizeof(lhs.name)) > 0;
                }
            } break;
        }

        std::unreachable();
    };

    if (m_menu->m_show_hidden.Get()) {
        m_entries_current = m_entries_index_hidden;
    } else {
        m_entries_current = m_entries_index;
    }

    std::sort(m_entries_current.begin(), m_entries_current.end(), sorter);
}

void FsView::SortAndFindLastFile(bool scan) {
    std::optional<LastFile> last_file;
    if (!m_path.empty() && !m_entries_current.empty()) {
        last_file = LastFile(GetEntry().name, m_index, m_list->GetYoff(), m_entries_current.size());
    }

    if (scan) {
        Scan(m_path);
    } else {
        Sort();
    }

    if (last_file.has_value()) {
        SetIndexFromLastFile(*last_file);
    }
}

void FsView::SetIndexFromLastFile(const LastFile& last_file) {
    SetIndex(0);

    s64 index = -1;
    for (u64 i = 0; i < m_entries_current.size(); i++) {
        if (last_file.name == GetEntry(i).name) {
            log_write("found name\n");
            index = i;
            break;
        }
    }
    if (index >= 0) {
        if (index == last_file.index && m_entries_current.size() == last_file.entries_count) {
            m_list->SetYoff(last_file.offset);
            log_write("index is the same as last time\n");
        } else {
            // file position changed!
            log_write("file position changed\n");
            // guesstimate where the position is
            if (index >= 8) {
                m_list->SetYoff(((index - 8) + 1) * m_list->GetMaxY());
            } else {
                m_list->SetYoff(0);
            }
        }
        SetIndex(index);
    }
}

auto FsView::get_collection(fs::Fs* fs, const fs::FsPath& path, const fs::FsPath& parent_name, FsDirCollection& out, bool inc_file, bool inc_dir, bool inc_size) -> Result {
    out.path = path;
    out.parent_name = parent_name;

    const auto fetch = [fs, &path](std::vector<FsDirectoryEntry>& out, u32 flags) -> Result {
        fs::Dir d;
        R_TRY(fs->OpenDirectory(path, flags, &d));
        return d.ReadAll(out);
    };

    if (inc_file) {
        u32 flags = FsDirOpenMode_ReadFiles;
        if (!inc_size) {
            flags |= FsDirOpenMode_NoFileSize;
        }
        R_TRY(fetch(out.files, flags));
    }

    if (inc_dir) {
        R_TRY(fetch(out.dirs, FsDirOpenMode_ReadDirs));
    }

    R_SUCCEED();
}

auto FsView::get_collections(fs::Fs* fs, const fs::FsPath& path, const fs::FsPath& parent_name, FsDirCollections& out, bool inc_size) -> Result {
    // get a list of all the files / dirs
    FsDirCollection collection;
    R_TRY(get_collection(fs, path, parent_name, collection, true, true, inc_size));
    log_write("got collection: %s parent_name: %s files: %zu dirs: %zu\n", path.s, parent_name.s, collection.files.size(), collection.dirs.size());
    out.emplace_back(collection);

    // for (size_t i = 0; i < collection.dirs.size(); i++) {
    for (const auto&p : collection.dirs) {
        // use heap as to not explode the stack
        const auto new_path = std::make_unique<fs::FsPath>(FsView::GetNewPath(path, p.name));
        const auto new_parent_name = std::make_unique<fs::FsPath>(FsView::GetNewPath(parent_name, p.name));
        log_write("trying to get nested collection: %s parent_name: %s\n", new_path->s, new_parent_name->s);
        R_TRY(get_collections(fs, *new_path, *new_parent_name, out, inc_size));
    }

    R_SUCCEED();
}

auto FsView::get_collection(const fs::FsPath& path, const fs::FsPath& parent_name, FsDirCollection& out, bool inc_file, bool inc_dir, bool inc_size) -> Result {
    return get_collection(m_fs.get(), path, parent_name, out, true, true, inc_size);
}

auto FsView::get_collections(const fs::FsPath& path, const fs::FsPath& parent_name, FsDirCollections& out, bool inc_size) -> Result {
    return get_collections(m_fs.get(), path, parent_name, out, inc_size);
}

Result FsView::DeleteAllCollections(ProgressBox* pbox, fs::Fs* fs, const FsDirCollections& collections, u32 mode) {
    // delete everything in collections, reversed
    for (const auto& c : std::views::reverse(collections)) {
        const auto delete_func = [&](auto& array) -> Result {
            for (const auto& p : array) {
                pbox->Yield();
                R_TRY(pbox->ShouldExitResult());

                const auto full_path = FsView::GetNewPath(c.path, p.name);
                pbox->SetTitle(p.name);
                pbox->NewTransfer("Deleting "_i18n + full_path.toString());
                if ((mode & FsDirOpenMode_ReadDirs) && p.type == FsDirEntryType_Dir) {
                    log_write("deleting dir: %s\n", full_path.s);
                    R_TRY(fs->DeleteDirectory(full_path));
                    svcSleepThread(1e+5);
                } else if ((mode & FsDirOpenMode_ReadFiles) && p.type == FsDirEntryType_File) {
                    log_write("deleting file: %s\n", full_path.s);
                    R_TRY(fs->DeleteFile(full_path));
                    svcSleepThread(1e+5);
                }
            }

            R_SUCCEED();
        };

        R_TRY(delete_func(c.files));
        R_TRY(delete_func(c.dirs));
    }

    R_SUCCEED();
}

void FsView::SetFs(const fs::FsPath& new_path, const FsEntry& new_entry) {
    if (m_fs && m_fs_entry.root == new_entry.root && m_fs_entry.type == new_entry.type) {
        log_write("same fs, ignoring\n");
        return;
    }

    // m_fs.reset();
    m_path = new_path;
    m_entries.clear();
    m_entries_index.clear();
    m_entries_index_hidden.clear();
    m_entries_index_search.clear();
    m_entries_current = {};
    m_previous_highlighted_file.clear();
    m_fs_entry = new_entry;

    switch (new_entry.type) {
         case FsType::Sd:
            m_fs = std::make_unique<fs::FsNativeSd>(m_menu->m_ignore_read_only.Get());
            break;
        case FsType::Stdio:
            m_fs = std::make_unique<fs::FsStdio>(true, new_entry.root);
            break;
    }

    if (HasFocus()) {
        if (m_path.empty()) {
            Scan(m_fs->Root());
        } else {
            Scan(m_path);
        }
    }
}

void FsView::DisplayOptions() {
    auto options = std::make_unique<Sidebar>("File Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    SidebarEntryArray::Items sort_items;
    sort_items.push_back("Size"_i18n);
    sort_items.push_back("Alphabetical"_i18n);

    SidebarEntryArray::Items order_items;
    order_items.push_back("Descending"_i18n);
    order_items.push_back("Ascending"_i18n);

    options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this](s64& index_out){
        m_menu->m_sort.Set(index_out);
        SortAndFindLastFile();
    }, m_menu->m_sort.Get());

    options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this](s64& index_out){
        m_menu->m_order.Set(index_out);
        SortAndFindLastFile();
    }, m_menu->m_order.Get());

    options->Add<SidebarEntryBool>("Show Hidden"_i18n, m_menu->m_show_hidden.Get(), [this](bool& v_out){
        m_menu->m_show_hidden.Set(v_out);
        SortAndFindLastFile();
    });

    options->Add<SidebarEntryBool>("Folders First"_i18n, m_menu->m_folders_first.Get(), [this](bool& v_out){
        m_menu->m_folders_first.Set(v_out);
        SortAndFindLastFile();
    });

    options->Add<SidebarEntryBool>("Hidden Last"_i18n, m_menu->m_hidden_last.Get(), [this](bool& v_out){
        m_menu->m_hidden_last.Set(v_out);
        SortAndFindLastFile();
    });
}

Menu::Menu(u32 flags) : MenuBase{"Rom Loader"_i18n, flags} {
    if (!IsTab()) {
        SetAction(Button::SELECT, Action{"Close"_i18n, [this](){
            PromptIfShouldExit();
        }});
    }

    view = std::make_unique<FsView>(this, ViewSide::Left);
    ueventCreate(&g_change_uevent, true);
}

Menu::~Menu() {
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (R_SUCCEEDED(waitSingle(waiterForUEvent(&g_change_uevent), 0))) {
        view->SortAndFindLastFile(true);
    }

    // workaround the buttons not being display properly.
    // basically, inherit all actions from the view, draw them,
    // then restore state after.
    const auto view_actions = view->GetActions();
    m_actions.insert_range(view_actions);
    ON_SCOPE_EXIT(RemoveActions(view_actions));

    MenuBase::Update(controller, touch);
    view->Update(controller, touch);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    // see Menu::Update().
    const auto view_actions = view->GetActions();
    m_actions.insert_range(view_actions);
    ON_SCOPE_EXIT(RemoveActions(view_actions));

    MenuBase::Draw(vg, theme);
    view->Draw(vg, theme);
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    view->OnFocusGained();
}

void Menu::UpdateSubheading() {
    const auto index = view->m_entries_current.empty() ? 0 : view->m_index + 1;
    this->SetSubHeading(std::to_string(index) + " / " + std::to_string(view->m_entries_current.size()));
}

void Menu::RefreshViews() {
    view->Scan(view->m_path);
}

void Menu::PromptIfShouldExit() {
    if (IsTab()) {
        return;
    }

    App::Push<ui::OptionBox>(
        "Close FileBrowser?"_i18n,
        "No"_i18n, "Yes"_i18n, 1, [this](auto op_index){
            if (op_index && *op_index) {
                SetPop();
            }
        }
    );
}

} // namespace sphaira::ui::menu::filebrowser
