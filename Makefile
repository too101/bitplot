CC ?= cc
CFLAGS ?= -O2 -Wall

bitplot: bitplot.c
	$(CC) $(CFLAGS) -o $@ $< -lX11

clean:
	rm -f bitplot

.PHONY: clean
