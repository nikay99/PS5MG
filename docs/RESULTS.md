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
dragon 2:08, crate 4:04, mushroom 2:58, chess knight 2:03, robot 2:20, anvil 1:44. The crate is the slow one (like the chest
before it); why is not investigated.

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

## The three presets (gargoyle, seed 1234)

| preset | steps | face budget | atlas | time | faces | texture | GLB |
|---|---|---|---|---|---|---|---|
| Fast | 6 | default | default | 1:52 | 143,592 | 1024 px | 7.3 MB |
| Balanced | 8 | default | default | 2:13 | 139,370 | 1024 px | 7.2 MB |
| Detailed | 12 | 300,000 | 2048 | 2:56 | 295,504 | 2048 px | 18.4 MB |

The picture with the three results side by side is `docs/images/presets.jpg` (`tools/make-presets-compare.py`).

## Soak test: many jobs in a row (37 of 80, stopped by hand)

`tools/jobs-soak.txt`: short jobs (4 steps, no texture, 20k faces) one after the other through `tools/run-queue.sh`. The title restarts itself
after every job, so this is also a test of 37 restarts. It was stopped at job 37 so that the app could be reinstalled on the console.

- **Result: every one of the 37 jobs finished.** No job failed, the title came back after each restart.
- **But the console gets slower, and stays slower:**

| jobs | median | slowest |
|---|---|---|
| 1 to 16 | 58 s | 88 s |
| 17 to 24 | 148 s | 185 s |
| 25 to 32 | 147 s | 172 s |
| 33 to 37 | 166 s | 172 s |

From job 17 on, the same short job takes about 2.6 times as long and it did not recover over the next 20 jobs. The first night showed the same
pattern (CPU stages 2.5 times slower after hours of jobs, cured by restarting the console).

**What is not known:** the cause. Candidates: heat (the console throttles), the system's own memory ("Internal Memory is running out" is in every job's log),
or something that builds up with every self-restart. This test cannot tell them apart. Next steps for 0.3.0: run the same list right after a console restart and
log the temperature; pause for ten minutes at the slow point and see whether it recovers; compare with a run that does not restart the title.

## Where the time goes (lantern)

The first run on the console, with trellis.cpp unchanged, took 19:51 and had no texture; freeing the picture's
background alone took 875 s. After the changes listed in the README, the stages are: background removal and image features
(seconds), sparse structure, shape flow, mesh extraction and simplification, texture flow and PBR decode, and the write.
`tools/runinfo.py <job.log>` prints the stage times of a job from its log (`/data/PS5MG/logs/<job>.log`).

## Picture rules (PC trial, RX 7800 XT)

- One object, three-quarter view, plain light background, no stand, no shadow. Glass and flames get lost.
- `--fa-fast` is needed: without it a run takes 21 instead of 9 minutes on the PC (no BF16). `--gss 1` breaks the shape.
