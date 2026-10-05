# Manual playtest plan

Checklist for the full interactive playtest before publishing. Each block has: what to check,
the exact command to get there fast (`--test-*`/`--cheat-*` flags, **Debug build only** — see
`apps/openfear/DebugQuickstart.cpp`), and what to look at.

All flags go after the executable: `build\bin\Debug\openfear.exe --test-encounter 7`. Several
can be combined at once unless noted otherwise. `--test-players N` adds N-1 filler AI players
where applicable.

## 0. Real flow, no flags (clean boot)

- [x] `openfear.exe` with no arguments → Intro → PickPlayers → PickChar/FearSelect per player →
  (1P) ChooseLevel → MasterControl → (1P) AIAssign → board. No skips, no black screens.
- [x] With 2+ players: PickChar/FearSelect repeats per player, ChooseLevel does NOT appear.
- [x] `--test-char N` (N=0-5) during Screen 4 forces that character if free — test with 2+
  players to confirm the 2nd can't repeat the same one.

## 1. Board — movement and special tiles

- [x] `--test-board` — clean match, real dice roll, click-based movement, junction turns
  (90°/180°/360°), step counter query (province map).
- [x] `--test-stone 0` — normal path stone, triggers a random encounter (normal roll).
- [x] `--test-stone 1` — key stone: grants the province's key (or NOKEY if the pool is
  exhausted — combine with `--test-no-keys-left`).
- [x] `--test-stone 1 --test-no-keys-left` — key pool exhausted, NOKEY instead of KEY, no key
  granted.
- [x] `--test-stone 2` — Black Hole: AVI + up to 4 lost turns (`retry_flag==4` is the real hard
  cap, `ComputeBlackHoleResolves`), then EYELOOP release.
- [x] Repeat with `--test-duel-keys 3f` (all keys) to confirm immunity if the character has it.
- [x] `--test-stone 3 --test-stone-repeat 5` — Crypt of Fate, 5 resolutions in a row, paced one
  per card's own result audio — each of the 5 variants (power penalty, lost turn, teleport to
  black hole) should show/play distinctly, not stacked.
- [x] `--test-stone 4 --test-stone-repeat 5` — Tomb of Fortune, same pacing, 5 prize variants
  shown/played distinctly.
- [x] `--test-dice-restricted` — confirms the game respects a forced single-die roll.
- [x] `--test-toboggan` — Hellin's Toboggan (province 5, stone 63): landing refuses rotate
  (straight section + step already spent), matching the original.
- [x] `--test-gate-arrival 0` through `23` — spot-check the 24 real gates (crossing direction,
  correct destination). Sampling ~6 at random is enough if trusted from a prior full pass.
- [x] `--test-tunnel-entry 0` through `5` — tunnel entry per province. Province 3's stone 0 has
  3 wall directions (E/S/W); the tunnel is direction-agnostic there, matching the original.
- [x] `--test-tunnel-exit 0` through `5` — tunnel exit (counter-slab) per province.
- [x] `--test-flight` — Flight power, function key (F1-F6) from a type-6 stone mid-roll.
- [x] `--test-chosen-one` — double-6 forces "The Chosen One"; a 1-die pick does not
  consume/trigger the force (by design — only applies inside the 2-dice branch).
- [x] `--test-portrait 0` through `5` — visual check of each character's key strip icon color,
  all 6 (Gevaudan/Khufu/Baron Samedi/Bathory/Anne de Chantraine/Hellin).

## 2. The 16 encounter types (`--test-encounter N`)

Each one with `--test-encounter N --test-players 2 --test-no-ai-duel` (2 players so the ones
that need active opponents actually work; `--test-no-ai-duel` stops an AI-initiated duel from
interrupting the encounter test). Verify: correct audio, correct video/AVI if applicable,
effect on `render_state` (power, keys, restrictions), and that the turn ends cleanly.

- [x] `1` — insult with no effect (audio only, no state change).
- [x] `2` — loses the next turn (audio + `skip_turn_counter`, no minigame screen — not to be
  confused with the real OneDie minigame, enc_type 9).
- [x] `3` — GateOpen: teleport to the nearest black hole.
- [x] `4` — restricted to 1 die.
- [x] `5` — Roll a Six (even-die restriction until a 6 is rolled). Use
  `--test-encounter-once` so the restriction is observed across several rolls.
- [x] `6` — extra turn (player rotation). Use `--test-encounter-once` to confirm play passes
  to the next player right after.
- [x] `7` — Doors: the three door types are visual variants, one representative is enough.
  Timeout deducts power; clicking a door opens it and grants power.
