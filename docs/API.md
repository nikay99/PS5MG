# Web API (port 7870)

| Route | |
|---|---|
| `GET /` | web interface |
| `GET /api/status` | `busy`, `job`, `stage`, `progress`, `elapsed`, pool memory, thread count, log tail |
| `GET /api/models` | families (`trellis`, `pixal3d`) and the weight sets found under `/data/PS5MG/models` |
| `GET /api/jobs` | all jobs with status, settings, seconds, GLB size |
| `POST /api/jobs` | multipart: `image` (png/jpg), `family`, `weights` (`q4`/`q8`), `res` (512/1024), `texture`, `seed`, `steps`, `faces`, `uv`, `fov`, `label` |
| `GET /api/jobs/<id>/glb` | the mesh (binary glTF) |
| `GET /api/jobs/<id>/image` | the input picture |
| `POST /api/jobs/<id>/cancel` | cancel a queued job |
| `POST /api/quit` | the title ends itself after the current job |

There is no authentication in 0.1.0: run it on a trusted network only (see ROADMAP).
Jobs are plain files in `/data/PS5MG/jobs/<id>.job` (one line of trellis-cli arguments); `tools/mg.sh` and
`tools/run-queue.sh` drive that queue over FTP.
