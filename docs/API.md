# Web API (port 7870)

| Route | |
|---|---|
| `GET /` | web interface |
| `GET /api/status` | `busy`, `job`, `stage`, `progress`, `elapsed`, pool memory, thread count, log tail |
| `GET /api/models` | families (`trellis`, `pixal3d`) and the weight sets found under `/data/PS5MG/models` |
| `GET /api/jobs` | all jobs with status, settings, seconds, GLB size |
| `POST /api/jobs` | multipart: `image` (png/jpg) **or** `from_ps5sd` (a file name from PS5SD's pictures), `family`, `weights` (`q4`/`q8`), `res` (512/1024), `texture`, `seed`, `steps`, `faces`, `atlas` (512/1024/2048, default per resolution), `uv`, `fov`, `label` |
| `GET /api/jobs/<id>/glb` | the mesh (binary glTF) |
| `GET /api/jobs/<id>/image` | the input picture |
| `POST /api/jobs/<id>/cancel` | cancel a queued job |
| `GET /api/jobs/<id>/log` | the last lines of the job's log: `{"lines":[...]}` |
| `GET /api/ps5sd/pictures` | the pictures in PS5SD's `/data/PS5SD/outputs` (png and jpg, newest first, at most 60): `[{"name","bytes"}]` |
| `GET /api/ps5sd/picture/<name>` | one of those pictures |
| `POST /api/quit` | the title ends itself after the current job |

## Parameters of `POST /api/jobs`

Send them as form fields: multipart (`curl -F`), url-encoded (`curl -d`) or in the query string all work. Unknown fields are ignored.
A number outside its range is clamped; a choice that does not exist is answered with `400` and `{"error": "..."}`.

| Field | Values | Default | What it does |
|---|---|---|---|
| `image` or `from_ps5sd` | a picture (png/jpg) upload, or a file name from PS5SD's pictures | none (one of the two is required) | the picture to turn into a mesh |
| `family` | `trellis`, `pixal3d` | `trellis` | the model; Pixal3D needs its weights |
| `weights` | a folder under `/data/PS5MG/models` (for example `q4`, `q8`) | `q4` | which weight set |
| `res` | `512`, `1024` | `512` | resolution of the shape; 1024 needs the 1024 weights |
| `texture` | `on`, `off` | `on` | `off` gives plain geometry |
| `seed` | 0 to 2147483647 | `42` | a different seed gives a different variant of the same picture |
| `steps` | 1 to 50 | `8` | more steps: finer, slower (6 is fast, 12 is detailed) |
| `faces` | `0`, or 1000 to 2000000 | `0` (about 150k) | face budget of the final mesh |
| `atlas` | `512`, `1024`, `2048` | by resolution | size of the texture |
| `uv` | `box`, `xatlas` | `box` | UV unwrapping: box is fast, xatlas is cleaner |
| `fov` | degrees | the model's own | Pixal3D only: the picture's field of view |
| `label` | text, up to 80 characters | the job id | the name shown in the list |

The quality presets of the page are only values of these fields: Fast is `steps=6`, Balanced is `steps=8`, Detailed is `steps=12`,
`faces=300000`, `atlas=2048`.

Example: a detailed mesh from a picture, a different seed, no texture.

```sh
curl -F image=@picture.png -F steps=12 -F faces=300000 -F atlas=2048 -F seed=7 -F texture=off http://<console>:7870/api/jobs
# {"id":"w20261022-141752-0"}
curl http://<console>:7870/api/jobs            # the queue; the id above shows status, seconds and size
curl -o mesh.glb http://<console>:7870/api/jobs/w20261022-141752-0/glb
```

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

## Pictures from PS5SD

PS5SD and PS5MG run on the same console, one at a time. `GET /api/ps5sd/pictures` lists what PS5SD has drawn, and a job can take one
of them with `from_ps5sd=<file name>` instead of an upload. The name is checked: letters, digits and `. _ -` only, no path, no `..`,
and a `.png`, `.jpg` or `.jpeg` extension. A name that does not pass is `400`, a missing file is `404`.
