TARGET=n64brew-gamejam-6
BUILD_DIR=build
N64_ROM_HEADER=ipl3_dev_patched.z64
include $(N64_INST)/include/n64.mk
include $(T3D_INST)/t3d.mk

src = main.c persistence.c logo.c entrypoint.S

#N64_CFLAGS = -Wno-error

N64_LDFLAGS := -Theaps.ld $(N64_LDFLAGS)

all: $(TARGET).z64

assets_png = $(wildcard assets/*.png)
assets_gltf = $(wildcard assets/*.glb)
assets_mp3 = $(wildcard assets/*.mp3)
assets_conv = $(addprefix filesystem/,$(notdir $(assets_png:%.png=%.sprite))) \
			  $(addprefix filesystem/,$(notdir $(assets_ttf:%.ttf=%.font64))) \
			  $(addprefix filesystem/,$(notdir $(assets_gltf:%.glb=%.t3dm))) \
			  $(addprefix filesystem/,$(notdir $(assets_mp3:%.mp3=%.wav64)))

filesystem/%.sprite: assets/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -o filesystem "$<"

filesystem/%.t3dm: assets/%.glb
	@mkdir -p $(dir $@)
	@echo "    [T3D-MODEL] $@"
	$(T3D_GLTF_TO_3D) "$<" $@
	$(N64_BINDIR)/mkasset -c 2 -o filesystem $@

filesystem/%.wav64: assets/%.mp3
	@mkdir -p $(dir $@)
	@echo "    [SFX] $@"
	$(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) -o $(dir $@) "$<"

filesystem/dragon.wav64: AUDIOCONV_FLAGS += --wav-resample 32000 --wav-mono --wav-compress 3

$(BUILD_DIR)/$(TARGET).dfs: $(assets_conv)

$(BUILD_DIR)/$(TARGET).elf: $(src:%.c=$(BUILD_DIR)/%.o) $(src:%.S=$(BUILD_DIR)/%.o)

$(TARGET).z64: N64_ROM_TITLE="N64brew Gamejam 2025"
$(TARGET).z64: $(BUILD_DIR)/$(TARGET).dfs

clean:
	rm -rf $(BUILD_DIR) $(TARGET).z64
	rm -rf filesystem

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean

