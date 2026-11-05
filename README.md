# PocketVideo

An Invidious client for the Playstation Vita,that connects to a server to play videos on this fantastic machine.
It can DVS (Direct Video Stream),serach by using a video id,view channel info,etc..

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

## LLM DISCLAMER

This project was also made with LLMs,Though that,a real human has created the graphics,tested and built all of this.
It's all bcuz im not good at coding :\
