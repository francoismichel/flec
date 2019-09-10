SRC=$(shell find . -wholename "*protoops/*.c")
OBJ=$(SRC:.c=.o)
CFLAGS=-I../../picoquic #-DDISABLE_PROTOOP_PRINTF

all: $(SRC) $(OBJ)

$(OBJ): %.o

%.o: %.c
	clang $(CFLAGS) -O2 -fno-gnu-inline-asm -emit-llvm -c $< -o - | llc -march=bpf -filetype=obj -o $@

.PHONY: %.o

clean:
	rm -rf $(OBJ)
	rm -rf verif
