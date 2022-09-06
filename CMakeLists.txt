CMAKE_MINIMUM_REQUIRED(VERSION 2.8.11)
CMAKE_POLICY(SET CMP0003 NEW)

PROJECT(picoquic C)
SET(CMAKE_C_STANDARD 11)

# FIND_PACKAGE(PkgConfig REQUIRED)

if((NOT DEFINED ENV{NS3}) OR !$ENV{NS3})
    SET(CMAKE_C_FLAGS "-std=gnu99 -Wall -O2 -g ${CC_WARNING_FLAGS} ${CMAKE_C_FLAGS}")
    SET(GCC_COVERAGE_LINK_FLAGS    "-Wl,--no-as-needed,-lprofiler,--as-needed")
    SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_COVERAGE_LINK_FLAGS}")
    find_library(AVUTIL_LIBRARY avutil)
endif()

if(DEFINED OSX_SYSROOT)
    MESSAGE("Before, CMAKE_OSX_SYSROOT set at: ${CMAKE_OSX_SYSROOT}")
    SET(CMAKE_OSX_SYSROOT ${OSX_SYSROOT})
    MESSAGE("CMAKE_OSX_SYSROOT now set at: ${CMAKE_OSX_SYSROOT}")
endif()

if($ENV{DISABLE_DEBUG_PRINTF})
    SET(CMAKE_C_FLAGS "-DDISABLE_DEBUG_PRINTF ${CMAKE_C_FLAGS}")
endif()

if($ENV{DISABLE_QLOG})
    SET(CMAKE_C_FLAGS "-DDISABLE_QLOG ${CMAKE_C_FLAGS}")
endif()

if($ENV{STATIC_RESPONSE})
    SET(CMAKE_C_FLAGS "-DSTATIC_RESPONSE=$ENV{STATIC_RESPONSE} ${CMAKE_C_FLAGS}")
endif()

if($ENV{DEBUG_PLUGIN_EXECUTION_TIME})
    SET(CMAKE_C_FLAGS "-DDEBUG_PLUGIN_EXECUTION_TIME ${CMAKE_C_FLAGS}")
endif()

if($ENV{NS3})
    SET(GCC_COVERAGE_LINK_FLAGS "")
    SET(CMAKE_C_FLAGS "-std=gnu99 -Wall -O2 -g -fPIC -DNS3 ${CC_WARNING_FLAGS} ${CMAKE_C_FLAGS}")
    SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -pie -rdynamic")
endif()

INCLUDE_DIRECTORIES(picoquic picoquic/gf256/flec-moepgf/include/ picoquictest ../picotls/include
    ${PICOTLS_INCLUDE_DIR})

SET(MOEPGF_STATIC_LIB
        ${CMAKE_SOURCE_DIR}/picoquic/gf256/libmoepgf.a)

SET(MICHELFRALLOC_STATIC_LIBS
        ${CMAKE_SOURCE_DIR}/picoquic/michelfralloc/libmichelfralloc.a
        ${CMAKE_SOURCE_DIR}/picoquic/michelfralloc/libptmalloc3.a)

SET(PICOQUIC_LIBRARY_FILES
    picoquic/cubic.c
    picoquic/endianness.c
    picoquic/fnv1a.c
    picoquic/frames.c
    picoquic/getset.c
    picoquic/http0dot9.c
    picoquic/intformat.c
    picoquic/logger.c
    picoquic/memory.c
    picoquic/memcpy.c
    picoquic/newreno.c
    picoquic/packet.c
    picoquic/picohash.c
    picoquic/picosocks.c
    picoquic/picosplay.c
    picoquic/plugin.c
    picoquic/protoop.c
    picoquic/queue.c
    picoquic/quicctx.c
    picoquic/sacks.c
    picoquic/sender.c
    picoquic/ticket_store.c
    picoquic/tls_api.c
    picoquic/transport.c
    picoquic/ubpf.c
    picoquic/util.c
    picoquic/red_black_tree.c
        picoquic/michelfralloc/sbrk.c
        picoquic/michelfralloc/sbrk.h
        picoquic/michelfralloc/michelfralloc.c
        picoquic/michelfralloc/michelfralloc.h
    picoquic/cc_common.c picoquic/cc_common.h picoquic/bbr.c
    picoquic/gf256/gf256.c)

