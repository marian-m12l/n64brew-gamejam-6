# Console Clash

This is my entry for the N64brew Game Jam #6. Protect your N64 consoles against the attacks from competitors. Don't let them take over and make you overheat!

The game controls are... let's say experimental:
- You play with a single controller that you move across the ports to operate each of your (up to) 4 on-screen consoles
- You can reset each console with the hardware reset button
- You can actually power off your real console for a few seconds to take advantage of RDRAM slow decay and eliminate threats

It has been tested on a PAL N64 console and with ares v147 (although ares does not support soft reset in a usable way to play this game).

I have only tested the game on hardware with a Picocart64 flashcart which has no menu and runs the ROM directly. For other flashcart, it probably won't work if it does not support booting directly to the ROM itself.


# Build instructions

To build this ROM, you need to set up libdragon using the `preview` branch and Tiny3D. Make sure the `N64_INST` and `T3D_INST` are exported, then run:
```
make
```

This ROM uses a modified libdragon IPL3 to disable clearing RDRAM and reinitializing the tick counter (see `libdragon.patch`). There's no need to execute the patch on libdragon, as the repo contains the patched ipl3 binary and `entrypoint.S`.


# Assets attributions

## Music

Menus: ["in memory" by arachno & seablue](https://modarchive.org/index.php?request=view_by_moduleid&query=138089)

In-game: ["Fly away" by Shiru Otaku (DJ Uranus)](https://modarchive.org/index.php?request=view_by_moduleid&query=202913)

## Sound FX

Blip, Attack: [from the N64brew-GameJam2024](https://github.com/n64brew/N64brew-GameJam2024)
Whoosh: ["Whoosh 07" by DRAGON-STUDIO"](https://pixabay.com/sound-effects/film-special-effects-whoosh-07-410877/)
CRT turning off: ["Cathode screen off #3" by Joseph SARDIN](https://bigsoundbank.com/cathode-screen-off-3-s1785.html)
Game Over: ["Game Over !!!" by EdenVe](https://opengameart.org/content/game-over-effect-sound)

## Models

CRT: [from Tiny3D examples](https://github.com/HailToDodongo/tiny3d)

Console: myself

## Sprites

Controller buttons, checkered background: [from the N64brew-GameJam2024](https://github.com/n64brew/N64brew-GameJam2024)

Smoke: [from Tiny3D examples](https://github.com/HailToDodongo/tiny3d)

N64, Saturn, PS logos: trademarked by their respective owners, used here under "fair use" rules (probably)