- [x] `8` — DiceKeys: choose a displayed number against a hidden target (1-8); the prize is
  selected before the guess, which can hit or miss.
- [x] `9` — OneDie minigame: with 2+ players it opens and resolves; with 1 player it's skipped
  and the turn ends normally.
- [x] `10` — FortuneDoor: closing (X) leaves no result plate; opening (OK or the 4s timeout)
  shows a result plate.
- [x] `11` — CoffinChoice: the selected outcome affects the player's next turn.
- [x] `12` — Bribe: **needs at least 1 key and power≥10** — use `--test-power 20
  --test-duel-keys 01`. Accepting spends 10 power and keeps the key.
- [x] `13` — Blackjack: deal, hit/stand, natural 21, bust, bet resolution.
- [x] `14` — SlotMachine: low power (e.g. 10) can produce a jackpot with the player's own face;
  high power (e.g. 50) more often loses.
- [x] `15` — KeyAward: without keys the encounter can resolve as a loss; with all 6 keys it's
  skipped and the turn continues.
- [x] `16` — ambient EYELOOP during the last 5 minutes. It's a fallback when a normal encounter
  roll produces no encounter, so another encounter may happen first. Use `--test-duration 4
  --test-duel-keys 07` to start in this window with 3 keys — below 3 keys (or power below 10)
  the last-10-minute weighting forces a minigame category instead. Do not force type 16 directly.

## 3. Duel (Portrait screen's invisible corner, own turn)

- [x] `--test-duel 3` — 3 all-human players, hotseat. Click the corner (533,365)-(639,479) →
  opponent picker → attack/defense gauge → result (steal a key or curse).
- [x] `--test-duel 2` — minimum player count: with 2 players the duel corner opens the opponent
  picker.
- [x] `--test-board` with 1 player — the duel corner does not open a picker or consume the turn.
- [x] `--test-duel-ai 3` — player 0 human, rest AI. Try initiating a duel against AI AND wait
  for AI turns to see if AI initiates a duel against the player.
- [x] `--test-duel-keys 3f,00,01` — one player with all 6 keys, another with none, another with
  just one — verify which keys show up as stealable on the rack after winning.
- [x] `--test-duel-counter` — forces "Protection": the duel resolves automatically with
  reversed roles (no interactive gauge), in either direction (human attacks AI, AI attacks
  human).
- [x] Click the opponent picker's blank window (cancel) — returns to the Portrait screen
  without spending the turn.
- [x] Win a duel: confirm both options (steal a key from the rack / cast a curse) work and
  consume the turn correctly.

## 4. Curses (6 modes)

`--test-curse-mode N` (1-6) `--test-curse-victim P` (defaults to player 0; use P>0 if the mode
matches the attacking character — mode 1 is cast BY Gevaudan/char_id 0, so the quickstart
auto-bumps player 0's char_id to 1 in that case to avoid an impossible self-curse).

- [x] `1` — Gevaudan: portrait degrades progressively over several turns, controls disappear.
- [x] `2` — Anne de Chantraine (toad): only 1 step per roll, regardless of the die result.
- [x] `3` — Khufu: blindness on one map wall.
- [x] `4` — Baron Samedi: slow/painful movement.
- [x] `5` — Hellin: Portrait screen shattered, no duels or power control. One-time curse audio
  fires only at step 0, cure offer appears exactly at step 6, curse clears on accept.
- [x] `6` — Bathory: keys won by the victim go to whoever has char_id 3; redirect WAV starts
  after the key AVI ends, and its final frame remains visible until the WAV finishes.
- [x] Bathory key gain via KeyAward (15):
  `--test-encounter 15 --test-curse-mode 6 --test-players 4 --test-no-ai-duel`; win the key
  prize and observe the redirect WAV relative to the result audio.
- [x] Bathory key gain via DiceKeys (8):
  `--test-encounter 8 --test-curse-mode 6 --test-players 4 --test-no-ai-duel`; repeat until the
  prize is a key and win it.
- [x] Bathory key gain via CoffinChoice (11):
  `--test-encounter 11 --test-curse-mode 6 --test-players 4 --test-no-ai-duel`; select a key
  reward if offered.
- [x] Bathory key gain via Blackjack (13):
  `--test-encounter 13 --test-curse-mode 6 --test-players 4 --test-no-ai-duel --test-duel-keys
  01`; wager a key and win.
- [x] Bathory key gain via Fortune card:
  `--test-stone 4 --test-curse-mode 6 --test-players 4 --test-no-ai-duel`; repeat until the key
  reward occurs.
- [x] Bathory key gain via duel steal:
  `--test-duel 4 --test-curse-mode 6 --test-curse-victim 0 --test-duel-keys 00,01,00,00`;
  win the duel and steal the key from player 1.
- [x] Cure offer triggers once `curse_step_counter` hits the mode's threshold (7 for Gevaudan,
  6 for the rest, 0/10 for Bathory depending on whether a key was already redirected) — tested
  via `--test-curse-mode 3` (Khufu, threshold 6): offer appeared, accepted, curse cleared.

