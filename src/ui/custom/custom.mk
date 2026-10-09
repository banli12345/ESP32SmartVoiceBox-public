
# The complete pinyin table lives on the SD card and is loaded at boot.
# Keep the old generated C table out of firmware builds.
GEN_CSRCS += $(filter-out pinyin_full_dict.c,$(notdir $(wildcard $(PRJ_DIR)/custom/*.c)))

# Weather icon images (filenames only; VPATH set in generated/images/images.mk)
GEN_CSRCS += sunny.c
GEN_CSRCS += cloudy.c
GEN_CSRCS += overcast.c
GEN_CSRCS += bigrain.c
GEN_CSRCS += clock_icon.c

DEPPATH += --dep-path $(PRJ_DIR)/custom
VPATH += :$(PRJ_DIR)/custom

CFLAGS += "-I$(PRJ_DIR)/custom"
