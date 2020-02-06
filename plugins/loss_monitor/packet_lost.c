#include <picoquic.h>
#include "loss_monitor.h"

protoop_arg_t packet_lost(picoquic_cnx_t *cnx) {

    PROTOOP_PRINTF(cnx, "MONITOR PACKET LOST\n");


    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_INPUT, 0);
    int64_t packet_number = (int64_t) get_cnx(cnx, AK_CNX_INPUT, 1);
    uint64_t send_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 2);
    uint64_t current_time = (uint64_t) get_cnx(cnx, AK_CNX_INPUT, 3);

    PROTOOP_PRINTF(cnx, "BEFORE GET PATH MD\n");
    loss_monitor_t *monitor = (loss_monitor_t *) get_path_metadata(cnx, path, PATH_METADATA_LOSS_MONITOR_IDX);
    PROTOOP_PRINTF(cnx, "NACK GOT MONITOR %p\n", (protoop_arg_t) monitor);
    if (!monitor) {
        PROTOOP_PRINTF(cnx, "ALLOC NEW MONITOR\n");
        monitor = my_malloc(cnx, sizeof(loss_monitor_t));
        if (!monitor) {
            return PICOQUIC_ERROR_MEMORY;
        }
        loss_monitor_init(cnx, monitor, current_time, DEFAULT_ESTIMATION_GRANULARITY, DEFAULT_ESTIMATION_INTERVAL_MICROSEC, DEFAULT_EVENT_LIFETIME_MICROSEC);
        PROTOOP_PRINTF(cnx, "INITED NEW MONITOR\n");
        set_path_metadata(cnx, path, PATH_METADATA_LOSS_MONITOR_IDX, (protoop_arg_t) monitor);
    }

    loss_monitor_packet_lost(cnx, monitor, packet_number, send_time);
    PROTOOP_PRINTF(cnx, "MONITOR PACKET LOST END\n");

    return 0;
}