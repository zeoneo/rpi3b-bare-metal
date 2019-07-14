#ifndef RPI_BASE_H
#define RPI_BASE_H

#include <stdint.h>


#define PERIPHERAL_BASE     0x3F000000UL + 0x80000000UL

typedef volatile uint32_t rpi_reg_rw_t;
typedef volatile const uint32_t rpi_reg_ro_t;
typedef volatile uint32_t rpi_reg_wo_t;

typedef volatile uint64_t rpi_wreg_rw_t;
typedef volatile const uint64_t rpi_wreg_ro_t;

#endif
