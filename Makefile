CC = gcc

CFLAGS = -Wall -Werror -O1
LDFLAGS = -lgit2

LOG_LEVEL ?= WARN
EXTRA_CFLAGS ?= -DLOG_LEVEL=LOG_$(LOG_LEVEL)

.PHONY: build
build: continuously

continuously: continuously.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $< $(LDFLAGS)

DESTDIR ?= $(HOME)/.local/bin
.PHONY: install
install: build
	install -t $(DESTDIR) continuously

.PHONY: clean
clean:
	rm -f continuously
