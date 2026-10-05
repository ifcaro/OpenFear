#include "Ps2Boot.h"
#include "openfear/assets/Ps2AssetProvider.h"
#include "openfear/platform/BitmapFont.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <debug.h>
#include <delaythread.h>
#include <ee_debug.h>
#include <gsKit.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <libcdvd.h>
#include <audsrv.h>
#include <libmtap.h>
#include <libpad.h>
// fileXio_rpc.h refuses to compile without this (#error otherwise) - an
// acknowledgment flag that direct fileXio/fio calls are intentional here,
// not a behavior toggle.
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

// bin2c-embedded IOP modules (cmake/OpenFearPS2.cmake's
// openfear_embed_ps2_irx_modules()) - no filesystem exists yet at boot to
// SifLoadModule(path) them from, so they travel in the ELF's own data and
// get pushed to the IOP via SifExecModuleBuffer() instead. iomanX+fileXio
// (base IOP RPC filesystem layer), bdm+bdmfs_fatfs+usbd+usbmass_bd (USB mass
// storage, exposes "mass:"). NOT cdfs (real CD) - the real CD is read
// entirely via raw sceCdRead() (EE-side libcdvd, needs only sceCdInit()
// below); cdfs.irx's own "cdfs:" filesystem driver is never used for a
// real read, so it isn't loaded.
extern unsigned char iomanX_irx[];
extern unsigned int size_iomanX_irx;
extern unsigned char fileXio_irx[];
extern unsigned int size_fileXio_irx;
extern unsigned char bdm_irx[];
extern unsigned int size_bdm_irx;
extern unsigned char bdmfs_fatfs_irx[];
extern unsigned int size_bdmfs_fatfs_irx;
extern unsigned char usbd_irx[];
extern unsigned int size_usbd_irx;
extern unsigned char usbmass_bd_irx[];
extern unsigned int size_usbmass_bd_irx;

// Same technique, for the audio (LIBSD+AUDSRV) and pad (SIO2MAN+MTAPMAN+
// PADMAN) stacks - see init_audio_driver()/init_joystick_driver() below for
// why this project loads them itself instead of linking a third-party
// wrapper for it.
extern unsigned char libsd_irx[];
extern unsigned int size_libsd_irx;
extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;
extern unsigned char sio2man_irx[];
extern unsigned int size_sio2man_irx;
extern unsigned char mtapman_irx[];
extern unsigned int size_mtapman_irx;
extern unsigned char padman_irx[];
extern unsigned int size_padman_irx;

