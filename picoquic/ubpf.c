#include "ubpf.h"
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <elf.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <time.h>
#include <zlib.h>
#include "gf256/gf256.h"
#include "plugin.h"
#include "memcpy.h"
#include "memory.h"
#include "tls_api.h"
#include "endianness.h"
#include "getset.h"
#include "picoquic_logger.h"
#include "red_black_tree.h"
#include "cc_common.h"

#if defined(NS3)
#define JIT false
#elif defined(__APPLE__)
#define JIT false
#elif defined(__arm__) && !defined(__aarch64__) // 32bit ARM currently not supported
#define JIT false
#else
#define JIT true  /* putting to false show out of memory access */
#endif

void picoquic_memory_bound_error(uint64_t val, uint64_t mem_ptr, uint64_t stack_ptr) {
    printf("Out of bound access with val 0x%" PRIx64 ", start of mem is 0x%" PRIx64 ", top of stack is 0x%" PRIx64 "\n", val, mem_ptr, stack_ptr);
}

wrapextern(plugin_run_protoop, picoquic_cnx_t *, protoop_params_t *, char *, protoop_id_t *)
wrapextern(reserve_frames, picoquic_cnx_t*, uint8_t, reserve_frame_slot_t*)
wrapextern(get_cnx, picoquic_cnx_t*, access_key_t, uint16_t)
wrapexternvoid(set_cnx, picoquic_cnx_t*, access_key_t, uint16_t, protoop_arg_t)
wrapextern(get_cnx_metadata, picoquic_cnx_t*, int)
wrapexternvoid(set_cnx_metadata, picoquic_cnx_t*, int, protoop_arg_t)
wrapextern(get_path, picoquic_path_t*, access_key_t, uint16_t)
wrapexternvoid(set_path, picoquic_path_t *, access_key_t, uint16_t, protoop_arg_t)
wrapextern(get_path_metadata, picoquic_cnx_t *, picoquic_path_t *, int)
wrapexternvoid(set_path_metadata, picoquic_cnx_t *, picoquic_path_t*, int, protoop_arg_t)
wrapextern(get_pkt_ctx, picoquic_packet_context_t *, access_key_t)
wrapexternvoid(set_pkt_ctx, picoquic_packet_context_t *, access_key_t, protoop_arg_t)
wrapextern(get_pkt, picoquic_packet_t *, access_key_t)
wrapexternvoid(set_pkt, picoquic_packet_t *, access_key_t, protoop_arg_t)
wrapextern(get_pkt_n_metadata, picoquic_cnx_t *, picoquic_packet_t *, int *, int, protoop_arg_t *)
wrapexternvoid(set_pkt_n_metadata, picoquic_cnx_t *, picoquic_packet_t *, int *, protoop_arg_t *, int)
wrapextern(get_sack_item, picoquic_sack_item_t *, access_key_t)
wrapexternvoid(set_sack_item, picoquic_sack_item_t *, access_key_t, protoop_arg_t)
wrapextern(get_cnxid, picoquic_connection_id_t *, access_key_t)
wrapexternvoid(set_cnxid, picoquic_connection_id_t *, access_key_t, protoop_arg_t)
wrapextern(get_stream_head, picoquic_stream_head *, access_key_t)
wrapexternvoid(set_stream_head, picoquic_stream_head *, access_key_t, protoop_arg_t)
wrapextern(get_stream_data, picoquic_stream_data *, access_key_t)
wrapextern(get_crypto_context, picoquic_crypto_context_t *, access_key_t)
wrapexternvoid(set_crypto_context, picoquic_crypto_context_t *, access_key_t, protoop_arg_t)
wrapextern(get_ph, picoquic_packet_header *, access_key_t)
wrapexternvoid(set_ph, picoquic_packet_header *, access_key_t, protoop_arg_t)
wrapextern(cancel_head_reservation, picoquic_cnx_t *, uint8_t *, int)
wrapexternvoid(picoquic_reinsert_cnx_by_wake_time, picoquic_cnx_t *, uint64_t)
wrapextern(my_malloc, picoquic_cnx_t *, unsigned int)
wrapextern(my_calloc, picoquic_cnx_t *, size_t, size_t)
wrapexternvoid(my_free, picoquic_cnx_t *, void *)
wrapextern(my_realloc, picoquic_cnx_t *, void *, unsigned int)
wrapextern(my_memcpy, void *, void *, size_t)
wrapextern(my_memmove, void *, void *, size_t)
wrapextern(my_memset, void *, int, size_t)
wrapextern(clock_gettime, clockid_t, struct timespec *)
wrapextern(getsockopt, int, int, int, void *, socklen_t *)
wrapextern(setsockopt, int, int, int, const void *, socklen_t)
wrapextern(socket, int, int, int)
wrapextern(connect, int, const struct sockaddr *, socklen_t)
wrapextern(send, int, const void *, size_t, int)
wrapextern(inet_aton, const char *, struct in_addr *)
wrapextern(socketpair, int, int, int, int *)
wrapextern(write, int, const void*, size_t)
wrapextern(close, int)
wrapexternnoarg(get_errno)

