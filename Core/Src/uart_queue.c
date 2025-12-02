/*
 * uart_queue.c
 *
 *  Created on: Dec 1, 2025
 *      Author: AI Assistant
 */

#include "uart_queue.h"
#include <string.h>

static UART_HandleTypeDef *q_huart;

static RingBuffer tx_buffer;
static RingBuffer rx_buffer;

static volatile uint8_t tx_busy = 0;
static uint8_t rx_byte_tmp;

void UART_Queue_Init(UART_HandleTypeDef *huart) {
    q_huart = huart;

    // Initialize indices
    tx_buffer.head = 0;
    tx_buffer.tail = 0;
    rx_buffer.head = 0;
    rx_buffer.tail = 0;

    tx_busy = 0;

    // Start Reception
    HAL_UART_Receive_IT(q_huart, &rx_byte_tmp, 1);
}

// --- TX Functions ---

int UART_SendByte(uint8_t data) {
    uint16_t next_head = (tx_buffer.head + 1) % UART_BUFFER_SIZE;

    // Check if buffer is full
    if (next_head == tx_buffer.tail) {
        return -1; // Buffer Full
    }

    tx_buffer.buffer[tx_buffer.head] = data;
    tx_buffer.head = next_head;

    // Start transmission if idle
    if (!tx_busy) {
        tx_busy = 1;
        uint8_t *pData = &tx_buffer.buffer[tx_buffer.tail];
        // Only transmit 1 byte at a time to simplify buffer wrapping logic
        // and let ISR handle the rest
        if (HAL_UART_Transmit_IT(q_huart, pData, 1) != HAL_OK) {
             tx_busy = 0;
             return -2; // Error starting transmission
        }
    }
    return 0; // Success
}

int UART_SendArray(uint8_t *data, uint16_t length) {
    for (uint16_t i = 0; i < length; i++) {
        if (UART_SendByte(data[i]) != 0) {
            return -1; // Error or Buffer Full
        }
    }
    return 0;
}

int UART_SendString(char *str) {
    while (*str) {
        if (UART_SendByte((uint8_t)*str++) != 0) {
            return -1;
        }
    }
    return 0;
}

// --- RX Functions ---

int UART_IsRxNotEmpty(void) {
    return (rx_buffer.head != rx_buffer.tail);
}

int UART_ReadByte(uint8_t *data) {
    if (rx_buffer.head == rx_buffer.tail) {
        return -1; // Buffer Empty
    }

    *data = rx_buffer.buffer[rx_buffer.tail];
    rx_buffer.tail = (rx_buffer.tail + 1) % UART_BUFFER_SIZE;
    return 0;
}

// --- HAL Callbacks ---

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == q_huart->Instance) {
        // Advance tail since the previous byte was sent
        tx_buffer.tail = (tx_buffer.tail + 1) % UART_BUFFER_SIZE;

        if (tx_buffer.tail != tx_buffer.head) {
            // More data to send
            uint8_t *pData = &tx_buffer.buffer[tx_buffer.tail];
            HAL_UART_Transmit_IT(q_huart, pData, 1);
        } else {
            // No more data
            tx_busy = 0;
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == q_huart->Instance) {
        // Store received byte
        uint16_t next_head = (rx_buffer.head + 1) % UART_BUFFER_SIZE;
        if (next_head != rx_buffer.tail) {
            rx_buffer.buffer[rx_buffer.head] = rx_byte_tmp;
            rx_buffer.head = next_head;
        }
        // Else: Buffer overflow, drop byte

        // Restart Reception
        HAL_UART_Receive_IT(q_huart, &rx_byte_tmp, 1);
    }
}

