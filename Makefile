CC := gcc
PKG_CONFIG := pkg-config

CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
          -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -Isrc
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS   := $(shell $(PKG_CONFIG) --libs gtk+-3.0)

TARGET   := build/g2y-gui
TEST_BIN := build/test_g2y_gui
DIST_DIR := dist

SRCS := src/main.c src/ui.c src/runner.c
OBJS := $(patsubst src/%.c,build/%.o,$(SRCS))

.PHONY: all clean test package

all: $(TARGET)

build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(GTK_LIBS)

$(TEST_BIN): tests/test_g2y_gui.c src/runner.c src/ui.c | build
	$(CC) $(CFLAGS) $(GTK_CFLAGS) tests/test_g2y_gui.c src/runner.c src/ui.c -o $@ $(GTK_LIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

package: $(TARGET)
	mkdir -p $(DIST_DIR)
	cp $(TARGET) $(DIST_DIR)/g2y-gui
	strip $(DIST_DIR)/g2y-gui
	chmod 0755 $(DIST_DIR)/g2y-gui
	sed 's|@EXEC_PATH@|$(CURDIR)/dist/g2y-gui|g' \
		packaging/g2y-gui.desktop > $(DIST_DIR)/g2y-gui.desktop
	chmod 0644 $(DIST_DIR)/g2y-gui.desktop

clean:
	rm -rf build $(DIST_DIR)
