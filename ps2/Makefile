EE_BIN = build/SLENDY.ELF
EE_OBJS = build/ps2_main.o build/game.o build/render.o build/assets.o
EE_INCS = -Isrc -I$(GSKIT)/include
EE_CFLAGS = -std=gnu99 -O2 -fno-math-errno -fno-trapping-math -ffunction-sections -fdata-sections -Wextra
EE_LDFLAGS = -L$(GSKIT)/lib -Wl,--gc-sections -Wl,-Map,build/SLENDY.map
EE_LIBS = -lgskit -ldmakit -lpad -laudsrv -lpatches -lm

all: $(EE_BIN)
build/render.o: EE_CFLAGS += -O3 -ffast-math
build/ps2_main.o: tests/walk_route.h
build/ps2_main.o: EE_CFLAGS += -O3

build/%.o: src/%.c src/game.h src/render.h src/steering.h src/transform.h src/audio_voice.h src/terrain_surface.h generated/noonoo.h generated/menu/menu_layout.h generated/scene_visuals.h generated/scene_audio.h generated/fidelity.h generated/victory/victory_layout.h generated/player_walk.h tests/menu_route.h tests/benchmark.h Makefile
	mkdir -p build
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

build/assets.S: generated/world.bin generated/menu/world.bin generated/victory/world.bin generated/player_walk.bin
	mkdir -p build
	printf '.section .rodata\n.balign 128\n.global world_start, world_end\nworld_start:\n.incbin "generated/world.bin"\nworld_end:\n' > $@
	printf '.balign 128\n.global menu_world_start,menu_world_end\nmenu_world_start:\n.incbin "generated/menu/world.bin"\nmenu_world_end:\n' >> $@
	printf '.balign 128\n.global victory_world_start,victory_world_end\nvictory_world_start:\n.incbin "generated/victory/world.bin"\nvictory_world_end:\n' >> $@
	printf '.balign 128\n.global player_walk_start,player_walk_end\nplayer_walk_start:\n.incbin "generated/player_walk.bin"\nplayer_walk_end:\n' >> $@
	printf '.balign 64\n.global sio2_start,sio2_end\nsio2_start:\n.incbin "$(PS2SDK)/iop/irx/freesio2.irx"\nsio2_end:\n' >> $@
	printf '.balign 64\n.global pad_start,pad_end\npad_start:\n.incbin "$(PS2SDK)/iop/irx/freepad.irx"\npad_end:\n' >> $@
	printf '.balign 64\n.global audsrv_start,audsrv_end\naudsrv_start:\n.incbin "$(PS2SDK)/iop/irx/audsrv.irx"\naudsrv_end:\n' >> $@

build/assets.o: build/assets.S
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
