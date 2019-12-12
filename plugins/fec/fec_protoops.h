#ifndef FEC_BPF_H
#define FEC_BPF_H

#define MAX_RECOVERED_PACKETS_IN_BUFFER 50
#define RECEIVE_BUFFER_MAX_LENGTH 110
#define MIN(a, b) ((a < b) ? a : b)
#define FEC_PKT_METADATA_SENT_SLOT 0
#define FEC_PKT_METADATA_IS_FEC_PROTECTED 1
#define FEC_PKT_METADATA_CONTAINS_FEC_PACKET 2
#define FEC_PKT_METADATA_FIRST_SOURCE_SYMBOL_ID 3
#define MAX_FEC_FRAMES_SENT_BEFORE_HANDSHAKE_FINISHED 2
#include <picoquic_logger.h>
#include <picoquic.h>
#include "../helpers.h"
#include "fec.h"
#include "../simple_fec/utils.h"

typedef void * fec_framework_t;

typedef struct {
    bool has_sent_stream_data;
    bool should_check_block_flush;
    char underlying_fec_scheme[8];
    uint32_t oldest_fec_block_number : 24;
    uint8_t *current_symbol;
    uint16_t current_symbol_length;
    fec_framework_t framework_sender;
    fec_framework_t framework_receiver;
    fec_scheme_t scheme_sender;
    fec_scheme_t scheme_receiver;
    fec_redundancy_controller_t controller;
    source_fpid_frame_t *current_sfpid_frame;    // this variable is not-null only between prepare_packet_ready and finalize_and_protect_packet
    int sfpid_frame_position_in_current_packet_payload;
    bool is_in_skip_frame;    // set to true if we are currently in skip_frame
    bool current_packet_contains_fec_frame;    // set to true if the current packet contains a FEC Frame (FEC and FPID frames are mutually exclusive)
    bool current_packet_contains_fpid_frame;    // set to true if the current packet contains a FPID Frame
    bool sfpid_reserved;                        // set to true when a SFPID frame has been reserved
    bool cancel_sfpid_in_current_packet;     // set to true when no SFPID frame should be written in the current packet
    bool in_recovery;                        // set to true when the plugin is currently in the process of a packet recovery
    bool has_ready_stream;                   // set to true when there is a ready stream in the current packet loop
    bool handshake_finished;
    uint8_t *written_sfpid_frame;            // set by write_sfpid_frame to the address of the sfpid frame written in the packet, used to undo a packet protection
    lost_packet_queue_t lost_packets;
    fec_block_t *fec_blocks[MAX_FEC_BLOCKS]; // ring buffer
    uint64_t last_protected_slot;
    uint64_t last_fec_slot;
    recovered_packets_buffer_t recovered_packets;
    protoop_id_t    protect_id;
    protoop_id_t    packet_to_source_symbol_id;
    protoop_id_t    get_redundancy_parameters_id;
    protoop_id_t    should_send_recovered_id;
    protoop_id_t    receive_source_symbol_id;
    protoop_id_t    get_source_fpid_id;
    uint64_t        n_fec_frames_sent_before_handshake_finished;

    // FIXME: horrible hack to work around the fact that in dequeue_retransmit_packet, "should_free" is equivalent to "received", so we add the "received" signal using this...
    bool current_packet_is_lost;
} bpf_state;

