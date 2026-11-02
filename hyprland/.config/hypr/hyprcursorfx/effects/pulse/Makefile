CXX ?= g++
CXXFLAGS ?= -O2 -std=c++23 -fPIC -Wall -Wextra -Wpedantic
# In the source tree, use the repository header. In the installed user-effect
# folder, use the header installed by `make install`.
ifneq ($(wildcard ../../../include/hyprcursorfx/effect_api.h),)
HCFX_INCLUDE ?= ../../../include
else
HCFX_INCLUDE ?= $(HOME)/.local/lib/hyprcursorfx/include
endif
CPPFLAGS += -I$(HCFX_INCLUDE)
TARGET := effect.so

.PHONY: all clean
all: $(TARGET)

$(TARGET): effect.cpp $(HCFX_INCLUDE)/hyprcursorfx/effect_api.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -shared effect.cpp -o $@ -lm

clean:
	rm -f $(TARGET)
