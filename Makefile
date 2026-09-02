# Host tooling, plus the third-party fetch the firmware build needs.
#
#   make                     build the preview binary
#   make reveal              render the oracle reveal to reveal.gif
#   make reveal ANSWER=NO MODIFIER=-
#   make boot                render the power-on sequence to boot.gif
#   make menu                render one full turn through the die list to menu.gif
#   make coin                render the D2 coin turning and flipping to coin.gif
#   make check               build and run every test program under tests/
#   make fonts               regenerate src/generated from the DejaVu faces
#   make deps                fetch the third-party sources (none are committed)
#   make firmware            deps, then PlatformIO
#   make release             firmware, merged into one image for web flashers
#   make upload PORT=/dev/ttyACM1
#   make clean               remove build output
#   make distclean           also remove the fetched driver
#
# Sources are globbed, so a new .c under src/ joins the preview without editing
# this file, and a new .c under tests/ becomes a test program that make check
# runs. Anything they need from the ESP-IDF gets a stub in tools/host.
#
# Building the shared rendering sources here with -Wall -Wextra also gives them
# a warning pass that the PlatformIO build does not.

CC ?= gcc
CFLAGS ?= -O2 -std=gnu11 -Wall -Wextra
CPPFLAGS := -Isrc -Itools/host -Itools/host/third_party
LDLIBS := -lm

BUILD := build
PREVIEW := $(BUILD)/preview
FONT_GENERATOR := $(BUILD)/make_fonts