static __attribute__((always_inline)) bpf_state *initialize_bpf_state(picoquic_cnx_t *cnx)
{
    bpf_state *state = (bpf_state *) my_malloc(cnx, sizeof(bpf_state));
    if (!state) return NULL;
    my_memset(state, 0, sizeof(bpf_state));
    protoop_arg_t frameworks[2];
    protoop_arg_t schemes[2];
    // create_fec_schemes creates the receiver (0) and sender (1) FEC Schemes. If an error happens, ret != 0 and both schemes are freed by the protoop
    int ret = (int) run_noparam(cnx, "create_fec_schemes", 0, NULL, schemes);
    if (ret) {
//        PROTOOP_PRINTF(cnx, "ERROR WHEN CREATING FEC SCHEMES\n");
        my_free(cnx, state);
        return NULL;
    }
    // create_redundancy_controller creates the redundancy controller
    ret = (int) run_noparam(cnx, "create_redundancy_controller", 0, NULL, (protoop_arg_t *) &state->controller);
    if (ret) {
//        PROTOOP_PRINTF(cnx, "ERROR WHEN CREATING REDUNDANCY CONTROLLER\n");
        my_free(cnx, state);
        return NULL;
    }
    state->scheme_receiver = (fec_scheme_t) schemes[0];
    state->scheme_sender = (fec_scheme_t) schemes[1];
    protoop_arg_t args[3];
    args[0] = schemes[0];
    args[1] = schemes[1];
    args[2] = (protoop_arg_t) state->controller;
    // create_fec_framework creates the receiver (0) and sender (1) FEC Frameworks. If an error happens, ret != 0 and both frameworks are freed by the protoop
    ret = (int) run_noparam(cnx, "create_fec_framework", 3, args, frameworks);
    if (ret) {
        my_free(cnx, state);
//        PROTOOP_PRINTF(cnx, "ERROR WHEN CREATING FRAMEWORKS\n");
        return NULL;
    }
    state->framework_receiver = (fec_framework_t) frameworks[0];
    state->framework_sender = (fec_framework_t) frameworks[1];
    return state;
}

static __attribute__((always_inline)) bpf_state *get_bpf_state(picoquic_cnx_t *cnx)
{
    bpf_state *state_ptr = (bpf_state *) get_cnx_metadata(cnx, FEC_OPAQUE_ID);
    if (!state_ptr) {
        state_ptr = initialize_bpf_state(cnx);
        set_cnx_metadata(cnx, FEC_OPAQUE_ID, (protoop_arg_t) state_ptr);
    }
    return state_ptr;
}

static __attribute__((always_inline)) int helper_write_source_fpid_frame(picoquic_cnx_t *cnx, source_fpid_frame_t *f, uint8_t *bytes, size_t bytes_max, size_t *consumed) {
    if (bytes_max <  (1 + sizeof(source_fpid_t)))
        return PICOQUIC_ERROR_FRAME_BUFFER_TOO_SMALL;
    *bytes = SOURCE_FPID_TYPE;
    bytes++;
    encode_u32(f->source_fpid.raw, bytes);
    *consumed = (1 + sizeof(source_fpid_frame_t));
    return 0;
}


static __attribute__((always_inline)) void remove_and_free_fec_block_at(picoquic_cnx_t *cnx, bpf_state *state, uint32_t where){
    free_fec_block(cnx, state->fec_blocks[where % MAX_FEC_BLOCKS], false);
    state->fec_blocks[where % MAX_FEC_BLOCKS] = NULL;
}

static __attribute__((always_inline)) int get_redundancy_parameters(picoquic_cnx_t *cnx, fec_redundancy_controller_t controller, bool flush, uint8_t *n, uint8_t *k){
    bpf_state *state = get_bpf_state(cnx);
    if (!state) return -1;
    protoop_arg_t out[2];
    protoop_arg_t args[2] = {(protoop_arg_t) controller, flush};
    int ret = (int) run_noparam_with_pid(cnx, "get_redundancy_parameters", 2, args, out, &state->get_redundancy_parameters_id);
    if (ret) {
        PROTOOP_PRINTF(cnx, "ERROR WHEN GETTING REDUNDANCY PARAMETERS\n");
        return -1;
    }
    if (n) *n = (uint8_t) out[0];
    if (k) *k = (uint8_t) out[1];
    return 0;
}