SET(PICOQUIC_TEST_LIBRARY_FILES
    picoquictest/ack_of_ack_test.c
    picoquictest/cleartext_aead_test.c
    picoquictest/cnx_creation_test.c
    picoquictest/float16test.c
    picoquictest/fnv1atest.c
    picoquictest/hashtest.c
    picoquictest/http0dot9test.c
    picoquictest/intformattest.c
    picoquictest/parseheadertest.c
    picoquictest/pn2pn64test.c
    picoquictest/sacktest.c
    picoquictest/skip_frame_test.c
    picoquictest/sim_link.c
    picoquictest/socket_test.c
    picoquictest/splay_test.c
    picoquictest/stream0_frame_test.c
    picoquictest/stresstest.c
    picoquictest/ticket_store_test.c
    picoquictest/tls_api_test.c
    picoquictest/transport_param_test.c
    picoquictest/datagram.c
    picoquictest/microbench.c
        picoquictest/util.c
        )

SET(PLUGINS_DATAGRAM
    plugins/datagram/parse_datagram_frame.c
    plugins/datagram/send_datagram.c
    plugins/datagram/process_datagram_frame.c
    plugins/datagram/write_datagram_frame.c
    plugins/datagram/get_datagram_socket.c
    plugins/datagram/cnx_state_changed.c
    plugins/datagram/process_datagram_buffer.c
)

SET(PLUGINS_MONITORING
    plugins/monitoring/cnx_state_changed.c
    plugins/monitoring/packet_received.c
    plugins/monitoring/packet_sent.c
    plugins/monitoring/rtt_updated.c
    plugins/monitoring/check_ooo_stream_frame.c
    plugins/monitoring/check_spurious_stream_frame.c
    plugins/monitoring/packet_lost.c
    plugins/monitoring/fast_retransmit.c
    plugins/monitoring/retransmission_timeout.c
    plugins/monitoring/tail_loss_probe.c
    plugins/monitoring/stream_opened.c
    plugins/monitoring/stream_closed.c
)

SET(PLUGINS_MULTIPATH
    plugins/multipath/connection_state_changed.c
    plugins/multipath/get_destination_connection_id.c
    plugins/multipath/get_incoming_path.c
    plugins/multipath/parse_add_address_frame.c
    plugins/multipath/parse_mp_ack_frame.c
    plugins/multipath/parse_mp_new_connection_id_frame.c
    plugins/multipath/predict_packet_header_length.c
    plugins/multipath/process_mp_ack_frame.c
    plugins/multipath/process_mp_new_connection_id_frame.c
    plugins/multipath/process_possible_ack_of_ack_frame.c
    plugins/multipath/set_next_wake_time.c
    plugins/multipath/write_add_address_frame.c
    plugins/multipath/write_mp_ack_frame.c
    plugins/multipath/write_mp_new_connection_id_frame.c
    plugins/multipath/write_rtt_probe.c
    plugins/multipath/path_schedulers/schedule_path_rr.c
    plugins/multipath/path_schedulers/schedule_path_rtt.c
    plugins/multipath/path_schedulers/schedule_frames.c
    plugins/multipath/qlog/mp_ack_frame_parsed.c
    plugins/multipath/qlog/mp_new_connection_id_frame_parsed.c
    plugins/multipath/qlog/add_address_parsed.c
    plugins/multipath/path_manager.c
    plugins/multipath/addresses_filters/no_v6_no_10.c
    plugins/multipath/update_rtt.c
)

SET(PLUGINS_STREAM_SCHEDULER
    plugins/stream_scheduling_rr/stream_scheduling_rr.c
)

SET(PLUGINS_WESTWOOD
    plugins/westwood/westwood.h
    plugins/westwood/westwood_notify.c
)

SET(PLUGINS_NO_PACING
    plugins/no_pacing/set_next_wake_time_without_pacing_with_event.c
    plugins/no_pacing/no_pacing_with_event.h
)