wrapextern(my_htons, uint16_t)
wrapextern(my_ntohs, uint16_t)
wrapextern(strncmp, const char *, const char *, size_t)
wrapextern(strlen, const char *)

wrapextern(picoquic_has_booked_plugin_frames, picoquic_cnx_t *)
wrapextern(picoquic_decode_frames_without_current_time, picoquic_cnx_t *, uint8_t *, size_t, int, picoquic_path_t *)

wrapextern(picoquic_varint_decode, const uint8_t *, size_t, uint64_t *)
wrapextern(picoquic_varint_encode, uint8_t *, size_t, uint64_t)
wrapextern(picoquic_varint_skip, const uint8_t *)
wrapexternvoid(picoquic_create_random_cnx_id_for_cnx, picoquic_cnx_t *, picoquic_connection_id_t *, uint8_t)
wrapextern(picoquic_create_cnxid_reset_secret_for_cnx, picoquic_cnx_t *, picoquic_connection_id_t *, uint8_t *)
wrapextern(picoquic_register_cnx_id_for_cnx, picoquic_cnx_t *, picoquic_connection_id_t *)
wrapextern(picoquic_create_path, picoquic_cnx_t *, uint64_t, struct sockaddr *)
wrapextern(picoquic_getaddrs, struct sockaddr_storage *, uint32_t *, int)
wrapextern(picoquic_compare_connection_id, picoquic_connection_id_t *, picoquic_connection_id_t *)

wrapextern(picoquic_compare_addr, struct sockaddr *, struct sockaddr *)
//wrapextern(picoquic_parse_stream_header, const uint8_t *, size_t, uint64_t *, uint64_t *, size_t, int *, size_t *)
wrapextern(picoquic_find_stream, picoquic_cnx_t *, uint64_t, int)
wrapextern(picoquic_reset_stream, picoquic_cnx_t *, uint64_t, uint64_t)
wrapextern(picoquic_add_to_stream, picoquic_cnx_t *, uint64_t, const uint8_t *, size_t, int)
wrapextern(find_ready_stream_round_robin, picoquic_cnx_t *)
wrapexternvoid(picoquic_set_cnx_state, picoquic_cnx_t *, picoquic_state_enum)
wrapextern(picoquic_frames_varint_decode, uint8_t *, const uint8_t *, uint64_t *)
wrapextern(picoquic_record_pn_received, picoquic_cnx_t *, picoquic_path_t *, picoquic_packet_context_enum, uint64_t, uint64_t)
wrapextern(picoquic_cc_get_sequence_number, picoquic_path_t *)
wrapextern(picoquic_cc_was_cwin_blocked, picoquic_path_t *, uint64_t)
wrapextern(picoquic_is_sending_authorized_by_pacing, picoquic_path_t *, uint64_t, uint64_t *)
wrapexternvoid(picoquic_update_pacing_data, picoquic_path_t *)
wrapexternvoid(picoquic_implicit_handshake_ack, picoquic_cnx_t *, picoquic_path_t *, picoquic_packet_context_enum, uint64_t)

