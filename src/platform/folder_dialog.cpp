#include "platform/folder_dialog.h"

#include <SDL3/SDL_dialog.h>

#include "platform/open_folder_dialog.h"
#include "platform/window.h"

namespace hailstone::platform {

struct FolderDialog::Shared {
    std::mutex mutex;
    bool open = false;
    std::optional<std::filesystem::path> chosen;
    std::string start; // kept alive while the dialog is up
};

namespace {

// SDL's callback: the list is null on an error, empty on a cancel.
void SDLCALL answered(void* userdata, const char* const* list, int) {
    auto* holder = static_cast<std::shared_ptr<FolderDialog::Shared>*>(userdata);
    {
        std::lock_guard lock((*holder)->mutex);
        (*holder)->open = false;
        if (list && list[0]) (*holder)->chosen = std::filesystem::path(reinterpret_cast<const char8_t*>(list[0]));
    }
    delete holder;
}

} // namespace

FolderDialog::FolderDialog() : shared_(std::make_shared<Shared>()) {}
FolderDialog::~FolderDialog() = default;

void FolderDialog::open(Window& window, const std::filesystem::path& start) {
    {
        std::lock_guard lock(shared_->mutex);
        if (shared_->open) return;
        shared_->open = true;
        auto u8 = start.u8string();
        shared_->start.assign(u8.begin(), u8.end());
    }
    show_open_folder_dialog(&answered, new std::shared_ptr<Shared>(shared_), window.sdl_window(),
                            shared_->start.empty() ? nullptr : shared_->start.c_str());
}

bool FolderDialog::is_open() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->open;
}

std::optional<std::filesystem::path> FolderDialog::poll() {
    std::lock_guard lock(shared_->mutex);
    return std::exchange(shared_->chosen, std::nullopt);
}

} // namespace hailstone::platform
