# Unit tests for the parts of src/ that do not need libvpx, SDL2 or FFmpeg.
# Only a C++17 compiler is required.
#
#   make -C tests        build the tests
#   make -C tests run    build and run them
#   make -C tests clean

CXX      ?= g++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -pthread -Wall -Wextra -Werror
SRC      := ../src
INCLUDES := -I$(SRC)/util -I$(SRC)/app
BUILD    := build

UTIL := $(addprefix $(SRC)/util/, serialization.cc udp_socket.cc socket.cc \
          address.cc file_descriptor.cc)

TESTS := test_protocol test_frame_ring test_handshake

all: $(addprefix $(BUILD)/, $(TESTS))

$(BUILD):
	mkdir -p $@

$(BUILD)/test_protocol: test_protocol.cc $(SRC)/app/protocol.cc $(UTIL) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

$(BUILD)/test_frame_ring: test_frame_ring.cc $(SRC)/app/frame_ring.cc | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

$(BUILD)/test_handshake: test_handshake.cc $(SRC)/app/common.cc $(SRC)/app/protocol.cc $(UTIL) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

run: all
	@set -e; for t in $(TESTS); do ./$(BUILD)/$$t; done

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
