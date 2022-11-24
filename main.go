package main

// #cgo CFLAGS: -O3 -Wall -DNDEBUG -D_GNU_SOURCE -march=broadwell -mtune=broadwell -I modules/libbpf/src
// #cgo LDFLAGS: -Lmodules/libbpf/src -lm -lelf -lbpf -pthread -lz
// #pragma GCC diagnostic ignored "-Wstringop-truncation"
// #include "af_xdp.h"
// #include <stdlib.h>
// #include <stdio.h>
import "C"
import (
	"bytes"
	"fmt"
	"net"
	"os/exec"
	"unsafe"

	"github.com/netsys-lab/scion_packet_gen/cmd"
	log "github.com/sirupsen/logrus"
)

func main() {
	args := cmd.MustParseFlags()
	cmd.ConfigureLogging()
	log.Debug("[Main] starting with opt: ", cmd.Opts)

	if len(args) < 2 {
		log.Fatal("Not enough arguments, src and dst address are required")
	}

	srcAddr, err := net.ResolveUDPAddr("udp", args[0])
	if err != nil {
		log.Fatal("Could not parse src addr ", args[0], ": ", err)
	}

	dstAddr, err := net.ResolveUDPAddr("udp", args[1])
	if err != nil {
		log.Fatal("Could not parse dst addr ", args[1], ": ", err)
	}

	err, srcMac, errString := Exec("cat", fmt.Sprintf("/sys/class/net/%s/address", cmd.Opts.Interface))
	if err != nil {
		log.Fatal("Failed to obtain mac from interface ", cmd.Opts.Interface, ": ", err, "; verbose: ", errString)
	}

	err, dstMac, errString := Exec("ip", "neigh", "|", fmt.Sprintf("grep ", dstAddr.IP), "|", "awk", "'{print $5}'")
	if err != nil {
		log.Fatal("Failed to obtain dst mac from arp table for ip ", dstAddr.IP, ": ", err, "; verbose: ", errString)
	}

	bts := make([]byte, 10)
	ret := int(C.perform_tx(
		C.CString(cmd.Opts.Interface),
		C.CString(srcAddr.IP.String()),
		C.CString(dstAddr.IP.String()),
		C.ushort(srcAddr.Port),
		C.ushort(dstAddr.Port),
		C.CString(srcMac),
		C.CString(dstMac),
		C.ushort(cmd.Opts.Queue),
		C.ushort(cmd.Opts.BatchSize),
		C.ushort(cmd.Opts.DataLen),
		unsafe.Pointer(&bts[0]),
		C.int(10)))

	if ret == -1 {
		errStr := C.GoString(C.LastError())
		log.Fatal("Failed to perform tx: Internal error: ", errStr)
	}

}

func Exec(command string, args ...string) (error, string, string) {
	cmd := exec.Command(command, args...)
	var out bytes.Buffer
	var stdErr bytes.Buffer
	cmd.Stderr = &stdErr
	cmd.Stdout = &out
	log.Tracef("Executing: %s\n", cmd.String())
	err := cmd.Run()
	if err == nil {
		log.Tracef("Execute successful")
	} else {
		log.Tracef("Execute failed %s", err.Error())
	}
	return err, out.String(), stdErr.String()
}
