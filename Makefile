# Makefile for the Mini-C to C transpiler (GNU make).
ifeq ($(origin CC),default)
CC = gcc
endif
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g

SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,build/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)
EXAMPLES := $(wildcard examples/*.c)

.PHONY: all test examples memcheck clean

all: minic

minic: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

test: all
	bash tests/test.sh

examples: all
	@mkdir -p build/examples
	@for ex in $(EXAMPLES); do \
	    name=$$(basename $$ex .c); \
	    echo "=== $$name ==="; \
	    ./minic $$ex -o build/examples/$$name.c || exit 1; \
	    $(CC) -std=c11 -Wall -Wextra -Werror build/examples/$$name.c -o build/examples/$$name || exit 1; \
	    ./build/examples/$$name || exit 1; \
	done

memcheck: all
	@if command -v valgrind >/dev/null 2>&1; then \
	    for ex in $(EXAMPLES); do \
	        echo "=== valgrind $$ex ==="; \
	        valgrind --leak-check=full --error-exitcode=1 -q ./minic $$ex -o /dev/null || exit 1; \
	    done; \
	else \
	    echo "valgrind not found; skipping memcheck"; \
	fi

clean:
	rm -rf build minic out.c out

-include $(DEPS)
