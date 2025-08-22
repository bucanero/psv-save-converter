TOOLS	=	src/main
PSUEXT	=	src/psu-extractor
COMMON	=	src/psu.o src/armax.o src/cbs.o src/aes.o src/mcs.o src/lzari.o \
			src/miniz_tinfl.o src/sha1.o src/xps.o
DEPS	=	Makefile

CC	=	gcc
CFLAGS	=	-g -O3 -W -I./include -I. -D_GNU_SOURCE

OBJS	= $(COMMON) $(addsuffix .o, $(TOOLS))

all: $(TOOLS) $(PSUEXT)

$(TOOLS): %: %.o $(COMMON) $(DEPS)
	$(CC) $(CFLAGS) -o psv-save-converter $< $(COMMON) $(LDFLAGS)

$(PSUEXT): %: %.o $(DEPS)
	$(CC) $(CFLAGS) -DPSU_EXTRACTOR -o psu-extractor src/psu.c $< $(LDFLAGS)

$(OBJS): %.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-rm -f $(OBJS) $(TOOLS)
