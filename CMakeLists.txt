cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED ENV{VITASDK})
  message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
endif()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")
project(PocketVideo C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -Wall -Wno-unused-variable")

include_directories(
  include
  ${VITASDK}/arm-vita-eabi/include
)

set(SOURCES
  src/main.c
  src/network.c
  src/invidious.c
  src/ui.c
)

add_executable(pocketvideo ${SOURCES})

target_link_libraries(pocketvideo
  vita2d
  freetype
  png
  jpeg
  z
  curl
  mbedtls
  mbedx509
  mbedcrypto
  cjson
  SceLibKernel_stub
  SceDisplay_stub
  SceGxm_stub
  SceCtrl_stub
  SceTouch_stub
  ScePgf_stub
  SceCommonDialog_stub
  SceNet_stub
  SceNetCtl_stub
  SceHttp_stub
  SceSsl_stub
  SceSysmodule_stub
  SceAppMgr_stub
  SceAvPlayer_stub
  m
)

vita_create_self(eboot.bin pocketvideo UNSAFE)

vita_create_vpk(PocketVideo.vpk "PKTV00001" eboot.bin
  VERSION "01.00"
  NAME "PocketVideo"
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE assets/fonts/NotoSans-Regular.ttf assets/fonts/NotoSans-Regular.ttf
  FILE assets/textures/bg.png assets/textures/bg.png
  FILE assets/textures/bubble_glass.png assets/textures/bubble_glass.png
  FILE assets/icons/trending.png assets/icons/trending.png
  FILE assets/icons/search.png assets/icons/search.png
  FILE assets/icons/settings.png assets/icons/settings.png
  FILE assets/icons/play.png assets/icons/play.png
  FILE assets/icons/pause.png assets/icons/pause.png
  FILE assets/sounds/bubble_pop.wav assets/sounds/bubble_pop.wav
  FILE assets/sounds/peel.wav assets/sounds/peel.wav
)
