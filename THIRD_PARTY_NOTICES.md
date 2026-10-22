# Third-party notices

PS5MG's own code is GPL-3.0 (`LICENSE`). The title also contains, or is built with, the following. Each keeps its own
licence, and the licence texts that ship with their sources are copied into `licenses/` of the title.

## In the title

| Component | What for | Copyright | Licence |
|---|---|---|---|
| [trellis.cpp](https://github.com/pwilkin/trellis.cpp) | the TRELLIS.2 and Pixal3D pipeline in C++ | Piotr Wilkin | MIT |
| [ggml](https://github.com/pwilkin/ggml) (fork of ggml-org/ggml) | tensor library, Vulkan backend | The ggml authors | MIT |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | the web server | Yuji Hirose | MIT |
| [stb](https://github.com/nothings/stb) | picture reading and writing | Sean Barrett and contributors | public domain or MIT |
| [meshoptimizer](https://github.com/zeux/meshoptimizer) | mesh clean-up | Arseny Kapoulkine | MIT |
| [fqms](https://github.com/sp4cerat/Fast-Quadric-Mesh-Simplification) | mesh simplification (the parallel version in `ps5/decimate_qem_fast.cpp` is a rewrite of the same method) | Spacerat (sp4cerat) and contributors | MIT |
| [xatlas](https://github.com/jpcy/xatlas) | UV unwrapping | Jonathan Young and contributors | MIT |
| [PS5SD](https://github.com/Darkatek7/PS5SD) and [PS5LM](https://github.com/cobanov/PS5LM) code | title start-up, screen, build recipe | their authors | GPL-3.0 |
| [ps5-homebrew-ui](https://github.com/Phononenom/ps5-homebrew-ui) | the dashboard on the screen | BlackBearReloaded and contributors | GPL-3.0-or-later (its own notices: `licenses/ps5-homebrew-ui-THIRD_PARTY_NOTICES.md`) |
| Inter, Montserrat, DejaVu Sans Mono | the dashboard's fonts | their authors | SIL OFL 1.1 / Bitstream Vera (texts in `assets/fonts/`) |
| [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), PS5_Mesa (RADV), PS5_PayloadSDK | Vulkan on the console, the toolchain | mihawk-99 and the Mesa authors | see those repositories (Mesa: MIT) |
| three.js | the 3D viewer in the web page (loaded by your browser from a CDN, not shipped) | three.js authors | MIT |

## Not in the title, not in this repository

The model weights: TRELLIS.2 (Microsoft, MIT), the GGUF conversion by ilintar, Pixal3D (vegax87, MIT), BiRefNet (MIT)
and DINOv3 (Meta, **Meta's own licence**). You download them yourself (`tools/get-weights.sh` asks you to accept their
licences first) and copy them to your console. PS5MG never downloads anything: it has no network client at all, only a web
server for your own network.

PS5MG is not affiliated with or endorsed by Sony Interactive Entertainment, Microsoft or Meta. "PlayStation" and "PS5" are
trademarks of Sony Interactive Entertainment; TRELLIS is the name of Microsoft's model; DINOv3 is Meta's. They are named
here only to say what PS5MG works with.
