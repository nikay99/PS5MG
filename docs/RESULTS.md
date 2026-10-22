# Measurements

All on one console (PS5, 11.4 GiB GPU pool, 11 worker threads), 512 resolution, q4 weights unless noted, picture with
a plain background, seed 1234, 8 steps, `--fa-fast --box-uv`, 150k faces, 1024 px texture. Times are the whole job
(picture in, GLB written) in minutes:seconds.

## The five test objects

| object | time |
|---|---|
| lantern | 1:53 |
| sword | 1:08 |
| shield | 1:51 |
| potion | 2:22 |
| blaster | 1:27 |

The README's example set (pictures drawn by PS5SD with Z-Image Turbo, same settings): helmet 2:07, gargoyle 2:14,
dragon 2:08, crate 4:04, mushroom 2:58, chess knight 2:03, robot 2:20, anvil 1:44. The crate is the slow one: a large
flat-sided object with a lot of surface to texture.

## Variants (lantern, sword)

| variant | lantern | sword |
|---|---|---|
| standard | 1:53 | 1:08 |
| 6 steps instead of 8 | 1:34 | 0:55 |
| q8 instead of q4 weights | 1:56 | 1:22 |
| texture atlas 2048 | 1:55 | 1:09 |
| face budget 100k | 1:54 | 1:08 |
| face budget 300k | 1:54 | 1:08 |
| Pixal3D, 1024, textured | 4:48 | stopped without an error message |

Seed 5: chest 4:17 (unexplained), blaster 1:26, potion 2:22.
The face budget and the atlas size hardly change the time: it goes into the flows, not into simplification.
Pixal3D at 512 writes a raw, geometry-only mesh in 1:25 to 2:00 (raw winding: draw both sides to look at it).

## Where the time goes (lantern)

The first run on the console, with trellis.cpp unchanged, took 19:51 and had no texture; freeing the picture's
background alone took 875 s. After the changes listed in the README, the stages are: background removal and image features
(seconds), sparse structure, shape flow, mesh extraction and simplification, texture flow and PBR decode, and the write.
`tools/runinfo.py <job.log>` prints the stage times of a job from its log (`/data/PS5MG/logs/<job>.log`).

## Picture rules (PC trial, RX 7800 XT)

- One object, three-quarter view, plain light background, no stand, no shadow. Glass and flames get lost.
- `--fa-fast` is needed: without it a run takes 21 instead of 9 minutes on the PC (no BF16). `--gss 1` breaks the shape.
