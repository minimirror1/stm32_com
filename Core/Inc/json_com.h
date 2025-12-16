/*
 * json_com.h
 *
 *  Created on: Dec 2, 2025
 *      Author: AI Assistant
 */

#ifndef INC_JSON_COM_H_
#define INC_JSON_COM_H_

#include "uart_queue.h"

#define MAX_JSON_LEN 1024

typedef struct {
    UART_Context *uart; // Dependency
    char rx_line_buffer[MAX_JSON_LEN];
    uint16_t rx_index;
} JSON_Context;

void JSON_COM_Init(JSON_Context *ctx, UART_Context *uart);
void JSON_COM_Process(JSON_Context *ctx);

#endif /* INC_JSON_COM_H_ */
