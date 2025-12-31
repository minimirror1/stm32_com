/*
 * json_com.c
 *
 *  Created on: Dec 2, 2025
 *      Author: AI Assistant
 *
 *  JSON Communication Library
 *  - Handles all JSON parsing and response formatting
 *  - Application operations delegated to App_* functions (device_hal.h)
 *  - App_* functions are pure application logic - no communication knowledge needed
 */

#include "json_com.h"
#include "uart_queue.h"
#include "cJSON.h"
#include "device_hal.h"
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
        UART_SendStringBlocking(ctx->uart, str);
        UART_SendStringBlocking(ctx->uart, "\n"); // Protocol expects newline
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
    cJSON_AddStringToObject(root, "msg", "resp");
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
    cJSON_AddStringToObject(root, "msg", "resp");
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
// All handlers call App_* functions and build responses in this layer

static void HandlePing(JSON_Context *ctx, uint8_t req_src_id, bool respond) {
    // Call pure application function
    bool success = App_Ping();
    
    if (!success) {
        SendError(ctx, req_src_id, "pong", "Not implemented", respond);
        return;
    }
    
    // Build response in communication layer
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "message", "pong");
    SendSuccess(ctx, req_src_id, "pong", payload, respond);
}

static void HandleMove(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    (void)req_payload;
    
    // Call pure application function
    bool success = App_Move(ctx->my_id);
    
    if (!success) {
        SendError(ctx, req_src_id, "move", "Not implemented", respond);
        return;
    }
    
    // Build response in communication layer
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "moved");
    cJSON_AddNumberToObject(payload, "deviceId", ctx->my_id);
    SendSuccess(ctx, req_src_id, "move", payload, respond);
}

static void HandleMotionCtrl(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *action_item = cJSON_GetObjectItem(req_payload, "action");
    const char *action = action_item ? action_item->valuestring : NULL;
    
    if (action == NULL) {
        SendError(ctx, req_src_id, "motion_ctrl", "Missing action", respond);
        return;
    }
    
    // Call appropriate App function based on action
    bool success = false;
    if (strcmp(action, "start") == 0) {
        success = App_MotionStart(ctx->my_id);
    } else if (strcmp(action, "stop") == 0) {
        success = App_MotionStop(ctx->my_id);
    } else if (strcmp(action, "pause") == 0) {
        success = App_MotionPause(ctx->my_id);
    } else {
        SendError(ctx, req_src_id, "motion_ctrl", "Unknown action", respond);
        return;
    }
    
    if (!success) {
        SendError(ctx, req_src_id, "motion_ctrl", "Not implemented", respond);
        return;
    }
    
    // Build response in communication layer
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "executed");
    cJSON_AddStringToObject(payload, "action", action);
    cJSON_AddNumberToObject(payload, "deviceId", ctx->my_id);
    SendSuccess(ctx, req_src_id, "motion_ctrl", payload, respond);
}

/* Icon unicode escape strings for file tree (JSON format) */
#define ICON_FOLDER "\\uE8B7"
#define ICON_FILE   "\\uE7C3"

/**
 * @brief Stream a single file item as JSON to UART
 */
static void StreamFileItem(JSON_Context *ctx, AppFileInfo *file, bool has_children_start) {
    char numbuf[16];
    
    UART_SendStringBlocking(ctx->uart, "{\"Children\":");
    if (has_children_start) {
        UART_SendStringBlocking(ctx->uart, "[");
    } else {
        UART_SendStringBlocking(ctx->uart, "[]");
    }
    
    // Only close Children array if no children will follow
    if (!has_children_start) {
        UART_SendStringBlocking(ctx->uart, ",\"Icon\":\"");
        UART_SendStringBlocking(ctx->uart, file->is_directory ? ICON_FOLDER : ICON_FILE);
        UART_SendStringBlocking(ctx->uart, "\",\"Name\":\"");
        UART_SendStringBlocking(ctx->uart, file->name);
        UART_SendStringBlocking(ctx->uart, "\",\"Path\":\"");
        UART_SendStringBlocking(ctx->uart, file->path);
        UART_SendStringBlocking(ctx->uart, "\",\"IsDirectory\":");
        UART_SendStringBlocking(ctx->uart, file->is_directory ? "true" : "false");
        UART_SendStringBlocking(ctx->uart, ",\"Size\":");
        snprintf(numbuf, sizeof(numbuf), "%lu", (unsigned long)file->size);
        UART_SendStringBlocking(ctx->uart, numbuf);
        UART_SendStringBlocking(ctx->uart, "}");
    }
}

