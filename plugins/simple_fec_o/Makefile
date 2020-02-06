SRC=$(shell find . -wholename "*protoops/*.c")
OBJ=$(SRC:.c=.o)
CFLAGS=-I../../picoquic #-DDISABLE_PROTOOP_PRINTF

all: $(SRC) $(OBJ)

tetrys: tetrys_flags all

tetrys_flags:
	$(eval CFLAGS += -DCHUNK_SIZE=1360)

$(OBJ): %.o

%.o: %.c
	clang $(CFLAGS) -O2 -fno-gnu-inline-asm -emit-llvm -c $< -o - | llc -march=bpf -filetype=obj -o $@

.PHONY: %.o

clean:
	rm -rf $(OBJ)
	rm -rf verif

remove_non_objects:
	rm -rf $(SRC)
	rm -rf $(HDR)
	rm -rf $(shell find . -wholename "*.c")
	rm -rf $(shell find . -wholename "*.h")