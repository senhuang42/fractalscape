# Fractal visualizer — macOS / Apple Silicon build.
#
# Uses Apple's system OpenGL framework (runs on Metal, GL up to 4.1) plus GLFW
# from Homebrew. No external GL loader needed. Build artifacts: ./fractal and
# ./run_tests.

CXX        := clang++
CXXSTD     := -std=c++17
OPT        := -O3 -flto
ARCH       := -arch arm64 -mcpu=apple-m1
WARN       := -Wall -Wextra -Wno-deprecated-declarations
INC        := -Isrc -Ithird_party

GLFW_PREFIX := $(shell brew --prefix glfw 2>/dev/null)
INC        += -I$(GLFW_PREFIX)/include

# -MMD -MP emit .d files so objects rebuild when any included header changes.
CXXFLAGS   := $(CXXSTD) $(OPT) $(ARCH) $(WARN) $(INC) -MMD -MP
LDFLAGS    := $(ARCH) -L$(GLFW_PREFIX)/lib -lglfw \
              -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo

BIN        := fractal
APP_SRCS   := src/main.cpp src/cli.cpp src/palette.cpp src/renderer.cpp \
              src/buddhabrot.cpp src/explorer.cpp
APP_OBJS   := $(APP_SRCS:.cpp=.o)

# Tests link only the GL-free units, so they run without a display. buddhabrot
# is GL-free (CPU scatter), so it's covered too; explorer/renderer are GL-only.
TEST_BIN   := run_tests
TEST_SRCS  := tests/test_main.cpp tests/test_palette.cpp \
              tests/test_fractal_math.cpp tests/test_cli.cpp \
              tests/test_buddhabrot.cpp tests/test_periodic.cpp \
              src/cli.cpp src/palette.cpp src/buddhabrot.cpp

.PHONY: all test clean run app

all: $(BIN)

# Package the explorer as a double-clickable macOS .app bundle.
app: $(BIN)
	@bash packaging/make_app.sh

$(BIN): $(APP_OBJS)
	$(CXX) $(APP_OBJS) -o $@ $(LDFLAGS)
	@echo "built ./$(BIN)"

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Pull in auto-generated header dependencies.
-include $(APP_OBJS:.o=.d)

# Build and run the unit tests (no GPU required).
test: $(TEST_BIN)
	@./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS) $(wildcard src/*.h) $(wildcard tests/*.h)
	$(CXX) $(CXXFLAGS) -Itests $(TEST_SRCS) -o $(TEST_BIN)

# Convenience: render a demo still.
run: $(BIN)
	./$(BIN) render -o demo.png

clean:
	rm -f $(APP_OBJS) $(APP_OBJS:.o=.d) $(BIN) $(TEST_BIN)
	rm -rf $(BIN).dSYM $(TEST_BIN).dSYM FractalScape.app