wrapextern(queue_peek, const queue_t *)
wrapextern(picoquic_frame_fair_reserve, picoquic_cnx_t *, picoquic_path_t *, picoquic_stream_head *, uint64_t)
wrapextern(plugin_pluglet_exists, picoquic_cnx_t *, protoop_id_t *, param_id_t, pluglet_type_enum)
wrapextern(inet_ntop, int, const void *, char *, socklen_t)
wrapextern(strerror, int)
wrapextern(memcmp, void *, void *, size_t)
wrapextern(my_malloc_dbg, picoquic_cnx_t *, unsigned int, char *, int)
//wrapextern(my_malloc_ex, picoquic_cnx_t *, unsigned int)
wrapexternvoid(my_free_dbg, picoquic_cnx_t *, void *, char *, int)
wrapextern(my_memcpy_dbg, void *, void *, size_t, char *, int)
wrapextern(my_memset_dbg, void *, int, size_t, char *, int)
wrapextern(my_calloc_dbg, picoquic_cnx_t *, size_t, size_t, char *, int)

wrapextern(dprintf, int, const char *, uint64_t, uint64_t, uint64_t)
wrapextern(snprintf, char *, size_t, const char *, uint64_t, uint64_t)
wrapextern(lseek, int, off_t, int)
wrapextern(ftruncate, int, off_t)
wrapextern(snprintf_bytes, char *, size_t, const uint8_t *, size_t)
wrapextern(strncpy, char *, char *, size_t)
wrapextern(get_preq, plugin_req_pid_t *, access_key_t)
wrapexternvoid(set_preq, plugin_req_pid_t *, access_key_t, protoop_arg_t)
wrapextern(bind, int, const struct sockaddr *, socklen_t)

wrapextern(recv, int, void *, size_t, int)
wrapextern(crc32, uLong, const Bytef *, uInt)
wrapextern(rbt_init, picoquic_cnx_t *, red_black_tree_t *)
wrapextern(rbt_is_empty, picoquic_cnx_t *, red_black_tree_t *)
wrapextern(rbt_size, picoquic_cnx_t *, red_black_tree_t *)
wrapexternvoid(rbt_put, picoquic_cnx_t *, red_black_tree_t *, rbt_key, rbt_val)
wrapextern(rbt_get, picoquic_cnx_t *, red_black_tree_t *, rbt_key, rbt_val *)
wrapextern(rbt_contains, picoquic_cnx_t *, red_black_tree_t *, rbt_key)
wrapextern(rbt_min_val, picoquic_cnx_t *, red_black_tree_t *)
wrapextern(rbt_min_key, picoquic_cnx_t *, red_black_tree_t *)
wrapextern(rbt_min, picoquic_cnx_t *, red_black_tree_t *, rbt_key *, rbt_val *)
wrapextern(rbt_max_key, picoquic_cnx_t *, red_black_tree_t *)

wrapextern(rbt_max_val, picoquic_cnx_t *, red_black_tree_t *)
wrapextern(rbt_ceiling_val, picoquic_cnx_t *, red_black_tree_t *, rbt_key, rbt_val *)
wrapextern(rbt_ceiling_key, picoquic_cnx_t *, red_black_tree_t *, rbt_key, rbt_key *)
wrapextern(rbt_ceiling, picoquic_cnx_t *, red_black_tree_t *, rbt_key, rbt_key *, rbt_val *)
wrapexternvoid(rbt_delete, picoquic_cnx_t *, red_black_tree_t *, rbt_key)
wrapextern(rbt_delete_min, picoquic_cnx_t *, red_black_tree_t *)
wrapextern(rbt_delete_max, picoquic_cnx_t *, red_black_tree_t *)
wrapextern(rbt_delete_and_get_min, picoquic_cnx_t *, red_black_tree_t *, rbt_key *, rbt_val *)
wrapextern(rbt_delete_and_get_max, picoquic_cnx_t *, red_black_tree_t *, rbt_key *, rbt_val *)

