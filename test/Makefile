CFLAGS ?= -std=c11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Werror \
          -fsanitize=address,undefined -fno-omit-frame-pointer
SRC = ../main/health.c ../main/condition.c ../main/dice.c ../main/wordlist.c ../main/passphrase.c

test_host: test_host.c $(SRC)
	$(CC) $(CFLAGS) -I../main -o $@ $^ -lm -lcrypto

test_protocol: test_protocol.c ../main/serial_protocol.c
	$(CC) $(CFLAGS) -I../main -o $@ $^

run: test_host test_protocol
	./test_host
	./test_protocol
	python3 -m unittest discover -s . -p 'test_*.py'

clean:
	rm -f test_host test_protocol

.PHONY: run clean
.DEFAULT_GOAL := run
