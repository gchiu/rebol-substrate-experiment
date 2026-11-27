CC       ?= cc
CFLAGS   ?= -std=c17 -Wall -Wextra -O2 -g
CPPFLAGS += -Iinclude
PKG_CONFIG ?= pkg-config

SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS   := $(shell $(PKG_CONFIG) --libs sdl2)
CAIRO_CFLAGS := $(shell $(PKG_CONFIG) --cflags cairo)
# INTENT-4B-5A: the SDL backend decodes JPEG images (PNG through cairo)
CAIRO_LIBS   := $(shell $(PKG_CONFIG) --libs cairo libjpeg)
PANGO_CFLAGS := $(shell $(PKG_CONFIG) --cflags pangocairo)
PANGO_LIBS   := $(shell $(PKG_CONFIG) --libs pangocairo)

BUILD := build
# GLON-MEM-3 (A): the generated runtime dependency lists (see "runtime
# dependencies" below); included here, before any rule that uses them
GV_CORE_DEPS_MK := $(BUILD)/gv-core-deps.mk
-include $(GV_CORE_DEPS_MK)
EVIDENCE := evidence

.PHONY: all test test-view test-view-asan test-table test-table-asan test-table-host demo demo-window view-demo test-glon glon-demo table-demo table-glon-demo bench-table probe headless_probe measure bench-text bench-text-big test-vocab test-address clean

all: test

$(BUILD):
	mkdir -p $(BUILD)

$(EVIDENCE):
	mkdir -p $(EVIDENCE)

$(BUILD)/gv_backend_headless.o: src/gv_backend_headless.c include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/gv_backend_sdl.o: src/gv_backend_sdl.c include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) -c $< -o $@

$(BUILD)/gv_native_win32.o: src/gv_native_win32.c include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/gv_view.o: src/gv_view.c include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/gv_table.o: src/gv_table.c include/gv_table.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/test_table.o: tests/test_table.c include/gv_table.h include/gv_view.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/test_table: $(BUILD)/test_table.o $(BUILD)/gv_table.o $(BUILD)/gv_view.o $(BUILD)/gv_backend_headless.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(BUILD)/table_demo: tests/table_demo.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(GV_GLON_CORE) $(GV_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h glon/demo.glon glon/demo.spec | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I.. -o $@ tests/table_demo.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(GV_GLON_CORE) -lm

$(BUILD)/test_table_host: tests/test_table_host.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(GV_GLON_CORE) $(GV_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h glon/demo.glon glon/demo.spec | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I.. -o $@ tests/test_table_host.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(GV_GLON_CORE) -lm

$(BUILD)/bench_table: tests/bench_table.c src/gv_table.c src/gv_view.c src/gv_backend_headless.c include/gv_table.h include/gv_view.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/bench_table.c src/gv_table.c src/gv_view.c src/gv_backend_headless.c -lm

$(BUILD)/test_backend.o: tests/test_backend.c include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/gv_demo.o: tests/gv_demo.c include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/native_probe.o: tests/native_probe.c include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/headless_probe.o: tests/headless_probe.c include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/test_view.o: tests/test_view.c include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/view_demo.o: tests/view_demo.c include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/test_backend: $(BUILD)/test_backend.o $(BUILD)/gv_backend_headless.o $(BUILD)/gv_backend_sdl.o $(BUILD)/gv_native_win32.o
	$(CC) $(CFLAGS) -o $@ $^ $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm

$(BUILD)/gv_demo: $(BUILD)/gv_demo.o $(BUILD)/gv_backend_headless.o $(BUILD)/gv_backend_sdl.o $(BUILD)/gv_native_win32.o
	$(CC) $(CFLAGS) -o $@ $^ $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm

$(BUILD)/native_probe: $(BUILD)/native_probe.o $(BUILD)/gv_native_win32.o
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/headless_probe: $(BUILD)/headless_probe.o $(BUILD)/gv_backend_headless.o
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/test_view: $(BUILD)/test_view.o $(BUILD)/gv_view.o $(BUILD)/gv_backend_headless.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(BUILD)/view_demo: $(BUILD)/view_demo.o $(BUILD)/gv_view.o $(BUILD)/gv_backend_headless.o $(BUILD)/gv_backend_sdl.o $(BUILD)/gv_native_win32.o
	$(CC) $(CFLAGS) -o $@ $^ $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm

test: $(BUILD)/test_backend $(BUILD)/test_view $(BUILD)/test_glon $(BUILD)/test_table $(BUILD)/test_table_host_1m $(BUILD)/test_table_data $(BUILD)/test_vid_widgets $(EVIDENCE)
	SDL_VIDEODRIVER=dummy ./$(BUILD)/test_backend
	./$(BUILD)/test_view
	./$(BUILD)/test_table
	./$(BUILD)/test_glon
	./$(BUILD)/test_table_host_1m ..
	./$(BUILD)/test_table_data ..
	./$(BUILD)/test_vid_widgets ..

test-table-host: $(BUILD)/test_table_host_1m
	./$(BUILD)/test_table_host_1m ..
	./$(BUILD)/test_table_data ..
	./$(BUILD)/test_vid_widgets ..

test-view: $(BUILD)/test_view
	./$(BUILD)/test_view

test-table: $(BUILD)/test_table
	./$(BUILD)/test_table

$(BUILD)/test_table_asan: tests/test_table.c src/gv_table.c src/gv_view.c src/gv_backend_headless.c include/gv_table.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude -o $@ tests/test_table.c src/gv_table.c src/gv_view.c src/gv_backend_headless.c -lm

test-table-asan: $(BUILD)/test_table_asan
	./$(BUILD)/test_table_asan

table-demo: $(BUILD)/table_demo
	./$(BUILD)/table_demo ..

bench-table: $(BUILD)/bench_table
	./$(BUILD)/bench_table 300
	./$(BUILD)/bench_table 10000
	./$(BUILD)/bench_table 100000

demo: $(BUILD)/gv_demo $(EVIDENCE)
	SDL_VIDEODRIVER=dummy ./$(BUILD)/gv_demo

demo-window: $(BUILD)/gv_demo
	./$(BUILD)/gv_demo

view-demo: $(BUILD)/view_demo $(EVIDENCE)
	SDL_VIDEODRIVER=dummy ./$(BUILD)/view_demo

probe: $(BUILD)/native_probe
	./$(BUILD)/native_probe

headless_probe: $(BUILD)/headless_probe
	./$(BUILD)/headless_probe

clean:
	rm -rf $(BUILD)
# AddressSanitizer/UBSan build for the View core
$(BUILD)/test_view_asan: tests/test_view.c src/gv_view.c src/gv_backend_headless.c include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude -o $@ tests/test_view.c src/gv_view.c src/gv_backend_headless.c -lm

test-view-asan: $(BUILD)/test_view_asan
	./$(BUILD)/test_view_asan

# ---- GV-1 Glon/VID integration -------------------------------------------
GV_GLON_CORE = ../r0_s1_g1a_live.c ../r0_s1_g1a.c ../r0_s1_show.c ../r0_s1_session.c ../r0_s1_runtime.c ../s1.c
GV_GLON_SRC = src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c src/gv_backend_sdl.c src/gv_native_win32.c

$(BUILD)/test_glon: tests/test_glon.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c glon/demo.glon glon/demo.spec $(GV_GLON_CORE) $(GV_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I.. -o $@ tests/test_glon.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(GV_GLON_CORE) -lm

$(BUILD)/glon_demo: tests/glon_demo.c $(GV_GLON_SRC) $(GV_GLON_CORE) $(GV_CORE_DEPS) glon/demo.glon glon/demo.spec include/gv_glon_host.h include/gv_view.h include/gv_table.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) -o $@ tests/glon_demo.c $(GV_GLON_SRC) $(GV_GLON_CORE) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm

test-glon: $(BUILD)/test_glon
	./$(BUILD)/test_glon

glon-demo: $(BUILD)/glon_demo
	SDL_VIDEODRIVER=dummy ./$(BUILD)/glon_demo --root .. --screenshot evidence/gv2a-glon.png

# ---- GV-2A Glon application runner (1M profile) --------------------------
# GLON-VIEW-CORE-V3: the 256K / 1M / 4M runtimes are C2's generated memory
# profiles (profiles/desktop-*.profile; scripts/glon-profile.sh), built from
# the root runtime sources with the profile header force-included and the
# profile's generated S1 copy; the 64K runtime is the constrained reference
# (frozen s1.c, header defaults).  The former experiments/ms1 alternate headers
# are not used.  MS1_FLAGS / MS1_FLAGS_4M / MS1_FLAGS_256K keep their names
# (the 1M, 4M and 256K profile selections) so the rules below are unchanged.
PROFILE_DIR := ../build/profile
MS1_FLAGS := -include $(PROFILE_DIR)/desktop-1m/glon_profile.h
MS1_FLAGS_4M := -include $(PROFILE_DIR)/desktop-4m/glon_profile.h
MS1_FLAGS_256K := -include $(PROFILE_DIR)/desktop-256k/glon_profile.h
S1_1M := $(PROFILE_DIR)/desktop-1m/s1_profile.c
S1_4M := $(PROFILE_DIR)/desktop-4m/s1_profile.c
S1_256K := $(PROFILE_DIR)/desktop-256k/s1_profile.c
PROFILE_FILES := $(foreach p,desktop-256k desktop-1m desktop-4m,$(PROFILE_DIR)/$(p)/glon_profile.h $(PROFILE_DIR)/$(p)/s1_profile.c)
# the root Makefile's own generation rule (sh scripts/glon-profile.sh), run
# directly so that make -n sees it (tests/build_deps_test.sh)
$(PROFILE_DIR)/%/glon_profile.h $(PROFILE_DIR)/%/s1_profile.c: ../profiles/%.profile ../scripts/glon-profile.sh ../s1.c ../s1.h
	sh ../scripts/glon-profile.sh ../profiles/$*.profile $(PROFILE_DIR)/$*
# the runtime sources (the S1 object is chosen per profile in each recipe)
MS1_CORE := ../r0_s1_g1a_live.c ../r0_s1_g1a.c ../r0_s1_show.c ../r0_s1_session.c ../r0_s1_runtime.c
MS1_CORE_DEPS := $(GV_CORE_DEPS) ../r0_s1_layout.h ../m1_layout.h $(PROFILE_FILES)

# GLON-MEM-3 (A): runtime dependencies (GLON-VIEW-CORE-V3: the profiled
# builds compile the root runtime sources and depend on the generated profile
# files above; the constrained build's list is generated).  The compiler lists every file the runtime sources
# include (cc -MM) into small generated makefiles; make regenerates them when a
# listed file changes and re-reads them.  Each rule naming $(MS1_CORE) or
# $(GV_GLON_CORE) as a prerequisite also depends on its list.
# (the lists are included near the top: a rule's prerequisites are expanded
# when it is read)
$(GV_CORE_DEPS_MK): $(GV_GLON_CORE) $(GV_CORE_DEPS) Makefile | $(BUILD)
	@{ printf 'GV_CORE_DEPS :='; $(CC) -MM -I.. $(GV_GLON_CORE) | sed -e 's/^[^:]*://' -e 's/\\$$//' | tr '\n' ' '; echo; } > $@.tmp && mv $@.tmp $@
# the build-dependency regression test (no file is modified: make -n -W)
test-build-deps:
	@sh tests/build_deps_test.sh


$(BUILD)/test_table_host_1m: tests/test_table_host.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h ../s1.h ../r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS) -o $@ tests/test_table_host.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) $(S1_1M) -lm