wrapexternvoidnoarg(picoquic_gf256_init)
wrapexternvoid(picoquic_gf256_symbol_add_scaled, void *, uint8_t, void *, uint32_t, uint8_t **)
wrapexternvoid(picoquic_gf256_symbol_add, void *, uint8_t *, uint32_t)
wrapextern(picoquic_gf256_symbol_is_zero, void *, uint32_t)
wrapexternvoid(picoquic_gf256_symbol_mul, void *, uint8_t, uint32_t, uint8_t **)
wrapexternvoid(picoquic_memory_bound_error, uint64_t, uint64_t, uint64_t)





static void
register_functions(struct ubpf_vm *vm) {
    /* We only have 64 values ... (so far) */
    unsigned int current_idx = 0;
    /* specific API related */
    ubpf_register(vm, current_idx++, "plugin_run_protoop", wrapped_ext_func(plugin_run_protoop));
    ubpf_register(vm, current_idx++, "reserve_frames", wrapped_ext_func(reserve_frames));
    ubpf_register(vm, current_idx++, "get_cnx", wrapped_ext_func(get_cnx));
    ubpf_register(vm, current_idx++, "set_cnx", wrapped_ext_func(set_cnx));
    ubpf_register(vm, current_idx++, "get_cnx_metadata", wrapped_ext_func(get_cnx_metadata));
    ubpf_register(vm, current_idx++, "set_cnx_metadata", wrapped_ext_func(set_cnx_metadata));
    ubpf_register(vm, current_idx++, "get_path", wrapped_ext_func(get_path));
    ubpf_register(vm, current_idx++, "set_path", wrapped_ext_func(set_path));
    ubpf_register(vm, current_idx++, "get_path_metadata", wrapped_ext_func(get_path_metadata));
    ubpf_register(vm, current_idx++, "set_path_metadata", wrapped_ext_func(set_path_metadata));
    ubpf_register(vm, current_idx++, "get_pkt_ctx", wrapped_ext_func(get_pkt_ctx));
    ubpf_register(vm, current_idx++, "set_pkt_ctx", wrapped_ext_func(set_pkt_ctx));
    ubpf_register(vm, current_idx++, "get_pkt", wrapped_ext_func(get_pkt));
    ubpf_register(vm, current_idx++, "set_pkt", wrapped_ext_func(set_pkt));
    ubpf_register(vm, current_idx++, "get_pkt_n_metadata", wrapped_ext_func(get_pkt_n_metadata));
    ubpf_register(vm, current_idx++, "set_pkt_n_metadata", wrapped_ext_func(set_pkt_n_metadata));
    ubpf_register(vm, current_idx++, "get_sack_item", wrapped_ext_func(get_sack_item));
    ubpf_register(vm, current_idx++, "set_sack_item", wrapped_ext_func(set_sack_item));
    ubpf_register(vm, current_idx++, "get_cnxid", wrapped_ext_func(get_cnxid));
    ubpf_register(vm, current_idx++, "set_cnxid", wrapped_ext_func(set_cnxid));
    ubpf_register(vm, current_idx++, "get_stream_head", wrapped_ext_func(get_stream_head));
    ubpf_register(vm, current_idx++, "set_stream_head", wrapped_ext_func(set_stream_head));
    ubpf_register(vm, current_idx++, "get_stream_data", wrapped_ext_func(get_stream_data));
    ubpf_register(vm, current_idx++, "get_crypto_context", wrapped_ext_func(get_crypto_context));
    ubpf_register(vm, current_idx++, "set_crypto_context", wrapped_ext_func(set_crypto_context));
    ubpf_register(vm, current_idx++, "get_ph", wrapped_ext_func(get_ph));
    ubpf_register(vm, current_idx++, "set_ph", wrapped_ext_func(set_ph));
    ubpf_register(vm, current_idx++, "cancel_head_reservation", wrapped_ext_func(cancel_head_reservation));
    /* specific to picoquic, how to remove this dependency ? */
    ubpf_register(vm, current_idx++, "picoquic_reinsert_cnx_by_wake_time", wrapped_ext_func(picoquic_reinsert_cnx_by_wake_time));
    ubpf_register(vm, current_idx++, "picoquic_current_time", (ext_func_t) picoquic_current_time);
    /* for memory */
    ubpf_register(vm, current_idx++, "my_malloc", wrapped_ext_func(my_malloc));
    ubpf_register(vm, current_idx++, "my_calloc", wrapped_ext_func(my_calloc));
    ubpf_register(vm, current_idx++, "my_free", wrapped_ext_func(my_free));
    ubpf_register(vm, current_idx++, "my_realloc", wrapped_ext_func(my_realloc));
    ubpf_register(vm, current_idx++, "my_memcpy", wrapped_ext_func(my_memcpy));
    ubpf_register(vm, current_idx++, "my_memmove", wrapped_ext_func(my_memmove));
    ubpf_register(vm, current_idx++, "my_memset", wrapped_ext_func(my_memset));

    ubpf_register(vm, current_idx++, "clock_gettime", wrapped_ext_func(clock_gettime));

    /* Network with linux */
    ubpf_register(vm, current_idx++, "getsockopt", wrapped_ext_func(getsockopt));
    ubpf_register(vm, current_idx++, "setsockopt", wrapped_ext_func(setsockopt));
    ubpf_register(vm, current_idx++, "socket", wrapped_ext_func(socket));
    ubpf_register(vm, current_idx++, "connect", wrapped_ext_func(connect));
    ubpf_register(vm, current_idx++, "send", wrapped_ext_func(send));
    ubpf_register(vm, current_idx++, "inet_aton", wrapped_ext_func(inet_aton));
    ubpf_register(vm, current_idx++, "socketpair", wrapped_ext_func(socketpair));
    ubpf_register(vm, current_idx++, "write", wrapped_ext_func(write));
    ubpf_register(vm, current_idx++, "close", wrapped_ext_func(close));
    ubpf_register(vm, current_idx++, "get_errno", wrapped_ext_func(get_errno));

    ubpf_register(vm, current_idx++, "my_htons", wrapped_ext_func(my_htons));
    ubpf_register(vm, current_idx++, "my_ntohs", wrapped_ext_func(my_ntohs));

    ubpf_register(vm, current_idx++, "strncmp", wrapped_ext_func(strncmp));
    ubpf_register(vm, current_idx++, "strlen", wrapped_ext_func(strlen));

    // logging func

    ubpf_register(vm, current_idx++, "picoquic_has_booked_plugin_frames", wrapped_ext_func(picoquic_has_booked_plugin_frames));

    /* Specific QUIC functions */
    ubpf_register(vm, current_idx++, "picoquic_decode_frames_without_current_time", wrapped_ext_func(picoquic_decode_frames_without_current_time));
    ubpf_register(vm, current_idx++, "picoquic_varint_decode", wrapped_ext_func(picoquic_varint_decode));
    ubpf_register(vm, current_idx++, "picoquic_varint_encode", wrapped_ext_func(picoquic_varint_encode));
    ubpf_register(vm, current_idx++, "picoquic_varint_skip", wrapped_ext_func(picoquic_varint_skip));
    ubpf_register(vm, current_idx++, "picoquic_create_random_cnx_id_for_cnx", wrapped_ext_func(picoquic_create_random_cnx_id_for_cnx));
    ubpf_register(vm, current_idx++, "picoquic_create_cnxid_reset_secret_for_cnx", wrapped_ext_func(picoquic_create_cnxid_reset_secret_for_cnx));
    ubpf_register(vm, current_idx++, "picoquic_register_cnx_id_for_cnx", wrapped_ext_func(picoquic_register_cnx_id_for_cnx));
    ubpf_register(vm, current_idx++, "picoquic_create_path", wrapped_ext_func(picoquic_create_path));
    ubpf_register(vm, current_idx++, "picoquic_getaddrs", wrapped_ext_func(picoquic_getaddrs));
    ubpf_register(vm, current_idx++, "picoquic_compare_connection_id", wrapped_ext_func(picoquic_compare_connection_id));

    ubpf_register(vm, current_idx++, "picoquic_compare_addr", wrapped_ext_func(picoquic_compare_addr));
//    ubpf_register(vm, current_idx++, "picoquic_parse_stream_header", wrapped_ext_func(picoquic_parse_stream_header);
    ubpf_register(vm, current_idx++, "picoquic_find_stream", wrapped_ext_func(picoquic_find_stream));
    ubpf_register(vm, current_idx++, "picoquic_reset_stream", wrapped_ext_func(picoquic_reset_stream));
    ubpf_register(vm, current_idx++, "picoquic_add_to_stream", wrapped_ext_func(picoquic_add_to_stream));
    ubpf_register(vm, current_idx++, "find_ready_stream_round_robin", wrapped_ext_func(find_ready_stream_round_robin));
    ubpf_register(vm, current_idx++, "picoquic_set_cnx_state", wrapped_ext_func(picoquic_set_cnx_state));
    ubpf_register(vm, current_idx++, "picoquic_frames_varint_decode", wrapped_ext_func(picoquic_frames_varint_decode));
    ubpf_register(vm, current_idx++, "picoquic_record_pn_received", wrapped_ext_func(picoquic_record_pn_received));
    ubpf_register(vm, current_idx++, "picoquic_cc_get_sequence_number", wrapped_ext_func(picoquic_cc_get_sequence_number));
    ubpf_register(vm, current_idx++, "picoquic_cc_was_cwin_blocked", wrapped_ext_func(picoquic_cc_was_cwin_blocked));
    ubpf_register(vm, current_idx++, "picoquic_is_sending_authorized_by_pacing", wrapped_ext_func(picoquic_is_sending_authorized_by_pacing));
    ubpf_register(vm, current_idx++, "picoquic_update_pacing_data", wrapped_ext_func(picoquic_update_pacing_data));
    ubpf_register(vm, current_idx++, "picoquic_implicit_handshake_ack", wrapped_ext_func(picoquic_implicit_handshake_ack));

    ubpf_register(vm, current_idx++, "queue_peek", wrapped_ext_func(queue_peek));
    /* FIXME remove this function */
    ubpf_register(vm, current_idx++, "picoquic_frame_fair_reserve", wrapped_ext_func(picoquic_frame_fair_reserve));
    ubpf_register(vm, current_idx++, "plugin_pluglet_exists", wrapped_ext_func(plugin_pluglet_exists));

    ubpf_register(vm, current_idx++, "inet_ntop", wrapped_ext_func(inet_ntop));
    ubpf_register(vm, current_idx++, "strerror", wrapped_ext_func(strerror));
    ubpf_register(vm, current_idx++, "memcmp", wrapped_ext_func(memcmp));
    ubpf_register(vm, current_idx++, "my_malloc_dbg", wrapped_ext_func(my_malloc_dbg));
    ubpf_register(vm, current_idx++, "my_malloc_ex", wrapped_ext_func(my_malloc));
    ubpf_register(vm, current_idx++, "my_free_dbg", wrapped_ext_func(my_free_dbg));
    ubpf_register(vm, current_idx++, "my_memcpy_dbg", wrapped_ext_func(my_memcpy_dbg));
    ubpf_register(vm, current_idx++, "my_memset_dbg", wrapped_ext_func(my_memset_dbg));
    ubpf_register(vm, current_idx++, "my_calloc_dbg", wrapped_ext_func(my_calloc_dbg));

    ubpf_register(vm, current_idx++, "dprintf", wrapped_ext_func(dprintf));
    ubpf_register(vm, current_idx++, "snprintf", wrapped_ext_func(snprintf));
    ubpf_register(vm, current_idx++, "lseek", wrapped_ext_func(lseek));
    ubpf_register(vm, current_idx++, "ftruncate", wrapped_ext_func(ftruncate));
    ubpf_register(vm, current_idx++, "strlen", wrapped_ext_func(strlen));
    ubpf_register(vm, current_idx++, "snprintf_bytes", wrapped_ext_func(snprintf_bytes));
    ubpf_register(vm, current_idx++, "strncpy", wrapped_ext_func(strncpy));
    ubpf_register(vm, current_idx++, "inet_ntop", wrapped_ext_func(inet_ntop));
    ubpf_register(vm, current_idx++, "get_preq", wrapped_ext_func(get_preq));
    ubpf_register(vm, current_idx++, "set_preq", wrapped_ext_func(set_preq));

    ubpf_register(vm, current_idx++, "bind", wrapped_ext_func(bind));
    ubpf_register(vm, current_idx++, "recv", wrapped_ext_func(recv));

    ubpf_register(vm, current_idx++, "strcmp", wrapped_ext_func(strncmp));
    ubpf_register(vm, current_idx++, "crc32", wrapped_ext_func(crc32));

    /* red black tree */
    ubpf_register(vm, current_idx++, "rbt_init", wrapped_ext_func(rbt_init));
    ubpf_register(vm, current_idx++, "rbt_is_empty", wrapped_ext_func(rbt_is_empty));
    ubpf_register(vm, current_idx++, "rbt_size", wrapped_ext_func(rbt_size));
    ubpf_register(vm, current_idx++, "rbt_put", wrapped_ext_func(rbt_put));
    ubpf_register(vm, current_idx++, "rbt_get", wrapped_ext_func(rbt_get));
    ubpf_register(vm, current_idx++, "rbt_contains", wrapped_ext_func(rbt_contains));
    ubpf_register(vm, current_idx++, "rbt_min_val", wrapped_ext_func(rbt_min_val));
    ubpf_register(vm, current_idx++, "rbt_min_key", wrapped_ext_func(rbt_min_key));
    ubpf_register(vm, current_idx++, "rbt_min", wrapped_ext_func(rbt_min));
    ubpf_register(vm, current_idx++, "rbt_max_key", wrapped_ext_func(rbt_max_key));
    ubpf_register(vm, current_idx++, "rbt_max_val", wrapped_ext_func(rbt_max_val));
    ubpf_register(vm, current_idx++, "rbt_ceiling_val", wrapped_ext_func(rbt_ceiling_val));
    ubpf_register(vm, current_idx++, "rbt_ceiling_key", wrapped_ext_func(rbt_ceiling_key));
    ubpf_register(vm, current_idx++, "rbt_ceiling", wrapped_ext_func(rbt_ceiling));
    ubpf_register(vm, current_idx++, "rbt_delete", wrapped_ext_func(rbt_delete));
    ubpf_register(vm, current_idx++, "rbt_delete_min", wrapped_ext_func(rbt_delete_min));
    ubpf_register(vm, current_idx++, "rbt_delete_max", wrapped_ext_func(rbt_delete_max));
    ubpf_register(vm, current_idx++, "rbt_delete_and_get_min", wrapped_ext_func(rbt_delete_and_get_min));
    ubpf_register(vm, current_idx++, "rbt_delete_and_get_max", wrapped_ext_func(rbt_delete_and_get_max));

    /* GF256 lirary */
    ubpf_register(vm, current_idx++, "picoquic_gf256_init", wrapped_ext_func(picoquic_gf256_init));
    ubpf_register(vm, current_idx++, "picoquic_gf256_symbol_add_scaled", wrapped_ext_func(picoquic_gf256_symbol_add_scaled));
    ubpf_register(vm, current_idx++, "picoquic_gf256_symbol_add", wrapped_ext_func(picoquic_gf256_symbol_add));
    ubpf_register(vm, current_idx++, "picoquic_gf256_symbol_is_zero", wrapped_ext_func(picoquic_gf256_symbol_is_zero));
    ubpf_register(vm, current_idx++, "picoquic_gf256_symbol_mul", wrapped_ext_func(picoquic_gf256_symbol_mul));

    /* This value is reserved. DO NOT OVERRIDE IT! */
    ubpf_register(vm, 0x7f, "picoquic_memory_bound_error", wrapped_ext_func(picoquic_memory_bound_error));
}

