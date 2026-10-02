# Picture sharpness, measured outside the app

Not part of the build or of ctest. This is the stand-in behind the figures in the 0.2.1 pictures
change: Lumen's scene graph cannot be measured headless (offscreen Qt uses the software renderer),
so `gltex.cpp` draws a texture with the canvas's own parameters through EGL surfaceless + GLES 3,
and `cut.cpp` produces the textures with the app's own `picturepixels` code.

- `cut.cpp` — links against `app/src/media/picturepixels.cpp` and Qt Gui.
- `gltex.cpp` — links against EGL and GLESv2.
- `beforeafter.py <tag> <picture> <x> <y> <w> <h> [crop]` — needs numpy and Pillow, and the two
  programs above built as `cut` and `gltex` beside it. `SOFT=0` uses the GPU instead of llvmpipe;
  `TURN="<rotation> <cropX> <cropY> <cropW> <cropH>"` measures a turned or trimmed picture.
- `runba.sh` — the ten cases that were run. `PICTURES` is a folder with a 1920×1080 screenshot
  (`shot1080.png`) and a 4000×3000 photo (`photo4000.jpg`); they are not in the repository.
- `results-intel-lnl-2026-10-02.txt` — what it printed on an Intel Lunar Lake GPU.

"Before" is the 2560 px mipmapped texture 0.2.0 draws; "after" lays the texture cut for the settled
view over it; both are compared with a Lanczos resample of the file. It says nothing about what
Lumen's own window shows on a GPU: that has not been checked.
