package cmd

import (
	"fmt"
	"os"

	"github.com/jessevdk/go-flags"
)

var Opts struct {
	// TODO: Replace with packet len
	DataLen   int    `short:"d" long:"dataLen" description:"Payload length after all headers (default 1000)" default:"1000"`
	Interface string `short:"i" long:"interface" description:"Specify the network interface by name" required:"true"`
	Queue     int    `short:"q" long:"queue" description:"Queue Number" default:"0"`
	BatchSize int    `short:"b" long:"batchSize" description:"Batch size of packets being passed to the kernel at once" default:"32"`
	LogLevel  string `short:"l" long:"logLevel" description:"Log-level (ERROR|WARN|INFO|DEBUG|TRACE)" default:"INFO"`
}

func MustParseFlags() []string {
	p := flags.NewParser(&Opts, flags.HelpFlag)
	p.Usage = "scion_packet_gen [OPTIONS] [SRC_IPV4_ADDR] [DEST_IPV4_ADDR]"
	p.ShortDescription = "scion_packet_gen - AF_XDP based SCION packet generator"
	p.LongDescription = "scion_packet_gen - AF_XDP based SCION packet generator"
	// err is containing the usage description
	dests, err := p.Parse()
	if err != nil {
		fmt.Println(err) // here we don't use log because we dont want any timestamps or similar being printed
		os.Exit(1)
	}

	return dests
}
