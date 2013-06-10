#include "consoleConnectivity.h"
#include "command.h"
#include "task.h"
#include "serial.h"
#include "memory.h"
#include "usart.h"

#define BUFFER_SIZE MEMORY_PAGE_SIZE * 2
static char g_buffer[BUFFER_SIZE];

void consoleConnectivityTask(void *params){

	initUsart0(8, 0, 1, 230400);
	Serial *serial = get_serial_usart0();

	while (1) {
		process_command(serial, g_buffer, BUFFER_SIZE);
	}
}