## 5. Gatekeeper / end of match

- [x] `--test-gatekeeper` — all 6 keys already granted, real Gatekeeper roll (can win or lose):
  FEAR{roll}.AVI → ENDVID → PLAYAGN → No → credits.
- [x] `--cheat-all-keys --cheat-gatekeeper-lose`, 2 real humans through the actual menu flow
  (quickstart can't set up multiple humans here) — eliminated player doesn't end the match for
  the others when no AI is present.
- [x] PLAYAGN: click Yes → taunt eyeloop (GKLOOP1) then restart. Click No → credits roll.
- [x] Credits: single click advances one slide, double-click skips further, auto-advance by
  timeout — all via `--test-gatekeeper-lose`.

## 6. Pause and quit

Reached via `--test-board` (any normal match works).

- [x] Click the Portrait screen's bottom-left corner (pause zone, own turn) → Pause/Quit menu.
- [x] Quit from Pause → the app closes directly, no ENDVID/credits.
- [x] Resume from Pause → turn continues with no lost game time regardless of pause length
  (`game_duration_s_ += paused_s`). ~1s overhead remains per resume from the portrait theme WAV
  decode — see `handoff.md` for the fix and the residual gap vs the original.

## 7. Gamepad (if one is connected)

Reached via the real flow (`openfear.exe`, no flags) to cover setup screens too.

- [x] D-pad navigation with discrete focus on every selection screen (setup, minigames,
  PLAYAGN). Never a free analog cursor.
- [x] Y button = duel corner, Start = pause, A = confirm/advance, B = close minimap.
- [x] Switch from gamepad to mouse mid-match and back — the cursor/focus adapts without
  getting stuck halfway.

## Quick flag reference

| Flag | What it does |
|---|---|
| `--test-encounter N` (1-16) | Forces the encounter type on every landing |
| `--test-encounter-once` | The force above self-disarms after the first trigger |
| `--test-players N` | N players in the quickstart (1 human + N-1 filler AI) |
| `--test-power N` | Forces player 0's power |
| `--test-no-ai-duel` | Disables the AI-initiated duel check (isolates other tests) |
| `--test-duel-keys h,h,...` | Per-player key mask (hex, bit i = province i) |
| `--test-duel N` (2-6) | Duel quickstart, all human |
| `--test-duel-ai N` (2-6) | Duel quickstart, player 0 human + rest AI |
| `--test-duel-counter` | Forces Protection (duel auto-resolved, reversed roles) |
| `--test-stone N` (0-7) | Teleport to the first stone of that type |
| `--test-stone-repeat K` | Repeats the resolution K times (types 3/4 only) |
| `--test-dice-restricted` | Forces player 0's `dice_restricted` |
| `--test-board` | Clean quickstart, nothing forced |
| `--test-toboggan` | Direct setup of Hellin's Toboggan |
| `--test-gatekeeper` / `-lose` | Final Gatekeeper challenge, forced win/lose |
| `--test-flight` | Direct setup of the Flight power |
| `--test-chosen-one` | Forces the next double-6 |
| `--test-no-keys-left` | Empties province 0's key pool |
| `--test-gate-arrival DI` (0-23) | Spot-check a real gate by index |
| `--test-tunnel-entry/-exit P` (0-5) | Spot-check tunnel entry/exit |
| `--test-portrait N` (0-5) | Jumps straight to ShowingPortrait with all keys |
| `--test-duration N` | Match duration in minutes (to force time windows) |
| `--test-curse-mode N` (1-6) / `--test-curse-victim P` | Curse active from the start |
| `--cheat-easy-win` | Stop on any stone + infinite key pool |
| `--cheat-all-keys` | All players start with all 6 keys |
| `--cheat-gatekeeper-lose` | Forces a loss on every Gatekeeper roll |
| `--test-char N` (0-5) | Forces the character in the real flow's Screen 4 (not quickstart) |

All defined in `apps/openfear/DebugQuickstart.cpp` — if anything here doesn't match the code,
the code wins; update this document.
