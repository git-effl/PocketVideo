# PocketVideo

A dedicated video client front-end that streams videos directly from public Invidious API instances (defaulting to `https://invidious.f5.si`). Designed with a classic 2010s tactile console user interface featuring glossy bubbles, corner tabs, and textured layouts.

## Recommended Tech Stack
- **Toolchain**: C99 Cross-Compiler (`arm-vita-eabi-gcc` or standard embedded GCC toolchain)
- **2D Graphics & UI**: `vita2dlib` hardware-accelerated 2D graphics at 960x544 native OLED resolution.
- **Typography**: Google Fonts **Noto Sans** TrueType font (`assets/fonts/NotoSans-Regular.ttf`) loaded via FreeType.
- **Networking**: `libcurl` with `mbedtls` for HTTPS/TLS requests with timeout handling and redirect support.
- **JSON Parser**: `cJSON` (fast, lightweight memory footprint for embedded systems).
- **Video Decoding**: Hardware-accelerated `sceAvPlayer` (H.264 up to 720p with AAC audio).
- **Controls**: Physical D-Pad, action buttons, and front capacitive touchscreen.

## Project Structure
```
PocketVideo/
├── CMakeLists.txt              # CMake build configuration
├── Makefile                    # Direct Makefile compilation alternative
├── README.md                   # Project documentation & build guide
├── LICENSE                     # GPL-3.0 License
├── include/
│   ├── invidious.h             # Invidious API types & endpoints
│   ├── network.h               # libcurl network & SSL wrapper
│   └── ui.h                    # 2010s tactile UI definitions
├── src/
│   ├── main.c                  # App entry point, event loop & input handling
│   ├── invidious.c             # cJSON parsing for /api/v1/trending & search
│   ├── network.c               # Network initialization & curl requests
│   └── ui.c                    # Bubble, bevel, & tactile rendering
├── assets/                     # Custom UI textures & sounds (CC0)
│   ├── LICENSE                 # Creative Commons Zero 1.0 Universal
│   ├── fonts/
│   │   └── NotoSans-Regular.ttf# Noto Sans TrueType font
│   ├── textures/
│   │   ├── bg.png              # 960x544 textured wallpaper
│   │   └── bubble_glass.png    # Skeuomorphic gloss overlay
│   └── sounds/
│       ├── bubble_pop.wav      # Tactile click sound
│       └── peel.wav            # Corner tab sound
└── sce_sys/                    # Application package metadata
    ├── icon0.png               # 128x128 app icon
    └── livearea/contents/
        ├── bg.png              # 840x500 portal background
        ├── startup.png         # 280x158 startup button
        └── template.xml        # Portal layout definition
```

## How to Build

### Using Docker
```bash
docker run --rm -v $(pwd):/build vitasdk/vitasdk /bin/bash -c "cd /build && mkdir -p build && cd build && cmake .. && make"
```

### Using Local SDK
```bash
mkdir build && cd build
cmake ..
make
```

### Testing with Invidious Endpoint
PocketVideo queries `https://invidious.f5.si/api/v1/trending` by default. You can test the endpoint directly using curl:
```bash
curl -s "https://invidious.f5.si/api/v1/trending" | jq .
```