static __attribute__((always_inline)) void maybe_notify_recovered_packets_to_everybody(picoquic_cnx_t *cnx,
                                                                                       recovered_packets_buffer_t *b,
                                                                                       uint64_t current_time) {
    // TODO: handle multipath
    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
    picoquic_packet_context_t *pkt_ctx = (picoquic_packet_context_t *) get_path(path, AK_PATH_PKT_CTX, picoquic_packet_context_application);
    picoquic_packet_t *current_packet = (picoquic_packet_t *) get_pkt_ctx(pkt_ctx, AK_PKTCTX_RETRANSMIT_OLDEST);
    while(b->size > 0 && current_packet) {
        picoquic_packet_t *pnext = (picoquic_packet_t *) get_pkt(current_packet, AK_PKT_NEXT_PACKET);
        uint64_t current_pn64 = get_pkt(current_packet, AK_PKT_SEQUENCE_NUMBER);
        if (current_pn64 == peek_first_recovered_packet_in_buffer(b)) {
            int timer_based = 0;
            if (!helper_retransmit_needed_by_packet(cnx, current_packet, current_time, &timer_based, NULL, NULL)) {
                // we don't need to notify it now: the packet is not considered as lost
                // don't try any subsequenc packets as they have been sent later
                break;
            }
            uint64_t slot = (uint64_t) get_pkt_metadata(cnx, current_packet, FEC_PKT_METADATA_SENT_SLOT);
            source_symbol_id_t first_id = (source_symbol_id_t) get_pkt_metadata(cnx, current_packet, FEC_PKT_METADATA_FIRST_SOURCE_SYMBOL_ID);
            uint16_t n_symbols = (uint16_t) get_pkt_metadata(cnx, current_packet, FEC_PKT_METADATA_NUMBER_OF_SOURCE_SYMBOLS);
            bool fec_protected = (bool) FEC_PKT_IS_FEC_PROTECTED(get_pkt_metadata(cnx, current_packet, FEC_PKT_METADATA_FLAGS));
            bool contains_repair_frame = (bool) FEC_PKT_CONTAINS_REPAIR_FRAME(get_pkt_metadata(cnx, current_packet, FEC_PKT_METADATA_FLAGS));
            //we need to remove this packet from the retransmit queue
            uint64_t retrans_cc_notification_timer = get_pkt_ctx(pkt_ctx, AK_PKTCTX_LATEST_RETRANSMIT_CC_NOTIFICATION_TIME) + get_path(path, AK_PATH_SMOOTHED_RTT, 0);
            bool packet_is_pure_ack = get_pkt(current_packet, AK_PKT_IS_PURE_ACK);
            // notify everybody that this packet is officially lost
            helper_packet_was_lost(cnx, current_packet, path);
            // it has been lost, but recovered, thus the symbol contained in the packet have been received
            fec_packet_symbols_have_been_received(cnx, current_pn64, slot, first_id, n_symbols, fec_protected,
                                                  contains_repair_frame);
            helper_dequeue_retransmit_packet(cnx, current_packet, 1);
            if (current_time >= retrans_cc_notification_timer && !packet_is_pure_ack) {    // do as in core: if is pure_ack or recently notified, do not notify cc
                set_pkt_ctx(pkt_ctx, AK_PKTCTX_LATEST_RETRANSMIT_CC_NOTIFICATION_TIME, current_time);
                helper_congestion_algorithm_notify(cnx, path, picoquic_congestion_notification_repeat, 0, 0,
                                                   current_pn64, current_time);
            }
            PROTOOP_PRINTF(cnx, "[[PACKET RECOVERED]] %lu,%lu\n", current_pn64, current_time - get_pkt(current_packet, AK_PKT_SEND_TIME));
            dequeue_recovered_packet_from_buffer(b);
        } else if (current_pn64 > peek_first_recovered_packet_in_buffer(b)) {
            // the packet to remove is already gone from the retransmit queue
            dequeue_recovered_packet_from_buffer(b);
        } // else, do nothing, try the next packet
        current_packet = pnext;
    }
}

