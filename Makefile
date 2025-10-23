# Mutable Instruments LV2 Plugins Makefile
# ARM64 build for Zynthian

# Paths
AUDIBLE_DIR = ./AudibleInstruments
EURORACK_DIR = $(AUDIBLE_DIR)/eurorack
SRC_DIR = ./src

# ARM64 cross-compilation
CXX = aarch64-linux-gnu-g++
ARCH_FLAGS = -march=armv8-a+crc -mcpu=cortex-a72 -mtune=cortex-a72
BUILD_DIR = ./aarch64-build

CXXFLAGS = -std=c++11 -O2 -fPIC -DTEST \
	$(ARCH_FLAGS) \
	-ffast-math \
	-fno-finite-math-only \
	-I$(EURORACK_DIR) \
	-Wno-unused-local-typedefs \
	-Wno-unused-parameter

LDFLAGS = -shared -lm

# Plugin names
PLUGINS = plaits braids

# Default target
all: $(PLUGINS)

# Common Mutable Instruments source files
STMLIB_SOURCES = \
	$(EURORACK_DIR)/stmlib/utils/random.cc \
	$(EURORACK_DIR)/stmlib/dsp/atan.cc \
	$(EURORACK_DIR)/stmlib/dsp/units.cc

# Plaits sources
PLAITS_SOURCES = \
	$(wildcard $(EURORACK_DIR)/plaits/dsp/*.cc) \
	$(wildcard $(EURORACK_DIR)/plaits/dsp/engine/*.cc) \
	$(wildcard $(EURORACK_DIR)/plaits/dsp/speech/*.cc) \
	$(wildcard $(EURORACK_DIR)/plaits/dsp/physical_modelling/*.cc) \
	$(AUDIBLE_DIR)/eurorack/plaits/resources.cc

# Braids sources
BRAIDS_SOURCES = \
	$(EURORACK_DIR)/braids/macro_oscillator.cc \
	$(EURORACK_DIR)/braids/analog_oscillator.cc \
	$(EURORACK_DIR)/braids/digital_oscillator.cc \
	$(EURORACK_DIR)/braids/quantizer.cc \
	$(EURORACK_DIR)/braids/resources.cc

# Stages sources
STAGES_SOURCES = \
	$(EURORACK_DIR)/stages/segment_generator.cc \
	$(EURORACK_DIR)/stages/resources.cc \
	$(EURORACK_DIR)/stages/ramp_extractor.cc

# Build Plaits
plaits: $(BUILD_DIR)/plaits.lv2/plaits.so $(BUILD_DIR)/plaits.lv2/manifest.ttl $(BUILD_DIR)/plaits.lv2/plaits.ttl

$(BUILD_DIR)/plaits.lv2/plaits.so: $(SRC_DIR)/plaits.cpp $(PLAITS_SOURCES) $(STAGES_SOURCES) $(STMLIB_SOURCES)
	@echo "Building Plaits..."
	@mkdir -p $(BUILD_DIR)/plaits.lv2
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(PLAITS_SOURCES) $(STAGES_SOURCES) $(STMLIB_SOURCES)
	@echo "Plaits built successfully!"

$(BUILD_DIR)/plaits.lv2/manifest.ttl: $(SRC_DIR)/plaits_manifest.ttl
	@mkdir -p $(BUILD_DIR)/plaits.lv2
	@cp $< $@

$(BUILD_DIR)/plaits.lv2/plaits.ttl: $(SRC_DIR)/plaits.ttl
	@mkdir -p $(BUILD_DIR)/plaits.lv2
	@cp $< $@

# Build Braids
braids: $(BUILD_DIR)/braids.lv2/braids.so $(BUILD_DIR)/braids.lv2/manifest.ttl $(BUILD_DIR)/braids.lv2/braids.ttl

$(BUILD_DIR)/braids.lv2/braids.so: $(SRC_DIR)/braids.cpp $(BRAIDS_SOURCES) $(STAGES_SOURCES) $(STMLIB_SOURCES)
	@echo "Building Braids..."
	@mkdir -p $(BUILD_DIR)/braids.lv2
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(BRAIDS_SOURCES) $(STAGES_SOURCES) $(STMLIB_SOURCES)
	@echo "Braids built successfully!"

$(BUILD_DIR)/braids.lv2/manifest.ttl: $(SRC_DIR)/braids_manifest.ttl
	@mkdir -p $(BUILD_DIR)/braids.lv2
	@cp $< $@

$(BUILD_DIR)/braids.lv2/braids.ttl: $(SRC_DIR)/braids.ttl
	@mkdir -p $(BUILD_DIR)/braids.lv2
	@cp $< $@

# Install target
install: all
	@echo "Plugins built and ready to deploy from $(BUILD_DIR)/"
	@echo "Deploy to Zynthian: scp -r $(BUILD_DIR)/*.lv2 root@zynthian.local:/zynthian/zynthian-plugins/lv2/"

# Clean target
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete!"

# Verify target - checks bundle integrity
verify:
	@echo "Verifying LV2 bundles..."
	@for plugin in $(PLUGINS); do \
		echo "Checking $$plugin.lv2:"; \
		if [ -f $(BUILD_DIR)/$$plugin.lv2/$$plugin.so ]; then \
			file $(BUILD_DIR)/$$plugin.lv2/$$plugin.so | grep -q "ARM aarch64" && echo "  ✓ Binary is ARM64" || echo "  ✗ Binary is NOT ARM64!"; \
		else \
			echo "  ✗ Missing $$plugin.so"; \
		fi; \
		if [ -f $(BUILD_DIR)/$$plugin.lv2/manifest.ttl ]; then \
			echo "  ✓ manifest.ttl present"; \
		else \
			echo "  ✗ Missing manifest.ttl"; \
		fi; \
		if [ -f $(BUILD_DIR)/$$plugin.lv2/$$plugin.ttl ]; then \
			echo "  ✓ $$plugin.ttl present"; \
		else \
			echo "  ✗ Missing $$plugin.ttl"; \
		fi; \
		extra_files=$$(ls $(BUILD_DIR)/$$plugin.lv2/ | grep -v "\.so$$" | grep -v "\.ttl$$" | wc -l); \
		if [ $$extra_files -eq 0 ]; then \
			echo "  ✓ No extra files"; \
		else \
			echo "  ⚠ Warning: Extra files found"; \
			ls $(BUILD_DIR)/$$plugin.lv2/; \
		fi; \
		echo ""; \
	done

# Help target
help:
	@echo "Mutable Instruments LV2 Plugins for Zynthian (ARM64)"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build all plugins (default)"
	@echo "  plaits   - Build Plaits macro oscillator"
	@echo "  braids   - Build Braids macro oscillator"
	@echo "  install  - Build and prepare for deployment"
	@echo "  verify   - Verify bundle integrity"
	@echo "  clean    - Remove build artifacts"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Output directory: $(BUILD_DIR)/"
	@echo ""
	@echo "Deploy: scp -r $(BUILD_DIR)/*.lv2 root@zynthian.local:/zynthian/zynthian-plugins/lv2/"

.PHONY: all plaits braids install verify clean help

