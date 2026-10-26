# Install on Ubuntu: sudo apt install build-essential libssl-dev libgtest-dev
CC = gcc
CXX = g++

CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -Iinclude -Wall -Wextra -Wpedantic -Wconversion -Wshadow
CXXFLAGS = -std=c++17 -Iinclude -Itests -Wall -Wextra -O0 -g
TEST_BUILD = build/tests
COVERAGE_FLAGS =

.PHONY: all test coverage check analyze valgrind clean

all:
	$(CC) $(CFLAGS) -O2 -o bloodbank src/*.c -lcrypto

test:
	mkdir -p $(TEST_BUILD)
	for source in src/*.c; do \
	  name=$$(basename "$$source" .c); \
	  if [ "$$name" = main ]; then extra=-Dmain=bloodbank_app_main; else extra=; fi; \
	  $(CC) $(CFLAGS) -O0 -g $(COVERAGE_FLAGS) $$extra -c "$$source" -o $(TEST_BUILD)/$$name.o || exit 1; \
	done
	$(CXX) $(CXXFLAGS) $(COVERAGE_FLAGS) -o $(TEST_BUILD)/test_suite tests/test_*.cpp $(TEST_BUILD)/*.o \
	  -Wl,--wrap=malloc -lgtest_main -lgtest -lcrypto -pthread
	./$(TEST_BUILD)/test_suite

coverage:
	rm -f build/coverage/*.gcda
	$(MAKE) test TEST_BUILD=build/coverage COVERAGE_FLAGS=--coverage
	gcov -b -c -o build/coverage src/*.c

check:
	cppcheck --enable=all --inconclusive --std=c99 --language=c --platform=unix64 \
	  --library=posix --library=openssl --inline-suppr --error-exitcode=1 \
	  --suppressions-list=tools/cppcheck-suppressions.txt -Iinclude src
	@if [ -f tools/misra_rules.txt ]; then \
	  cppcheck --addon=tools/misra.json --std=c99 --language=c --inline-suppr --error-exitcode=1 \
	    --suppressions-list=tools/cppcheck-suppressions.txt -Iinclude src; \
	else echo "MISRA addon skipped: put your rule texts in tools/misra_rules.txt"; fi

valgrind: test
	valgrind --leak-check=full --show-leak-kinds=all \
	  --errors-for-leak-kinds=definite,indirect --error-exitcode=1 \
	  ./$(TEST_BUILD)/test_suite --gtest_filter=-MainTests.*:UserTests.*

clean:
	rm -f bloodbank *.tmp *.gcov
	rm -rf build/tests build/coverage
