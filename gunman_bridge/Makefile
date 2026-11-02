SDK := ../halflife
LOADER := refs/loadlibrary/peloader
include ../build_settings.mk
CFLAGS := $(COMMON) -std=gnu99 -fshort-wchar -D_GNU_SOURCE -Wno-multichar -I$(LOADER) -Isrc
CXXFLAGS := $(COMMON) -std=c++17 -DLINUX -D_LINUX -Wno-endif-labels -I$(SDK)/dlls -I$(SDK)/engine -I$(SDK)/common -I$(SDK)/public -I$(SDK)/pm_shared -Isrc
RUNTIME := ../pe_runtime/build/libgoldsrc_pe.so
LDLIBS := -L../pe_runtime/build -lgoldsrc_pe -Wl,-rpath,'$$ORIGIN:$$ORIGIN/../../pe_runtime/build' -lm -lpthread -lrt
.PHONY: all test
all: build/handshake build/abi_probe build/gunman.so build/bridge.so build/wrapper_handshake build/export_loader build/server_smoke build/winapi_probe
build/runtime.o build/handshake.o build/bridge.o build/config.o build/elf_exports.o build/abi_probe build/wrapper_handshake build/native_host build/export_loader: ../build_settings.mk
.PHONY: runtime
runtime:
	$(MAKE) -C ../pe_runtime
$(RUNTIME): runtime
build/runtime.o: src/runtime.c src/runtime.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@
build/handshake.o: tests/handshake.cpp src/runtime.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/handshake: build/handshake.o build/runtime.o $(RUNTIME)
	$(CXX) $(COMMON) $(filter %.o,$^) $(LDLIBS) -o $@
build/abi_probe: tests/abi_probe.cpp
	$(CXX) $(CXXFLAGS) $< -o $@
logs/pe_metadata.json: tools/inspect_pe.py ../../dlls/gunman.dll
	python3 tools/inspect_pe.py
build/export_slots.S: tools/generate_export_slots.py
	python3 tools/generate_export_slots.py
build/export_slots.h: build/export_slots.S
	@test -f $@
build/export_slots.o: build/export_slots.S
	$(CC) $(COMMON) -c $< -o $@
build/engine_thunks.inc: tools/generate_engine_thunks.py $(SDK)/engine/eiface.h
	python3 tools/generate_engine_thunks.py
build/bridge.o: src/bridge.cpp src/runtime.h src/config.h src/elf_exports.h Makefile build/engine_thunks.inc
	$(CXX) $(CXXFLAGS) -fvisibility=hidden -Ibuild -c $< -o $@
build/config.o: src/config.cpp src/config.h Makefile
	$(CXX) $(CXXFLAGS) -fvisibility=hidden -c $< -o $@
build/elf_exports.o: src/elf_exports.cpp src/elf_exports.h build/export_slots.h Makefile
	$(CXX) $(CXXFLAGS) -fvisibility=hidden -Ibuild -c $< -o $@
build/gunman.so: build/bridge.o build/config.o build/elf_exports.o build/export_slots.o build/runtime.o $(RUNTIME)
	$(CXX) $(COMMON) -shared $(filter %.o,$^) $(LDLIBS) -ldl -Wl,--hash-style=sysv,--exclude-libs,ALL -o $@
build/bridge.so: build/gunman.so
	cp $< $@
build/wrapper_handshake: tests/wrapper_handshake.cpp
	$(CXX) $(CXXFLAGS) $< -ldl -o $@
build/engine_slot_names.inc: build/engine_thunks.inc
	@test -f $@
build/server_smoke: tests/server_smoke.cpp build/engine_slot_names.inc ../build_settings.mk
	$(CXX) $(CXXFLAGS) -Ibuild $< -ldl -o $@
build/winapi_probe: tests/winapi_probe.cpp ../build_settings.mk
	$(CXX) $(CXXFLAGS) $< -ldl -o $@
build/export_loader: tests/export_loader.cpp
	$(CXX) $(CXXFLAGS) $< -ldl -o $@
build/native_host: tests/native_host.cpp
	$(CXX) $(CXXFLAGS) $< -ldl -o $@
test: all
	./build/handshake ../../dlls/gunman.dll
	./build/wrapper_handshake ./build/gunman.so
	./build/wrapper_handshake ./build/gunman.so relocate
	bash tools/test_export_loader.sh
	bash tools/test_winapi_fixture.sh