SET(PLUGINS_QLOG
    plugins/qlog/cnx_state_changed.c
    plugins/qlog/set_output_file.c
    plugins/qlog/log_event.c
    plugins/qlog/frames/stream_opened.c
    plugins/qlog/frames/stream_flags_changed.c
    plugins/qlog/frames/reset_stream_frame_parsed.c
    plugins/qlog/frames/new_connection_id_parsed.c
    plugins/qlog/frames/new_token_frame_parsed.c
    plugins/qlog/frames/stop_sending_parsed.c
    plugins/qlog/frames/crypto_frame_parsed.c
    plugins/qlog/frames/ack_frame_parsed.c
    plugins/qlog/frames/is_ack_needed.c
    plugins/qlog/frames/connection_close_parsed.c
    plugins/qlog/frames/application_close_parsed.c
    plugins/qlog/frames/max_data_parsed.c
    plugins/qlog/frames/max_stream_data_parsed.c
    plugins/qlog/frames/path_challenge_created.c
    plugins/qlog/frames/path_response_parsed.c
    plugins/qlog/frames/blocked_frame_parsed.c
    plugins/qlog/frames/stream_blocked_frame_parsed.c
    plugins/qlog/frames/stream_id_blocked_frame_parsed.c
    plugins/qlog/frames/padding_or_ping_parsed.c
    plugins/qlog/frames/frame_prepared.c
    plugins/qlog/sender/retransmit_needed_by_packet.c
    plugins/qlog/sender/retransmit_needed.c
    plugins/qlog/sender/congestion_algorithm_notified.c
    plugins/qlog/sender/ready_stream.c
    plugins/qlog/sender/next_stream_scheduled.c
    plugins/qlog/sender/next_wake_time.c
    plugins/qlog/sender/header_prepared.c
        plugins/qlog/sender/segment_prepared.c
    plugins/qlog/push_log_context.c
    plugins/qlog/pop_log_context.c
    plugins/qlog/log_frame.c
    plugins/qlog/receiver/header_parsed.c
        plugins/qlog/receiver/segment_received.c
        plugins/qlog/frames/stream_frame_parsed.c
        plugins/qlog/frames/handshake_done_parsed.c
)

#
SET(PLUGINS_FEC
        )


SET(PLUGINS_LOSS_MONITOR
        plugins/loss_monitor/loss_monitor.h
        plugins/loss_monitor/packet_acknowledged.c
        plugins/loss_monitor/packet_lost.c
        plugins/loss_monitor/get_loss_parameters.c
        )
