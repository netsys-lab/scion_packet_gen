#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/types.h>
#include <getopt.h>
#include <errno.h>

#include "af_xdp.h"

int main(int argc, char *argv[])
{

    struct send_info info;
    info.data_len = 1440;
    info.device = argv[1];
    info.src_ip = argv[2];
    info.dst_ip = argv[3];
    info.src_port = (unsigned short)atoi(argv[4]);
    info.dst_port = (unsigned short)atoi(argv[5]);
    info.src_mac = argv[6];
    info.dst_mac = argv[7];
    info.queue = (unsigned short)atoi(argv[8]);

    struct xsk_socket_info *xsk_inf = setup_socket(argv[1], info.queue);
    if (xsk_inf == NULL)
    {
        printf("FAILED");
        return 1;
    }
    info.xsk = xsk_inf;

    printf("Starting to send packets\n");

    prepare_and_send_packets(&info);

    // Close program successfully.
    return EXIT_SUCCESS;
}