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

    struct xsk_socket_info *xsk_inf = setup_socket(argv[1], 0);
    if (xsk_inf == NULL)
    {
        printf("FAILED");
        return 1;
    }

    struct send_info info;
    info.data_len = 1000;
    info.device = argv[1];
    info.src_ip = argv[2];
    info.dst_ip = argv[3];
    info.src_port = *((unsigned short *)argv[4]);
    info.dst_port = *((unsigned short *)argv[5]);

    prepare_and_send_packets(&info);

    // Close program successfully.
    return EXIT_SUCCESS;
}