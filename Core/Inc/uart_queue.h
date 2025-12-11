/*
 * uart_queue.h
 *
 *  Created on: Dec 1, 2025
 *      Author: AI Assistant
 */

#ifndef INC_UART_QUEUE_H_
#define INC_UART_QUEUE_H_

#include "main.h"

#define UART_BUFFER_SIZE 256

typedef struct {
	uint8_t buffer[UART_BUFFER_SIZE];
	volatile uint16_t head;
	volatile uint16_t tail;
} RingBuffer;

void UART_Queue_Init(UART_HandleTypeDef *huart);

// TX Functions
int UART_SendByte(uint8_t data);
int UART_SendArray(uint8_t *data, uint16_t length);
int UART_SendString(char *str);

// RX Functions
int UART_IsRxNotEmpty(void);
int UART_ReadByte(uint8_t *data);

#endif /* INC_UART_QUEUE_H_ */




