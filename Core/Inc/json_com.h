/*
 * json_com.h
 *
 *  Created on: Dec 2, 2025
 *      Author: AI Assistant
 */

#ifndef INC_JSON_COM_H_
#define INC_JSON_COM_H_

#include "uart_queue.h"
#include <stdint.h>

#define MAX_JSON_LEN 1024

/**
 * JSON packet protocol (newline-delimited JSON objects)
 *
 * Common fields:
 * - msg   : required, one of "req" | "resp" | "evt"
 * - src_id: sender device id (0-255)
 * - tar_id: target device id (0-255) or broadcast id (see RS485_BROADCAST_ID)
 * - cmd   : command/event name (string)
 * - payload: optional object/array with command data
 *
 * Request ("req"):
 * - msg:"req"
 * - MUST NOT include top-level "status" (reserved for responses)
 *
 * Response ("resp"):
 * - msg:"resp"
 * - includes status:"ok"|"error" and optional payload/message
 *
 * Event ("evt"):
 * - msg:"evt"
 * - typically no top-level status (unless separately specified)
 *
 * Host/PC note:
 * - When sending commands to the device, include msg:"req" so logs can clearly
 *   distinguish commands vs responses/events.
 */

#define JSON_MSG_REQ  "req"
#define JSON_MSG_RESP "resp"
#define JSON_MSG_EVT  "evt"

typedef struct {
    UART_Context *uart; // Dependency
    uint8_t my_id;
    char rx_line_buffer[MAX_JSON_LEN];
    uint16_t rx_index;
} JSON_Context;

void JSON_COM_Init(JSON_Context *ctx, UART_Context *uart, uint8_t my_id);
void JSON_COM_Process(JSON_Context *ctx);

#endif /* INC_JSON_COM_H_ */
