package main

// #cgo CFLAGS: -O3 -Wall -DNDEBUG -D_GNU_SOURCE -march=broadwell -mtune=broadwell -I modules/libbpf/src
// #cgo LDFLAGS: -Lmodules/libbpf/src -lm -lelf -lbpf -pthread -lz
// #pragma GCC diagnostic ignored "-Wstringop-truncation"
// #include "af_xdp.h"
// #include <stdlib.h>
// #include <stdio.h>
// static void* allocArgv(int argc) {
//    return malloc(sizeof(char *) * argc);
// }
import "C"

import (
	"fmt"
	"os"
	"unsafe"
)

func main() {
	argv := os.Args
	argc := C.int(len(argv))
	c_argv := (*[0xfff]*C.char)(C.allocArgv(argc))
	defer C.free(unsafe.Pointer(c_argv))

	for i, arg := range argv {
		c_argv[i] = C.CString(arg)
		defer C.free(unsafe.Pointer(c_argv[i]))
	}
	fmt.Println("Starting c code")
	C.main_c(argc, (**C.char)(unsafe.Pointer(c_argv)))
}