SET(PLUGINS_SIMPLE_FEC
        plugins/simple_fec/utils.h
        plugins/simple_fec/fec.h
        plugins/simple_fec/fec_constants.h
        plugins/simple_fec/protoops/post_write_src_fpi_frame.c
        plugins/simple_fec/protoops/post_write_repair_frame.c
        plugins/simple_fec/protoops/before_schedule_frames.c
        plugins/simple_fec/protoops/after_schedule_frames.c
        plugins/simple_fec/protoops/available_slot.c
        plugins/simple_fec/protoops/pre_incoming_encrypted.c
        plugins/simple_fec/protoops/after_incoming_encrypted.c
        plugins/simple_fec/protoops/check_for_available_slot.c
        plugins/simple_fec/protoops/disable_retransmissions.c
        plugins/simple_fec/protoops/process_ack_range.c
        plugins/simple_fec/protoops/maybe_notify_recovered_packets_to_everybody.c
        plugins/simple_fec/protoops/has_protected_data_to_send.c
        plugins/simple_fec/protoops/stream_bytes_max_no_stream_frame_with_repair_frame.c

        plugins/simple_fec/protoops/pre_skip_frame.c
        plugins/simple_fec/protoops/post_skip_frame.c

        plugins/simple_fec/protoops/pre_packet_has_been_received.c
        plugins/simple_fec/protoops/pre_packet_has_been_lost.c
        plugins/simple_fec/protoops/sent_packet.c


        plugins/simple_fec/window_framework/framework_sender.h
        plugins/simple_fec/window_framework/framework_receiver.h
        plugins/simple_fec/window_framework/window_receive_buffers_old.h
        plugins/simple_fec/window_framework/window_receive_buffers.h
        plugins/simple_fec/window_framework/types.h
        plugins/simple_fec/window_framework/search_structures.h
        plugins/simple_fec/window_framework/protoops/compressed_repair_frame.h

        plugins/simple_fec/window_framework/protoops/window_get_next_source_symbol_id.c

        plugins/simple_fec/window_framework/protoops/window_create_framework.c
        plugins/simple_fec/window_framework/protoops/window_write_src_fpi_frame.c
        plugins/simple_fec/window_framework/protoops/window_parse_src_fpi_frame.c
        plugins/simple_fec/window_framework/protoops/window_process_src_fpi_frame.c
        plugins/simple_fec/window_framework/protoops/window_notify_src_fpi_frame.c
        plugins/simple_fec/window_framework/protoops/window_write_repair_frame.c
        plugins/simple_fec/window_framework/protoops/window_parse_repair_frame.c
        plugins/simple_fec/window_framework/protoops/window_predict_repair_frame_length.c
        plugins/simple_fec/window_framework/protoops/window_predict_compressed_repair_frame_length.c
        plugins/simple_fec/window_framework/protoops/window_parse_compressed_repair_frame.c
        plugins/simple_fec/window_framework/protoops/window_process_repair_frame.c
        plugins/simple_fec/window_framework/protoops/window_process_window_rwin_frame.c
        plugins/simple_fec/window_framework/protoops/window_write_window_rwin_frame.c
        plugins/simple_fec/window_framework/protoops/window_parse_window_rwin_frame.c
        plugins/simple_fec/window_framework/protoops/window_notify_window_rwin_frame.c
        plugins/simple_fec/window_framework/protoops/window_notify_repair_frame.c
        plugins/simple_fec/window_framework/protoops/window_write_recovered_frame.c
        plugins/simple_fec/window_framework/protoops/window_write_compressed_repair_frame.c
        plugins/simple_fec/window_framework/protoops/window_parse_recovered_frame.c
        plugins/simple_fec/window_framework/protoops/window_process_recovered_frame.c
        plugins/simple_fec/window_framework/protoops/window_notify_recovered_frame.c

        plugins/simple_fec/window_framework/protoops/window_receive_packet_payload.c
        plugins/simple_fec/window_framework/protoops/window_protect_packet_payload.c
        plugins/simple_fec/window_framework/protoops/window_reserve_repair_frames.c
        plugins/simple_fec/window_framework/protoops/get_current_window_bounds.c
        plugins/simple_fec/window_framework/protoops/window_packet_has_been_lost.c
        plugins/simple_fec/window_framework/protoops/window_packet_has_been_received.c
        plugins/simple_fec/window_framework/protoops/window_packet_sent.c
        plugins/simple_fec/window_framework/protoops/window_select_symbols_to_protect.c
        plugins/simple_fec/window_framework/protoops/window_try_to_recover_after_incoming_packet.c
        plugins/simple_fec/window_framework/protoops/connection_state_changed.c

        plugins/simple_fec/window_framework/protoops/window_stream_frame_written.c
        plugins/simple_fec/window_framework/protoops/is_max_stream_data_frame_required.c

        plugins/simple_fec/window_framework/protoops/reserve_window_rwin_frame_if_needed.c


        plugins/simple_fec/window_framework/fec_schemes/rlc_gf256/gf256/generated_table_code.c
        plugins/simple_fec/window_framework/fec_schemes/rlc_gf256/gf256/gf256.h
        plugins/simple_fec/window_framework/fec_schemes/rlc_gf256/gf256/swif_symbol.c
        plugins/simple_fec/window_framework/fec_schemes/rlc_gf256/gf256/swif_symbol.h
        plugins/simple_fec/window_framework/fec_schemes/rlc_gf256/gf256/prng/tinymt32.c

        plugins/simple_fec/window_framework/fec_schemes/rlc_gf256/protoops/create_rlc_fec_scheme_gf256.c
        plugins/simple_fec/window_framework/fec_schemes/rlc_gf256/protoops/rlc_fec_scheme_generate_gf256.c
        plugins/simple_fec/window_framework/fec_schemes/rlc_gf256/protoops/rlc_fec_scheme_gf256.h
        plugins/simple_fec/window_framework/fec_schemes/rlc_gf256/protoops/rlc_fec_scheme_gf256_causal.c

        plugins/simple_fec/causal_redundancy_controller_protoops/causal_redundancy_controller.h
        plugins/simple_fec/causal_redundancy_controller_protoops/causal_what_to_send.c
        plugins/simple_fec/causal_redundancy_controller_protoops/causal_sent_packet.c
        plugins/simple_fec/causal_redundancy_controller_protoops/causal_cancelled_packet.c

        plugins/simple_fec/bulk_redundancy_controller_protoops/bulk_causal_ew.c
        plugins/simple_fec/bulk_redundancy_controller_protoops/bulk_causal_threshold.c

        plugins/simple_fec/buffer_limited_redundancy_controller_protoops/buffer_limited_causal.h
        plugins/simple_fec/buffer_limited_redundancy_controller_protoops/buffer_limited_causal_ew.c
        plugins/simple_fec/buffer_limited_redundancy_controller_protoops/buffer_limited_causal_threshold.c

        plugins/simple_fec/message_based_redundancy_controller_protoops/message_causal_ew.c
        plugins/simple_fec/message_based_redundancy_controller_protoops/message_causal_threshold.c
        plugins/simple_fec/message_based_redundancy_controller_protoops/message_causal_update_state.c


        plugins/simple_fec/tetrys_framework/tetrys_framework_sender.c
        plugins/simple_fec/tetrys_framework/tetrys_framework_receiver.c
        plugins/simple_fec/tetrys_framework/tetrys_framework.h


        plugins/simple_fec/window_framework/extern/protoops/next_message_microsec.c
        plugins/simple_fec/window_framework/extern/protoops/window_framework_send_fec_protected_message.c
        plugins/simple_fec/window_framework/extern/protoops/window_framework_send_fec_protected_unreliable_message.c
        plugins/simple_fec/window_framework/extern/protoops/window_framework_cancel_expired_fec_protected_unreliable_messages.c
        plugins/simple_fec/window_framework/extern/protoops/set_rwin_size.c
        plugins/simple_fec/window_framework/extern/protoops/set_repair_rwin_size.c

        plugins/simple_fec/window_framework/stream_scheduler_protoops/unreliable_messages_stream_scheduler.c
        plugins/simple_fec/window_framework/stream_scheduler_protoops/fec_flow_control_aware_stream_scheduler.c


        plugins/simple_fec/tetrys_framework/protoops/tetrys_create_framework.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_receive_packet_payload.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_protect_packet_payload.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_flush_repair_symbols.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_what_to_send.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_pre_notify_sfpid_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_reserve_recovered_frames.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_reserve_repair_frames.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_write_ack_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_parse_ack_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_process_ack_frame.c
        plugins/simple_fec/tetrys_framework/wire.h
        plugins/simple_fec/tetrys_framework/search_structures.h

        plugins/simple_fec/tetrys_framework/protoops/tetrys_write_src_fpi_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_write_recovered_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_write_repair_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_parse_src_fpi_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_parse_recovered_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_parse_repair_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_process_src_fpi_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_process_recovered_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_process_repair_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_notify_src_fpi_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_notify_recovered_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_notify_repair_frame.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_packet_has_been_lost.c
        plugins/simple_fec/tetrys_framework/protoops/tetrys_packet_has_been_received.c

        )



