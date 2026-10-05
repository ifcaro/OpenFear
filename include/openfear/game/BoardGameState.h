#pragma once

#include <cstdint>

namespace openfear::game {

// BoardGameScreen's state machine. Kept standalone, NOT nested inside
// BoardGameScreen, to break a circular include: BoardGameScreen.h includes
// every minigame header (DuelAttackGauge.h, SlotMachineGame.h, etc.) to declare
// them as members, and each of those headers needs to name this type too —
// if it lived inside BoardGameScreen, they'd have to include the whole of
// BoardGameScreen.h just to see it, which loops straight back to them.
// Values are this port's own sequential assignment (plain auto-increment
// from 0) — the original game is Win16 message-driven, with no equivalent
// single state-id table.
enum class BoardGameState : uint8_t {
    // Character portrait at turn start (INFO{n}_0.BMP + theme audio).
    ShowingPortrait,
    // Waiting for player to roll (brief pause).
    WaitingForPlayer,
    // Dice animation.
    RollingDice,
    // Player moving on board.
    Moving,
    // Encounter/event triggered.
    Encounter,
    // Turn ending, advance to next player.
    TurnEnd,
    // Game finished.
    GameOver,
    // AVI plays then player teleports to teleport_.dest_stone.
    TeleportAVI,
    // Key-stone AVI (AV3/KEY{province} or AV3/NOKEY{province}) or
    // black-hole stone AVI (AV1/BHOLE{province}) — always replaces the
    // normal enc type 1-16 roll for this landing.
    StoneArrivalAVI,
    // GKLOOP{1,2,3}.AVI loops while EYELOOP insult audio plays (enc types
    // 1,2,3,4,5,6,15,16).
    GkLoopAVI,
    // One-die roll game (enc type 9).
    OneDieGame,
    // KeyAward (enc type 15): single die roll against a precomputed 1-6
    // target, entered via a GkLoopAVI intro.
    KeyAwardGame,
    // Number-guessing 1-10 vs a secret target 1-8 (enc type 8).
    DiceKeysGame,
    // Click a door before the timer runs out (enc type 7).
    DoorsGame,
    // Dealer-first blackjack (enc type 13).
    BlackjackGame,
    // One real rendered frame of black before ShowingPortrait or
    // GkLoopAVI. Normal teleports use TeleportVideoPreload instead, not this.
    BlackFlash,
    // Province minimap overlay, opened by clicking the step counter box
    // during Moving.
    MinimapScreen,
    // Board entry video (AV2/HOME{char_id}.AVI), once per player on their
    // first turn, before the dice roll.
    HomeAVI,
    // Turn timer expired: AT_0063.WAV playing, then TurnEnd.
    TurnTimeoutWait,
    // Binary coffin choice: known (+10 power) vs closed coffin (enc type 11).
    CoffinChoiceGame,
    // Pay 10 power to keep a held key, or let a 3s grace timer expire and
    // lose it (enc type 12).
    BribeGame,
    // 3-reel slot machine (enc type 14): 3-match on own character -> +50
    // power (jackpot); any other 3-match -> +20 power; no match -> -5 power.
    SlotMachineGame,
    // Fortune door (enc type 10): door bitmap + OK/X choice. X -> taunt
    // audio. OK -> door creak, then 50/50 between ResolveFortuneCard and
    // ResolveFateCard (the same 2 resolvers shared stone-type-3/4 landings
    // use).
    FortuneDoorGame,
    // Duel trigger: portrait bottom-right-corner click, gated on
    // total_players_ > 1 (2-6P, or 1P with AI opponents filled in by
    // difficulty) -> interactive 6-portrait target picker.
    DuelChooseOpponent,
    // Duel gauge battle: both players fill power bars; higher power wins.
    // Trigger: a target was picked in DuelChooseOpponent's interactive
    // picker, or named automatically by the Protection effect — both enter
    // this same state (Protection just shows the gauges broken/empty).
    DuelAttackGauge,
    // Post-gauge winner choice: steal a stealable key (zones 0-5) or curse
    // the loser (zone 6, always available).
    DuelStealOrCurse,
    // Modal pause menu: Continue resumes + compensates game_duration_s_ for
    // elapsed pause time, Exit quits the app directly (no credits).
    // Trigger: portrait bottom-left-corner click, own turn only.
    PauseScreen,
    // Flight Key teleport: province map + 6 clickable province zones.
    // Trigger: any F-key, mid-roll (steps_remaining>0), standing on a
    // type-6 stone (each province's single central stone, always stone
    // 0), key_power_flags bit2 set — NOT triggered by landing. Picking
    // a province teleports to that province's stone 0, plays
    // FLIGHT{n}.AVI, then the turn ends.
    FlightMap,
    // The Gatekeeper's periodic "cure your curse" offer (human only — AI
    // auto-cures free, no UI). Accept pays 25% power to clear the curse;
    // declining or letting the window expire leaves the curse active until
    // the Gatekeeper's next offer.
    CureCurseGame,
    // Port-only (no ASM equivalent): keeps the board on screen through
    // TeleportAVI's blocking video-open call, so the decoder's blank
    // first frame never flashes against the still-bright board.
    TeleportVideoPreload
};

}  // namespace openfear::game
