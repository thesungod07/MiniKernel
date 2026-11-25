CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -g -O0 -pthread
SRC     = hw/cpu.c kernel/boot.c kernel/traps.c kernel/proc.c kernel/sched.c kernel/schedrun.c \
          kernel/ipc.c kernel/sync.c kernel/deadlock.c kernel/smp.c kernel/threads.c
TESTS   = test_sched_basic test_sched_adv test_ipc test_sync test_deadlock test_smp test_threads

all: $(TESTS:%=build/%) build/demo

build/%: tests/%.c $(SRC) $(wildcard kernel/*.h hw/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -I. $< $(SRC) -o $@

build/demo: demos/demo.c $(SRC) $(wildcard kernel/*.h hw/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -I. demos/demo.c $(SRC) -o $@

test: $(TESTS:%=build/%)
	@for t in $(TESTS); do echo "##### $$t"; ./build/$$t | grep -E "FAIL|all ok|FAILED" ; done

demo: build/demo
	./build/demo

clean:
	rm -rf build
.PHONY: all test demo clean