// protects the packet and writes the source_fpid
static __attribute__((always_inline)) int protect_packet(picoquic_cnx_t *cnx, source_fpid_t *source_fpid, uint8_t *data, uint16_t length){
    PROTOOP_PRINTF(cnx, "IN PROTECT_PACKET\n");
    bpf_state *state = get_bpf_state(cnx);
    PROTOOP_PRINTF(cnx, "AFTER GET STATE, data = %p, sfpid = %u\n", (protoop_arg_t) data, (*source_fpid).raw);

    source_symbol_t *ss = malloc_source_symbol_with_allocated_data(cnx, *source_fpid, data, length);
    PROTOOP_PRINTF(cnx, "AFTER MALLOC SYMBOL\n");
    if (!ss)
        return PICOQUIC_ERROR_MEMORY;
    PROTOOP_PRINTF(cnx, "PROTECT PACKET OF SIZE %u\n", (unsigned long) length);
    // protect_source_symbol lets the underlying sender-side FEC Framework protect the source symbol
    // the SFPID of the SS is set by protect_source_symbol
    protoop_arg_t params[2];
    params[0] = (protoop_arg_t) state->framework_sender;
    params[1] = (protoop_arg_t) ss;

    int ret = (int) run_noparam_with_pid(cnx, "fec_protect_source_symbol", 2, params, NULL, &state->protect_id);
    // write the source fpid
    source_fpid->raw = ss->source_fec_payload_id.raw;
    if (ret) {
        PROTOOP_PRINTF(cnx, "ERROR WHEN PROTECTING\n");
        free_source_symbol(cnx, ss);
        return ret;
    }
    return 0;
}

static __attribute__((always_inline)) bool should_send_recovered_frames(picoquic_cnx_t *cnx, recovered_packets_t *rp) {
    bpf_state *state = get_bpf_state(cnx);
    if (!state) return false;
    return (bool) run_noparam_with_pid(cnx, "should_send_recovered_frames", 1, (protoop_arg_t *) &rp, NULL, &state->should_send_recovered_id);
}



static __attribute__((always_inline)) int receive_source_symbol_helper(picoquic_cnx_t *cnx, source_symbol_t *ss){
    bpf_state *state = get_bpf_state(cnx);
    if (!state) {
        return PICOQUIC_ERROR_MEMORY;
    }
    protoop_arg_t  inputs[2];
    inputs[0] = (protoop_arg_t) ss;
    inputs[1] = true;
    int err = (int) run_noparam_with_pid(cnx, "receive_source_symbol", 2, inputs, NULL, &state->receive_source_symbol_id);
    if (err)
        return err;
    if (!state->in_recovery) {
        inputs[0] = (protoop_arg_t) state->framework_receiver;
        err = run_noparam(cnx, "try_to_recover", 1, inputs, NULL);
    }
    return err;

}

#define MAX_RECOVERED_IN_ONE_ROW 5
#define MIN_DECODED_SYMBOL_TO_PARSE 20

