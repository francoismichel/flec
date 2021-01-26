#include <picoquic.h>
#include "loss_monitor.h"

protoop_arg_t monitor_packet_acknowledged(picoquic_cnx_t *cnx) {


    picoquic_packet_t *p = (picoquic_packet_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 1);

    picoquic_path_t *path = (picoquic_path_t *) get_pkt(p, AK_PKT_SEND_PATH);
    uint64_t send_time = (uint64_t) get_pkt(p, AK_PKT_SEND_TIME);
    int64_t packet_number = (int64_t) get_pkt(p, AK_PKT_SEQUENCE_NUMBER);

    loss_monitor_t *monitor = (loss_monitor_t *) get_path_metadata(cnx, path, PATH_METADATA_LOSS_MONITOR_IDX);
    PROTOOP_PRINTF(cnx, "ACK GOT MONITOR %p\n", (protoop_arg_t) monitor);
    if (!monitor) {
        monitor = my_malloc(cnx, sizeof(loss_monitor_t));
        if (!monitor) {
            return PICOQUIC_ERROR_MEMORY;
        }
        loss_monitor_init(cnx, monitor, current_time, DEFAULT_ESTIMATION_GRANULARITY, DEFAULT_ESTIMATION_INTERVAL_MICROSEC, DEFAULT_EVENT_LIFETIME_MICROSEC);
        set_path_metadata(cnx, path, PATH_METADATA_LOSS_MONITOR_IDX, (protoop_arg_t) monitor);
    }

    loss_monitor_packet_acknowledged(cnx, monitor, packet_number, send_time);

    return 0;
}