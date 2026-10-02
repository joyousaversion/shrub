CC=gcc
CFLAGS=-Wall -Werror -Wno-missing-braces -std=gnu99  -I.
SHRUB_DEPS= shrub.c  genome.c perlin.c roulette.c serialize.c worldview.c game.c items.c
SHRUB_LIBS= -lncurses -lpanel -lm -lmenu -lform -ltinfo
fast: $(SHRUB_DEPS) TAGS
	$(CC) $(CFLAGS) -o shrub $(SHRUB_DEPS) $(SHRUB_LIBS) -O3
serialize: $(SHRUB_DEPS) TAGS
	$(CC) $(CFLAGS) -o serialize $(SHRUB_DEPS) $(SHRUB_LIBS) -DSERIALIZE_IS_MAIN -O3
TAGS: $(SHRUB_DEPS)
	etags --language=c -R *.c *.h
debug: $(SHRUB_DEPS) TAGS
	$(CC) $(CFLAGS) -o shrub $(SHRUB_DEPS) $(SHRUB_LIBS) -g -pg
menu: TAGS menu1.c
	$(CC) $(CFLAGS) menu1.c  -lform -lncurses -lpanel -lmenu -o menu -g
bg:	bg.c TAGS
	gcc -Wall -std=gnu99 -lncurses bg.c -o bg
utest: roulette.c
	$(CC) $(CFLAGS) -o utest utest.c roulette.c

TEST_ENGINE=shrub.c genome.c perlin.c roulette.c items.c
TEST_SUITES=biology genetics_extra growth_extra differentiation_extra lifecycle tick_integration
TEST_BINS=$(addprefix .build/test_,$(TEST_SUITES))
TEST_SANITIZED=$(addsuffix _sanitize,$(TEST_BINS))
TEST_HEADERS=$(wildcard *.h tests/*.h)
TEST_FLAGS=-g -O1 -Wl,--wrap=rand
.PHONY: test test-sanitize test-smoke
test: $(TEST_BINS)
	@set -e; for suite in $(TEST_BINS); do $$suite --all; done
test-sanitize: $(TEST_SANITIZED)
	@set -e; for suite in $(TEST_SANITIZED); do ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $$suite --all; done
.build/test_%_sanitize: tests/test_%.c $(TEST_ENGINE) $(TEST_HEADERS)
	@mkdir -p .build
	$(CC) $(CFLAGS) $(TEST_FLAGS) -fno-omit-frame-pointer -fno-pie -no-pie -fsanitize=address,undefined -fno-sanitize-recover=all -o $@ $< $(TEST_ENGINE) -lm
.build/test_%: tests/test_%.c $(TEST_ENGINE) $(TEST_HEADERS)
	@mkdir -p .build
	$(CC) $(CFLAGS) $(TEST_FLAGS) -o $@ $< $(TEST_ENGINE) -lm
# Suites using the actual RNG need no linker replacement.
.build/test_differentiation_extra .build/test_differentiation_extra_sanitize .build/test_tick_integration .build/test_tick_integration_sanitize: TEST_FLAGS=-g -O1
test-smoke: fast .build/test_terminal
	.build/test_terminal ./shrub
.build/test_terminal: tests/test_terminal.c
	@mkdir -p .build
	$(CC) $(CFLAGS) -g -O1 -o $@ $< -lutil