$(BUILD)/test_table_data: tests/test_table_data.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h ../s1.h ../r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS) -o $@ tests/test_table_data.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) $(S1_1M) -lm

test-table-data: $(BUILD)/test_table_data
	./$(BUILD)/test_table_data ..
	./$(BUILD)/test_vid_widgets ..

$(BUILD)/test_vid_widgets: tests/test_vid_widgets.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h ../s1.h ../r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS) -o $@ tests/test_vid_widgets.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) $(S1_1M) -lm

test-vid-widgets: $(BUILD)/test_vid_widgets
	./$(BUILD)/test_vid_widgets ..

$(BUILD)/widget_gallery_shot: tests/widget_gallery_shot.c $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h include/gv_backend.h ../s1.h ../r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS) -o $@ tests/widget_gallery_shot.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_1M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm

widget-gallery-shot: $(BUILD)/widget_gallery_shot

test-widget-gallery: $(BUILD)/widget_gallery_shot
	./tests/test_widget_gallery.sh

test-gui-core: $(BUILD)/widget_gallery_shot
	./tests/test_gui_core.sh

test-gui3: $(BUILD)/widget_gallery_shot
	./tests/test_gui3.sh

test-gui4: $(BUILD)/widget_gallery_shot
	./tests/test_gui4.sh

test-gui4-scroll: $(BUILD)/widget_gallery_shot
	./tests/test_gui4_scroll.sh

test-gui-f1b:
	./tests/test_gui_f1b.sh

$(BUILD)/test_vid_load_64k: tests/test_vid_load_64k.c $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h include/gv_backend.h ../s1.h ../r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. -o $@ tests/test_vid_load_64k.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) ../s1.c -lm

test-vid-load-64k: $(BUILD)/test_vid_load_64k
	$(BUILD)/test_vid_load_64k ..

$(BUILD)/test_global_capacity_1m: tests/test_global_capacity.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h include/gv_backend.h ../s1.h ../r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS) -o $@ tests/test_global_capacity.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) $(S1_1M) -lm

# ---- GUI-F2 bounded GUI state and focused input --------------------------
GUI_F2_CASES = widgets focus updates nofocus multifield clipboard destroy gc

$(BUILD)/test_gui_f2: tests/test_gui_f2.c tests/fixtures/gui_f2_app.glon glon/gui3_lib.glon src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h include/gv_backend.h ../s1.h ../r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS) -o $@ tests/test_gui_f2.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) $(S1_1M) -lm

# ---- GUI-4B2C scrolling, clipping and widget lifecycle -------------------
GUI_B2C_CASES = wheel nested clip activate edit gc collide rebuild stale resize
$(BUILD)/test_gui4b2c: tests/test_gui4b2c.c tests/fixtures/gui4b2c_app.glon glon/gui3_lib.glon $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h include/gv_backend.h ../s1.h ../r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS) -o $@ tests/test_gui4b2c.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_1M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm

# ---- Checkpoint A: the WG-3 gallery through the interactive SDL runner ---
WG_RUNNER_CASES = nav type hover theme resize checks keys clear
$(BUILD)/test_wg_runner: tests/test_wg_runner.c glon/widget_gallery.glon $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS) -o $@ tests/test_wg_runner.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_1M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm

test-wg-runner: $(BUILD)/test_wg_runner $(BUILD)/widget_gallery_shot
	@SDL_VIDEODRIVER=dummy ./$(BUILD)/widget_gallery_shot --root .. --app view/glon/widget_gallery.glon --width 1200 --height 700 --screenshot /tmp/wg-headless.png > /dev/null 2>&1
	@./$(BUILD)/test_wg_runner .. launch /tmp/wg-headless.png
	@set -e; for c in $(WG_RUNNER_CASES); do ./$(BUILD)/test_wg_runner .. $$c; done

# ---- Checkpoint B: VID presentation over GUI-3 behaviour -------------------
VG_RUNNER_CASES = look hover press edit keys disabled theme gc
$(BUILD)/test_vg_runner: tests/test_vg_runner.c tests/fixtures/vg_app.glon glon/vid_gui3.glon glon/gui3_lib.glon $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS) -o $@ tests/test_vg_runner.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_1M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm

test-vg-runner: $(BUILD)/test_vg_runner
	@set -e; for c in $(VG_RUNNER_CASES); do ./$(BUILD)/test_vg_runner .. $$c; done

# ---- GUI-P2: interactive cost through the SDL runner path ---------------
# bench_gui_p2 times operations; bench_gui_p2_prof is the same program on a
# -DR0_S1_PROFILE runtime (diagnostic build) and adds allocation counts.
$(BUILD)/bench_gui_p2: tests/bench_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS) -o $@ tests/bench_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_1M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm
$(BUILD)/bench_gui_p2_prof: tests/bench_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0_S1_PROFILE -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS) -o $@ tests/bench_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_1M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm

# GUI-P2 responsive resizing, through the SDL runner path
GUI_P2_CASES = sizes rapid clicks edit state themes lifecycle reveal scrollbars press caret edges edges-default tabs tabs-resize tabs-lifecycle table table-unicode source table-drag source-drag table-cols native-window capture edge-promise-cursor edge-promise-both edge-promise-default edge-clearance-6 edge-clearance-12 intent-demo-clearance-6 intent-demo-clearance-12 edge-policy-wslg edge-policy-default edge-policy-override
$(BUILD)/test_gui_p2: tests/test_gui_p2.c glon/vid_look.glon glon/intent4_demo.glon glon/intent_forms_demo.glon glon/intent_select_demo.glon glon/widget_gallery.glon glon/vid_layout.glon glon/vid_tabs.glon glon/intent.glon glon/intent_test.glon glon/intent_demo.glon glon/intent_members.glon $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS) -o $@ tests/test_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_1M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm
# INTENT-6: the same program in the 4M profile (1,048,576 managed cells), for
# the membership application (INTENT-6.md 7: the 1M heap fragments)
$(BUILD)/test_gui_p2_4m: tests/test_gui_p2.c glon/vid_look.glon glon/intent4_demo.glon glon/intent_forms_demo.glon glon/intent_select_demo.glon glon/widget_gallery.glon glon/vid_layout.glon glon/vid_tabs.glon glon/intent.glon glon/intent_test.glon glon/intent_demo.glon glon/intent_members.glon $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS_4M) -o $@ tests/test_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_4M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm
# INTENT-7: the same with the linear set-word scan (-DR0S1_SET_LINEAR), for comparison
$(BUILD)/test_gui_p2_4m_setlin: tests/test_gui_p2.c glon/vid_look.glon glon/intent4_demo.glon glon/intent_forms_demo.glon glon/intent_select_demo.glon glon/widget_gallery.glon glon/vid_layout.glon glon/vid_tabs.glon glon/intent.glon glon/intent_test.glon glon/intent_demo.glon glon/intent_members.glon $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS_4M) -DR0S1_SET_LINEAR -o $@ tests/test_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_4M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm
# GLON-MEM-4: the collector-profiling build (-DR0S1_GC_PROF: C-side address
# ranges only, the emitted code is identical; tests/gcprof.h), for the
# intent6-gcprof cases
$(BUILD)/test_gui_p2_4m_gcprof: tests/test_gui_p2.c tests/gcprof.h $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS_4M) -DR0S1_GC_PROF -o $@ tests/test_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_4M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm
# GLON-MEM-5: the same with the kind-only closure check (the collector before
# GLON-MEM-5), and the plain 4M test with it, for the old/new measurements
$(BUILD)/test_gui_p2_4m_gcprof_walk: tests/test_gui_p2.c tests/gcprof.h $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS_4M) -DR0S1_GC_PROF -DR0S1_GC_CLOSURE_WALK_ALWAYS -o $@ tests/test_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_4M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm
$(BUILD)/test_gui_p2_4m_walk: tests/test_gui_p2.c glon/vid_look.glon glon/intent4_demo.glon glon/intent_forms_demo.glon glon/intent_select_demo.glon glon/widget_gallery.glon glon/vid_layout.glon glon/vid_tabs.glon glon/intent.glon glon/intent_test.glon glon/intent_demo.glon glon/intent_members.glon $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS_4M) -DR0S1_GC_CLOSURE_WALK_ALWAYS -o $@ tests/test_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_4M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm
# GLON-MEM-3 (E): the same tests with the GLON-MEM-2 hint rule
# (-DR0S1_ALLOC_HINT_MEM2), and in 1M, for the old/new measurements
$(BUILD)/test_gui_p2_4m_mem2: tests/test_gui_p2.c glon/vid_look.glon glon/intent4_demo.glon glon/intent_forms_demo.glon glon/intent_select_demo.glon glon/widget_gallery.glon glon/vid_layout.glon glon/vid_tabs.glon glon/intent.glon glon/intent_test.glon glon/intent_demo.glon glon/intent_members.glon $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS_4M) -DR0S1_ALLOC_HINT_MEM2 -o $@ tests/test_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_4M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm
$(BUILD)/test_gui_p2_1m: tests/test_gui_p2.c glon/vid_look.glon glon/intent4_demo.glon glon/intent_forms_demo.glon glon/intent_select_demo.glon glon/widget_gallery.glon glon/vid_layout.glon glon/vid_tabs.glon glon/intent.glon glon/intent_test.glon glon/intent_demo.glon glon/intent_members.glon $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS) -o $@ tests/test_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_1M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm
$(BUILD)/test_gui_p2_1m_mem2: tests/test_gui_p2.c glon/vid_look.glon glon/intent4_demo.glon glon/intent_forms_demo.glon glon/intent_select_demo.glon glon/widget_gallery.glon glon/vid_layout.glon glon/vid_tabs.glon glon/intent.glon glon/intent_test.glon glon/intent_demo.glon glon/intent_members.glon $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS) -DR0S1_ALLOC_HINT_MEM2 -o $@ tests/test_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_1M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm

GUI_P2_WSLG_CASES = edge-cursor-decision table-drag table-cols source-drag capture edges edge-promise-default edge-clearance-12 native-window
test-gui-p2: $(BUILD)/test_gui_p2
	@set -e; for c in $(GUI_P2_CASES); do ./$(BUILD)/test_gui_p2 .. $$c; done
	@# the WSLg edge-band defaults (12 px, cursors) must not disturb the
	@# gallery's drags; and no resize cursor without a resize there
	@set -e; for c in $(GUI_P2_WSLG_CASES); do GV_ASSUME_WSLG=1 ./$(BUILD)/test_gui_p2 .. $$c; done