namespace openfear::ps2 {

namespace {

// Prints EE exception state via stdout, then halts instead of
// resetting/hanging silently. excode: 1=TLB modification, 2/3=TLB load/
// store miss, 4/5=address error load/store, 6/7=bus error instr/data,
// 10=reserved instruction, 11=coprocessor unusable, 12=overflow, 13=trap.
//
// Also prints via scr_printf() (init_scr(), independent of gsKit/SDL —
// see BitmapFont.h) since stdout alone is invisible on a standalone
// USB/mass: boot. Can render garbled if gsKit/the real renderer already
// owns the GS by the time the crash happens, but still strictly more
// information than nothing.
int ExceptionHandler(EE_RegFrame* frame) {
    const int excode = M_EE_GET_CAUSE_EXCODE(frame->cause);
    std::printf("[EE EXCEPTION] excode=%d epc=%08x badvaddr=%08x ra=%08x sp=%08x\n", excode,
                static_cast<unsigned int>(frame->epc), static_cast<unsigned int>(frame->badvaddr),
                static_cast<unsigned int>(frame->ra[0]), static_cast<unsigned int>(frame->sp[0]));
    std::fflush(stdout);

    init_scr();
    scr_printf("\n[EE EXCEPTION]\n");
    scr_printf("excode=%d\n", excode);
    scr_printf("epc     =%08x\n", static_cast<unsigned int>(frame->epc));
    scr_printf("badvaddr=%08x\n", static_cast<unsigned int>(frame->badvaddr));
    scr_printf("ra      =%08x\n", static_cast<unsigned int>(frame->ra[0]));
    scr_printf("sp      =%08x\n", static_cast<unsigned int>(frame->sp[0]));
    unsigned int trace[8] = {};
    ps2GetStackTrace(trace, 8);
    for (int i = 0; i < 8 && trace[i]; ++i) {
        scr_printf("  #%d %08x\n", i, trace[i]);
    }

    for (;;) {}
    return 0;
}

void InstallExceptionHandler() {
    ee_dbg_install(1);
    static const int kCauses[] = {1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13};
    for (int cause : kCauses) ee_dbg_set_level1_handler(cause, ExceptionHandler);
}

void Trace(const char* msg) {
    std::printf("[Ps2Boot] %s\n", msg);
    std::fflush(stdout);
}

// Loads an embedded IOP module from RAM (SifExecModuleBuffer, not
// SifLoadModule - there's no filesystem yet to load a path from).
bool ExecEmbeddedModule(const char* name, unsigned char* image, unsigned int size) {
    char msg[96];
    std::snprintf(msg, sizeof(msg), "loading %s (%u bytes)...", name, size);
    Trace(msg);
    int ret = 0;
    int id = SifExecModuleBuffer(image, size, 0, nullptr, &ret);
    bool ok = id >= 0 && ret == 0;
    std::snprintf(msg, sizeof(msg), "%s: id=%d ret=%d ok=%d", name, id, ret, ok);
    Trace(msg);
    return ok;
}

}  // namespace

// SDL3's own PS2 audio/joystick backends (SDL_ps2audio.c, SDL_sysjoystick.c)
// call these 4 functions by name. Defined here against plain PS2SDK/
// audsrv/libpad/libmtap calls, instead of linking the third-party wrapper
// library (ps2_drivers, GPLv3) that otherwise provides them - keeps
// nothing outside this project's own MIT-licensed source in the final
// binary. extern "C": SDL's call sites are compiled as C, so these need
// unmangled names regardless of this file being C++.
extern "C" int init_audio_driver() {
    if (!ExecEmbeddedModule("libsd", libsd_irx, size_libsd_irx)) return -1;
    if (!ExecEmbeddedModule("audsrv", audsrv_irx, size_audsrv_irx)) return -1;
    return audsrv_init() == 0 ? 0 : -1;
}

extern "C" void deinit_audio_driver() {
    audsrv_quit();
}

// SDL always calls this with init_dependencies=true (SIO2MAN is always
// loaded alongside MTAPMAN/PADMAN here) - no caller ever passes false.
extern "C" int init_joystick_driver(bool) {
    if (!ExecEmbeddedModule("sio2man", sio2man_irx, size_sio2man_irx)) return -1;
    if (!ExecEmbeddedModule("mtapman", mtapman_irx, size_mtapman_irx)) return -1;
    if (!ExecEmbeddedModule("padman", padman_irx, size_padman_irx)) return -1;
    if (mtapInit() != 1) return -1;
    if (padInit(0) != 1) return -1;
    return 0;
}

extern "C" void deinit_joystick_driver(bool) {
    padEnd();
}

void ResetIop() {
    InstallExceptionHandler();
    Trace("enter ResetIop()");

    Trace("SifInitRpc");
    SifInitRpc(0);
    Trace("SifIopReset");
    while (!SifIopReset(nullptr, 0)) {}
    Trace("SifIopSync");
    while (!SifIopSync()) {}
    Trace("SifInitRpc (again)");
    SifInitRpc(0);
    Trace("sbv_patch_enable_lmb");
    sbv_patch_enable_lmb();
    Trace("sbv_patch_disable_prefix_check");
    sbv_patch_disable_prefix_check();

    Trace("ResetIop() done");
}

void LoadDriversAndFilesystems() {
    Trace("enter LoadDriversAndFilesystems()");

    // Base IOP filesystem RPC layer - everything below (cdfs, USB mass
    // storage) registers itself against this: iomanX then fileXio, before
    // anything else.
    ExecEmbeddedModule("iomanX", iomanX_irx, size_iomanX_irx);
    ExecEmbeddedModule("fileXio", fileXio_irx, size_fileXio_irx);

    // sio2man/mtapman/padman stay untouched here - only
    // init_joystick_driver() (below), triggered by SDL_Init(SDL_INIT_GAMEPAD),
    // may touch the pad driver, exactly once per process. Any 2nd touch
    // (reloading sio2man, re-running init_joystick_driver(), even a bare
    // padPortOpen()/padRead()) reliably breaks the SIO2 interface or
    // SDL's later port-scan.

    // sceCdInit() establishes the EE-side libcdvd RPC connection to
    // cdvdman - all Iso9660Reader/Ps2CdSectorSource reads need this (raw
    // sceCdRead()). cdfs.irx stays unloaded here too, same reason as the
    // extern-decls comment above. Must run BEFORE the USB block: if
    // cdvdman starts after usbd/usbmass_bd, this RPC call hangs - a real
    // ordering dependency, not a timing race.
    Trace("sceCdInit");
    sceCdInit(SCECdINoD);

    // Settles the drive into a known ready/not-ready state before the
    // first raw sceCdRead(). mode 1 (non-blocking check), not mode 0 —
    // mode 0 blocks until the drive reports ready, which never happens on
    // a USB/mass:-only boot with no disc at all.
    Trace("sceCdDiskReady");
    sceCdDiskReady(1);

    // USB mass storage ("mass:"). bdm (generic block-device layer) first
    // - bdmfs_fatfs (FAT filesystem) only needs bdm, so it can load right
    // after. usbd (USB core) is independent of both. usbmass_bd loads
    // last - it needs bdm AND usbd already up.
    ExecEmbeddedModule("bdm", bdm_irx, size_bdm_irx);
    ExecEmbeddedModule("bdmfs_fatfs", bdmfs_fatfs_irx, size_bdmfs_fatfs_irx);
    ExecEmbeddedModule("usbd", usbd_irx, size_usbd_irx);
    ExecEmbeddedModule("usbmass_bd", usbmass_bd_irx, size_usbmass_bd_irx);

    // fileXioInit() binds the EE-side RPC client to fileXio.irx - used by
    // the USB mass storage ("mass:") path above.
    Trace("fileXioInit");
    fileXioInit();

    Trace("LoadDriversAndFilesystems() done");
}

namespace {
// Static/persistent across retries - this function can loop (retry ->
// still missing -> shown again), so the font loads once, not per retry.
platform::BitmapFont g_prompt_font;
bool g_prompt_font_loaded = false;

void DrawPromptScreen(Platform& platform, const char* const* lines, int line_count) {
    platform.renderer->beginFrame();
    platform.renderer->clear(0, 0, 0);
    if (g_prompt_font_loaded) {
        int y = 60;
        for (int i = 0; i < line_count; ++i) {
            g_prompt_font.DrawText(*platform.renderer, 60, y, lines[i], 255, 255, 255);
            y += platform::BitmapFont::kGlyphHeight + 6;
        }
    }
    platform.renderer->endFrame();
}
}  // namespace

bool PromptAssetsMissing(Platform& platform) {
    Trace("enter PromptAssetsMissing()");

    if (!g_prompt_font_loaded) {
        g_prompt_font_loaded = g_prompt_font.Load(*platform.renderer);
        if (!g_prompt_font_loaded)
            Trace("WARNING: basic font failed to load, screen will be blank");
    }

    // Kept under ~35 chars/line - comfortably inside the real budget
    // (kGlyphWidth=8 + kCharSpacing=2 per char, (640-60)/10 ≈ 58 columns
    // from x=60 on a 640px screen).
    // clang-format off
    static const char* const kMissingLinesBase[] = {
        "Atmosfear assets not found.",
        "",
        "Insert the original game CD, or",
        "connect a USB stick with the", "game data.",
        "",
        "Press X to check again.",
    };
    // clang-format on
    static const char* const kCheckingLines[] = {"Checking..."};

    // Diagnostic: shows why AssetLocator's real-CD scan failed, split
    // across 2 lines of up to 39 chars each (40-byte buffers) -
    // comfortably inside the real per-line budget.
    char diag_line1[40] = "";
    char diag_line2[40] = "";
    const char* diag = Ps2LastCdScanDiagnostic();
    if (diag[0] != '\0') {
        std::snprintf(diag_line1, sizeof(diag_line1), "Diag: %.34s", diag);
        const size_t len = std::strlen(diag);
        if (len > 34) std::snprintf(diag_line2, sizeof(diag_line2), "%.39s", diag + 34);
    }
    // clang-format off
    const char* kMissingLines[] = {
        kMissingLinesBase[0], kMissingLinesBase[1], kMissingLinesBase[2], kMissingLinesBase[3],
        kMissingLinesBase[4], kMissingLinesBase[5], kMissingLinesBase[6],
        "",
        diag_line1, diag_line2,
    };
    // clang-format on

    // Trigger on release, not on "is it down right now" — this loop polls
    // every iteration, so level-triggering would re-fire the retry
    // continuously while the button is held.
    bool was_held = false;
    for (;;) {
        if (!platform.poll()) return false;  // quit requested - give up cleanly, not a retry
        // There's no mouse on PS2 - force the fallback cursor off
        // unconditionally instead of waiting for gamepad activity to hide it.
        platform.cursor->SetVisible(false);
        const bool held_now = platform.input->gamepad().held[static_cast<size_t>(GamepadButton::A)];
        if (was_held && !held_now) {
            // The retry below can take a moment - without feedback the
            // screen looks frozen right after releasing X.
            DrawPromptScreen(platform, kCheckingLines, 1);

            // mode 1 (non-blocking), not 0 (block until ready) - this
            // retry also covers a missing USB stick, where there may
            // never be a physical disc to become ready.
            sceCdDiskReady(1);
            Trace("confirm released, retrying discovery");
            return true;
        }
        was_held = held_now;
        DrawPromptScreen(platform, kMissingLines,
                         static_cast<int>(sizeof(kMissingLines) / sizeof(kMissingLines[0])));
    }
}

}  // namespace openfear::ps2