static void *readfile(const char *path, size_t maxlen, size_t *len)
{
	FILE *file;
    if (!strcmp(path, "-")) {
        file = fdopen(STDIN_FILENO, "r");
    } else {
        file = fopen(path, "r");
    }

    if (file == NULL) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        return NULL;
    }

    char *data = calloc(maxlen, 1);
    size_t offset = 0;
    size_t rv;
    while ((rv = fread(data+offset, 1, maxlen-offset, file)) > 0) {
        offset += rv;
    }

    if (ferror(file)) {
        fprintf(stderr, "Failed to read %s: %s\n", path, strerror(errno));
        fclose(file);
        free(data);
        return NULL;
    }

    if (!feof(file)) {
        fprintf(stderr, "Failed to read %s because it is too large (max %u bytes)\n",
                path, (unsigned)maxlen);
        fclose(file);
        free(data);
        return NULL;
    }

    fclose(file);
    if (len) {
        *len = offset;
    }
    return data;
}

pluglet_t *load_elf(void *code, size_t code_len, uint64_t memory_ptr, uint32_t memory_size) {
    pluglet_t *pluglet = (pluglet_t *)calloc(1, sizeof(pluglet_t));
    if (!pluglet) {
        return NULL;
    }

    pluglet->vm = ubpf_create();
    if (!pluglet->vm) {
            fprintf(stderr, "Failed to create VM\n");
            free(pluglet);
            return NULL;
    }

    register_functions(pluglet->vm);

    bool elf = code_len >= SELFMAG && !memcmp(code, ELFMAG, SELFMAG);

    char *errmsg;
    int rv;
    if (elf) {
        rv = ubpf_load_elf(pluglet->vm, code, code_len, &errmsg, memory_ptr, memory_size);
    } else {
        rv = ubpf_load(pluglet->vm, code, code_len, &errmsg, memory_ptr, memory_size);
    }

    if (rv < 0) {
        fprintf(stderr, "Failed to load code: %s\n", errmsg);
        free(errmsg);
        ubpf_destroy(pluglet->vm);
        free(pluglet);
        return NULL;
    }

    if (JIT) {
        pluglet->fn = ubpf_compile(pluglet->vm, &errmsg);
        if (pluglet->fn == NULL) {
            fprintf(stderr, "Failed to compile: %s\n", errmsg);
            free(errmsg);
            ubpf_destroy(pluglet->vm);
            free(pluglet);
            return NULL;
        }
    } else {
        pluglet->fn = NULL;
    }

    free(errmsg);

    return pluglet;
}

