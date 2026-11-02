LOADER := refs/loadlibrary/peloader
include ../build_settings.mk
PREFIX := -Dgb_load=cb_load -Dgb_symbol=cb_symbol -Dgb_symbol_name=cb_symbol_name -Dgb_contains=cb_contains -Dgb_report=cb_report -Dgb_unload=cb_unload
CFLAGS := $(COMMON) $(PREFIX) -std=gnu99 -fshort-wchar -D_GNU_SOURCE -Wno-multichar -I$(LOADER) -Isrc
CXXFLAGS := $(COMMON) $(PREFIX) -std=c++17 -DLINUX -D_LINUX -Wno-endif-labels -I../halflife/cl_dll -I../halflife/common -I../halflife/engine -I../halflife/public -I../halflife/pm_shared -I../halflife/external/SDL2 -Isrc
RUNTIME := ../pe_runtime/build/libgoldsrc_pe.so
LDLIBS := -L../pe_runtime/build -lgoldsrc_pe -Wl,-rpath,'$$ORIGIN/../../pe_runtime/build:$$ORIGIN/../dlls' -lm -lpthread -lrt
all: build/client.so build/vgui_host.dll
# Optional tests; smoke needs an installed game and mod (scout/tools/build.sh --test).
tests: build/smoke build/mouse_input_test
.PHONY: all tests
build:
	mkdir -p build
build/bridge.o build/render.o build/native_viewport.o build/mouse_input.o build/smoke build/mouse_input_test build/engine_thunks.inc build/vgui_headers.stamp build/stub/vgui.so: | build
build/bridge.o build/render.o build/native_viewport.o build/mouse_input.o build/smoke build/mouse_input_test: ../build_settings.mk
.PHONY: runtime
runtime:
	$(MAKE) -C ../pe_runtime
$(RUNTIME): runtime
build/engine_thunks.inc: tools/generate_thunks.py ../halflife/engine/APIProxy.h ../halflife/common/r_studioint.h
	python3 tools/generate_thunks.py
build/engine_slot_names.inc build/studio_thunks.inc: build/engine_thunks.inc
	@test -f $@
build/bridge.o: src/bridge.cpp build/studio_thunks.inc ../halflife/public/cl_dll/IGameClientExports.h src/sdk.h src/runtime.h ../pe_runtime/src/runtime.h src/render.h src/vgui_host_api.h src/native_viewport.h src/mouse_input.h Makefile build/engine_thunks.inc
	$(CXX) $(CXXFLAGS) -fvisibility=hidden -Ibuild -c $< -o $@
build/render.o: src/render.cpp src/render.h src/vgui_host_api.h
	$(CXX) $(CXXFLAGS) -fvisibility=hidden -c $< -o $@
build/native_viewport.o: src/native_viewport.cpp src/native_viewport.h src/runtime.h
	$(CXX) $(CXXFLAGS) -I../halflife/utils/vgui/include -fvisibility=hidden -c $< -o $@
build/mouse_input.o: src/mouse_input.cpp src/mouse_input.h src/sdk.h src/native_viewport.h Makefile
	$(CXX) $(CXXFLAGS) -fvisibility=hidden -c $< -o $@
# The engine's vgui.so (game root, no SONAME) resolves the VGUI1 symbols at run
# time; an empty stub with that name only records the dependency at link time.
build/stub/vgui.so:
	mkdir -p build/stub
	printf '' | $(CC) $(COMMON) -shared -nostdlib -x c - -o $@
build/client.so: build/bridge.o build/render.o build/native_viewport.o build/mouse_input.o build/stub/vgui.so $(RUNTIME)
	$(CXX) $(COMMON) -shared $(filter %.o,$^) $(LDLIBS) -Lbuild/stub -Wl,--no-as-needed -l:vgui.so -Wl,--as-needed -Wl,-rpath,'$$ORIGIN/../..' -ldl -lGL -lSDL2 -Wl,--exclude-libs,ALL -o $@
build/smoke: tests/smoke.cpp src/sdk.h build/engine_slot_names.inc
	$(CXX) $(CXXFLAGS) -Ibuild $< -ldl -o $@
build/mouse_input_test: tests/mouse_input.cpp src/mouse_input.cpp src/mouse_input.h src/native_viewport.h src/sdk.h
	$(CXX) $(CXXFLAGS) tests/mouse_input.cpp src/mouse_input.cpp -o $@
# DLL snapshots keep their original timestamps. Check the ABI even when a
# newly selected DLL happens to be older than the generated headers.
.PHONY: force-vgui-headers
force-vgui-headers:
build/vgui_headers.stamp: force-vgui-headers tools/prepare_vgui.py vgui.def $(wildcard refs/vgui.dll) $(wildcard ../halflife/utils/vgui/include/*.h)
	python3 tools/prepare_vgui.py
build/vgui.lib: build/vgui_headers.stamp
	llvm-dlltool -m i386 -d build/vgui.def -l $@
build/vgui_host.obj: src/vgui_host.cpp src/vgui_host_api.h build/vgui_headers.stamp
	clang-cl /nologo /c /GS- /GR- /EHs-c- /Zl /O1 /clang:-fno-builtin /I build/win_include /I build/vgui_include /I src /Fo:$@ --target=i686-pc-windows-msvc $<
build/vgui_host.dll: build/vgui_host.obj build/vgui.lib
	lld-link /dll /noentry /nodefaultlib /machine:x86 /base:0x20000000 /out:$@ $^