static __attribute__((always_inline)) int recover_block(picoquic_cnx_t *cnx, bpf_state *state, fec_block_t *block){
    PROTOOP_PRINTF(cnx, "RECOVER BLOCK\n");
    fec_block_t *fb = malloc_fec_block(cnx, block->fec_block_number);
    PROTOOP_PRINTF(cnx, "MALLOCED %p\n", (protoop_arg_t) fb);
    // we copy the FEC block to avoid any impact on the internal state of the underlying framework
    my_memcpy(fb, block, sizeof(fec_block_t));

    protoop_arg_t args[5], outs[1];
    args[0] = (protoop_arg_t) fb;
    args[1] = (protoop_arg_t) state->scheme_receiver;
    uint8_t *to_recover = (uint8_t *) my_malloc(cnx, MAX_RECOVERED_IN_ONE_ROW);
    int n_to_recover = 0;
    for (uint8_t i = 0; i < fb->total_source_symbols && n_to_recover < MAX_RECOVERED_IN_ONE_ROW; i++) {
        if (fb->source_symbols[i] == NULL) {
            to_recover[n_to_recover++] = i;
        }
    }

    int ret = 0;
    if (n_to_recover > 0) {
        recovered_packets_t *rp = my_malloc(cnx, sizeof(recovered_packets_t));
        if(rp) {    // if rp is null, this is not a big deal, just don't send the recovered frame
            my_memset(rp, 0, sizeof(recovered_packets_t));
            rp->packets = my_malloc(cnx, n_to_recover*sizeof(uint64_t));
            rp->number_of_packets = 0;
            if (!rp->packets) {
                my_free(cnx, rp);
                rp = NULL;
            } else {
                rp->recovered_sfpids = my_malloc(cnx, n_to_recover*sizeof(uint64_t));
                rp->number_of_sfpids = 0;
                if (!rp->recovered_sfpids) {
                    my_free(cnx, rp->packets);
                    my_free(cnx, rp);
                    rp = NULL;
                }
            }
        }
        PROTOOP_PRINTF(cnx, "START RECOVERY\n");
        ret = (int) run_noparam(cnx, "fec_recover", 2, args, outs);
        int idx = 0;
        int i = 0;
        for (idx = 0 ; idx < n_to_recover ; idx++) {
            i = to_recover[idx];
            if (fb->source_symbols[i] && fb->source_symbols[i]->data_length > MIN_DECODED_SYMBOL_TO_PARSE) {
                uint64_t pn = decode_u64(fb->source_symbols[i]->data + 1);

                int payload_length = fb->source_symbols[i]->data_length - 1 - sizeof(uint64_t);
                if (!ret) {
                    picoquic_path_t *path = (picoquic_path_t *) get_cnx(cnx, AK_CNX_PATH, 0);
                    PROTOOP_PRINTF(cnx,
                                   "DECODING FRAMES OF RECOVERED SYMBOL (offset %d): pn = %llx, len_frames = %u, start = 0x%x\n",
                                   (protoop_arg_t) i, pn,
                                   payload_length, fb->source_symbols[i]->data[0]);

                    uint8_t *tmp_current_packet = state->current_symbol;
                    uint16_t tmp_current_packet_length = state->current_symbol_length;
                    // ensure that we don't consider it as a new Soruce Symbol when parsing
                    state->current_symbol = fb->source_symbols[i]->data;
                    state->current_symbol_length = fb->source_symbols[i]->data_length;
                    state->in_recovery = true;
                    receive_source_symbol_helper(cnx, fb->source_symbols[i]);
                    ret = picoquic_decode_frames_without_current_time(cnx, fb->source_symbols[i]->data + sizeof(uint64_t) + 1, (size_t) payload_length, 3, path);

                    state->in_recovery = false;


                    if (!ret) {
                        PROTOOP_PRINTF(cnx, "DECODED ! \n");
                        if (rp) {
                            rp->packets[rp->number_of_packets++] = pn;
                            rp->recovered_sfpids[rp->number_of_sfpids++] = fb->source_symbols[i]->source_fec_payload_id;
                        }
                    } else {
                        PROTOOP_PRINTF(cnx, "ERROR WHILE DECODING: %u ! \n", (uint32_t) ret);
                    }
                    // we should free the recovered symbol: it has been correctly handled when decoding the packet
                    my_free(cnx, fb->source_symbols[i]);
                    if (state->current_symbol) {
                        // the symbol has not been consumed, so we must free its payload
                        my_free(cnx, state->current_symbol);
                    } // otherwise, don't free the payload: it is used by the source symbol

                    state->current_symbol = tmp_current_packet;
                    state->current_symbol_length = tmp_current_packet_length;
                    fb->source_symbols[i] = NULL;
                }
            }
        }

        if (rp) {
            reserve_frame_slot_t *slot = NULL;
            if (rp->number_of_packets > 0 && should_send_recovered_frames(cnx, rp)) {
                PROTOOP_PRINTF(cnx, "BEFORE MALLOC RF\n");
                slot = my_malloc(cnx, sizeof(reserve_frame_slot_t));
                PROTOOP_PRINTF(cnx, "AFTER MALLOC RF\n");
            }
            if (slot) {
                my_memset(slot, 0, sizeof(reserve_frame_slot_t));
                slot->frame_ctx = rp;
                slot->frame_type = RECOVERED_TYPE;
                slot->nb_bytes = 250; /* FIXME dynamic count */
                size_t reserved_size = reserve_frames(cnx, 1, slot);
                PROTOOP_PRINTF(cnx, "RESERVED FOR FRAME %u (%lu bytes)\n", slot->frame_type, reserved_size);
                if (reserved_size < slot->nb_bytes) {
                    PROTOOP_PRINTF(cnx, "Unable to reserve frame slot\n");
                    my_free(cnx, rp->packets);
                    my_free(cnx, rp->recovered_sfpids);
                    my_free(cnx, rp);
                    my_free(cnx, slot);
                }
            } else {
                my_free(cnx, rp->packets);
                my_free(cnx, rp->recovered_sfpids);
                my_free(cnx, rp);
            }
        }
    }
    my_free(cnx, to_recover);
    my_free(cnx, fb);


    return ret;

}