# Diagnostic (INTENT-4B-3 cursor investigation): tests/x11_cursor_probe.c is
# an LD_PRELOAD probe recording the Xlib cursor calls and _NET_WM_MOVERESIZE
# messages a program makes.  test-x11-edge-cursors opens an X11 window and
# drives the edge cursors with events in the program's own SDL queue (no
# input reaches the desktop), then checks what SDL asked the X server.
$(BUILD)/x11_cursor_probe.so: tests/x11_cursor_probe.c | $(BUILD)
	$(CC) -shared -fPIC -O2 -o $@ tests/x11_cursor_probe.c -ldl -lX11
test-x11-edge-cursors: $(BUILD)/test_gui_p2 $(BUILD)/x11_cursor_probe.so
	@log=$$(mktemp); SDL_VIDEODRIVER=x11 GV_ASSUME_WSLG=1 GV_X11_PROBE_LOG=$$log LD_PRELOAD=$(CURDIR)/$(BUILD)/x11_cursor_probe.so ./$(BUILD)/test_gui_p2 .. x11-edge-cursors; rc=$$?; echo "probe log: $$log"; exit $$rc

# GLON-MEM-1: the 4M test program with the runtime's profiler counters
# (-DR0_S1_PROFILE: allocation and lookup counts), for intent6-mem
$(BUILD)/test_gui_p2_4m_pf: tests/test_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0_S1_PROFILE -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS_4M) -o $@ tests/test_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_4M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm
# (the profiled build cannot load the Intent stack: its counters fill the
# fixed masm code region -- GLON-MEM-1.md; intent6-mem runs in the plain 4M)
mem1-app: $(BUILD)/test_gui_p2_4m
	./$(BUILD)/test_gui_p2_4m .. intent6-mem

# INTENT-6: the membership application's acceptance tests (4M profile; each
# case starts from a copy of view/data/members-seed.tsv in build/intent6/)
MEMBERS_CASES = intent6-load intent6-select intent6-search intent6-new-edit intent6-validation intent6-unsaved intent6-delete intent6-reload intent6-keys intent6-resize intent6-stress intent6-scale
test-members: $(BUILD)/test_gui_p2_4m
	@set -e; for c in $(MEMBERS_CASES); do ./$(BUILD)/test_gui_p2_4m .. $$c; done
# INTENT-7: the same cases in the 1M profile (the second load works there now),
# and the INTENT-7 cases (the operations timed, search narrowing, the second
# load step by step) in both
INTENT7_CASES = intent7-search intent7-ops intent7-reload
test-members-1m: $(BUILD)/test_gui_p2_1m
	@set -e; for c in $(MEMBERS_CASES) $(INTENT7_CASES); do ./$(BUILD)/test_gui_p2_1m .. $$c; done
test-intent7: $(BUILD)/test_gui_p2_4m $(BUILD)/test_gui_p2_1m
	@set -e; for c in $(INTENT7_CASES); do ./$(BUILD)/test_gui_p2_4m .. $$c; ./$(BUILD)/test_gui_p2_1m .. $$c; done
# the scale measurement with 1,000 members (expected to fail to load: INTENT-6.md 7)
members-scale-1000: $(BUILD)/test_gui_p2_4m
	python3 -I tools/gen_members.py 1000 $(BUILD)/members-1000.tsv
	GV_MB_REPORT_SETUP=1 GV_MEMBERS_SOURCE=view/build/members-1000.tsv ./$(BUILD)/test_gui_p2_4m .. intent6-scale

# INTENT-1: the declarative dialect (glon/intent.glon) through the same runner
INTENT_CASES = intent-parse intent-directions intent-unequal intent-alias intent-nested intent-edit intent-escape intent-lifecycle intent-resize intent-demo intent-stretch intent-demo-resize intent-demo-native intent-handlers intent-foreign intent-foreign-tab intent-browser-mock webkit-address intent3-styles intent3-commands intent3-follow intent3-forms intent3-errors intent4-check intent4-tabs intent4-table intent4-demo-disabled intent4-edit intent4-forms-demo intent4-select intent4-select-demo intent4-layout intent4-fill intent4-state intent4-layout-demo theme-contrast intent5-theme intent5-look intent5-image intent5-gallery intent-members-handlers intent-members-forms intent-members-validate
test-intent: $(BUILD)/test_gui_p2
	@set -e; for c in $(INTENT_CASES); do ./$(BUILD)/test_gui_p2 .. $$c; done
# FOREIGN-1 on a real X11 display: the x11-edit component, no input sent
test-foreign-x11: $(BUILD)/test_gui_p2
	SDL_VIDEODRIVER=x11 ./$(BUILD)/test_gui_p2 .. intent-foreign-x11
# FOREIGN-2: a real browser engine (WebKitGTK 4.1, loaded at run time) in an
# Intent form (interactive; needs libwebkit2gtk-4.1-0)
browser-demo: $(BUILD)/glon_demo_1m
	./$(BUILD)/glon_demo_1m --root .. --vocab-vid --app view/glon/intent_browser.glon --width 1100 --height 640
# INTENT-3: the same browser demo, declared with styles, command and follow
browser3-demo: $(BUILD)/glon_demo_1m
	./$(BUILD)/glon_demo_1m --root .. --vocab-vid --app view/glon/intent3_browser.glon --width 1100 --height 640

# INTENT-3 with the real engine (X11 + WebKitGTK; a window opens, no input sent)
test-intent3-browser-x11: $(BUILD)/test_gui_p2
	SDL_VIDEODRIVER=x11 ./$(BUILD)/test_gui_p2 .. intent3-browser-x11

# FOREIGN-2 with the real engine (X11 + WebKitGTK; a window opens, no input sent)
test-browser-x11: $(BUILD)/test_gui_p2
	SDL_VIDEODRIVER=x11 ./$(BUILD)/test_gui_p2 .. intent-browser-x11
# FOREIGN-1: an independently rendered component in an Intent form (interactive)
foreign-demo: $(BUILD)/glon_demo_1m
	./$(BUILD)/glon_demo_1m --root .. --vocab-vid --app view/glon/intent_foreign.glon --width 760 --height 420
# the demo, on a real window (interactive)
# INTENT-4B-3: selection and data (an interactive window; the user runs it)
select-demo: $(BUILD)/glon_demo_1m
	./$(BUILD)/glon_demo_1m --root .. --vocab-vid --app view/glon/intent_select_demo.glon --width 720 --height 560

# INTENT-4B-5A: the appearance gallery, light and dark side by side (an
# interactive window; the user runs it)
gallery-demo: $(BUILD)/glon_demo_1m
	./$(BUILD)/glon_demo_1m --root .. --vocab-vid --app view/glon/intent_gallery_demo.glon --width 1180 --height 800

# INTENT-6: the membership application (an interactive window; the user runs
# it).  The 4M profile: with its records loaded the 1M heap fragments
# (INTENT-6.md 7).  Its data: view/data/members.tsv (made from the seed)
members-app: $(BUILD)/glon_demo_4m
	./$(BUILD)/glon_demo_4m --root .. --vocab-vid --app view/glon/membership_app.glon --width 1240 --height 820

# INTENT-4B-4: layout controls (an interactive window; the user runs it)
layout-demo: $(BUILD)/glon_demo_1m
	./$(BUILD)/glon_demo_1m --root .. --vocab-vid --app view/glon/intent_layout_demo.glon --width 900 --height 600

# INTENT-4B-2: a data-entry form (an interactive window; the user runs it)
forms-demo: $(BUILD)/glon_demo_1m
	./$(BUILD)/glon_demo_1m --root .. --vocab-vid --app view/glon/intent_forms_demo.glon --width 720 --height 520

