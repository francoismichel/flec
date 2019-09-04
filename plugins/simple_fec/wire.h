#include <stdint.h>
#include <stdbool.h>
#include "fec.h"


typedef struct fec_src_fpi_frame {
    source_symbol_id_t  id;
} _fec_src_fpi_frame_t;

typedef struct fec_repair_frame {
    repair_symbol_t symbol;
} _fec_repair_frame_t;

typedef _fec_src_fpi_frame_t * fec_src_fpi_frame_t;
typedef _fec_repair_frame_t * fec_repair_frame_t;

