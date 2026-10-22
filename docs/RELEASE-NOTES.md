## PS5MG 0.1.1

### New
- Optional key for the web interface and the API: put one line in /data/PS5MG/api-key.txt, or run tools/mg.sh setkey from the repository. Without a key everything stays open, as in 0.1.0.
- A failed job shows the last lines of its log in the page ("Why?").

### Safer
- Pages of other websites can no longer drive the console from a browser on your network: the server sends no wildcard CORS header any more, and a POST has to come from the console's own page.

### Unchanged
- Model weights are not included. The title has no network client and downloads nothing.
- The title restarts itself after every job.
- Pixal3D with texture at 1024 can stop without an error message.