// assumes that the data_length field of the frame is safe
static __attribute__((always_inline)) int process_fec_frame_helper(picoquic_cnx_t *cnx, fec_frame_t *frame) {
    repair_symbol_t *rs = malloc_repair_symbol_with_data(cnx, frame->header.repair_fec_payload_id, frame->data,
                                                         frame->header.data_length);
    PROTOOP_PRINTF(cnx, "MALLOCED %p, frame = %p\n", (protoop_arg_t) rs, (protoop_arg_t) frame);
    if (!rs) {
        return PICOQUIC_ERROR_MEMORY;
    }
    rs->nss = frame->header.nss;
    PROTOOP_PRINTF(cnx, "FRAME DATA = %p\n", (protoop_arg_t) frame->data);
    bpf_state *state = get_bpf_state(cnx);
    if (!state) {
        free_repair_symbol(cnx, rs);
        return PICOQUIC_ERROR_MEMORY;
    }
    protoop_arg_t params[4];
    params[0] = (protoop_arg_t) state->framework_receiver;
    params[1] = (protoop_arg_t) rs;
    params[2] = (protoop_arg_t) frame->header.nss;
    params[3] = (protoop_arg_t) frame->header.nrs;
    PROTOOP_PRINTF(cnx, "FRAME DATA 2 = %p\n", (protoop_arg_t) frame->data);
    // receive_repair_symbol asks the underlying receiver-side FEC Framework to handle a received Repair Symbol
    int ret = (int) run_noparam(cnx, "receive_repair_symbol", 4, params, NULL);
    PROTOOP_PRINTF(cnx, "FRAME DATA 3 = %p\n", (protoop_arg_t) frame->data);
    if(ret != 1) {
        PROTOOP_PRINTF(cnx, "FREE RS\n");
        // the symbol could not be inserted: we do not care if an error happened, we free anyway and return the received error code
        free_repair_symbol(cnx, rs);
    }
    PROTOOP_PRINTF(cnx, "FRAME DATA 4 = %p\n", (protoop_arg_t) frame->data);
    return ret;
}

static __attribute__((always_inline)) int flush_repair_symbols(picoquic_cnx_t *cnx) {
    bpf_state *state = get_bpf_state(cnx);
    if (!state) {
        return PICOQUIC_ERROR_MEMORY;
    }
    int ret = (int) run_noparam(cnx, "flush_repair_symbols", 1, (protoop_arg_t *) &state->framework_sender, NULL);
    return ret;
}



static __attribute__((always_inline)) int set_source_fpid(picoquic_cnx_t *cnx, source_fpid_t *sfpid){
    bpf_state *state = get_bpf_state(cnx);
    if (!state) {
        return PICOQUIC_ERROR_MEMORY;
    }
    sfpid->raw = (uint32_t) run_noparam_with_pid(cnx, "get_source_fpid", 1, (protoop_arg_t *) &state->framework_sender, NULL, &state->get_source_fpid_id);
    PROTOOP_PRINTF(cnx, "SFPID HAS BEEN SET TO %u\n", sfpid->raw);
    return 0;
}



#endif