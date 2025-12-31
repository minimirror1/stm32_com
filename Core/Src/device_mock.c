/*
 * device_mock.c
 *
 *  Created on: Dec 31, 2025
 *      Author: AI Assistant
 *
 *  Mock device implementation for testing
 *  - Only compiled when USE_MOCK_DEVICE is enabled
 *  - Returns simulated data for all App_* operations
 *  - Uses same App_* interface as device_real.c
 */

#include "main.h"

#if USE_MOCK_DEVICE

#include "device_hal.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Mock File System Data (with tree structure)
 ******************************************************************************/

/**
 * Mock file entries with tree structure.
 * - depth: 0 = root, 1 = first level subfolder, 2 = second level
 * - parent_index: index of parent folder, -1 for root items
 * 
 * IMPORTANT: Parents must appear before their children in the array!
 */
static const struct {
    const char *name;
    const char *path;
    bool is_directory;
    uint32_t size;
    uint8_t depth;
    int16_t parent_index;
} mock_files[] = {
    /* Index 0: Error folder (root) */
    {"Error", "Error", true, 0, 0, -1},
    /* Index 1-3: Error folder contents (depth=1, parent=0) */
    {"err_lv.ini", "Error/err_lv.ini", false, 20, 1, 0},
    {"ERR_LVF.TXT", "Error/ERR_LVF.TXT", false, 28, 1, 0},
    {"note.ini", "Error/note.ini", false, 18, 1, 0},
    
    /* Index 4: Log folder (root) */
    {"Log", "Log", true, 0, 0, -1},
    /* Index 5-8: Log folder contents (depth=1, parent=4) */
    {"BOOT.TXT", "Log/BOOT.TXT", false, 11, 1, 4},
    {"ERROR.TXT", "Log/ERROR.TXT", false, 12, 1, 4},
    {"INSP.TXT", "Log/INSP.TXT", false, 17, 1, 4},
    {"SENSOR.TXT", "Log/SENSOR.TXT", false, 13, 1, 4},
    
    /* Index 9: Media folder (root) */
    {"Media", "Media", true, 0, 0, -1},
    /* Index 10-15: Media folder contents (depth=1, parent=9) */
    {"MT_2.CSV", "Media/MT_2.CSV", false, 20, 1, 9},
    {"MT_3.CSV", "Media/MT_3.CSV", false, 20, 1, 9},
    {"MT_4.CSV", "Media/MT_4.CSV", false, 20, 1, 9},
    {"MT_5.CSV", "Media/MT_5.CSV", false, 20, 1, 9},
    {"MT_6.CSV", "Media/MT_6.CSV", false, 20, 1, 9},
    {"MT_ALL.CSV", "Media/MT_ALL.CSV", false, 20, 1, 9},
    
    /* Index 16: Midi folder (root) */
    {"Midi", "Midi", true, 0, 0, -1},
    /* Index 17: Midi/motor subfolder (depth=1, parent=16) */
    {"motor", "Midi/motor", true, 0, 1, 16},
    /* Index 18: Midi/motor contents (depth=2, parent=17) */
    {"placeholder.txt", "Midi/motor/placeholder.txt", false, 0, 2, 17},
    /* Index 19: Midi/page subfolder (depth=1, parent=16) */
    {"page", "Midi/page", true, 0, 1, 16},
    /* Index 20: Midi/page contents (depth=2, parent=19) */
    {"placeholder.txt", "Midi/page/placeholder.txt", false, 0, 2, 19},
    
    /* Index 21: Setting folder (root) */
    {"Setting", "Setting", true, 0, 0, -1},
    /* Index 22-34: Setting folder contents (depth=1, parent=21) */
    {"DI_ID.TXT", "Setting/DI_ID.TXT", false, 10, 1, 21},
    {"MT_AT.TXT", "Setting/MT_AT.TXT", false, 9, 1, 21},
    {"MT_ATT.TXT", "Setting/MT_ATT.TXT", false, 9, 1, 21},
    {"MT_CT.TXT", "Setting/MT_CT.TXT", false, 9, 1, 21},
    {"MT_EL.TXT", "Setting/MT_EL.TXT", false, 9, 1, 21},
    {"MT_LI.TXT", "Setting/MT_LI.TXT", false, 9, 1, 21},
    {"MT_LK.TXT", "Setting/MT_LK.TXT", false, 9, 1, 21},
    {"MT_MD.TXT", "Setting/MT_MD.TXT", false, 9, 1, 21},
    {"MT_MS.TXT", "Setting/MT_MS.TXT", false, 9, 1, 21},
    {"MT_PL.TXT", "Setting/MT_PL.TXT", false, 9, 1, 21},
    {"MT_RP.TXT", "Setting/MT_RP.TXT", false, 10, 1, 21},
    {"MT_ST.TXT", "Setting/MT_ST.TXT", false, 10, 1, 21},
    {"RE_TI.TXT", "Setting/RE_TI.TXT", false, 10, 1, 21},
};

#define MOCK_FILE_COUNT (sizeof(mock_files) / sizeof(mock_files[0]))

/*******************************************************************************
 * Mock Motor Data
 ******************************************************************************/

/**
 * Mock motor entries matching GUI expected format.
 * - id: Unique motor ID
 * - group_id, sub_id: For display as "GroupId-SubId" in GUI
 * - type: "Servo", "DC", "Stepper"
 * - status: "Normal", "Error"
 * - position, velocity: Initial values (will vary in App_GetMotorState)
 */
