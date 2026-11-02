LOADER := ../client_bridge/refs/loadlibrary/peloader
include ../build_settings.mk
PREFIX := -Dgb_load=cb_load -Dgb_symbol=cb_symbol -Dgb_symbol_name=cb_symbol_name -Dgb_contains=cb_contains -Dgb_report=cb_report -Dgb_unload=cb_unload
CFLAGS := $(COMMON) $(PREFIX) -std=gnu99 -fshort-wchar -D_GNU_SOURCE -Wno-multichar -I$(LOADER) -Isrc
.PHONY: all
all: build/libgoldsrc_pe.so
build:
	mkdir -p build
build/runtime.o build/gdi.o build/filesystem.o build/sdl.o build/fmod.o: | build
build/runtime.o build/gdi.o build/filesystem.o build/sdl.o build/fmod.o: ../build_settings.mk
$(LOADER)/libpeloader.a: $(wildcard $(LOADER)/*.c $(LOADER)/*.h $(LOADER)/winapi/*.c) Makefile ../build_settings.mk
	$(MAKE) -C $(LOADER) clean
	$(MAKE) -C $(LOADER) CFLAGS='$(COMMON) -std=gnu99 -fshort-wchar -Wno-multichar -Wno-incompatible-pointer-types -Iinclude'
build/runtime.o: src/runtime.c src/runtime.h src/filesystem.h src/sdl.h src/fmod.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@
build/filesystem.o: src/filesystem.c src/filesystem.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@
build/sdl.o: src/sdl.c src/sdl.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@
build/fmod.o: src/fmod.c src/fmod.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@
build/gdi.o: src/gdi.c Makefile
	$(CC) $(CFLAGS) -I/usr/include/freetype2 -c $< -o $@
build/libgoldsrc_pe.so: build/runtime.o build/gdi.o build/filesystem.o build/sdl.o build/fmod.o $(LOADER)/libpeloader.a
	$(CC) $(COMMON) -shared $(filter %.o,$^) -Wl,--whole-archive,$(LOADER)/libpeloader.a,--no-whole-archive -Wl,--exclude-libs,ALL,-soname,libgoldsrc_pe.so -lm -ldl -lpthread -lrt -lfreetype -lfontconfig -o $@