FIND_LIBRARY(ZLIB z)
FIND_LIBRARY(PTLS_CORE picotls-core PATH ../picotls)
MESSAGE(STATUS "Found picotls-core at : ${PTLS_CORE} " )

FIND_LIBRARY(PTLS_MINICRYPTO picotls-minicrypto PATH ../picotls)
MESSAGE(STATUS "Found picotls-crypto at : ${PTLS_MINICRYPTO} " )

FIND_LIBRARY(PTLS_OPENSSL picotls-openssl PATH ../picotls)
MESSAGE(STATUS "Found picotls-openssl at : ${PTLS_OPENSSL} " )

FIND_LIBRARY(UBPF ubpf PATH ubpf/vm)
MESSAGE(STATUS "Found ubpf at : ${UBPF} " )

FIND_PACKAGE(OpenSSL )
MESSAGE("root: ${OPENSSL_ROOT_DIR}")
MESSAGE("OpenSSL_VERSION: ${OPENSSL_VERSION}")
MESSAGE("OpenSSL_INCLUDE_DIR: ${OPENSSL_INCLUDE_DIR}")
MESSAGE("OpenSSL_LIBRARIES: ${OPENSSL_LIBRARIES}")
INCLUDE_DIRECTORIES(${OPENSSL_INCLUDE_DIR})

FIND_PACKAGE(LibArchive)
MESSAGE("root: ${LibArchive_FOUND}")
MESSAGE("libarchive_version: ${LibArchive_VERSION}")
MESSAGE("libarchive include dirs: ${LibArchive_INCLUDE_DIRS}")
MESSAGE("libarchive_LIBRARIES: ${LibArchive_LIBRARIES}")
INCLUDE_DIRECTORIES(${LibArchive_INCLUDE_DIRS})

ADD_LIBRARY(picoquic-core
    ${PICOQUIC_LIBRARY_FILES}
)

