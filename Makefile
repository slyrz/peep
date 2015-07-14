CFLAGS ?= \
	-ansi \
	-pedantic \
	-Wall \
	-Wextra \
	-Wno-unused-parameter \
	-O2

CFLAGS += \
	-D_POSIX_SOURCE=1 \
	-D_POSIX_C_SOURCE=200809L

all: peep

peep: peep.c
	$(CC) $(CFLAGS) -o $@ $<

install: peep
	install -d ${DESTDIR}${PREFIX}/share/man/man1
	install peep.1 ${DESTDIR}${PREFIX}/share/man/man1
	install -d "${DESTDIR}${PREFIX}/bin"
	install -t "${DESTDIR}${PREFIX}/bin" -o root -g root -m 4755 $<

clean:
	rm -rf peep
