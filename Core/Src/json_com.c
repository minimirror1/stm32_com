/*
 * json_com.c
 *
 *  Created on: Dec 2, 2025
 *      Author: AI Assistant
 */

#include "json_com.h"
#include "uart_queue.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_JSON_LEN 1024

static char rx_line_buffer[MAX_JSON_LEN];
static uint16_t rx_index = 0;

void JSON_COM_Init(void) {
    rx_index = 0;
}

static void SendResponse(cJSON *response_json) {
    char *str = cJSON_PrintUnformatted(response_json);
    if (str) {
        UART_SendString(str);
        UART_SendString("\n"); // Protocol expects newline
        free(str); // cJSON uses malloc
    }
    cJSON_Delete(response_json);
}

static void SendError(const char *msg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "message", msg);
    SendResponse(root);
}

static void SendSuccess(cJSON *payload) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    if (payload) {
        cJSON_AddItemToObject(root, "payload", payload);
    }
    SendResponse(root);
}

// --- Command Handlers ---

static void HandlePing(void) {
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "message", "pong");
    SendSuccess(payload);
}

static void HandleMove(int deviceId, cJSON *req_payload) {
    // Mock move
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "moved");
    cJSON_AddNumberToObject(payload, "deviceId", deviceId);
    SendSuccess(payload);
}

static void HandleMotionCtrl(int deviceId, cJSON *req_payload) {
    cJSON *action_item = cJSON_GetObjectItem(req_payload, "action");
    char *action = action_item ? action_item->valuestring : "unknown";

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "executed");
    cJSON_AddStringToObject(payload, "action", action);
    cJSON_AddNumberToObject(payload, "deviceId", deviceId);
    SendSuccess(payload);
}

static void HandleGetFiles(int deviceId) {
    // Mock file system
    cJSON *rootItems = cJSON_CreateArray();
    
    // Example file
    cJSON *file1 = cJSON_CreateObject();
    cJSON_AddStringToObject(file1, "Name", "test.txt");
    cJSON_AddStringToObject(file1, "Path", "test.txt");
    cJSON_AddBoolToObject(file1, "IsDirectory", cJSON_False);
    cJSON_AddNumberToObject(file1, "Size", 123);
    cJSON_AddItemToArray(rootItems, file1);

    SendSuccess(rootItems);
}

static void HandleGetFile(int deviceId, cJSON *req_payload) {
    cJSON *path_item = cJSON_GetObjectItem(req_payload, "path");
    char *path = path_item ? path_item->valuestring : "";

    // Mock content
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "path", path);
    cJSON_AddStringToObject(payload, "content", "Mock content for file");
    SendSuccess(payload);
}

static void HandleSaveFile(int deviceId, cJSON *req_payload) {
    cJSON *path_item = cJSON_GetObjectItem(req_payload, "path");
    
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "saved");
    if (path_item) cJSON_AddStringToObject(payload, "path", path_item->valuestring);
    SendSuccess(payload);
}

static void HandleVerifyFile(int deviceId, cJSON *req_payload) {
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddBoolToObject(payload, "match", cJSON_True); // Always match for now
    SendSuccess(payload);
}


static void HandleProcessPacket(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        SendError("Invalid JSON");
        return;
    }

    cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    cJSON *payload_item = cJSON_GetObjectItem(root, "payload");

    if (!cJSON_IsString(cmd_item)) {
        SendError("Missing or invalid cmd");
        cJSON_Delete(root);
        return;
    }

    int deviceId = 0;
    if (cJSON_IsNumber(id_item)) {
        deviceId = (int)id_item->valuedouble;
    }
    // Note: We ignore deviceId filtering for now, acting as the target device.

    char *cmd = cmd_item->valuestring;

    if (strcmp(cmd, "ping") == 0) HandlePing();
    else if (strcmp(cmd, "move") == 0) HandleMove(deviceId, payload_item);
    else if (strcmp(cmd, "motion_ctrl") == 0) HandleMotionCtrl(deviceId, payload_item);
    else if (strcmp(cmd, "get_files") == 0) HandleGetFiles(deviceId);
    else if (strcmp(cmd, "get_file") == 0) HandleGetFile(deviceId, payload_item);
    else if (strcmp(cmd, "save_file") == 0) HandleSaveFile(deviceId, payload_item);
    else if (strcmp(cmd, "verify_file") == 0) HandleVerifyFile(deviceId, payload_item);
    else {
        SendError("Unknown command");
    }

    cJSON_Delete(root);
}

void JSON_COM_Process(void) {
    uint8_t byte;
    // Process all available bytes
    while (UART_ReadByte(&byte) == 0) {
        if (byte == '\n' || byte == '\r') {
            if (rx_index > 0) {
                rx_line_buffer[rx_index] = '\0';
                HandleProcessPacket(rx_line_buffer);
                rx_index = 0;
            }
        } else {
            if (rx_index < MAX_JSON_LEN - 1) {
                rx_line_buffer[rx_index++] = (char)byte;
            } else {
                // Buffer overflow, reset
                rx_index = 0; 
                SendError("Buffer overflow");
            }
        }
    }
}


