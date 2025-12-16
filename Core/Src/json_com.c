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

void JSON_COM_Init(JSON_Context *ctx, UART_Context *uart) {
    ctx->uart = uart;
    ctx->rx_index = 0;
}

static void SendResponse(JSON_Context *ctx, cJSON *response_json) {
    char *str = cJSON_PrintUnformatted(response_json);
    if (str) {
        UART_SendString(ctx->uart, str);
        UART_SendString(ctx->uart, "\n"); // Protocol expects newline
        free(str); // cJSON uses malloc
    }
    cJSON_Delete(response_json);
}

static void SendError(JSON_Context *ctx, const char *msg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "message", msg);
    SendResponse(ctx, root);
}

static void SendSuccess(JSON_Context *ctx, cJSON *payload) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    if (payload) {
        cJSON_AddItemToObject(root, "payload", payload);
    }
    SendResponse(ctx, root);
}

// --- Command Handlers ---

static void HandlePing(JSON_Context *ctx) {
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "message", "pong");
    SendSuccess(ctx, payload);
}

static void HandleMove(JSON_Context *ctx, int deviceId, cJSON *req_payload) {
    // Mock move
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "moved");
    cJSON_AddNumberToObject(payload, "deviceId", deviceId);
    SendSuccess(ctx, payload);
}

static void HandleMotionCtrl(JSON_Context *ctx, int deviceId, cJSON *req_payload) {
    cJSON *action_item = cJSON_GetObjectItem(req_payload, "action");
    char *action = action_item ? action_item->valuestring : "unknown";

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "executed");
    cJSON_AddStringToObject(payload, "action", action);
    cJSON_AddNumberToObject(payload, "deviceId", deviceId);
    SendSuccess(ctx, payload);
}

static void HandleGetFiles(JSON_Context *ctx, int deviceId) {
    // Mock file system
    cJSON *rootItems = cJSON_CreateArray();
    
    // Example file
    cJSON *file1 = cJSON_CreateObject();
    cJSON_AddStringToObject(file1, "Name", "test.txt");
    cJSON_AddStringToObject(file1, "Path", "test.txt");
    cJSON_AddBoolToObject(file1, "IsDirectory", cJSON_False);
    cJSON_AddNumberToObject(file1, "Size", 123);
    cJSON_AddItemToArray(rootItems, file1);

    SendSuccess(ctx, rootItems);
}

static void HandleGetFile(JSON_Context *ctx, int deviceId, cJSON *req_payload) {
    cJSON *path_item = cJSON_GetObjectItem(req_payload, "path");
    char *path = path_item ? path_item->valuestring : "";

    // Mock content
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "path", path);
    cJSON_AddStringToObject(payload, "content", "Mock content for file");
    SendSuccess(ctx, payload);
}

static void HandleSaveFile(JSON_Context *ctx, int deviceId, cJSON *req_payload) {
    cJSON *path_item = cJSON_GetObjectItem(req_payload, "path");
    
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "saved");
    if (path_item) cJSON_AddStringToObject(payload, "path", path_item->valuestring);
    SendSuccess(ctx, payload);
}

static void HandleVerifyFile(JSON_Context *ctx, int deviceId, cJSON *req_payload) {
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddBoolToObject(payload, "match", cJSON_True); // Always match for now
    SendSuccess(ctx, payload);
}


static void HandleProcessPacket(JSON_Context *ctx, const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        SendError(ctx, "Invalid JSON");
        return;
    }

    cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    cJSON *payload_item = cJSON_GetObjectItem(root, "payload");

    if (!cJSON_IsString(cmd_item)) {
        SendError(ctx, "Missing or invalid cmd");
        cJSON_Delete(root);
        return;
    }

    int deviceId = 0;
    if (cJSON_IsNumber(id_item)) {
        deviceId = (int)id_item->valuedouble;
    }
    // Note: We ignore deviceId filtering for now, acting as the target device.

    char *cmd = cmd_item->valuestring;

    if (strcmp(cmd, "ping") == 0) HandlePing(ctx);
    else if (strcmp(cmd, "move") == 0) HandleMove(ctx, deviceId, payload_item);
    else if (strcmp(cmd, "motion_ctrl") == 0) HandleMotionCtrl(ctx, deviceId, payload_item);
    else if (strcmp(cmd, "get_files") == 0) HandleGetFiles(ctx, deviceId);
    else if (strcmp(cmd, "get_file") == 0) HandleGetFile(ctx, deviceId, payload_item);
    else if (strcmp(cmd, "save_file") == 0) HandleSaveFile(ctx, deviceId, payload_item);
    else if (strcmp(cmd, "verify_file") == 0) HandleVerifyFile(ctx, deviceId, payload_item);
    else {
        SendError(ctx, "Unknown command");
    }

    cJSON_Delete(root);
}

void JSON_COM_Process(JSON_Context *ctx) {
    uint8_t byte;
    // Process all available bytes
    while (UART_ReadByte(ctx->uart, &byte) == 0) {
        if (byte == '\n' || byte == '\r') {
            if (ctx->rx_index > 0) {
                ctx->rx_line_buffer[ctx->rx_index] = '\0';
                HandleProcessPacket(ctx, ctx->rx_line_buffer);
                ctx->rx_index = 0;
            }
        } else {
            if (ctx->rx_index < MAX_JSON_LEN - 1) {
                ctx->rx_line_buffer[ctx->rx_index++] = (char)byte;
            } else {
                // Buffer overflow, reset
                ctx->rx_index = 0; 
                SendError(ctx, "Buffer overflow");
            }
        }
    }
}
