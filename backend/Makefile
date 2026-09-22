CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=3 -fstack-protector-strong -I src
LDLIBS = -lcrypto
SRC = src/sha256.c src/unlock.c
TESTS = tests/test_checksum.c tests/test_password.c tests/test_device.c tests/test_sgio.c
BIN = wd-unlocker-backend
TEST_BIN = test-runner

all: $(BIN)

$(BIN): $(SRC) src/main.c
	$(CC) $(CFLAGS) -O2 -pie -o $@ $(SRC) src/main.c $(LDLIBS)

test: $(TEST_BIN)
	./$(TEST_BIN) --verbose

$(TEST_BIN): $(SRC) $(TESTS)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -o $@ $(SRC) $(TESTS) -lcriterion $(LDLIBS)

hardened: $(SRC) src/main.c
	$(CC) $(CFLAGS) -O2 -pie -Wl,-z,RelRO,Now -o $(BIN) $(SRC) src/main.c $(LDLIBS)

clean:
	rm -f $(BIN) $(TEST_BIN)

.PHONY: all test hardened clean