pluglet_t *load_elf_file(const char *code_filename, uint64_t memory_ptr, uint32_t memory_size) {
	size_t code_len;
	void *code = readfile(code_filename, 1024*1024, &code_len);
	if (code == NULL) {
			return NULL;
	}

	pluglet_t *ret = load_elf(code, code_len, memory_ptr, memory_size);
	free(code);
	return ret;
}

int release_elf(pluglet_t *pluglet) {
    if (pluglet->vm != NULL) {
        ubpf_destroy(pluglet->vm);
        pluglet->vm = NULL;
        pluglet->fn = 0;
        free(pluglet);
    }
    return 0;
}

uint64_t exec_loaded_code(pluglet_t *pluglet, void *arg, void *mem, size_t mem_len, char **error_msg) {
    if (pluglet->vm == NULL) {
        return -1;
    }
    if (JIT && pluglet->fn == NULL) {
        return -1;
    }

    /* printf("0x%"PRIx64"\n", ret); */
    pluglet->count++;
#ifndef DEBUG_PLUGIN_EXECUTION_TIME
    return _exec_loaded_code(pluglet, arg, mem, mem_len, error_msg, JIT);
#else
    uint64_t before = picoquic_current_time();
    uint64_t err = _exec_loaded_code(pluglet, arg, mem, mem_len, error_msg, JIT);
    uint64_t time = picoquic_current_time() - before;
    pluglet->total_execution_time += time;
    pluglet->max_execution_time = MAX(pluglet->max_execution_time, time);
    return err;
#endif
}
