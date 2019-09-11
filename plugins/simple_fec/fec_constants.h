
#ifndef FEC_CONSTANTS_H
#define FEC_CONSTANTS_H

// protoops
#define FEC_PROTOOP_AVAILABLE_SLOT "fec_available_slot"
#define FEC_PROTOOP_WHAT_TO_SEND   "fec_what_to_send"
#define FEC_PROTOOP_HAS_PROTECTED_DATA_TO_SEND   "fec_has_protected_data_to_send"
#define FEC_PROTOOP_GET_NEXT_SOURCE_SYMBOL_ID   "fec_next_sfpid"
#define FEC_RECEIVE_PACKET_PAYLOAD "fec_receive_packet_payload"
#define FEC_PROTECT_PACKET_PAYLOAD "fec_protect_packet_payload"
#define FEC_RESERVE_REPAIR_FRAMES "fec_reserve_repair_frames"


// frames
#define FRAME_FEC_SRC_FPI 0x29
#define FRAME_REPAIR 0x2a
#define FRAME_RECOVERED 0x2b

#define REPAIR_FRAME_TYPE_BYTE_SIZE 1
#define FPI_FRAME_TYPE_BYTE_SIZE 1

// values
#define MAX_SRC_FPI_SIZE 16
#define CHUNK_SIZE 199
#define SYMBOL_SIZE (CHUNK_SIZE + 1)

#define DEFAULT_SLOT_SIZE (PICOQUIC_MAX_PACKET_SIZE - 100)


#define FEC_PKT_METADATA_SENT_SLOT 0
#define FEC_PKT_METADATA_FLAGS 1
#define FEC_PKT_METADATA_FIRST_SOURCE_SYMBOL_ID 2
#define FEC_PKT_METADATA_NUMBER_OF_SOURCE_SYMBOLS 3

#define FEC_PKT_METADATA_FLAG_IS_FEC_PROTECTED 1LU
#define FEC_PKT_METADATA_FLAG_CONTAINS_REPAIR_FRAME 2LU

#define FEC_PKT_IS_FEC_PROTECTED(val) ((val & FEC_PKT_METADATA_FLAG_IS_FEC_PROTECTED) != 0)
#define FEC_PKT_CONTAINS_REPAIR_FRAME(val) ((val & FEC_PKT_METADATA_FLAG_CONTAINS_REPAIR_FRAME) != 0)



typedef enum available_slot_reason {
    available_slot_reason_ack,
    available_slot_reason_nack,
    available_slot_reason_none
} available_slot_reason_t;


typedef enum what_to_send {
    what_to_send_new_symbol,
    what_to_send_repair_symbol,
    what_to_send_feedback_implied_repair_symbol
} what_to_send_t;

typedef protoop_arg_t source_symbol_id_t;   // defined by the underlying framework
typedef protoop_arg_t framework_sender_t;
typedef protoop_arg_t framework_receiver_t;
typedef protoop_arg_t fec_scheme_t;



#endif //PICOQUIC_FEC_CONSTANTS_H
