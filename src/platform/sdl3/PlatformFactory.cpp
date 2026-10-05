#include "openfear/platform/Platform.h"
#include "openfear/platform/IInput.h"
#include "SDL3Renderer.h"
#include "SDL3Input.h"
#include "SDL3VideoPlayer.h"

namespace openfear {

bool Platform::init(const char* title) {
    if (!renderer->init(title)) return false;

    if (!cursor) {
        cursor = std::make_unique<platform::Cursor>();
    }
    cursor->Initialize(640, 480);
    renderer->setCursor(cursor.get());

    // Cursor sprite bitmaps load separately, once AssetManager is ready
    // (see loadGameCursors() call site in GameFlow.cpp).

    if (!audio->init()) return false;
    if (!input->init()) return false;

    // SDL3Input needs the live SDL3Renderer (not just the raw SDL_Renderer*) to
    // convert real window pixel coordinates into the fixed 640x480 logical space.
    auto* sdl_rend = static_cast<sdl3::SDL3Renderer*>(renderer.get());
    static_cast<sdl3::SDL3Input*>(input.get())->setRenderer(sdl_rend);

    // video needs a real asset provider (SDL3VideoPlayer takes one at
    // construction) - not known yet this early, see attachAssetProvider().

    return true;
}

void Platform::attachAssetProvider(IAssetProvider* provider) {
    asset_provider = provider;
    renderer->setAssetProvider(provider);
    audio->setAssetProvider(provider);
    if (!video) {
        video =
            std::make_unique<sdl3::SDL3VideoPlayer>(renderer.get(), provider, audio->sampleRate());
        video->init();
    }
}

void Platform::shutdown() {
    cursor.reset();
    if (video) video->shutdown();
    input->shutdown();
    audio->shutdown();
    renderer->shutdown();
}

bool Platform::poll(float dt) {
    if (!input->poll()) return false;
    if (!cursor) return true;

    const auto& mouse = input->mouse();

    // Default to hidden outside mouse mode. A screen using D-pad focus-nav
    // (FocusIndicator::DrawFocusCursor) re-shows it at the focused
    // element's center in its own render(), which runs after this poll()
    // (see RunScreenLoop) and so wins for the frame. ChooseLevelScreen/
    // DuelAttackGauge never call it, so they just stay hidden.
    cursor->SetVisible(mouse.in_window && input->activeDevice() == InputDevice::Mouse);

    // Touch is synthesized as mouse motion by SDL3, so it's already
    // covered by this same call — no separate touch plumbing needed.
    if (mouse.moved) {
        cursor->Update(mouse.x, mouse.y, mouse.native_x, mouse.native_y);
    }

    return true;
}

}  // namespace openfear
