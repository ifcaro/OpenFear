#pragma once

#include <optional>

namespace openfear {

// Handles `openfear --install [path]` to completion (own Platform
// bring-up, on-screen progress, exit code) if that flag is present;
// returns nullopt immediately, touching nothing, if it isn't - the caller
// then proceeds with the normal play flow. Native-only - needs writable
// storage distinct from the asset source, which isn't wired up on every
// target (see apps/openfear/CMakeLists.txt).
//
// Reorganizes the discovered CD/ISO source into the installed layout the
// game reads from (see InstallFlow.cpp) into `path` if given, otherwise
// an "assets" folder under the current working directory (or, if that's
// not writable, the same per-user config directory openfear.cfg lives
// in). Fails if the discovered source isn't a CD/ISO.
std::optional<int> RunInstallIfRequested(int argc, char* argv[]);

}  // namespace openfear
