# Use Clang to compile the common files.
CC = clang

# Directories.
BUILD_DIR := build
SRC_DIR := src
MODULES_DIR := modules

LIBBPF_DIR := $(MODULES_DIR)/libbpf


# LibBPF source directory.
LIBBPF_SRC_DIR := $(LIBBPF_DIR)/src
LIBBPF_OBJS_DIR := $(LIBBPF_SRC_DIR)/staticobjs

# LibBPF objects.
LIBBPF_OBJS = $(LIBBPF_OBJS_DIR)/bpf_prog_linfo.o $(LIBBPF_OBJS_DIR)/bpf.o $(LIBBPF_OBJS_DIR)/btf_dump.o
LIBBPF_OBJS += $(LIBBPF_OBJS_DIR)/btf.o $(LIBBPF_OBJS_DIR)/gen_loader.o $(LIBBPF_OBJS_DIR)/hashmap.o
LIBBPF_OBJS += $(LIBBPF_OBJS_DIR)/libbpf_errno.o $(LIBBPF_OBJS_DIR)/libbpf_probes.o $(LIBBPF_OBJS_DIR)/libbpf.o
LIBBPF_OBJS += $(LIBBPF_OBJS_DIR)/linker.o $(LIBBPF_OBJS_DIR)/netlink.o $(LIBBPF_OBJS_DIR)/nlattr.o
LIBBPF_OBJS += $(LIBBPF_OBJS_DIR)/relo_core.o $(LIBBPF_OBJS_DIR)/ringbuf.o $(LIBBPF_OBJS_DIR)/str_error.o
LIBBPF_OBJS += $(LIBBPF_OBJS_DIR)/strset.o $(LIBBPF_OBJS_DIR)/xsk.o

# Source and out files.
AF_XDP_SRC := af_xdp.c
AF_XDP_OUT := af_xdp.o

#SEQ_SRC := sequence.c
#SEQ_OUT := sequence.o

#CMD_LINE_SRC := cmd_line.c
#CMD_LINE_OUT := cmd_line.o

# Main object files.
MAIN_OBJS := $(BUILD_DIR)/$(AF_XDP_OUT)

MAIN_SRC := main.c
MAIN_OUT := pkt_gen

# Global and main flags.
GLOBAL_FLAGS := -O3 -march=sandybridge -mtune=broadwell
MAIN_FLAGS := -pthread -lelf -lz

# Chains.
all: mk_build libbpf # af_xdp main


# Creates the build directory if it doesn't already exist.
mk_build:
	mkdir -p $(BUILD_DIR)

# Build LibBPF objects.
libbpf:
	$(MAKE) -C $(LIBBPF_SRC_DIR)	

# Cleanup (remove build files).
clean:
	$(MAKE) -C $(LIBBPF_SRC_DIR)/ clean
	rm -f $(BUILD_DIR)/*.o
	rm -f $(BUILD_DIR)/$(MAIN_OUT)

.PHONY:

.DEFAULT: all