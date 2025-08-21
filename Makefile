TOOLS	=	src/main
COMMON	=	src/psu.o
DEPS	=	Makefile

CC	=	gcc
CFLAGS	=	-g -O3 -W -I./include -I. -D_GNU_SOURCE

OBJS	= $(COMMON) $(addsuffix .o, $(TOOLS))

all: $(TOOLS)

$(TOOLS): %: %.o $(COMMON) $(DEPS)
	$(CC) $(CFLAGS) -o psv-save-converter $< $(COMMON) $(LDFLAGS)

$(OBJS): %.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-rm -f $(OBJS) $(TOOLS)