# The rendering sources are shared; each host program adds its own file and
# its own adapters for the hardware it stands in for. Every tests/*.c is one
# test program with its own main; its exit status is its verdict.
SHARED_SOURCES := $(wildcard src/*.c) $(wildcard src/generated/*.c) tools/host/adapters.c
SHARED_OBJECTS := $(SHARED_SOURCES:%.c=$(BUILD)/%.o)
PREVIEW_SOURCES := tools/host/preview.c tools/host/gif.c
PREVIEW_OBJECTS := $(PREVIEW_SOURCES:%.c=$(BUILD)/%.o) $(SHARED_OBJECTS)
TEST_SOURCES := $(wildcard tests/*.c)
TEST_OBJECTS := $(TEST_SOURCES:%.c=$(BUILD)/%.o)
TEST_PROGRAMS := $(TEST_SOURCES:tests/%.c=$(BUILD)/tests/%)
DEPENDENCIES := $(PREVIEW_OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d) $(BUILD)/tools/make_fonts.d

# The ST77916 panel driver is fetched rather than committed, so nothing under
# $(ST77916_DIR) is in git.
#
# Upstream publishes it for the ESP-IDF component manager, which the PlatformIO
# Arduino builder has no hook for. Its archive also carries a test app defining
# app_main, and PlatformIO compiles every source under a manifest-less library
# root, so that symbol would reach the link and collide with the Arduino core's.
# Copying out only the two files the firmware needs sidesteps both problems with
# no build-time patch step.
#
# library.json is generated here too, because the component's version macros are
# normally produced by its cmake_utilities dependency and there is no CMake in
# this build. Generating it keeps ST77916_VERSION the single source of truth.
ST77916_VERSION := 1.0.1
ST77916_URL := https://components-file.espressif.com/components/espressif/esp_lcd_st77916/$(ST77916_VERSION)/espressif__esp_lcd_st77916-v$(ST77916_VERSION).zip
ST77916_SHA256 := 5cdfccf1f4847dbd865f9c2510d1779c9ec0cef19234cfd23da6c3143f838841
ST77916_DIR := lib/esp_lcd_st77916
ST77916_SOURCE := $(ST77916_DIR)/src/esp_lcd_st77916.c
ST77916_PARTS := $(subst ., ,$(ST77916_VERSION))

# stb_truetype rasterises the fonts in tools/make_fonts.c. Pinned to a commit
# rather than a branch, and checked by hash, because src/generated is committed:
# the glyph bytes in the repo have to stay reproducible from this exact header.
STB_TRUETYPE_VERSION := 1.26
STB_TRUETYPE_COMMIT := 6e9f34d5429cf16790ec43c9bac3f1ee4ad1f760
STB_TRUETYPE_URL := https://raw.githubusercontent.com/nothings/stb/$(STB_TRUETYPE_COMMIT)/stb_truetype.h
STB_TRUETYPE_SHA256 := ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab
STB_TRUETYPE := tools/host/third_party/stb_truetype.h

PIO ?= $(HOME)/.platformio/penv/bin/pio
ESPTOOL ?= $(HOME)/.platformio/penv/bin/python $(HOME)/.platformio/packages/tool-esptoolpy/esptool.py
PORT ?=
UPLOAD_PORT_FLAG := $(if $(PORT),--upload-port $(PORT),)

THEME ?= midnight
ANSWER ?= YES
MODIFIER ?= and
CAPTION ?= ORACLE
REVEAL_GIF ?= reveal.gif
BOOT_GIF ?= boot.gif
MENU_GIF ?= menu.gif
COIN_GIF ?= coin.gif

# The release image is the four parts `pio run -t upload` writes, laid out at
# their flash offsets, so a web flasher writes the whole thing at address 0.
FIRMWARE_DIR := .pio/build/knob
BOOT_APP0 := $(HOME)/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
RELEASE_BIN ?= pythia.bin

.PHONY: all preview check reveal boot menu fonts deps firmware release upload monitor clean distclean

# Test objects are reached through a pattern rule chain, and make would delete
# them as intermediates after every run, rebuilding them the next time.
.SECONDARY: $(TEST_OBJECTS)

all: preview

preview: $(PREVIEW)

$(PREVIEW): $(PREVIEW_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/tests/%: $(BUILD)/tests/%.o $(SHARED_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

check: $(TEST_PROGRAMS)
	@for program in $(TEST_PROGRAMS); do echo "$$program"; $$program || exit 1; done

$(FONT_GENERATOR): $(BUILD)/tools/make_fonts.o
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

reveal: $(PREVIEW)
	$(PREVIEW) reveal $(THEME) $(ANSWER) $(MODIFIER) $(CAPTION) $(REVEAL_GIF)

boot: $(PREVIEW)
	$(PREVIEW) boot $(THEME) $(BOOT_GIF)

menu: $(PREVIEW)
	$(PREVIEW) menu $(THEME) $(MENU_GIF)

coin: $(PREVIEW)
	$(PREVIEW) coin $(THEME) $(COIN_GIF)

fonts: $(FONT_GENERATOR)
	$(FONT_GENERATOR) src/generated

deps: $(ST77916_SOURCE) $(STB_TRUETYPE)

$(ST77916_SOURCE):
	@mkdir -p $(BUILD)/deps
	curl -sSfL $(ST77916_URL) -o $(BUILD)/deps/st77916.zip
	printf '%s  %s\n' $(ST77916_SHA256) $(BUILD)/deps/st77916.zip | sha256sum -c -
	rm -rf $(BUILD)/deps/st77916
	unzip -qo $(BUILD)/deps/st77916.zip -d $(BUILD)/deps/st77916
	@mkdir -p $(ST77916_DIR)/src $(ST77916_DIR)/include
	cp $(BUILD)/deps/st77916/esp_lcd_st77916.c $(ST77916_DIR)/src/
	cp $(BUILD)/deps/st77916/include/esp_lcd_st77916.h $(ST77916_DIR)/include/
	cp $(BUILD)/deps/st77916/license.txt $(ST77916_DIR)/LICENSE
	@printf '{\n  "name": "esp_lcd_st77916",\n  "version": "%s",\n  "license": "Apache-2.0",\n  "frameworks": "arduino",\n  "platforms": "espressif32",\n  "build": {\n    "flags": [\n      "-DESP_LCD_ST77916_VER_MAJOR=%s",\n      "-DESP_LCD_ST77916_VER_MINOR=%s",\n      "-DESP_LCD_ST77916_VER_PATCH=%s"\n    ]\n  }\n}\n' \
	  $(ST77916_VERSION) $(word 1,$(ST77916_PARTS)) $(word 2,$(ST77916_PARTS)) $(word 3,$(ST77916_PARTS)) \
	  > $(ST77916_DIR)/library.json

$(STB_TRUETYPE):
	@mkdir -p $(@D)
	curl -sSfL $(STB_TRUETYPE_URL) -o $@.part
	printf '%s  %s\n' $(STB_TRUETYPE_SHA256) $@.part | sha256sum -c -
	mv $@.part $@

# The font generator is the only thing that needs it, so fetch on demand.
$(BUILD)/tools/make_fonts.o: $(STB_TRUETYPE)

firmware: deps
	$(PIO) run

release: firmware
	$(ESPTOOL) --chip esp32s3 merge_bin --output $(RELEASE_BIN) \
	    --flash_mode dio --flash_freq 80m --flash_size 16MB \
	    0x0 $(FIRMWARE_DIR)/bootloader.bin \
	    0x8000 $(FIRMWARE_DIR)/partitions.bin \
	    0xe000 $(BOOT_APP0) \
	    0x10000 $(FIRMWARE_DIR)/firmware.bin

upload: deps
	$(PIO) run -t upload $(UPLOAD_PORT_FLAG)

monitor:
	$(PIO) device monitor -b 115200

clean:
	rm -rf $(BUILD) $(RELEASE_BIN)

distclean: clean
	rm -rf $(ST77916_DIR) $(dir $(STB_TRUETYPE))

-include $(DEPENDENCIES)
