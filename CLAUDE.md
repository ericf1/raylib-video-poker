# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Jacks-or-Better video poker in a single file, [src/main.c](src/main.c), on the raylib-quickstart template. `README.md` is still the upstream template's readme, not docs for this game.

## Build

```sh
mingw32-make                       # debug_x64 (default); output in bin/Debug/
mingw32-make config=release_x64
cd build && ./premake5.exe gmake   # regenerate makefiles after editing premake5.lua
cd build && ./premake5.exe ecc     # regenerate compile_commands.json for clangd
```

No tests, no linter. VS Code's default build task chains `ecc` → `gmake` → `make`.

- **`Makefile`, `raylib.make`, `raylib-video-poker.make` are generated and gitignored.** Edit `build/premake5.lua`.
- The executable name comes from the **containing directory name** — renaming the repo folder renames the target.
- raylib is downloaded from GitHub master into `build/external/` on first premake run, not vendored.
- `src/**.c` is globbed; new files need only a `gmake` regen.
- C23, `ShadowedVariables` on. Source is warning-clean at `-Wall -Wextra -Wshadow`.

## Architecture

Single translation unit, file-static state, sectioned by comment banners (audio / game / input / drawing). Three states:

```
STATE_BET --SPACE--> STATE_DRAW --SPACE--> STATE_RESULT --SPACE--> STATE_DRAW
```

`STATE_BET` is visited only once, at startup — afterwards the loop runs RESULT → DRAW, which is why bet adjustment must stay live in `STATE_RESULT`. `HandleVolumeInput()` runs ahead of the state machine so audio is adjustable in every state; its widget rect is deliberately far from the card rects so one click can't do two things.

Invariants worth preserving:

- **`deckTop` only advances via `DrawCard()`**, so replacements can't duplicate a card still in hand.
- **`HandRank`, `handNames[]`, `payouts[]` are parallel** — adding a hand type means touching all three.
- **`EvaluateHand()` is histogram-based.** A straight is `distinct == 5 && highest - lowest == 4`; a royal is that plus `lowest == 8`. The wheel (A-2-3-4-5) needs its own test because the ace is stored high (rank 12), so it spans 0..12. Verified against all 2,598,960 hands, reproducing the textbook frequency distribution — re-run that check if you touch it.
- Payout is a flat `payouts[rank] * bet`; the real-machine max-bet royal bonus is not modeled.

## Assets: there are none, by design

Rendering is raylib primitives; audio is synthesized at startup by `GenerateSound()`, which fills a `Wave` from a `Note[]` sequence. The game runs straight from a clone.

- **Suit pips are drawn from circles and triangles** (`DrawSuit`) because raylib's default font is ASCII-only — no `♥♦♠♣` glyphs without shipping a font.
- **`DrawTriangle` silently culls** non-counter-clockwise triangles; a wrong-winding shape just vanishes. Use the `DrawTriUp`/`DrawTriDown` wrappers.
- `GenerateSound` allocates with `MemAlloc` and frees via `UnloadWave` (both resolve to `RL_MALLOC`/`RL_FREE`). `LoadSoundFromWave` converts into its own `AudioBuffer` and only reads `wave.data` — verified in `raudio.c` — so the wave must be unloaded after.
- Notes need their attack/release envelope, or boundaries click.
- Sounds fire on state transitions only, never from `DrawScene()` — the draw path runs at 60fps.
- **`FLAG_WINDOW_HIGHDPI` is deliberately not set.** On a scaled display it desynchronised the framebuffer from drawing/mouse coordinates (observed: 1125×750 content in a 1406×937 buffer, letterboxed). Every coordinate here is a hardcoded pixel.

## Verifying visual changes

The layout is hardcoded pixels with no tests, so **look at it** rather than reasoning about coordinates. Temporarily add to the main loop:

```c
if (++frames == 3) { TakeScreenshot("shot.png"); break; }
```

stage state directly into `hand[]`/`held[]`/`state`, run `bin/Debug/raylib-video-poker.exe`, read the PNG, then remove the harness. This found both the paytable/card overlap and the HIGHDPI letterboxing; neither was visible from the source. The vertical anchors (`TITLE_Y`, `PAYTABLE_Y`, `WARNING_Y`, `STATUS_Y`, `FOOTER_Y`) must be retuned together when resizing — the paytable's last row has to clear `CARD_TOP`, and text below has to clear the HELD badges hanging under the cards.
