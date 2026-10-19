TITLE_ID = PKTV00001
TARGET   = PocketVideo
OBJS     = src/main.o src/network.o src/invidious.o src/ui.o src/cJSON.o src/compat.o

LIBS = -lvita2d -lfreetype -lbz2 -lpng -ljpeg -lz -lcurl -lssl -lcrypto -lzstd -lpthread \
       -lSceLibKernel_stub -lSceDisplay_stub -lSceGxm_stub -lSceCtrl_stub \
       -lSceTouch_stub -lScePgf_stub -lScePvf_stub -lSceCommonDialog_stub \
       -lSceNet_stub -lSceNetCtl_stub -lSceHttp_stub -lSceSsl_stub \
       -lSceSysmodule_stub -lSceAppMgr_stub -lSceAvPlayer_stub -lm

PREFIX  = arm-vita-eabi
CC      = $(PREFIX)-gcc
CFLAGS  = -Wall -O3 -Iinclude -I$(VITASDK)/arm-vita-eabi/include
LDFLAGS = -Wl,-q -Wl,--defsym=__sce_headroom=0x10000
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
		--add assets/textures/bg.png=assets/textures/bg.png \
		--add assets/textures/bubble_glass.png=assets/textures/bubble_glass.png \
		--add assets/sounds/bubble_pop.wav=assets/sounds/bubble_pop.wav \
		--add assets/sounds/peel.wav=assets/sounds/peel.wav \
		--add assets/openssl.cnf=assets/openssl.cnf \
		--add assets/openssl.cnf=usr/local/vitasdk/arm-vita-eabi/ssl/openssl.cnf \
		--add assets/cacert.pem=assets/cacert.pem \
		$@

eboot.bin: $(TARGET).velf
	vita-make-fself -c $< $@

%.velf: %.elf
	vita-elf-create $< $@

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LIBS) -o $@

clean:
	@rm -rf $(TARGET).vpk $(TARGET).velf $(TARGET).elf $(OBJS) param.sfo eboot.bin
