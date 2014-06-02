PEEP_CFLAGS= \
	-ansi \
	-pedantic \
	-Wall \
	-Wextra \
	-Wno-unused-parameter \
	-O2

PEEP_MACROS= \
	-D_POSIX_SOURCE=1 \
	-D_POSIX_C_SOURCE=200809L

all: peep

peep: peep.c
	$(CC) $(PEEP_CFLAGS) $(PEEP_MACROS) -o $@ $< $(PEEP_LIBS)

clean:
	rm -rf peep
