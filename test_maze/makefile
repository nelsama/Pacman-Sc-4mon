# ============================================================================
# makefile - TEST MAZE (proporcion laberinto <-> personajes 16x16)
# ============================================================================
# Genera output/test.bin (se carga en $0800 con el monitor).
# Proyecto AUTONOMO. Para comparar laberintos: edita MAZE_MODE en test_maze.c
# (MAZE_ARCADE o MAZE_WIDE) y recompila.
# ============================================================================

CC65_HOME ?= D:/cc65

CC  = $(CC65_HOME)/bin/cl65.exe
CA65 = $(CC65_HOME)/bin/ca65.exe
LD  = $(CC65_HOME)/bin/ld65.exe

BUILD_DIR  = build
OUTPUT_DIR = output

LD_CONFIG = programa.cfg
LIB       = lib/vc.lib

PROGRAM = $(OUTPUT_DIR)/test.bin
MAP_FILE = $(OUTPUT_DIR)/test.map

APP_OBJECTS = $(BUILD_DIR)/test_maze.o $(BUILD_DIR)/startup.o

CFLAGS  = -t none -O --cpu 6502 -I include
ASFLAGS = -t none --cpu 6502
LDFLAGS = -C $(LD_CONFIG) -m $(MAP_FILE)

all: dirs $(PROGRAM)
	@echo "========================================"
	@echo "Prototipo generado: $(PROGRAM)"
	@ls -l $(PROGRAM) | awk '{print "Tamano: " $$5 " bytes"}'
	@echo "Carga con:  LOAD TEST 0800   /   R 0800"
	@echo "========================================"

dirs:
	@mkdir -p $(BUILD_DIR) $(OUTPUT_DIR)

$(BUILD_DIR)/test_maze.o: test_maze.c include/video.h include/romapi.h
	$(CC) -c $(CFLAGS) -o $@ test_maze.c

$(BUILD_DIR)/startup.o: startup.s
	$(CA65) $(ASFLAGS) -o $@ startup.s

$(PROGRAM): $(APP_OBJECTS) $(LIB)
	$(LD) $(LDFLAGS) -o $@ $(APP_OBJECTS) $(LIB) $(CC65_HOME)/lib/none.lib

clean:
	rm -rf $(BUILD_DIR) $(OUTPUT_DIR)

.PHONY: all dirs clean
