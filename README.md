# PocketVideo (PS Vita Invidious Client)

A homebrew video client front-end for the Sony PlayStation Vita that streams from public Invidious API instances (defaulting to `https://invidious.f5.si`). Designed with an authentic 2010s tactile LiveArea user interface featuring glossy bubbles, curved tabs, and textured layouts.

## Recommended Tech Stack
- **Toolchain**: [VitaSDK](https://vitasdk.org/) (`arm-vita-eabi-gcc`)
- **2D Graphics & UI**: `vita2dlib` (`libvita2d`) hardware-accelerated GXM graphics at 960x544 native OLED resolution.
- **Networking**: `libcurl` with `mbedtls` for HTTPS/TLS requests with timeout handling and redirect support.
- **JSON Parser**: `cJSON` (fast, lightweight memory footprint for embedded ARM Cortex-A9).
- **Video Decoding**: Hardware-accelerated `sceAvPlayer` (H.264 up to 720p/1080i with AAC audio).
- **Controls**: `sceCtrl` for physical buttons / analog sticks and `sceTouch` for capacitive front touchscreen.

## Project Structure
```
PocketVideo/
├── CMakeLists.txt              # CMake build configuration for VitaSDK
├── Makefile                    # Direct Makefile compilation alternative
├── README.md                   # Project documentation & build guide
├── include/
│   ├── invidious.h             # Invidious API types & endpoints
│   ├── network.h               # libcurl network & SSL wrapper
│   └── ui.h                    # vita2d classic tactile UI definitions
├── src/
│   ├── main.c                  # App entry point, event loop & input handling
│   ├── invidious.c             # cJSON parsing for /api/v1/trending & search
│   ├── network.c               # Vita network/SSL initialization & curl requests
│   └── ui.c                    # 2012 Vita bubble, bevel, & LiveArea rendering
├── assets/                     # Runtime UI textures & sound assets
│   ├── fonts/                  # Custom TTF/PVF fonts
│   ├── sounds/                 # Authentic 2012 tactile click sounds
│   └── textures/
│       ├── bubble_glass.png    # Skeuomorphic gloss overlay
│       ├── livearea_bg.png     # Textured Vita wallpaper
│       └── icon_play.png       # Media player icon
└── sce_sys/                    # PlayStation Vita LiveArea metadata
    ├── icon0.png               # 128x128 app icon on Vita LiveArea
    ├── livearea/
    │   └── contents/
    │       ├── bg.png          # 840x500 LiveArea background screen
    │       ├── startup.png     # 280x158 Gate startup button
    │       └── template.xml    # LiveArea layout template definition
    └── param.sfo               # Generated title info (TITLE_ID=PKTV00001)
```

## How to Build

### Using VitaSDK
Make sure `VITASDK` environment variable is set:
```bash
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH

# Using CMake:
mkdir build && cd build
cmake ..
make

# Or using Makefile:
make
```
This generates `PocketVideo.vpk`, which can be transferred to your PS Vita via VitaShell (FTP or USB).

### Testing with Invidious Endpoint
PocketVideo queries `https://invidious.f5.si/api/v1/trending` by default. You can test the endpoint directly using curl:
```bash
curl -s "https://invidious.f5.si/api/v1/trending" | jq .
```
