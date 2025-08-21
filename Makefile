TOOLS	=	src/main
#PS1TOOLS=	src/ps1main
COMMON	=	src/psu.o src/armax.o src/cbs.o src/aes.o src/mcs.o src/lzari.o \
			src/miniz_tinfl.o src/sha1.o src/xps.o
DEPS	=	Makefile

CC	=	gcc
CFLAGS	=	-g -O3 -W -I./include -I. -D_GNU_SOURCE
#LDFLAGS =	-lz

OBJS	= $(COMMON) $(addsuffix .o, $(TOOLS))

all: $(TOOLS) $(PS1TOOLS)

$(TOOLS): %: %.o $(COMMON) $(DEPS)
	$(CC) $(CFLAGS) -o psv-save-converter $< $(COMMON) $(LDFLAGS)

#$(PS1TOOLS): %: %.o $(COMMON) $(DEPS)
#	$(CC) $(CFLAGS) -o ps1vmc-tool $< $(COMMON) $(LDFLAGS)

$(OBJS): %.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-rm -f $(OBJS) $(TOOLS)
