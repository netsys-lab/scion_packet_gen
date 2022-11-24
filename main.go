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
	"context"
	"fmt"
	"os/exec"
	"unsafe"

	"github.com/netsys-lab/scion_packet_gen/cmd"
	"github.com/netsys-lab/scion_packet_gen/scion"
	"github.com/scionproto/scion/pkg/snet"
	log "github.com/sirupsen/logrus"
)

func main() {
	args := cmd.MustParseFlags()
	cmd.ConfigureLogging()
	// log.Info("[Main] starting with opt: ", cmd.Opts)

	if len(args) < 2 {
		log.Fatal("Not enough arguments, ia, src and dst address are required")
	}

	srcSAddr, err := snet.ParseUDPAddr(args[0])
	if err != nil {
		log.Fatal("Could not parse src SCION addr ", args[0], ": ", err)
	}

	remoteSAddr, err := snet.ParseUDPAddr(args[1])
	if err != nil {
		log.Fatal("Could not parse dst SCION addr ", args[1], ": ", err)
	}

	dstAddr := remoteSAddr.Host
	srcAddr := srcSAddr.Host

	err, srcMac, errString := Exec("bash", "-c", fmt.Sprintf("cat /sys/class/net/%s/address", cmd.Opts.Interface))
	if err != nil {
		log.Fatal("Failed to obtain mac from interface ", cmd.Opts.Interface, ": ", err, "; verbose: ", errString)
	}

	err, dstMac, errString := Exec("bash", "-c", fmt.Sprintf("ip neigh | grep '%s' | awk '{print $5}'", dstAddr.IP))
	if err != nil {
		log.Fatal("Failed to obtain dst mac from arp table for ip ", dstAddr.IP, ": ", err, "; verbose: ", errString)
	}

	// Create SCION data payload

	connCtx, err := scion.PrepareConnectivityContext(context.Background())
	if err != nil {
		log.Fatal("Could not prepare SCION ConnectivityContext: ", err)
	}

	err = scion.SetDefaultPath(connCtx.DaemonConn, context.Background(), remoteSAddr)
	if err != nil {
		log.Fatal("Could not set default SCION Path ConnectivityContext: ", err)
	}

	ps, err := scion.NewPacketSerializer(srcSAddr.IA, srcAddr, remoteSAddr)
	if err != nil {
		log.Fatal("Could not prepare SCION Context: ", err)
	}

	// hdrLen := 20 + 14 + 8 // ETH + IP + UDP
	// pktLen := cmd.Opts.DataLen + hdrLen

	bts := make([]byte, cmd.Opts.DataLen)
	preparedPacket, err := ps.Serialize(bts)
	if err != nil {
		log.Fatal("Could not serialize SCION packet: ", err)
	}

	log.Info("Setup done, starting tx...")
	log.Info("Current configuration:")
	log.Info("Interface: ", cmd.Opts.Interface)
	log.Info("Src IP: ", srcAddr.IP.String())
	log.Info("Dst IP: ", dstAddr.IP.String())
	log.Info("Src Port: ", srcAddr.Port)
	log.Info("Dst Port: ", dstAddr.Port)
	log.Info("Src MAC: ", srcMac)
	log.Info("Dst MAC: ", dstMac)
	log.Info("Queue: ", cmd.Opts.Queue)
	log.Info("Batch Size: ", cmd.Opts.BatchSize)
	log.Info("DataLen: ", cmd.Opts.DataLen)
	log.Info("SCION Path: ", remoteSAddr.Path)

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
		unsafe.Pointer(&preparedPacket[0]),
		C.int(len(preparedPacket))))

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