# They add lot of noise at compile time without actually compiling them...
if($ENV{COMPILE_CLION})
    ADD_LIBRARY(plugins-monitoring
        ${PLUGINS_MONITORING}
    )

    ADD_LIBRARY(plugins-multipath
        ${PLUGINS_MULTIPATH}
    )
    ADD_LIBRARY(plugins-datagram
        ${PLUGINS_DATAGRAM}
    )
    ADD_LIBRARY(plugins-stream-scheduler
        ${PLUGINS_STREAM_SCHEDULER}
    )
    ADD_LIBRARY(plugins-qlog
        ${PLUGINS_QLOG}
    )
    ADD_LIBRARY(plugins-simple-fec
        ${PLUGINS_SIMPLE_FEC}
    )
    ADD_LIBRARY(plugins-loss-monitor
        ${PLUGINS_LOSS_MONITOR}
    )
    ADD_LIBRARY(plugins-fec
            ${PLUGINS_SIMPLE_FEC}
            )
    ADD_LIBRARY(plugins-loss-monitor
            ${PLUGINS_LOSS_MONITOR}
            )
    ADD_LIBRARY(plugins-westwood
            ${PLUGINS_WESTWOOD}
            )
    ADD_LIBRARY(plugins-no-pacing
            ${PLUGINS_NO_PACING}
            )
endif()

