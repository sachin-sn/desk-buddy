# Renders the clock faces with the REAL LVGL (and the real faces.cpp / screens.cpp) on a PC, to PPM/PNG files.
# Not part of the firmware build and not run in CI: it exists so the faces can be looked at, and so the
# thumbnails in data/www/faces/ can be regenerated:    make thumbs LV=/path/to/lvgl/src
#
# LV    folder with LVGL's sources: after `pio run` it is  ../../.pio/libdeps/crowpanel/lvgl/src
# BUILD where the compiled LVGL objects go (slow to build the first time, reused afterwards)
LV    ?= ../../.pio/libdeps/crowpanel/lvgl/src
BUILD ?= build
SRC   := ../../src
CFLAGS := -O1 -w -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -I$(SRC) -I$(LV)/.. -Istub

LVSRC := $(shell find $(LV) -name '*.c')
LVOBJ := $(patsubst $(LV)/%.c,$(BUILD)/obj/%.o,$(LVSRC))

.PHONY: all shots thumbs clean
all: $(BUILD)/render_faces

$(BUILD)/obj/%.o: $(LV)/%.c
	@mkdir -p $(dir $@)
	@gcc $(CFLAGS) -c $< -o $@

$(BUILD)/render_faces: render_faces.cpp photostub.cpp $(SRC)/screens.cpp $(SRC)/faces.cpp $(wildcard $(SRC)/*.h) $(LVOBJ)
	g++ -std=gnu++11 $(CFLAGS) -o $@ render_faces.cpp photostub.cpp $(SRC)/screens.cpp $(SRC)/faces.cpp $(LVOBJ) -lm

shots: $(BUILD)/render_faces
	mkdir -p $(BUILD)/shots
	$(BUILD)/render_faces $(BUILD)/shots
	python3 -I make_sheet.py $(BUILD)/shots

thumbs: shots
	python3 -I make_sheet.py $(BUILD)/shots --thumbs ../../data/www/faces

clean:
	rm -rf $(BUILD)
