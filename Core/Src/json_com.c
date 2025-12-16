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
#include <stdbool.h>

void JSON_COM_Init(JSON_Context *ctx, UART_Context *uart, uint8_t my_id) {
    ctx->uart = uart;
    ctx->my_id = my_id;
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

static bool TryGetU8(cJSON *item, uint8_t *out) {
    if (!cJSON_IsNumber(item)) return false;
    int v = (int)item->valuedouble;
    if (v < 0 || v > 255) return false;
    *out = (uint8_t)v;
    return true;
}

static void SendError(JSON_Context *ctx, uint8_t req_src_id, const char *resp_cmd, const char *msg, bool respond) {
    if (!respond) return;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "src_id", ctx->my_id);
    cJSON_AddNumberToObject(root, "tar_id", req_src_id);
    cJSON_AddStringToObject(root, "cmd", resp_cmd ? resp_cmd : "error");
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "message", msg);
    SendResponse(ctx, root);
}

static void SendSuccess(JSON_Context *ctx, uint8_t req_src_id, const char *resp_cmd, cJSON *payload, bool respond) {
    if (!respond) {
        if (payload) cJSON_Delete(payload);
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "src_id", ctx->my_id);
    cJSON_AddNumberToObject(root, "tar_id", req_src_id);
    cJSON_AddStringToObject(root, "cmd", resp_cmd ? resp_cmd : "ok");
    cJSON_AddStringToObject(root, "status", "ok");
    if (payload) {
        cJSON_AddItemToObject(root, "payload", payload);
    }
    SendResponse(ctx, root);
}

// --- Command Handlers ---

static void HandlePing(JSON_Context *ctx, uint8_t req_src_id, bool respond) {
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "message", "pong");
    SendSuccess(ctx, req_src_id, "pong", payload, respond);
}

static void HandleMove(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    // Mock move
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "moved");
    cJSON_AddNumberToObject(payload, "deviceId", ctx->my_id);
    SendSuccess(ctx, req_src_id, "move", payload, respond);
}

static void HandleMotionCtrl(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *action_item = cJSON_GetObjectItem(req_payload, "action");
    char *action = action_item ? action_item->valuestring : "unknown";

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "executed");
    cJSON_AddStringToObject(payload, "action", action);
    cJSON_AddNumberToObject(payload, "deviceId", ctx->my_id);
    SendSuccess(ctx, req_src_id, "motion_ctrl", payload, respond);
}

static void HandleGetFiles(JSON_Context *ctx, uint8_t req_src_id, bool respond) {
    // Mock file system
    cJSON *rootItems = cJSON_CreateArray();
    
    // Example file
    cJSON *file1 = cJSON_CreateObject();
    cJSON_AddStringToObject(file1, "Name", "test.txt");
    cJSON_AddStringToObject(file1, "Path", "test.txt");
    cJSON_AddBoolToObject(file1, "IsDirectory", cJSON_False);
    cJSON_AddNumberToObject(file1, "Size", 123);
    cJSON_AddItemToArray(rootItems, file1);

    SendSuccess(ctx, req_src_id, "get_files", rootItems, respond);
}

static void HandleGetFile(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *path_item = cJSON_GetObjectItem(req_payload, "path");
    char *path = path_item ? path_item->valuestring : "";

    // Mock content
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "path", path);
    cJSON_AddStringToObject(payload, "content", "Mock content for file");
    SendSuccess(ctx, req_src_id, "get_file", payload, respond);
}

static void HandleSaveFile(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *path_item = cJSON_GetObjectItem(req_payload, "path");
    
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "saved");
    if (path_item) cJSON_AddStringToObject(payload, "path", path_item->valuestring);
    SendSuccess(ctx, req_src_id, "save_file", payload, respond);
}

static void HandleVerifyFile(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddBoolToObject(payload, "match", cJSON_True); // Always match for now
    SendSuccess(ctx, req_src_id, "verify_file", payload, respond);
}


static void HandleProcessPacket(JSON_Context *ctx, const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        // No addressing info available -> ignore silently on RS485 multidrop
        return;
    }

    cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
    cJSON *src_id_item = cJSON_GetObjectItem(root, "src_id");
    cJSON *tar_id_item = cJSON_GetObjectItem(root, "tar_id");
    cJSON *payload_item = cJSON_GetObjectItem(root, "payload");

    uint8_t tar_id = 0;
    if (!TryGetU8(tar_id_item, &tar_id)) {
        // Not a valid addressed packet -> ignore
        cJSON_Delete(root);
        return;
    }

    if (tar_id != ctx->my_id && tar_id != RS485_BROADCAST_ID) {
        // Not for me -> ignore
        cJSON_Delete(root);
        return;
    }

    bool respond = (tar_id == ctx->my_id); // broadcast -> no response (avoid collisions)

    uint8_t src_id = 0;
    if (!TryGetU8(src_id_item, &src_id)) {
        // Can't route response -> ignore
        cJSON_Delete(root);
        return;
    }

    if (!cJSON_IsString(cmd_item)) {
        SendError(ctx, src_id, "error", "Missing or invalid cmd", respond);
        cJSON_Delete(root);
        return;
    }

    char *cmd = cmd_item->valuestring;

    if (strcmp(cmd, "ping") == 0) HandlePing(ctx, src_id, respond);
    else if (strcmp(cmd, "move") == 0) HandleMove(ctx, src_id, payload_item, respond);
    else if (strcmp(cmd, "motion_ctrl") == 0) HandleMotionCtrl(ctx, src_id, payload_item, respond);
    else if (strcmp(cmd, "get_files") == 0) HandleGetFiles(ctx, src_id, respond);
    else if (strcmp(cmd, "get_file") == 0) HandleGetFile(ctx, src_id, payload_item, respond);
    else if (strcmp(cmd, "save_file") == 0) HandleSaveFile(ctx, src_id, payload_item, respond);
    else if (strcmp(cmd, "verify_file") == 0) HandleVerifyFile(ctx, src_id, payload_item, respond);
    else {
        SendError(ctx, src_id, "error", "Unknown command", respond);
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
                // No addressing information here -> ignore silently on RS485 multidrop
            }
        }
    }
}
