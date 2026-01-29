#include <stdint.h>
#include <libdragon.h>
#include "pc64.h"


// PicoCart64 Address space

// [READ/WRITE]: Scratch memory used for various functions
#define PC64_BASE_ADDRESS_START    0x81000000
#define PC64_BASE_ADDRESS_LENGTH   0x00001000
#define PC64_BASE_ADDRESS_END      (PC64_BASE_ADDRESS_START + PC64_BASE_ADDRESS_LENGTH - 1)

// [READ/WRITE]: Command address space. See register definitions below for details.
#define PC64_CIBASE_ADDRESS_START  0x83000000
#define PC64_CIBASE_ADDRESS_LENGTH 0x00001000
#define PC64_CIBASE_ADDRESS_END    (PC64_CIBASE_ADDRESS_START + PC64_CIBASE_ADDRESS_LENGTH - 1)

// [WRITE]: Write number of bytes to print from TX buffer
#define PC64_REGISTER_UART_TX      0x00000004

typedef struct PI_regs_s {
	volatile void *ram_address;
	uint32_t pi_address;
	uint32_t read_length;
	uint32_t write_length;
} PI_regs_t;
static volatile PI_regs_t *const PI_regs = (PI_regs_t *) 0xA4600000;

static void verify_memory_range(uint32_t base, uint32_t offset, uint32_t len)
{
	uint32_t start = base | offset;
	uint32_t end = start + len - 1;

	switch (base) {
	case PC64_BASE_ADDRESS_START:
		assert(start >= PC64_BASE_ADDRESS_START);
		assert(start <= PC64_BASE_ADDRESS_END);
		assert(end >= PC64_BASE_ADDRESS_START);
		assert(end <= PC64_BASE_ADDRESS_END);
		break;

	case PC64_CIBASE_ADDRESS_START:
		assert(start >= PC64_CIBASE_ADDRESS_START);
		assert(start <= PC64_CIBASE_ADDRESS_END);
		assert(end >= PC64_CIBASE_ADDRESS_START);
		assert(end <= PC64_CIBASE_ADDRESS_END);
		break;

	default:
		assert(!"Unsupported base");
	}
}

static void pi_write_raw(const void *src, uint32_t base, uint32_t offset, uint32_t len)
{
	assert(src != NULL);
	verify_memory_range(base, offset, len);

	disable_interrupts();
	dma_wait();

	MEMORY_BARRIER();
	PI_regs->ram_address = UncachedAddr(src);
	MEMORY_BARRIER();
	PI_regs->pi_address = offset | base;
	MEMORY_BARRIER();
	PI_regs->read_length = len - 1;
	MEMORY_BARRIER();

	enable_interrupts();
	dma_wait();
}

static void pi_write_u32(const uint32_t value, uint32_t base, uint32_t offset)
{
	uint32_t buf[] = { value };

	data_cache_hit_writeback_invalidate(buf, sizeof(buf));
	pi_write_raw(buf, base, offset, sizeof(buf));
}

static char __attribute__((aligned(16))) write_buf[0x1000];

static void pc64_uart_write(const uint8_t * buf, uint32_t len)
{
	// 16-bit aligned
	assert((((uint32_t) buf) & 0x1) == 0);

	uint32_t len_aligned32 = (len + 3) & (-4);

	data_cache_hit_writeback_invalidate((uint8_t *) buf, len_aligned32);
	pi_write_raw(write_buf, PC64_BASE_ADDRESS_START, 0, len_aligned32);

	pi_write_u32(len, PC64_CIBASE_ADDRESS_START, PC64_REGISTER_UART_TX);
}


void debugf_uart(char* format, ...) {
#ifdef DEBUG_MODE
	va_list args;
	va_start(args, format);
	vsnprintf(write_buf, sizeof(write_buf), format, args);
	va_end(args);
	pc64_uart_write((const uint8_t *)write_buf, strlen(write_buf));
	debugf(write_buf);
#endif
}