if(NOT DEFINED ONLY_LIB)
    ADD_EXECUTABLE(picoquicdemo picoquicfirst/picoquicdemo.c
                                picoquicfirst/getopt.c )
    TARGET_LINK_LIBRARIES(picoquicdemo picoquic-core
            ${PTLS_CORE}
            ${PTLS_OPENSSL}
            ${PTLS_MINICRYPTO}
            ${OPENSSL_LIBRARIES}
            ${ZLIB}
            ${UBPF}
            ${CMAKE_DL_LIBS}
            ${LibArchive_LIBRARIES}
            ${MICHELFRALLOC_STATIC_LIBS}
	    ${MOEPGF_STATIC_LIB})

    ADD_EXECUTABLE(picoquicvpn picoquicfirst/picoquicvpn.c
            picoquicfirst/getopt.c )
    TARGET_LINK_LIBRARIES(picoquicvpn picoquic-core
            ${PTLS_CORE}
            ${PTLS_OPENSSL}
            ${PTLS_MINICRYPTO}
            ${OPENSSL_LIBRARIES}
            ${UBPF}
            ${CMAKE_DL_LIBS}
            ${LibArchive_LIBRARIES}
            ${MICHELFRALLOC_STATIC_LIBS}
            ${ZLIB}
	    ${MOEPGF_STATIC_LIB}
            )

    ADD_EXECUTABLE(picoquicdemobench picoquicfirst/picoquicdemobench.c
            picoquicfirst/getopt.c )
    TARGET_LINK_LIBRARIES(picoquicdemobench picoquic-core
            ${PTLS_CORE}
            ${PTLS_OPENSSL}
            ${PTLS_MINICRYPTO}
            ${OPENSSL_LIBRARIES}
            ${UBPF}
            ${CMAKE_DL_LIBS}
            ${LibArchive_LIBRARIES}
            ${MICHELFRALLOC_STATIC_LIBS}
            ${ZLIB}
	    ${MOEPGF_STATIC_LIB}
            )

    ADD_EXECUTABLE(picoquic_ct picoquic_t/picoquic_t.c
            ${PICOQUIC_TEST_LIBRARY_FILES} )
    TARGET_LINK_LIBRARIES(picoquic_ct picoquic-core
            ${PTLS_CORE}
            ${PTLS_OPENSSL}
            ${PTLS_MINICRYPTO}
            ${PTLS_OPENSSL}
            ${PTLS_CORE}
            ${OPENSSL_LIBRARIES}
            ${UBPF}
            ${CMAKE_DL_LIBS}
            ${LibArchive_LIBRARIES}
            ${MICHELFRALLOC_STATIC_LIBS}
            ${ZLIB}
	    ${MOEPGF_STATIC_LIB}
            )


    ADD_EXECUTABLE(get_video_frames picoquicfirst/get_video_frames.c
            picoquicfirst/streamer.h
            picoquicfirst/video_streamer.h
            picoquicfirst/streamers_buffer.h
                                picoquicfirst/getopt.c )
    TARGET_LINK_LIBRARIES(get_video_frames avcodec avutil avformat picoquic-core
            ${PTLS_CORE}
            ${PTLS_OPENSSL}
            ${PTLS_MINICRYPTO}
            ${OPENSSL_LIBRARIES}
            ${ZLIB}
            ${UBPF}
            ${CMAKE_DL_LIBS}
            ${LibArchive_LIBRARIES}
            ${MICHELFRALLOC_STATIC_LIBS}
	    ${MOEPGF_STATIC_LIB})
    INCLUDE_DIRECTORIES(/usr/include/ffmpeg)

    ADD_EXECUTABLE(picoquicburst_messages picoquicfirst/picoquic_bursty_messages.c
            picoquicfirst/streamer.h
            picoquicfirst/streamers_buffer.h
                                picoquicfirst/getopt.c )
    TARGET_LINK_LIBRARIES(picoquicburst_messages picoquic-core
            ${PTLS_CORE}
            ${PTLS_OPENSSL}
            ${PTLS_MINICRYPTO}
            ${OPENSSL_LIBRARIES}
            ${ZLIB}
            ${UBPF}
            ${CMAKE_DL_LIBS}
            ${LibArchive_LIBRARIES}
            ${MICHELFRALLOC_STATIC_LIBS}
	    ${MOEPGF_STATIC_LIB}
            )

    if($ENV{PQUIC_VIDEO})



        ADD_EXECUTABLE(picoquicvideo picoquicfirst/picoquic_video.c
                picoquicfirst/streamer.h
                picoquicfirst/video_streamer.h
                picoquicfirst/streamers_buffer.h
                picoquicfirst/getopt.c )

        if(((NOT DEFINED ENV{NS3}) OR !$ENV{NS3}) AND ((NOT DEFINED ENV{ANDROID}) OR !$ENV{ANDROID}))
            find_library(AVUTIL_LIBRARY avutil)
            find_library(AVCODEC_LIBRARY avcodec)
            find_library(AVFORMAT_LIBRARY avformat)

            SET(TEST_EXES picoquic_ct)
            ADD_EXECUTABLE(get_video_frames picoquicfirst/get_video_frames.c
                    picoquicfirst/streamer.h
                    picoquicfirst/video_streamer.h
                    picoquicfirst/streamers_buffer.h
                    picoquicfirst/getopt.c )

            TARGET_LINK_LIBRARIES(get_video_frames avcodec avutil avformat picoquic-core
                    ${PTLS_CORE}
                    ${PTLS_OPENSSL}
                    ${PTLS_MINICRYPTO}
                    ${OPENSSL_LIBRARIES}
                    ${ZLIB}
                    ${UBPF}
                    ${CMAKE_DL_LIBS}
                    ${LibArchive_LIBRARIES}
                    ${MICHELFRALLOC_STATIC_LIBS}
                    ${MOEPGF_STATIC_LIB})
            INCLUDE_DIRECTORIES(/usr/include/ffmpeg)

            TARGET_LINK_LIBRARIES(picoquicvideo avcodec avutil avformat picoquic-core
                    ${PTLS_CORE}
                    ${PTLS_OPENSSL}
                    ${PTLS_MINICRYPTO}
                    ${OPENSSL_LIBRARIES}
                    ${ZLIB}
                    ${UBPF}
                    ${CMAKE_DL_LIBS}
                    ${LibArchive_LIBRARIES}
                    ${MICHELFRALLOC_STATIC_LIBS}
                    ${MOEPGF_STATIC_LIB})
        else()
            TARGET_LINK_LIBRARIES(picoquicvideo picoquic-core
                    ${PTLS_CORE}
                    ${PTLS_OPENSSL}
                    ${PTLS_MINICRYPTO}
                    ${OPENSSL_LIBRARIES}
                    ${UBPF}
                    ${CMAKE_DL_LIBS}
                    ${LibArchive_LIBRARIES}
                    ${ZLIB}
                    ${MICHELFRALLOC_STATIC_LIBS}
                    ${MOEPGF_STATIC_LIB})

        endif()
    endif()


    SET(TEST_EXES picoquic_ct)
endif()

# get all project files for formatting
file(GLOB_RECURSE CLANG_FORMAT_SOURCE_FILES *.c *.h)

# Adds clangformat as target that formats all source files
add_custom_target(
    clangformat
    COMMAND clang-format
    -style=Webkit
    -i
    ${CLANG_FORMAT_SOURCE_FILES}
)
