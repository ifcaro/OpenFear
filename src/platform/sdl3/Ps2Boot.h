#pragma once
#include "openfear/platform/Platform.h"

namespace openfear::ps2 {

// IOP reset only (SifIopReset + the re-sync/patch calls after it) - split
// from LoadDriversAndFilesystems() so a live debug session, where
// resetting the IOP kills the debug link, can skip just this call.
// RuntimeSetup_ps2.cpp calls both (in full) via platform_bootstrap(), then
// window/audio/input init via create_platform()->init().
void ResetIop();

// Loads the filesystem IOP modules and brings up CD/USB filesystem layers
// ("cdfs:"/"mass:") onto an already-reset IOP (ResetIop() first). Must run
// before AssetLocator::discover()/NativeAssetProvider touch those paths.
// Never touches the pad driver - only init_joystick_driver() (inside
// SDL_Init(SDL_INIT_GAMEPAD)) may; a 2nd touch anywhere leaves SDL finding
// 0 joysticks.
void LoadDriversAndFilesystems();

// Shows "assets not found, insert the CD / plug in a USB stick" using
// BitmapFont (bin2c-embedded, renders with no game asset loaded). Blocks
// until confirm (returns true, caller should retry) or a quit request
// (returns false, caller should give up).
bool PromptAssetsMissing(Platform& platform);

}  // namespace openfear::ps2
