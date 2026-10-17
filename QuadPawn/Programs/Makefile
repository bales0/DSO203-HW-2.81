.DEFAULT_GOAL := all
ifeq ($(OS),Windows_NT)
PAWNCC = ../Compiler/bin/pawncc.exe
else
PAWNCC = ../Compiler/bin/pawncc
endif
SRCS = $(wildcard *.pawn)
BINDIR = ../../bin/QuadPawn/Programs
OBJS = $(addprefix $(BINDIR)/,$(SRCS:.pawn=.amx))

all: $(OBJS)

clean:
	$(RM) $(OBJS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/%.amx: %.pawn $(wildcard ../Compiler/include/*.inc) | $(BINDIR)
	$(PAWNCC) -d2 -v2 -V1 -o$@ $<

