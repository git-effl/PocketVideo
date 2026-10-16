TITLE_ID = PKTV00001
TARGET   = PocketVideo
OBJS     = src/main.o src/network.o src/invidious.o src/ui.o

LIBS = -lvita2d -lfreetype -lpng -ljpeg -lz -lcurl -lmbedtls -lmbedx509 -lmbedcrypto \
       -lcjson -lSceLibKernel_stub -lSceDisplay_stub -lSceGxm_stub -lSceCtrl_stub \
       -lSceTouch_stub -lScePgf_stub -lSceCommonDialog_stub -lSceNet_stub \
       -lSceNetCtl_stub -lSceHttp_stub -lSceSsl_stub -lSceSysmodule_stub \
       -lSceAppMgr_stub -lSceAvPlayer_stub -lm

PREFIX  = arm-vita-eabi
CC      = $(PREFIX)-gcc
CFLAGS  = -Wl,-q -Wall -O3 -Iinclude -I$(VITASDK)/arm-vita-eabi/include
ASFLAGS = $(CFLAGS)

all: $(TARGET).vpk

%.vpk: eboot.bin
	vita-mksfoex -s TITLE_ID=$(TITLE_ID) "PocketVideo" param.sfo
	vita-pack-vpk -s param.sfo -b eboot.bin \
		--add sce_sys/icon0.png=sce_sys/icon0.png \
		--add sce_sys/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png \
		--add sce_sys/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png \
		--add sce_sys/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml \
		--add assets/fonts/NotoSans-Regular.ttf=assets/fonts/NotoSans-Regular.ttf \
		--add assets/textures/livearea_bg.png=assets/textures/livearea_bg.png \
		--add assets/textures/bubble_glass.png=assets/textures/bubble_glass.png \
		--add assets/icons/trending.png=assets/icons/trending.png \
		--add assets/icons/search.png=assets/icons/search.png \
		--add assets/icons/settings.png=assets/icons/settings.png \
		--add assets/icons/play.png=assets/icons/play.png \
		--add assets/icons/pause.png=assets/icons/pause.png \
		--add assets/sounds/bubble_pop.wav=assets/sounds/bubble_pop.wav \
		--add assets/sounds/peel.wav=assets/sounds/peel.wav \
		$@

eboot.bin: $(TARGET).velf
	vita-make-fself -c $< $@

%.velf: %.elf
	vita-elf-create $< $@

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

clean:
	@rm -rf $(TARGET).vpk $(TARGET).velf $(TARGET).elf $(OBJS) param.sfo eboot.bin