static const struct {
    uint8_t id;
    uint8_t group_id;
    uint8_t sub_id;
    const char *type;
    const char *status;
    float position;
    float velocity;
} mock_motors[] = {
    /* Motor 1: Servo in Group 1-1 */
    {1, 1, 1, "Servo", "Normal", 90.0f, 0.5f},
    /* Motor 2: DC motor in Group 1-2, with Error status */
    {2, 1, 2, "DC", "Error", 45.0f, 1.0f},
    /* Motor 3: Stepper in Group 2-1 */
    {3, 2, 1, "Stepper", "Normal", 0.0f, 0.2f},
};

#define MOCK_MOTOR_COUNT (sizeof(mock_motors) / sizeof(mock_motors[0]))

/* Simple counter for simulating position/velocity changes */
static uint32_t mock_state_counter = 0;

/*******************************************************************************
 * Mock App_* Implementations
 ******************************************************************************/

bool App_Ping(void) {
    return true;  /* Mock always succeeds */
}

bool App_Move(uint8_t device_id) {
    (void)device_id;
    return true;  /* Mock always succeeds */
}

bool App_MotionStart(uint8_t device_id) {
    (void)device_id;
    return true;  /* Mock always succeeds */
}

bool App_MotionStop(uint8_t device_id) {
    (void)device_id;
    return true;  /* Mock always succeeds */
}

bool App_MotionPause(uint8_t device_id) {
    (void)device_id;
    return true;  /* Mock always succeeds */
}

int App_GetFiles(AppFileInfo *out_files, uint16_t max_count) {
    if (out_files == NULL) {
        return -1;
    }
    
    uint16_t count = 0;
    for (size_t i = 0; i < MOCK_FILE_COUNT && count < max_count; i++) {
        strncpy(out_files[count].name, mock_files[i].name, APP_NAME_MAX_LEN - 1);
        out_files[count].name[APP_NAME_MAX_LEN - 1] = '\0';
        
        strncpy(out_files[count].path, mock_files[i].path, APP_PATH_MAX_LEN - 1);
        out_files[count].path[APP_PATH_MAX_LEN - 1] = '\0';
        
        out_files[count].is_directory = mock_files[i].is_directory;
        out_files[count].size = mock_files[i].size;
        out_files[count].depth = mock_files[i].depth;
        out_files[count].parent_index = mock_files[i].parent_index;
        count++;
    }
    
    return (int)count;
}

bool App_GetFile(const char *path, char *out_content, uint16_t max_len) {
    if (path == NULL || out_content == NULL || max_len == 0) {
        return false;
    }
    
    /* Generate mock content based on path */
    snprintf(out_content, max_len, "Mock content for file: %s", path);
    return true;
}

bool App_SaveFile(const char *path, const char *content) {
    if (path == NULL || content == NULL) {
        return false;
    }
    
    /* Mock always succeeds - in real implementation, write to SD card */
    (void)path;
    (void)content;
    return true;
}

bool App_VerifyFile(const char *path, const char *content, bool *out_match) {
    if (path == NULL || content == NULL || out_match == NULL) {
        return false;
    }
    
    /* Mock always returns match = true */
    *out_match = true;
    return true;
}

int App_GetMotors(AppMotorInfo *out_motors, uint16_t max_count) {
    if (out_motors == NULL) {
        return -1;
    }
    
    uint16_t count = 0;
    for (size_t i = 0; i < MOCK_MOTOR_COUNT && count < max_count; i++) {
        out_motors[count].id = mock_motors[i].id;
        out_motors[count].group_id = mock_motors[i].group_id;
        out_motors[count].sub_id = mock_motors[i].sub_id;
        
        strncpy(out_motors[count].type, mock_motors[i].type, APP_MOTOR_TYPE_LEN - 1);
        out_motors[count].type[APP_MOTOR_TYPE_LEN - 1] = '\0';
        
        strncpy(out_motors[count].status, mock_motors[i].status, APP_MOTOR_STATUS_LEN - 1);
        out_motors[count].status[APP_MOTOR_STATUS_LEN - 1] = '\0';
        
        out_motors[count].position = mock_motors[i].position;
        out_motors[count].velocity = mock_motors[i].velocity;
        count++;
    }
    
    return (int)count;
}

int App_GetMotorState(AppMotorState *out_states, uint16_t max_count) {
    if (out_states == NULL) {
        return -1;
    }
    
    /* Increment counter for simulating changes */
    mock_state_counter++;
    
    uint16_t count = 0;
    for (size_t i = 0; i < MOCK_MOTOR_COUNT && count < max_count; i++) {
        out_states[count].id = mock_motors[i].id;
        
        strncpy(out_states[count].status, mock_motors[i].status, APP_MOTOR_STATUS_LEN - 1);
        out_states[count].status[APP_MOTOR_STATUS_LEN - 1] = '\0';
        
        /* Simulate position/velocity changes with small variations */
        /* Position oscillates around base value by +/- 5 degrees */
        float offset = (float)((mock_state_counter + i * 7) % 11) - 5.0f;
        out_states[count].position = mock_motors[i].position + offset;
        
        /* Velocity varies slightly */
        float vel_offset = (float)((mock_state_counter + i * 3) % 5) * 0.1f;
        out_states[count].velocity = mock_motors[i].velocity + vel_offset;
        
        count++;
    }
    
    return (int)count;
}

#endif /* USE_MOCK_DEVICE */
