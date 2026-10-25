# Install on Ubuntu: sudo apt install build-essential libssl-dev libgtest-dev
CC = gcc
CXX = g++

CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -Iinclude -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O2
CXXFLAGS = -std=c++17 -Iinclude -Itests -Wall -Wextra -O0 -g

SOURCES = $(wildcard src/*.c)
HEADERS = $(wildcard include/*.h)
TEST_SOURCES = $(wildcard tests/test_*.cpp)
TEST_C_SOURCES = $(filter-out src/main.c,$(SOURCES))

BUILD_DIR = build/tests
COVERAGE_FLAGS =
TEST_OBJECTS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(TEST_C_SOURCES)) \
               $(BUILD_DIR)/main.o
TEST_BINARY = $(BUILD_DIR)/test_suite

.PHONY: all test coverage check analyze valgrind clean

all: bloodbank

bloodbank: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(EXTRA) -o $@ $(SOURCES) -lcrypto

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(COVERAGE_FLAGS) $(EXTRA) -O0 -g -c $< -o $@

$(BUILD_DIR)/main.o: src/main.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(COVERAGE_FLAGS) $(EXTRA) -O0 -g -Dmain=bloodbank_app_main -c $< -o $@

$(TEST_BINARY): $(TEST_SOURCES) $(TEST_OBJECTS) $(wildcard tests/*.hpp)
	$(CXX) $(CXXFLAGS) $(COVERAGE_FLAGS) $(EXTRA) -o $@ $(TEST_SOURCES) $(TEST_OBJECTS) \
	  -lgtest_main -lgtest -lcrypto -pthread

test: $(TEST_BINARY)
	./$(TEST_BINARY)

coverage:
	rm -f build/coverage/*.gcda
	$(MAKE) BUILD_DIR=build/coverage COVERAGE_FLAGS=--coverage test
	gcov -b -c -o build/coverage $(SOURCES)

check:
	cppcheck --enable=all --inconclusive --std=c99 --language=c --platform=unix64 \
	  --library=posix --library=openssl --inline-suppr --error-exitcode=1 \
	  --suppressions-list=tools/cppcheck-suppressions.txt -Iinclude src
	@if [ -f tools/misra_rules.txt ]; then \
	  cppcheck --addon=tools/misra.json --std=c99 --language=c --inline-suppr --error-exitcode=1 \
	    --suppressions-list=tools/cppcheck-suppressions.txt -Iinclude src; \
	else echo "MISRA addon skipped: put your rule texts in tools/misra_rules.txt"; fi

analyze:
	@for f in $(SOURCES); do \
	  $(CC) $(CFLAGS) $(EXTRA) -Wcast-qual -Wnull-dereference -Wduplicated-cond -Wduplicated-branches \
	    -Wlogical-op -Wredundant-decls -Wstrict-prototypes -Wmissing-prototypes -Wswitch-default \
	    -Werror -fanalyzer -c $$f -o /dev/null || exit 1; \
	done; echo "gcc analysis: no findings"

valgrind: $(TEST_BINARY)
	valgrind --leak-check=full --show-leak-kinds=all \
	  --errors-for-leak-kinds=definite,indirect --error-exitcode=1 \
	  ./$(TEST_BINARY)

clean:
	rm -f bloodbank *.tmp *.gcov
	rm -rf build/tests build/coverage
