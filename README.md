# PocketVideo

An Invidious client for the Playstation Vita,that connects to a server to play videos on this fantastic machine.
It can DVS (Direct Video Stream),search by using a video id,view channel info,etc..

## How to Build

All you need is VitaSDK,CMake and you to clone this repo.

```bash
mkdir build && cd build
cmake ..
make
```
## Server Tests
You can test the default server of this app by using:
```bash
curl -s "https://yt.chocolatemoo53.com/api/v1/trending" | jq .
```
on your computer.

## LLM DISCLAMER

This project was also made with LLMs,Though that,a real human has created the graphics,tested and built all of this.
It's all bcuz im not good at coding :\

## LICENSE

Licensed under GPL3.
OpenSSL: Apache 2.0
curl: MPL 2.0
Noto Sans Symbols 2: OFL 1.1