/**
 * @brief Close a file item that had children
 */
static void StreamFileItemClose(JSON_Context *ctx, AppFileInfo *file) {
    char numbuf[16];
    
    UART_SendStringBlocking(ctx->uart, "],\"Icon\":\"");
    UART_SendStringBlocking(ctx->uart, file->is_directory ? ICON_FOLDER : ICON_FILE);
    UART_SendStringBlocking(ctx->uart, "\",\"Name\":\"");
    UART_SendStringBlocking(ctx->uart, file->name);
    UART_SendStringBlocking(ctx->uart, "\",\"Path\":\"");
    UART_SendStringBlocking(ctx->uart, file->path);
    UART_SendStringBlocking(ctx->uart, "\",\"IsDirectory\":");
    UART_SendStringBlocking(ctx->uart, file->is_directory ? "true" : "false");
    UART_SendStringBlocking(ctx->uart, ",\"Size\":");
    snprintf(numbuf, sizeof(numbuf), "%lu", (unsigned long)file->size);
    UART_SendStringBlocking(ctx->uart, numbuf);
    UART_SendStringBlocking(ctx->uart, "}");
}

/**
 * @brief Check if file at index has any children
 */
static bool HasChildren(AppFileInfo *files, int count, int parent_idx) {
    for (int i = parent_idx + 1; i < count; i++) {
        if (files[i].parent_index == parent_idx) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Stream file tree recursively (memory efficient - no cJSON allocation)
 */
static void StreamFileTreeRecursive(JSON_Context *ctx, AppFileInfo *files, int count, int16_t parent_idx, int depth) {
    bool first = true;
    
    for (int i = 0; i < count; i++) {
        if (files[i].parent_index != parent_idx) continue;
        
        if (!first) {
            UART_SendStringBlocking(ctx->uart, ",");
        }
        first = false;
        
        bool has_children = HasChildren(files, count, i);
        
        if (has_children) {
            // Start item with open Children array
            StreamFileItem(ctx, &files[i], true);
            // Recurse for children
            StreamFileTreeRecursive(ctx, files, count, i, depth + 1);
            // Close item
            StreamFileItemClose(ctx, &files[i]);
        } else {
            // Item with empty Children
            StreamFileItem(ctx, &files[i], false);
        }
    }
}

static void HandleGetFiles(JSON_Context *ctx, uint8_t req_src_id, bool respond) {
    if (!respond) return;
    
    // Static buffer for file list
    static AppFileInfo files[APP_MAX_FILES];
    
    // Call pure application function - returns count or -1
    int count = App_GetFiles(files, APP_MAX_FILES);
    
    if (count < 0) {
        SendError(ctx, req_src_id, "get_files", "Not implemented", respond);
        return;
    }
    
    // Stream response directly to UART (memory efficient)
    char numbuf[16];
    
    UART_SendStringBlocking(ctx->uart, "{\"msg\":\"resp\",\"src_id\":");
    snprintf(numbuf, sizeof(numbuf), "%u", (unsigned)ctx->my_id);
    UART_SendStringBlocking(ctx->uart, numbuf);
    UART_SendStringBlocking(ctx->uart, ",\"tar_id\":");
    snprintf(numbuf, sizeof(numbuf), "%u", (unsigned)req_src_id);
    UART_SendStringBlocking(ctx->uart, numbuf);
    UART_SendStringBlocking(ctx->uart, ",\"cmd\":\"get_files\",\"status\":\"ok\",\"payload\":[");
    
    // Stream file tree recursively (root items have parent_index = -1)
    StreamFileTreeRecursive(ctx, files, count, -1, 0);
    
    UART_SendStringBlocking(ctx->uart, "]}\n");
}

static void HandleGetFile(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *path_item = cJSON_GetObjectItem(req_payload, "path");
    const char *path = path_item ? path_item->valuestring : "";
    
    // Static buffer for file content
    static char content_buffer[APP_CONTENT_MAX_LEN];
    
    // Call pure application function
    bool success = App_GetFile(path, content_buffer, APP_CONTENT_MAX_LEN);
    
    if (!success) {
        SendError(ctx, req_src_id, "get_file", "Not implemented or file not found", respond);
        return;
    }
    
    // Build response in communication layer
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "path", path);
    cJSON_AddStringToObject(payload, "content", content_buffer);
    SendSuccess(ctx, req_src_id, "get_file", payload, respond);
}

static void HandleSaveFile(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *path_item = cJSON_GetObjectItem(req_payload, "path");
    cJSON *content_item = cJSON_GetObjectItem(req_payload, "content");
    const char *path = path_item ? path_item->valuestring : "";
    const char *content = content_item ? content_item->valuestring : "";
    
    // Call pure application function
    bool success = App_SaveFile(path, content);
    
    if (!success) {
        SendError(ctx, req_src_id, "save_file", "Not implemented or save failed", respond);
        return;
    }
    
    // Build response in communication layer
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", "saved");
    cJSON_AddStringToObject(payload, "path", path);
    SendSuccess(ctx, req_src_id, "save_file", payload, respond);
}

static void HandleVerifyFile(JSON_Context *ctx, uint8_t req_src_id, cJSON *req_payload, bool respond) {
    cJSON *path_item = cJSON_GetObjectItem(req_payload, "path");
    cJSON *content_item = cJSON_GetObjectItem(req_payload, "content");
    const char *path = path_item ? path_item->valuestring : "";
    const char *content = content_item ? content_item->valuestring : "";
    
    bool match = false;
    
    // Call pure application function
    bool success = App_VerifyFile(path, content, &match);
    
    if (!success) {
        SendError(ctx, req_src_id, "verify_file", "Not implemented or file not found", respond);
        return;
    }
    
    // Build response in communication layer
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddBoolToObject(payload, "match", match);
    SendSuccess(ctx, req_src_id, "verify_file", payload, respond);
}

static void HandleGetMotors(JSON_Context *ctx, uint8_t req_src_id, bool respond) {
    if (!respond) return;
    
    // Static buffer for motor list
    static AppMotorInfo motors[APP_MAX_MOTORS];
    
    // Call pure application function - returns count or -1
    int count = App_GetMotors(motors, APP_MAX_MOTORS);
    
    if (count < 0) {
        SendError(ctx, req_src_id, "get_motors", "Not implemented", respond);
        return;
    }
    
    // Build response using cJSON (motor count is typically small)
    cJSON *payload = cJSON_CreateObject();
    if (payload == NULL) {
        SendError(ctx, req_src_id, "get_motors", "Memory allocation failed", respond);
        return;
    }
    
    cJSON *motors_arr = cJSON_CreateArray();
    if (motors_arr == NULL) {
        cJSON_Delete(payload);
        SendError(ctx, req_src_id, "get_motors", "Memory allocation failed", respond);
        return;
    }
    
    for (int i = 0; i < count; i++) {
        cJSON *motor = cJSON_CreateObject();
        if (motor == NULL) {
            cJSON_Delete(motors_arr);
            cJSON_Delete(payload);
            SendError(ctx, req_src_id, "get_motors", "Memory allocation failed", respond);
            return;
        }
        
        cJSON_AddNumberToObject(motor, "id", motors[i].id);
        cJSON_AddNumberToObject(motor, "groupId", motors[i].group_id);
        cJSON_AddNumberToObject(motor, "subId", motors[i].sub_id);
        cJSON_AddStringToObject(motor, "type", motors[i].type);
        cJSON_AddStringToObject(motor, "status", motors[i].status);
        cJSON_AddNumberToObject(motor, "position", motors[i].position);
        cJSON_AddNumberToObject(motor, "velocity", motors[i].velocity);
        
        cJSON_AddItemToArray(motors_arr, motor);
    }
    
    cJSON_AddItemToObject(payload, "motors", motors_arr);
    SendSuccess(ctx, req_src_id, "get_motors", payload, respond);
}

static void HandleGetMotorState(JSON_Context *ctx, uint8_t req_src_id, bool respond) {
    if (!respond) return;
    
    // Static buffer for motor states
    static AppMotorState states[APP_MAX_MOTORS];
    
    // Call pure application function - returns count or -1
    int count = App_GetMotorState(states, APP_MAX_MOTORS);
    
    if (count < 0) {
        SendError(ctx, req_src_id, "get_motor_state", "Not implemented", respond);
        return;
    }
    
    // Build response using cJSON (motor count is typically small)
    cJSON *payload = cJSON_CreateObject();
    if (payload == NULL) {
        SendError(ctx, req_src_id, "get_motor_state", "Memory allocation failed", respond);
        return;
    }
    
    cJSON *motors_arr = cJSON_CreateArray();
    if (motors_arr == NULL) {
        cJSON_Delete(payload);
        SendError(ctx, req_src_id, "get_motor_state", "Memory allocation failed", respond);
        return;
    }
    
    for (int i = 0; i < count; i++) {
        cJSON *motor = cJSON_CreateObject();
        if (motor == NULL) {
            cJSON_Delete(motors_arr);
            cJSON_Delete(payload);
            SendError(ctx, req_src_id, "get_motor_state", "Memory allocation failed", respond);
            return;
        }
        
        cJSON_AddNumberToObject(motor, "id", states[i].id);
        cJSON_AddNumberToObject(motor, "position", states[i].position);
        cJSON_AddNumberToObject(motor, "velocity", states[i].velocity);
        cJSON_AddStringToObject(motor, "status", states[i].status);
        
        cJSON_AddItemToArray(motors_arr, motor);
    }
    
    cJSON_AddItemToObject(payload, "motors", motors_arr);
    SendSuccess(ctx, req_src_id, "get_motor_state", payload, respond);
}

static void HandleProcessPacket(JSON_Context *ctx, const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        // No addressing info available -> ignore silently on RS485 multidrop
        return;
    }

    cJSON *msg_item = cJSON_GetObjectItem(root, "msg");
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

    // Message type discrimination:
    // - "req": execute command handlers (may respond if unicast)
    // - "resp"/"evt": ignore on device side (no handler execution)
    // - legacy packets without "msg": treat as "req" for backwards compatibility
    const char *msg_type = NULL;
    bool legacy_no_msg = (msg_item == NULL);
    if (!legacy_no_msg) {
        if (!cJSON_IsString(msg_item) || msg_item->valuestring == NULL) {
            SendError(ctx, src_id, "error", "Missing or invalid msg", respond);
            cJSON_Delete(root);
            return;
        }
        msg_type = msg_item->valuestring;
    } else {
        msg_type = "req";
    }

    if (strcmp(msg_type, "req") != 0) {
        // Device role: ignore responses/events (host should consume these).
        cJSON_Delete(root);
        return;
    }

    // Requests must not include "status" to avoid ambiguity with responses.
    if (cJSON_GetObjectItem(root, "status") != NULL) {
        SendError(ctx, src_id, "error", "status not allowed in req", respond);
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
    else if (strcmp(cmd, "get_motors") == 0) HandleGetMotors(ctx, src_id, respond);
    else if (strcmp(cmd, "get_motor_state") == 0) HandleGetMotorState(ctx, src_id, respond);
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
