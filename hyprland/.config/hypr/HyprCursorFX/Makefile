CXX ?= g++
TARGET ?= hyprcursorfx.so
PREFIX ?= $(HOME)/.local/lib/hyprcursorfx
CXXFLAGS ?= -O2 -std=c++23 -fPIC -Wall -Wextra -Wpedantic -Wno-unused-parameter
CXXFLAGS += $(shell pkg-config --cflags hyprland 2>/dev/null)
# Hyprland 0.56.2 exposes g_pHyprRenderer as an inline global in its headers.
# -fvisibility=hidden gives the plugin a private null copy of that global, which
# makes mouse callbacks run while every render callback silently skips drawing.
# Keep default ELF visibility so the plugin resolves the compositor's instance.
CXXFLAGS += -fvisibility=default
LDFLAGS += -shared

SRC := src/main.cpp

.PHONY: all clean install uninstall check
all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)

# Always rebuild on install. Archives can preserve old mtimes, leaving an old .so
# newer than the updated source; a plain "make install" must never ship that stale binary.
install:
	$(MAKE) clean
	$(MAKE) all
	install -Dm755 $(TARGET) "$(PREFIX)/$(TARGET)"
	mkdir -p "$(HOME)/.config/hypr"
	@if [ ! -e "$(HOME)/.config/hypr/hyprcursorfx.conf" ]; then install -Dm644 config/hyprcursorfx.conf "$(HOME)/.config/hypr/hyprcursorfx.conf"; else echo "Keeping existing config: $(HOME)/.config/hypr/hyprcursorfx.conf"; fi
	install -Dm644 examples/autoload-hyprland.conf "$(PREFIX)/examples/autoload-hyprland.conf"
	install -Dm644 examples/autoload-hyprland.lua "$(PREFIX)/examples/autoload-hyprland.lua"
	@echo "Installed HyprCursorFX $(shell cat VERSION) to $(PREFIX)/$(TARGET)"
	@echo "NOTE: if an older copy is already loaded, run ./install-reload.sh to replace it in the current session."

uninstall:
	rm -f "$(PREFIX)/$(TARGET)"
	@echo "Removed the plugin binary. User config was left untouched."

clean:
	rm -f $(TARGET)

check:
	$(CXX) -std=c++23 -Wall -Wextra -Wpedantic tests/config_test.cpp -o /tmp/hyprcursorfx-config-test
	/tmp/hyprcursorfx-config-test
	rm -f /tmp/hyprcursorfx-config-test
	./tests/package_test.sh
