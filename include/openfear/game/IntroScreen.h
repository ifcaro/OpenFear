#pragma once

#include "openfear/game/GameScreen.h"
#include "openfear/platform/IVideoPlayer.h"
#include "openfear/platform/IAudio.h"

namespace openfear::game {

// Intro video screen. Plays INTRO.AVI at its native size, centered,
// advances on click or video finish.
class IntroScreen : public GameScreen {
public:
    IntroScreen(assets::AssetManager& assets, IRenderer& renderer, IAudio& audio,
                IVideoPlayer& video)
        : GameScreen(assets, renderer, audio), video_(video) {}

    bool load(const AssetPaths& paths);
    bool update(const IInput& input);
    void render();
    void unload();

private:
    IVideoPlayer& video_;
    bool video_loaded_ = false;
};

}  // namespace openfear::game
