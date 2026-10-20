CFLAGS ?= -std=c11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Werror \
          -fsanitize=address,undefined -fno-omit-frame-pointer
SRC = ../main/health.c ../main/condition.c ../main/dice.c ../main/wordlist.c ../main/passphrase.c ../main/display.c

test_host: test_host.c $(SRC)
	$(CC) $(CFLAGS) -I../main -o $@ $^ -lm -lcrypto

run: test_host
	./test_host

clean:
	rm -f test_host

.PHONY: run clean
.DEFAULT_GOAL := run
