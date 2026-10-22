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

## Soak test: many jobs in a row

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

**The cause (found for 0.2.3):** writing to the console's storage. A job's GLB took 0.3 s to write on a fresh console and 15 to 38 s
after about 15 jobs; one log line took up to 0.7 s. The CPU, its cores, the memory and the GPU stayed as fast as at the start (measured
with probes before, after and during the jobs). Two things in PS5MG made it much worse:

- Jobs without texture skipped the face budget and wrote the whole decoded mesh, about 4.7 million faces or 30 to 130 MB a GLB (2 GB after
  50 jobs). Now they are simplified like textured jobs (`ps5/patch-notex-faces.py`): about 0.3 MB.
- Every printed line went straight to a file, so a slow write held up the job. Now a thread of its own writes the log twice a second, and
  jobs no longer print the debug lines (about 80 lines a job instead of 600).

Why the console's writes slow down in the first place is not known; it looks like a write buffer that fills up.

### After the fix: 80 jobs in a row (0.2.3)

The same list, without a console restart before it (the console had already run more than 50 jobs that evening):

| jobs | median | slowest |
|---|---|---|
| 1 to 16 | 73 s | 136 s |
| 17 to 32 | 72 s | 94 s |
| 33 to 48 | 68 s | 119 s |
| 49 to 64 | 77 s | 143 s |
| 65 to 80 | 68 s | 101 s |

All 80 finished, none failed, the title came back after all 80 restarts, and there is no slowdown. The slowest job of each block is the same
picture (the mushroom). A job is about 10 s longer than the fastest ones before, because the mesh is now cleaned up and simplified.

## Where the time goes (lantern)

The first run on the console, with trellis.cpp unchanged, took 19:51 and had no texture; freeing the picture's
background alone took 875 s. After the changes listed in the README, the stages are: background removal and image features
(seconds), sparse structure, shape flow, mesh extraction and simplification, texture flow and PBR decode, and the write.
`tools/runinfo.py <job.log>` prints the stage times of a job from its log (`/data/PS5MG/logs/<job>.log`).

## Picture rules (PC trial, RX 7800 XT)

- One object, three-quarter view, plain light background, no stand, no shadow. Glass and flames get lost.
- `--fa-fast` is needed: without it a run takes 21 instead of 9 minutes on the PC (no BF16). `--gss 1` breaks the shape.