# INTENT-4B: check, heading, disabled (an interactive window; the user runs it)
intent4-demo: $(BUILD)/glon_demo_1m
	./$(BUILD)/glon_demo_1m --root .. --vocab-vid --app view/glon/intent4_demo.glon --width 640 --height 400

intent-demo: $(BUILD)/glon_demo_1m
	./$(BUILD)/glon_demo_1m --root .. --vocab-vid --app view/glon/intent_demo.glon --width 640 --height 480

# the whole GUI regression set (builds its prerequisites; SDL dummy driver;
# add X11=1 to also run the GUI-P2 and WG runner suites on $$DISPLAY)
gui-regression:
	./tests/run_gui_regression.sh $(if $(X11),--x11,)

bench-gui-p2: $(BUILD)/bench_gui_p2 $(BUILD)/bench_gui_p2_prof
	./$(BUILD)/bench_gui_p2 .. gallery
	./$(BUILD)/bench_gui_p2 .. gui3
	./$(BUILD)/bench_gui_p2_prof .. gallery
	./$(BUILD)/bench_gui_p2_prof .. gui3

test-gui4b2c: $(BUILD)/test_gui4b2c
	@set -e; for c in $(GUI_B2C_CASES); do ./$(BUILD)/test_gui4b2c .. $$c; done

test-gui-f2: $(BUILD)/test_gui_f2
	@set -e; for c in $(GUI_F2_CASES); do ./$(BUILD)/test_gui_f2 .. $$c; done

test-global-capacity: $(BUILD)/test_global_capacity_1m
	$(BUILD)/test_global_capacity_1m .. 1000 1

GLOBAL_TEST_SRC = tests/test_global_capacity.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c
GLOBAL_TEST_DEPS = $(GLOBAL_TEST_SRC) $(MS1_CORE) include/gv_glon_host.h include/gv_view.h include/gv_table.h include/gv_backend.h ../s1.h ../r0_s1.h

