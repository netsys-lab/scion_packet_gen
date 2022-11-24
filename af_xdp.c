
#include "af_xdp.h"
#include <time.h>
#define MAX_PCKT_LEN 0xFFFF

/**
 * ----------------------------------------- Global Vars --------------------------------------------
 */

__u32 xdp_flags = XDP_FLAGS_DRV_MODE | XDP_ZEROCOPY;
__u32 bind_flags = XDP_USE_NEED_WAKEUP;
__u16 batch_size = 512;

/**
 * ----------------------------------------- Setup Socket --------------------------------------------
 */

static struct xsk_umem_info *configure_xsk_umem(void *buffer, uint64_t size)
{
    struct xsk_umem_info *umem;
    int ret;

    umem = calloc(1, sizeof(*umem));
    if (!umem)
        return NULL;

    ret = xsk_umem__create(&umem->umem, buffer, size, &umem->fq, &umem->cq,
                           NULL);
    if (ret)
    {
        errno = -ret;
        return NULL;
    }

    umem->buffer = buffer;
    return umem;
}

static uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk)
{
    uint64_t frame;
    if (xsk->umem_frame_free == 0)
        return INVALID_UMEM_FRAME;

    frame = xsk->umem_frame_addr[--xsk->umem_frame_free];
    xsk->umem_frame_addr[xsk->umem_frame_free] = INVALID_UMEM_FRAME;
    return frame;
}

static struct xsk_socket_info *xsk_configure_socket(const char *ifname, unsigned int queue,
                                                    struct xsk_umem_info *umem)
{
    struct xsk_socket_config xsk_cfg;
    struct xsk_socket_info *xsk_info;
    uint32_t idx;
    int i;
    int ret;

    xsk_info = calloc(1, sizeof(*xsk_info));
    if (!xsk_info)
        return NULL;

    xsk_info->umem = umem;
    xsk_cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
    xsk_cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
    xsk_cfg.libbpf_flags = 0;
    xsk_cfg.xdp_flags = xdp_flags;
    xsk_cfg.bind_flags = bind_flags;
    ret = xsk_socket__create(&xsk_info->xsk, ifname,
                             queue, umem->umem, &xsk_info->rx,
                             &xsk_info->tx, &xsk_cfg);

    if (ret)
        goto error_exit;

    /* Initialize umem frame allocation */
    for (i = 0; i < NUM_FRAMES; i++)
        xsk_info->umem_frame_addr[i] = i * FRAME_SIZE;

    xsk_info->umem_frame_free = NUM_FRAMES;

    /* Stuff the receive path with buffers, we assume we have enough */
    ret = xsk_ring_prod__reserve(&xsk_info->umem->fq,
                                 XSK_RING_PROD__DEFAULT_NUM_DESCS,
                                 &idx);

    if (ret != XSK_RING_PROD__DEFAULT_NUM_DESCS)
        goto error_exit;

    for (i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i++)
        *xsk_ring_prod__fill_addr(&xsk_info->umem->fq, idx++) =
            xsk_alloc_umem_frame(xsk_info);

    xsk_ring_prod__submit(&xsk_info->umem->fq,
                          XSK_RING_PROD__DEFAULT_NUM_DESCS);

    return xsk_info;

error_exit:
    errno = -ret;
    return NULL;
}

struct xsk_socket_info *setup_socket(const char *interface, unsigned int queue)
{
    struct xsk_umem_info *umem;
    struct xsk_socket_info *xsk_socket;
    void *packet_buffer;
    uint64_t packet_buffer_size;

