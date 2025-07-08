#include <switch.h>
#include <memory>
#include "app.hpp"
#include "log.hpp"

#include <arpa/inet.h>
#include <switch/runtime/nxlink.h>

int main(int argc, char** argv) {
    if (!argc || !argv) {
        return 1;
    }

    auto app = std::make_unique<sphaira::App>(argc, argv);
    app->Loop();
    return 0;
}

extern "C" {

void userAppInit(void) {
    sphaira::App::SetBoostMode(true);

    Result rc;
    if (R_FAILED(rc = appletLockExit()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = plInitialize(PlServiceType_User)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = psmInitialize()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = setInitialize()))
        diagAbortWithResult(rc);

    // only init if nxlink addr is set.
    if (__nxlink_host.s_addr) {
        if (R_SUCCEEDED(rc = socketInitializeDefault()))
            log_nxlink_init();
    }

    // it doesn't matter if this fails.
    appletSetScreenShotPermission(AppletScreenShotPermission_Enable);
}

void userAppExit(void) {
    if (__nxlink_host.s_addr) {
        socketExit();
        log_nxlink_exit();
    }

    setExit();
    psmExit();
    plExit();

    // NOTE (DMC): prevents exfat corruption.
    if (auto fs = fsdevGetDeviceFileSystem("sdmc:")) {
        fsFsCommit(fs);
    }

    sphaira::App::SetBoostMode(false);
    appletUnlockExit();
}

} // extern "C"
