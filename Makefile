CC=gcc
SRCDIR=src
OBJDIR=build/obj
LIBDIR=libs
LIBOBJDIR=build/libobj
DEPDIR=build/dep
BINDIR=build/bin
SRCS=$(wildcard $(SRCDIR)/*.c)
OBJS=$(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS)) 
LIBS=$(wildcard $(LIBDIR)/*.c)
LIBSOBJS=$(patsubst $(LIBDIR)/%.c, $(LIBOBJDIR)/%.o, $(LIBS)) 
DEPS=$(patsubst $(SRCDIR)/%.c, $(DEPDIR)/%.d, $(SRCS))
BIN=$(BINDIR)/eventsub
CFLAGS=-std=c99 -Wpedantic -Wextra -Wall -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Wno-unused-parameter  -O2 -D _GNU_SOURCE -Wno-format-overflow
DEPFLAGS=-MT $@ -MMD -MP -MF $(DEPDIR)/$*.d
LDFLAGS=`pkg-config --libs openssl` -lm -pthread 
PREFIX=/usr
.PHONY: all clean run

all: $(BIN) 

debug: CFLAGS += -ggdb3 
debug: LDFLAGS = `pkg-config --libs openssl` -lm  -pthread 
debug: $(BIN)

$(BIN): $(OBJS) $(LIBSOBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR) $(DEPDIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(LIBOBJDIR)/%.o: $(LIBDIR)/%.c | $(LIBOBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR) $(LIBOBJDIR) $(BINDIR) $(DEPDIR):
	@mkdir -p $@

clean:
	rm -rf build

run:
	$(BIN)

$(DEPS):

include $(wildcard $(DEPS))