    /* Allocate memory for NUM_FRAMES of the default XDP frame size */
    packet_buffer_size = NUM_FRAMES * FRAME_SIZE;
    if (posix_memalign(&packet_buffer,
                       getpagesize(), /* PAGE_SIZE aligned */
                       packet_buffer_size))
    {
        fprintf(stderr, "ERROR: Can't allocate buffer memory \"%s\"\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Initialize shared packet_buffer for umem usage */
    umem = configure_xsk_umem(packet_buffer, packet_buffer_size);
    if (umem == NULL)
    {
        fprintf(stderr, "ERROR: Can't create umem \"%s\"\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Open and configure the AF_XDP (xsk) socket */
    xsk_socket = xsk_configure_socket(interface, queue, umem);
    if (xsk_socket == NULL)
    {
        fprintf(stderr, "ERROR: Can't setup AF_XDP socket \"%s\"\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    return xsk_socket;
}

/**
 * ----------------------------------------- Write Packets --------------------------------------------
 */

/*
 * Completes the TX call via a syscall and also checks if we need to free the TX buffer.
 *
 * @param xsk A pointer to the xsk_socket_info structure.
 *
 * @return Void
 **/
static void complete_tx(struct xsk_socket_info *xsk)
{
    // Initiate starting variables (completed amount and completion ring index).
    unsigned int completed;
    uint32_t idx_cq;

    // If outstanding is below 1, it means we have no packets to TX.
    if (!xsk->outstanding_tx)
    {
        return;
    }

    // If we need to wakeup, execute syscall to wake up socket.
    if (!(bind_flags & XDP_USE_NEED_WAKEUP) || xsk_ring_prod__needs_wakeup(&xsk->tx))
    {
        sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
    }

    // Try to free n (batch_size) frames on the completion ring.
    /*while ((completed = xsk_ring_cons__peek(&xsk->umem->cq, batch_size, &idx_cq)) < batch_size)
    {
        // If we need to wakeup, execute syscall to wake up socket.
        if (!(bind_flags & XDP_USE_NEED_WAKEUP) || xsk_ring_prod__needs_wakeup(&xsk->tx))
        {
            sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
        }
    }*/

    while (xsk->outstanding_tx > 0)
    {
        completed = xsk_ring_cons__peek(&xsk->umem->cq, batch_size, &idx_cq);
        // Release "completed" frames.
        xsk_ring_cons__release(&xsk->umem->cq, completed);

        xsk->outstanding_tx -= completed;
    }

    // completed = xsk_ring_cons__peek(&xsk->umem->cq, batch_size, &idx_cq);

    // printf("Released %d frames from cq, outstanding_tx = %d\n", completed, xsk->outstanding_tx);
}

/**
 * Sends a packet buffer out the AF_XDP socket's TX path.
 *
 * @param thread_id The thread ID to use to lookup the AF_XDP socket.
 * @param pckt The packet buffer starting at the Ethernet header.
 * @param length The packet buffer's length.
 * @param verbose Whether verbose is enabled or not.
 *
 * @return Returns 0 on success and -1 on failure.
 **/
int send_batch(struct xsk_socket_info *xsk, int thread_id, void *pckt, __u16 length)
{
    // This represents the TX index.
    __u32 tx_idx = 0;

    unsigned int reserved = 0;

    // Retrieve the TX index from the TX ring to fill.
    while ((reserved = xsk_ring_prod__reserve(&xsk->tx, batch_size, &tx_idx)) < batch_size)
    {
        complete_tx(xsk);
        // printf("reserved %d frames from tx, idx is now %d\n", reserved, tx_idx);
    }

    unsigned int idx = 0;

    // Loop through to batch size.
    for (int i = 0; i < batch_size; i++)
    {
        // Retrieve index we want to insert at in UMEM and make sure it isn't equal/above to max number of frames.
        idx = xsk->outstanding_tx + i;

        if (idx >= NUM_FRAMES)
            break;

        // We must retrieve the next available address in the UMEM.
        __u64 addrat = xsk->umem_frame_addr[idx];

        // We must copy our packet data to the UMEM area at the specific index (idx * frame size). We did this earlier.
        // memcpy(xsk_umem__get_data(xsk->umem->buffer, addrat), pckt, length);

        // Retrieve TX descriptor at index.
        struct xdp_desc *tx_desc = xsk_ring_prod__tx_desc(&xsk->tx, tx_idx + i);

        // Point the TX ring's frame address to what we have in the UMEM.
        tx_desc->addr = addrat;

        // Tell the TX ring the packet length.
        tx_desc->len = length;
    }

    // Submit the TX batch to the producer ring.
    xsk_ring_prod__submit(&xsk->tx, batch_size);

    // Increase outstanding.
    xsk->outstanding_tx += batch_size;
    // printf("Submitted %d frames from cq, outstanding_tx = %d\n", batch_size, xsk->outstanding_tx);

    // Complete TX again.
    complete_tx(xsk);

    return 0;
}

/**
 * The thread handler for sending/receiving.
 *
 * @param data Data (struct thread_info) for the sequence.
 *
 * @return Void
 **/
void *
prepare_and_send_packets(struct send_info *info)
{
    printf("Starting to send packets2\n");
    batch_size = info->batch_size;

    // Let's parse some config values before creating the socket so we know what we're doing.
    __u8 protocol = IPPROTO_UDP;
    __u8 src_mac[ETH_ALEN];
    __u8 dst_mac[ETH_ALEN];

    get_mac_address(info->src_mac, src_mac);
    get_mac_address(info->dst_mac, dst_mac);

    /* Our goal below is to set as many things before the while loop as possible since any additional instructions inside the while loop will impact performance. */

    // Create rand_r() seed.
    // unsigned int seed;

    // Initialize buffer for the packet itself.
    char buffer[MAX_PCKT_LEN];

    // Common packet characteristics.
    __u8 l4_len;

    // Source IP string for a random-generated IP address.
    // char s_ip[32];

    // Initialize Ethernet header.
    struct ethhdr *eth = (struct ethhdr *)(buffer);

    // Initialize IP header.
    struct iphdr *iph = (struct iphdr *)(buffer + sizeof(struct ethhdr));

    // Initialize UDP, TCP, and ICMP headers. Declare them as NULL until we know what protocol we're dealing with.
    struct udphdr *udph = NULL;

    // Fill out Ethernet header.
    eth->h_proto = htons(ETH_P_IP);
    memcpy(eth->h_source, src_mac, ETH_ALEN);
    memcpy(eth->h_dest, dst_mac, ETH_ALEN);

    // Fill out IP header generic fields.
    iph->ihl = 5;
    iph->version = 4;
    iph->protocol = protocol;
    iph->frag_off = 0;

    struct in_addr saddr;
    inet_aton(info->src_ip, &saddr);
    iph->saddr = saddr.s_addr;

    // // TODO: Dest IP
    struct in_addr daddr;
    inet_aton(info->dst_ip, &daddr);

    iph->daddr = daddr.s_addr;
    udph = (struct udphdr *)(buffer + sizeof(struct ethhdr) + (iph->ihl * 4));
    l4_len = sizeof(struct udphdr);

    // Check for static source/destination ports.
    if (info->src_port > 0)
    {
        udph->source = htons(info->src_port);
    }

    if (info->dst_port > 0)
    {
        udph->dest = htons(info->dst_port);
    }

    udph->len = htons(l4_len + info->data_len);

    // Check if we can set static IP header length.
    iph->tot_len = htons((iph->ihl * 4) + l4_len + info->data_len);

    // TODO: Get this method somewhere
    update_iph_checksum(iph);

    // Initialize payload data.
    unsigned char *data = (unsigned char *)(buffer + sizeof(struct ethhdr) + (iph->ihl * 4) + l4_len);
    memcpy(data, info->scion_header, info->scion_header_len);

    // TODO: UDP HEader checksum
    __u16 pckt_len = sizeof(struct ethhdr) + (iph->ihl * 4) + l4_len + info->data_len;
    // udph->check = udp_checksum(buffer, pckt_len, saddr.s_addr, daddr.s_addr);

    for (int i = 0; i < NUM_FRAMES; i++)
    {
        // We must retrieve the next available address in the UMEM.
        __u64 addrat = info->xsk->umem_frame_addr[i];

        // We must copy our packet data to the UMEM area at the specific index (idx * frame size). We did this earlier.
        memcpy(xsk_umem__get_data(info->xsk->umem->buffer, addrat), buffer, pckt_len);
    }

    // Loop.
    while (1)
    {
        int ret = 0;
        if ((ret = send_batch(info->xsk, 0, buffer, pckt_len)) != 0)
        {
            fprintf(stderr, "ERROR - Could not send batch on AF_XDP socket (%d) :: %s.\n", 0, strerror(errno));
        }

        // Check if we want to send verbose output or not.
        /*if (ret == 0)
        {
            // Retrieve source and destination ports for UDP/TCP protocols.
            __u16 srcport = 0;
            __u16 dstport = 0;

            if (protocol == IPPROTO_UDP)
            {
                srcport = ntohs(udph->source);
                dstport = ntohs(udph->dest);
            }
            // fprintf(stdout, "Sent %d bytes of data from %s:%d to %s:%d.\n", pckt_len, s_ip, srcport, info->dst_ip, dstport);
        }*/

        // usleep(1);
        // Check data.
    }
}

const char *LastError()
{
    // TBD:
    return "";
}

// TODO: We may pass this as struct, would look better
int perform_tx(char *device, char *src_ip, char *dst_ip, unsigned short src_port, unsigned short dst_port, char *src_mac, char *dst_mac, unsigned short queue, unsigned short batch_size, unsigned short data_len, void *scion_header, int scion_header_len)
{
    struct send_info info;
    info.data_len = data_len;
    info.device = device;
    info.src_ip = src_ip;
    info.dst_ip = dst_ip;
    info.src_port = src_port;
    info.dst_port = dst_port;
    info.src_mac = src_mac;
    info.dst_mac = dst_mac;
    info.queue = queue;
    info.batch_size = batch_size;
    info.scion_header = scion_header;
    info.scion_header_len = scion_header_len;

    struct xsk_socket_info *xsk_inf = setup_socket(info.device, info.queue);
    if (xsk_inf == NULL)
    {
        printf("FAILED");
        return -1;
    }
    info.xsk = xsk_inf;

    printf("Starting to send packets\n");

    prepare_and_send_packets(&info);

    // Close program successfully.
    return EXIT_SUCCESS;
}

/*
int main_c(int argc, char *argv[])
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
    info.batch_size = (unsigned short)atoi(argv[9]);

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
*/