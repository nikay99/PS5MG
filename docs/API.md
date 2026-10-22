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

## Access

Open by default. To protect the web interface and the API with a key, put one line in `/data/PS5MG/api-key.txt` on the console
(`tools/mg.sh setkey <key>` does it over FTP; an empty key opens it again). The title reads the file on every request, so no restart
is needed.

- The page asks for the key and keeps it as a cookie (`HttpOnly`, `SameSite=Strict`). Scripts send `Authorization: Bearer <key>`.
- Without the key every `/api/` route answers `401` (after a short delay); `GET /` (the page), `GET /api/auth` and `POST /api/login`
  stay reachable. `/api/auth` tells the page whether a key is needed.
- It is plain HTTP: on a network you do not trust, the key can be read on the wire. It keeps visitors and other devices out, it is not
  a vault.
- With or without a key, a `POST` that carries an `Origin` header must come from the console's own page: a web page of another site
  cannot start jobs or end the app from a browser on your network. The server sends no CORS header.

`GET /api/jobs/<id>/log` returns the last lines of a job's log (`{"lines":[...]}`), which the page shows for a failed job ("Why?").
Jobs are plain files in `/data/PS5MG/jobs/<id>.job` (one line of trellis-cli arguments); `tools/mg.sh` and
`tools/run-queue.sh` drive that queue over FTP.