$(BUILD)/test_global_capacity_64k: $(GLOBAL_TEST_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. -o $@ $(GLOBAL_TEST_SRC) $(MS1_CORE) ../s1.c -lm

$(BUILD)/test_global_capacity_256k: $(GLOBAL_TEST_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_256K) -o $@ $(GLOBAL_TEST_SRC) $(MS1_CORE) $(S1_256K) -lm

$(BUILD)/test_global_capacity_4m: $(GLOBAL_TEST_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ $(GLOBAL_TEST_SRC) $(MS1_CORE) $(S1_4M) -lm

test-global-capacity-profiles: $(BUILD)/test_global_capacity_64k $(BUILD)/test_global_capacity_256k $(BUILD)/test_global_capacity_1m $(BUILD)/test_global_capacity_4m
	$(BUILD)/test_global_capacity_64k .. 4 1 bare
	$(BUILD)/test_global_capacity_256k .. 500 1
	$(BUILD)/test_global_capacity_1m .. 1000 1
	$(BUILD)/test_global_capacity_4m .. 1000 1

# SITE-1: func-site capacity per profile (64K: the reference 640; larger
# profiles: GV_SITE_CAP), and the room an Intent application has left
SITE_TEST_SRC = tests/test_site_capacity.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c
SITE_TEST_DEPS = $(SITE_TEST_SRC) $(MS1_CORE) include/gv_glon_host.h ../s1.h ../r0_s1.h
$(BUILD)/test_site_capacity_64k: $(SITE_TEST_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. -o $@ $(SITE_TEST_SRC) $(MS1_CORE) ../s1.c -lm -ldl
$(BUILD)/test_site_capacity_256k: $(SITE_TEST_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_256K) -o $@ $(SITE_TEST_SRC) $(MS1_CORE) $(S1_256K) -lm -ldl
$(BUILD)/test_site_capacity_1m: $(SITE_TEST_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS) -o $@ $(SITE_TEST_SRC) $(MS1_CORE) $(S1_1M) -lm -ldl
$(BUILD)/test_site_capacity_4m: $(SITE_TEST_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ $(SITE_TEST_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
# GLON-MEM-1: allocation cost against the number of live blocks (headless,
# measurement only; GLON-MEM-1.md)
MEM1_SRC = tests/mem1_alloc_bench.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c
$(BUILD)/mem1_alloc_bench_1m: tests/mem1_alloc_bench.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS) -o $@ $(MEM1_SRC) $(MS1_CORE) $(S1_1M) -lm -ldl
$(BUILD)/mem1_alloc_bench_1m_mem2: tests/mem1_alloc_bench.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS) -DR0S1_ALLOC_HINT_MEM2 -o $@ $(MEM1_SRC) $(MS1_CORE) $(S1_1M) -lm -ldl
# INTENT-7: the hashed set-word search against the linear one
# (-DR0S1_SET_LINEAR, the _setlin builds): the GLON-MEM-5 closure workload and the
# GLON-MEM-2 allocator workload must print the same lines in every profile
$(BUILD)/mem5_closure_diff_64k_setlin: tests/mem5_closure_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_SET_LINEAR -Iinclude -I..  -o $@ $(MEM5_SRC) $(MS1_CORE) ../s1.c -lm -ldl
$(BUILD)/mem2_alloc_diff_64k_setlin: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_SET_LINEAR -Iinclude -I..  -o $@ $(MEM2_SRC) $(MS1_CORE) ../s1.c -lm -ldl
$(BUILD)/mem5_closure_diff_256k_setlin: tests/mem5_closure_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_SET_LINEAR -Iinclude -I.. $(MS1_FLAGS_256K) -o $@ $(MEM5_SRC) $(MS1_CORE) $(S1_256K) -lm -ldl
$(BUILD)/mem2_alloc_diff_256k_setlin: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_SET_LINEAR -Iinclude -I.. $(MS1_FLAGS_256K) -o $@ $(MEM2_SRC) $(MS1_CORE) $(S1_256K) -lm -ldl
$(BUILD)/mem5_closure_diff_1m_setlin: tests/mem5_closure_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_SET_LINEAR -Iinclude -I.. $(MS1_FLAGS) -o $@ $(MEM5_SRC) $(MS1_CORE) $(S1_1M) -lm -ldl
$(BUILD)/mem2_alloc_diff_1m_setlin: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_SET_LINEAR -Iinclude -I.. $(MS1_FLAGS) -o $@ $(MEM2_SRC) $(MS1_CORE) $(S1_1M) -lm -ldl
$(BUILD)/mem5_closure_diff_4m_setlin: tests/mem5_closure_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_SET_LINEAR -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ $(MEM5_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
$(BUILD)/mem2_alloc_diff_4m_setlin: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_SET_LINEAR -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ $(MEM2_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
intent7-set-diff: $(BUILD)/mem5_closure_diff_64k_setlin $(BUILD)/mem2_alloc_diff_64k_setlin $(BUILD)/mem5_closure_diff_64k $(BUILD)/mem2_alloc_diff_64k $(BUILD)/mem5_closure_diff_256k_setlin $(BUILD)/mem2_alloc_diff_256k_setlin $(BUILD)/mem5_closure_diff_256k $(BUILD)/mem2_alloc_diff_256k $(BUILD)/mem5_closure_diff_1m_setlin $(BUILD)/mem2_alloc_diff_1m_setlin $(BUILD)/mem5_closure_diff_1m $(BUILD)/mem2_alloc_diff_1m $(BUILD)/mem5_closure_diff_4m_setlin $(BUILD)/mem2_alloc_diff_4m_setlin $(BUILD)/mem5_closure_diff_4m $(BUILD)/mem2_alloc_diff_4m
	@sh tests/intent7_set_diff.sh $(BUILD)
# GLON-MEM-5: the mark-aware closure check against the kind-only one
# (-DR0S1_GC_CLOSURE_WALK_ALWAYS, the _walk builds): a closure-heavy
# differential workload, and the GC interior tests, in every profile
MEM5_SRC = tests/mem5_closure_diff.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c
$(BUILD)/mem5_closure_diff_64k: tests/mem5_closure_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I..  -o $@ $(MEM5_SRC) $(MS1_CORE) ../s1.c -lm -ldl
$(BUILD)/mem5_interior_64k: tests/mem5_interior.c ../r0_s1_gc_interior_tests.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I..  -o $@ tests/mem5_interior.c $(MS1_CORE) ../s1.c -lm
$(BUILD)/mem5_closure_diff_64k_walk: tests/mem5_closure_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_GC_CLOSURE_WALK_ALWAYS -Iinclude -I..  -o $@ $(MEM5_SRC) $(MS1_CORE) ../s1.c -lm -ldl
$(BUILD)/mem5_interior_64k_walk: tests/mem5_interior.c ../r0_s1_gc_interior_tests.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_GC_CLOSURE_WALK_ALWAYS -Iinclude -I..  -o $@ tests/mem5_interior.c $(MS1_CORE) ../s1.c -lm
$(BUILD)/mem5_closure_diff_256k: tests/mem5_closure_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_256K) -o $@ $(MEM5_SRC) $(MS1_CORE) $(S1_256K) -lm -ldl
$(BUILD)/mem5_interior_256k: tests/mem5_interior.c ../r0_s1_gc_interior_tests.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_256K) -o $@ tests/mem5_interior.c $(MS1_CORE) $(S1_256K) -lm
$(BUILD)/mem5_closure_diff_256k_walk: tests/mem5_closure_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_GC_CLOSURE_WALK_ALWAYS -Iinclude -I.. $(MS1_FLAGS_256K) -o $@ $(MEM5_SRC) $(MS1_CORE) $(S1_256K) -lm -ldl
$(BUILD)/mem5_interior_256k_walk: tests/mem5_interior.c ../r0_s1_gc_interior_tests.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_GC_CLOSURE_WALK_ALWAYS -Iinclude -I.. $(MS1_FLAGS_256K) -o $@ tests/mem5_interior.c $(MS1_CORE) $(S1_256K) -lm
$(BUILD)/mem5_closure_diff_1m: tests/mem5_closure_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS) -o $@ $(MEM5_SRC) $(MS1_CORE) $(S1_1M) -lm -ldl
$(BUILD)/mem5_interior_1m: tests/mem5_interior.c ../r0_s1_gc_interior_tests.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS) -o $@ tests/mem5_interior.c $(MS1_CORE) $(S1_1M) -lm
$(BUILD)/mem5_closure_diff_1m_walk: tests/mem5_closure_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_GC_CLOSURE_WALK_ALWAYS -Iinclude -I.. $(MS1_FLAGS) -o $@ $(MEM5_SRC) $(MS1_CORE) $(S1_1M) -lm -ldl
$(BUILD)/mem5_interior_1m_walk: tests/mem5_interior.c ../r0_s1_gc_interior_tests.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_GC_CLOSURE_WALK_ALWAYS -Iinclude -I.. $(MS1_FLAGS) -o $@ tests/mem5_interior.c $(MS1_CORE) $(S1_1M) -lm
$(BUILD)/mem5_closure_diff_4m: tests/mem5_closure_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ $(MEM5_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
$(BUILD)/mem5_interior_4m: tests/mem5_interior.c ../r0_s1_gc_interior_tests.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ tests/mem5_interior.c $(MS1_CORE) $(S1_4M) -lm
$(BUILD)/mem5_closure_diff_4m_walk: tests/mem5_closure_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_GC_CLOSURE_WALK_ALWAYS -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ $(MEM5_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
$(BUILD)/mem5_interior_4m_walk: tests/mem5_interior.c ../r0_s1_gc_interior_tests.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_GC_CLOSURE_WALK_ALWAYS -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ tests/mem5_interior.c $(MS1_CORE) $(S1_4M) -lm
mem5-closure-diff: $(BUILD)/mem5_closure_diff_64k $(BUILD)/mem5_interior_64k $(BUILD)/mem5_closure_diff_64k_walk $(BUILD)/mem5_interior_64k_walk $(BUILD)/mem5_closure_diff_256k $(BUILD)/mem5_interior_256k $(BUILD)/mem5_closure_diff_256k_walk $(BUILD)/mem5_interior_256k_walk $(BUILD)/mem5_closure_diff_1m $(BUILD)/mem5_interior_1m $(BUILD)/mem5_closure_diff_1m_walk $(BUILD)/mem5_interior_1m_walk $(BUILD)/mem5_closure_diff_4m $(BUILD)/mem5_interior_4m $(BUILD)/mem5_closure_diff_4m_walk $(BUILD)/mem5_interior_4m_walk
	@sh tests/mem5_closure_diff.sh $(BUILD)
# GLON-MEM-4: one collection's cost against N functions and K filler blocks
# (diagnostic build, -DR0S1_GC_PROF)
MEM4_SRC = tests/mem4_gc_scale.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c
$(BUILD)/mem4_gc_scale_4m: tests/mem4_gc_scale.c tests/gcprof.h $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_4M) -DR0S1_GC_PROF -o $@ $(MEM4_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
$(BUILD)/mem4_gc_scale_4m_walk: tests/mem4_gc_scale.c tests/gcprof.h $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_4M) -DR0S1_GC_PROF -DR0S1_GC_CLOSURE_WALK_ALWAYS -o $@ $(MEM4_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
mem4-gc-scale: $(BUILD)/mem4_gc_scale_4m
	$(BUILD)/mem4_gc_scale_4m
# GLON-MEM-4: the application profile (the collection edit + save triggers,
# one collection by itself with the replay's counts, and the same at each step)
mem4-gcprof: $(BUILD)/test_gui_p2_4m_gcprof
	./$(BUILD)/test_gui_p2_4m_gcprof .. intent6-gcprof-save
	./$(BUILD)/test_gui_p2_4m_gcprof .. intent6-gcprof
	./$(BUILD)/test_gui_p2_4m_gcprof .. intent6-gcprof-scale
$(BUILD)/mem1_alloc_bench_4m: tests/mem1_alloc_bench.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ $(MEM1_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
$(BUILD)/mem1_alloc_bench_4m_mem2: tests/mem1_alloc_bench.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_4M) -DR0S1_ALLOC_HINT_MEM2 -o $@ $(MEM1_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
# GLON-MEM-2: the free-block hint against the old scan from the heap base:
# the same workload in both builds must leave the same heap, chunk by chunk
MEM2_SRC = tests/mem2_alloc_diff.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c
$(BUILD)/mem2_alloc_diff_64k: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I..  -o $@ $(MEM2_SRC) $(MS1_CORE) ../s1.c -lm -ldl
$(BUILD)/mem2_alloc_diff_64k_base: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_ALLOC_SCAN_FROM_BASE -Iinclude -I..  -o $@ $(MEM2_SRC) $(MS1_CORE) ../s1.c -lm -ldl
$(BUILD)/mem2_alloc_diff_64k_mem2: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_ALLOC_HINT_MEM2 -Iinclude -I..  -o $@ $(MEM2_SRC) $(MS1_CORE) ../s1.c -lm -ldl
$(BUILD)/mem2_alloc_diff_256k: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_256K) -o $@ $(MEM2_SRC) $(MS1_CORE) $(S1_256K) -lm -ldl
$(BUILD)/mem2_alloc_diff_256k_base: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_ALLOC_SCAN_FROM_BASE -Iinclude -I.. $(MS1_FLAGS_256K) -o $@ $(MEM2_SRC) $(MS1_CORE) $(S1_256K) -lm -ldl
$(BUILD)/mem2_alloc_diff_256k_mem2: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_ALLOC_HINT_MEM2 -Iinclude -I.. $(MS1_FLAGS_256K) -o $@ $(MEM2_SRC) $(MS1_CORE) $(S1_256K) -lm -ldl
$(BUILD)/mem2_alloc_diff_1m: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS) -o $@ $(MEM2_SRC) $(MS1_CORE) $(S1_1M) -lm -ldl
$(BUILD)/mem2_alloc_diff_1m_base: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_ALLOC_SCAN_FROM_BASE -Iinclude -I.. $(MS1_FLAGS) -o $@ $(MEM2_SRC) $(MS1_CORE) $(S1_1M) -lm -ldl
$(BUILD)/mem2_alloc_diff_1m_mem2: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_ALLOC_HINT_MEM2 -Iinclude -I.. $(MS1_FLAGS) -o $@ $(MEM2_SRC) $(MS1_CORE) $(S1_1M) -lm -ldl
$(BUILD)/mem2_alloc_diff_4m: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ $(MEM2_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
$(BUILD)/mem2_alloc_diff_4m_base: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_ALLOC_SCAN_FROM_BASE -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ $(MEM2_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
$(BUILD)/mem2_alloc_diff_4m_mem2: tests/mem2_alloc_diff.c $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DR0S1_ALLOC_HINT_MEM2 -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ $(MEM2_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
# GLON-MEM-3 (D): each profile in three builds -- the current hint, the
# GLON-MEM-2 hint (-DR0S1_ALLOC_HINT_MEM2) and the scan from the base
mem2-alloc-diff: $(BUILD)/mem2_alloc_diff_64k $(BUILD)/mem2_alloc_diff_64k_base $(BUILD)/mem2_alloc_diff_256k $(BUILD)/mem2_alloc_diff_256k_base $(BUILD)/mem2_alloc_diff_1m $(BUILD)/mem2_alloc_diff_1m_base $(BUILD)/mem2_alloc_diff_4m $(BUILD)/mem2_alloc_diff_4m_base $(BUILD)/mem2_alloc_diff_64k_mem2 $(BUILD)/mem2_alloc_diff_256k_mem2 $(BUILD)/mem2_alloc_diff_1m_mem2 $(BUILD)/mem2_alloc_diff_4m_mem2
	@sh tests/mem2_alloc_diff.sh $(BUILD)

mem1-alloc-bench: $(BUILD)/mem1_alloc_bench_1m $(BUILD)/mem1_alloc_bench_4m
	$(BUILD)/mem1_alloc_bench_1m
	$(BUILD)/mem1_alloc_bench_4m

test-site-capacity-profiles: $(BUILD)/test_site_capacity_64k $(BUILD)/test_site_capacity_256k $(BUILD)/test_site_capacity_1m $(BUILD)/test_site_capacity_4m
	$(BUILD)/test_site_capacity_64k ..
	$(BUILD)/test_site_capacity_256k ..
	$(BUILD)/test_site_capacity_1m ..
	$(BUILD)/test_site_capacity_4m ..
	$(BUILD)/test_site_capacity_1m .. intent view/glon/intent_select_demo.glon
	$(BUILD)/test_site_capacity_1m .. intent view/glon/intent_test.glon

$(BUILD)/glon_demo_1m: tests/glon_demo.c $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) glon/demo.glon glon/demo.spec include/gv_glon_host.h include/gv_view.h include/gv_table.h include/gv_backend.h ../s1.h ../r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS) -o $@ tests/glon_demo.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_1M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm

$(BUILD)/glon_demo_4m: tests/glon_demo.c $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) glon/demo.glon glon/demo.spec include/gv_glon_host.h include/gv_view.h include/gv_table.h include/gv_backend.h ../s1.h ../r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS_4M) -o $@ tests/glon_demo.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_4M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm

glon-demo-4m: $(BUILD)/glon_demo_4m
	SDL_VIDEODRIVER=dummy ./$(BUILD)/glon_demo_4m --root .. --app view/glon/membership.glon --vid --width 1000 --height 650 --screenshot evidence/gv2b-membership-4m.png

$(BUILD)/test_vid_widgets_4m: tests/test_vid_widgets.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h ../s1.h ../r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ tests/test_vid_widgets.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(MS1_CORE) $(S1_4M) -lm

test-vid-widgets-4m: $(BUILD)/test_vid_widgets_4m
	./$(BUILD)/test_vid_widgets_4m ..

table-glon-demo: $(BUILD)/glon_demo_1m
	SDL_VIDEODRIVER=dummy ./$(BUILD)/glon_demo_1m --root .. --app view/glon/table_demo.glon --width 900 --height 420 --screenshot evidence/gv2a-table-demo.png

table1-demo: $(BUILD)/glon_demo_1m
	SDL_VIDEODRIVER=dummy ./$(BUILD)/glon_demo_1m --root .. --app view/glon/table1.glon --width 700 --height 300 --screenshot evidence/gv2a-table1.png

table3-demo: $(BUILD)/glon_demo_1m
	SDL_VIDEODRIVER=dummy ./$(BUILD)/glon_demo_1m --root .. --app view/glon/table3.glon --width 760 --height 360 --screenshot evidence/gv2a-table3.png

table-golden: $(BUILD)/glon_demo_1m
	SDL_VIDEODRIVER=dummy ./$(BUILD)/glon_demo_1m --root .. --app view/glon/table1.glon --width 700 --height 300 --screenshot evidence/gv2a-table1.png
	cd .. && sha256sum -c view/evidence/gv2a-table1.sha256
	SDL_VIDEODRIVER=dummy ./$(BUILD)/glon_demo_1m --root .. --app view/glon/table3.glon --width 760 --height 360 --screenshot evidence/gv2a-table3.png
	cd .. && sha256sum -c view/evidence/gv2a-table3.sha256

membership-golden: $(BUILD)/glon_demo_1m
	SDL_VIDEODRIVER=dummy ./$(BUILD)/glon_demo_1m --root .. --app view/glon/membership.glon --vid --width 1000 --height 650 --screenshot evidence/gv2b-membership.png
	cd .. && sha256sum -c view/evidence/gv2b-membership.sha256

# ---- GV-1A measurement ---------------------------------------------------
$(BUILD)/measure_gv1: tests/measure_gv1.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(GV_GLON_CORE) $(GV_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h include/gv_backend.h glon/demo.glon glon/demo.spec | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I.. -o $@ tests/measure_gv1.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(GV_GLON_CORE) -lm

measure: $(BUILD)/measure_gv1
	./$(BUILD)/measure_gv1 ..

$(BUILD)/bench_text: tests/bench_text.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(GV_GLON_CORE) $(GV_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h glon/demo.glon glon/demo.spec | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I.. -o $@ tests/bench_text.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(GV_GLON_CORE) -lm

bench-text: $(BUILD)/bench_text
	./$(BUILD)/bench_text ..

# ---- GV-1A vocabulary experiment ----------------------------------------
test-vocab: ../glon
	sh tests/test_vocab.sh


$(BUILD)/test_address: tests/test_address.c src/gv_view.c src/gv_backend_headless.c include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/test_address.c src/gv_view.c src/gv_backend_headless.c -lm

test-address: $(BUILD)/test_address
	./$(BUILD)/test_address

# Existing larger-managed-heap test build (11,232 cells; loader 10,800).
$(BUILD)/bench_text_big: tests/bench_text.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(GV_GLON_CORE) $(GV_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_table.h glon/demo.glon glon/demo.spec | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I.. -DR0S1_GC_OVERFLOW_TEST_BUILD -o $@ tests/bench_text.c src/gv_glon_host.c src/gv_host.c src/gv_host_x11.c src/gv_host_webkit.c src/gv_view.c src/gv_table.c src/gv_backend_headless.c $(GV_GLON_CORE) -lm

bench-text-big: $(BUILD)/bench_text_big
	./$(BUILD)/bench_text_big .. 1000

# GLON-LIB-1: the collection and text vocabulary (demo/shop/list.glon,
# demo/shop/strings-text.glon) in a plain session, per profile
LIB1_SRC = tests/lib1_tests.c
$(BUILD)/lib1_tests_64k: $(LIB1_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. -o $@ $(LIB1_SRC) $(MS1_CORE) ../s1.c -lm -ldl
$(BUILD)/lib1_tests_1m: $(LIB1_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS) -o $@ $(LIB1_SRC) $(MS1_CORE) $(S1_1M) -lm -ldl
$(BUILD)/lib1_tests_4m: $(LIB1_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(MS1_FLAGS_4M) -o $@ $(LIB1_SRC) $(MS1_CORE) $(S1_4M) -lm -ldl
test-lib1: $(BUILD)/lib1_tests_64k $(BUILD)/lib1_tests_1m $(BUILD)/lib1_tests_4m
	@set -e; for p in 64k 1m 4m; do rc=0; timeout 600 ./$(BUILD)/lib1_tests_$$p .. > $(BUILD)/lib1-$$p.txt 2>&1 || rc=$$?; \
	  grep -v '^\[dump\]' $(BUILD)/lib1-$$p.txt; echo "lib1_tests_$$p: exit $$rc"; [ $$rc -eq 0 ]; done

# GLON-INST-1 Stage 1: reusable components (one definition, several
# instances with the same internal names), in 4M and 1M
INST1_CASES = inst1-components inst1-scale
test-inst1: $(BUILD)/test_gui_p2_4m $(BUILD)/test_gui_p2_1m
	@set -e; for c in $(INST1_CASES); do ./$(BUILD)/test_gui_p2_4m .. $$c; ./$(BUILD)/test_gui_p2_1m .. $$c; done
# (interactive, on the user's display only when asked)
components-demo: $(BUILD)/glon_demo_1m
	./$(BUILD)/glon_demo_1m --root .. --vocab-vid --app view/glon/intent_components_demo.glon --width 900 --height 760

# GLON-INST-2: several layout instances in one window (interactive; on the
# user's display only when asked)
instances-demo: $(BUILD)/glon_demo_1m
	./$(BUILD)/glon_demo_1m --root .. --vocab-vid --app view/glon/intent_instances_demo.glon --width 760 --height 560
# GLON-INST-2: layout instances in one window (open, switch, close, routing,
# node ids; 1 .. 25 instances), in 4M and 1M
INST2_CASES = inst2-instances inst2-scale
test-inst2: $(BUILD)/test_gui_p2_4m $(BUILD)/test_gui_p2_1m
	@set -e; for c in $(INST2_CASES); do ./$(BUILD)/test_gui_p2_4m .. $$c; ./$(BUILD)/test_gui_p2_1m .. $$c; done
# GLON-INST-2: the collector-profiling build in the 1M profile (diagnostic)
$(BUILD)/test_gui_p2_1m_gcprof: tests/test_gui_p2.c tests/gcprof.h $(GV_GLON_SRC) $(MS1_CORE) $(MS1_CORE_DEPS) include/gv_glon_host.h include/gv_view.h include/gv_backend.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude -I.. $(SDL_CFLAGS) $(CAIRO_CFLAGS) $(PANGO_CFLAGS) $(MS1_FLAGS) -DR0S1_GC_PROF -o $@ tests/test_gui_p2.c $(GV_GLON_SRC) $(MS1_CORE) $(S1_1M) $(SDL_LIBS) $(CAIRO_LIBS) $(PANGO_LIBS) -lm
# GLON-INST-3A: node ids past 1,023, rapid reuse, rollback (4M and 1M)
test-inst3: $(BUILD)/test_gui_p2_4m $(BUILD)/test_gui_p2_1m
	@set -e; for c in inst3-lifecycle; do ./$(BUILD)/test_gui_p2_4m .. $$c; ./$(BUILD)/test_gui_p2_1m .. $$c; done
# GLON-INST-3B: native windows, one session (SDL dummy driver, synthetic
# events): routing, focus, independent state and lifecycle, failures, in 4M
# and 1M; the heap check against a filled heap (1M; 4M takes ~2 minutes:
# make test-win3-heap-4m); the 1 .. 10 window measurements (4M)
WIN3_CASES = win3-windows win3-lifecycle win3-failure
test-win3: $(BUILD)/test_gui_p2_4m $(BUILD)/test_gui_p2_1m
	@set -e; for c in $(WIN3_CASES); do ./$(BUILD)/test_gui_p2_4m .. $$c; ./$(BUILD)/test_gui_p2_1m .. $$c; done; \
	./$(BUILD)/test_gui_p2_1m .. win3-heap; ./$(BUILD)/test_gui_p2_4m .. win3-scale
test-win3-heap-4m: $(BUILD)/test_gui_p2_4m
	./$(BUILD)/test_gui_p2_4m .. win3-heap
# the measurements with live cells after a collection made for them
measure-win3: $(BUILD)/test_gui_p2_4m_gcprof
	./$(BUILD)/test_gui_p2_4m_gcprof .. win3-scale
# GLON-INST-3B: the three-window demonstration (a desktop: WSLg or X11)
windows-demo: $(BUILD)/glon_demo_4m
	./$(BUILD)/glon_demo_4m --root .. --vocab-vid --windows --app view/glon/intent_windows_demo.glon --width 760 --height 560

# GLON-VIEW-PREP-1: the C1 integration acceptance cases (c1-accept: component
# instances of a nested-vocabulary definition in the main window and native
# windows; c1-no-windows: the same without the window library), in 4M and 1M,
# and the membership application with the optional window library loaded
test-c1-prep: $(BUILD)/test_gui_p2_4m $(BUILD)/test_gui_p2_1m
	@set -e; for c in c1-accept c1-no-windows; do ./$(BUILD)/test_gui_p2_4m .. $$c; ./$(BUILD)/test_gui_p2_1m .. $$c; done; \
	for c in $(filter-out intent6-stress intent6-scale,$(MEMBERS_CASES)); do GV_TEST_WINDOWS_LIB=1 ./$(BUILD)/test_gui_p2_4m .. $$c; done
# GLON-VIEW-PREP-1: the bounded View acceptance suite for C1 integration
# (GLON-VIEW-C1-CONTRACT.md 6; logs and summary in build/c1-accept/).
# C1_ACCEPT_X11=1 adds the real-engine X11 tests (a display);
# C1_ACCEPT_VARIANTS=1 adds this branch's runtime-variant differential tests
test-c1-accept:
	sh tests/c1_accept.sh
