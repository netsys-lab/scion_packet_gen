package scion

import (
	"encoding/binary"
	"net"

	"github.com/scionproto/scion/pkg/addr"
	"github.com/scionproto/scion/pkg/snet"
)

type PacketSerializer struct {
	baseBytes        snet.Bytes
	headerBytes      int
	basePayloadBytes int

	listenAddr *net.UDPAddr
	remoteAddr *snet.UDPAddr
}

const SCION_PROTOCOL_NUMBER_SCION_UDP = 17

func NewPacketSerializer(localIA addr.IA, listenAddr *net.UDPAddr, remoteAddr *snet.UDPAddr) (*PacketSerializer, error) {

	scionDestinationAddress := snet.SCIONAddress{
		IA:   remoteAddr.IA,
		Host: addr.HostFromIP(remoteAddr.Host.IP),
	}

	scionListenAddress := snet.SCIONAddress{
		IA:   localIA,
		Host: addr.HostFromIP(listenAddr.IP),
	}

	var bytes snet.Bytes
	bytes.Prepare()

	preparedPacket := &snet.Packet{
		Bytes: bytes,
		PacketInfo: snet.PacketInfo{
			Destination: scionDestinationAddress,
			Source:      scionListenAddress,
			Path:        remoteAddr.Path,
			// This is a hack.
			Payload: snet.UDPPayload{
				Payload: make([]byte, 0),
				SrcPort: 0,
				DstPort: 0,
			},
		},
	}

	err := preparedPacket.Serialize()
	if err != nil {
		return nil, err
	}

	headerBytes := len(preparedPacket.Bytes) - 8
	// We use the Packet to calculate the correct sum and subtract our dummy payload.
	basePayloadBytes := int(binary.BigEndian.Uint16(preparedPacket.Bytes[6:8]) - 8)

	pS := PacketSerializer{
		listenAddr:       listenAddr,
		remoteAddr:       remoteAddr,
		baseBytes:        preparedPacket.Bytes,
		headerBytes:      headerBytes,
		basePayloadBytes: basePayloadBytes,
	}

	return &pS, nil
}

func (pS *PacketSerializer) Serialize(b []byte) ([]byte, error) {

	/*l4PayloadSize := 8 + len(b)

	// Network Byte Order is Big Endian
	binary.BigEndian.PutUint16(pS.baseBytes[6:8], uint16(pS.basePayloadBytes+l4PayloadSize))

	binary.BigEndian.PutUint16(pS.baseBytes[pS.headerBytes+0:pS.headerBytes+2], uint16(pS.listenAddr.Port))
	binary.BigEndian.PutUint16(pS.baseBytes[pS.headerBytes+2:pS.headerBytes+4], uint16(pS.remoteAddr.Host.Port))
	binary.BigEndian.PutUint16(pS.baseBytes[pS.headerBytes+4:pS.headerBytes+6], uint16(l4PayloadSize))
	binary.BigEndian.PutUint16(pS.baseBytes[pS.headerBytes+6:pS.headerBytes+8], uint16(0))

	copy(pS.baseBytes[pS.headerBytes+8:pS.headerBytes+l4PayloadSize], b)

	dataLength := pS.headerBytes + l4PayloadSize
	return pS.baseBytes[0:dataLength], nil*/
	copy(b, pS.baseBytes[0:pS.headerBytes]) // Copy SCION header into packet

	binary.BigEndian.PutUint16(b[pS.headerBytes+0:pS.headerBytes+2], uint16(pS.listenAddr.Port))
	binary.BigEndian.PutUint16(b[pS.headerBytes+2:pS.headerBytes+4], uint16(pS.remoteAddr.Host.Port))
	binary.BigEndian.PutUint16(b[pS.headerBytes+4:pS.headerBytes+6], uint16(len(b)-8-pS.headerBytes))
	binary.BigEndian.PutUint16(b[pS.headerBytes+6:pS.headerBytes+8], uint16(0))

	return b, nil
}

func (pS *PacketSerializer) GetHeaderLen() int {
	// ps.HeaderBytes contains the header length without the UDP header.
	// An UDP header is 8 bytes long.
	return pS.headerBytes + 8
}
