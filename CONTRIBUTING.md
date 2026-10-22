# Contributing

Thanks for looking. PS5MG is a small hobby project, and help is welcome.

## Build

See [docs/BUILDING.md](docs/BUILDING.md): GitHub Actions (fork and push) or Docker.

## Test

The web server has a test that runs in the build container:

```sh
MSYS_NO_PATHCONV=1 docker run --rm --platform linux/amd64 -v "$PWD:/src:ro" ps5sd-build bash /src/tools/test-web/run.sh
```

Build the `ps5sd-build` image first (step B in docs/BUILDING.md). `tools/test-deform` and `tools/test-decimate` check the
speedups against the original code; each has a `run.sh` that says how to run it.

## Share a mesh

Open a ["Show your mesh"](https://github.com/nikay99/PS5MG/issues/new?template=show-your-mesh.yml) issue with the picture,
the settings and the time it took. Questions and ideas can go to [Discussions](https://github.com/nikay99/PS5MG/discussions).

## Code style

Match the code around your change. Keep changes small and say why in the pull request.

## Licence

PS5MG is GPL-3.0. By sending a contribution you agree that it is released under the same licence.

## Out of scope

- Cloud services or accounts.
- Hosting model weights.
- Bundling model weights in the repository or in a release.

See [ROADMAP.md](ROADMAP.md) for what is planned.
