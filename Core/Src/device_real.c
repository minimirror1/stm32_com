/*
 * device_real.c
 *
 *  Created on: Dec 31, 2025
 *      Author: AI Assistant
 *
 *  Application function stubs (weak functions)
 *  - Only compiled when USE_MOCK_DEVICE is disabled (default)
 *  - All functions are declared __weak so developers can override
 *    them in a separate file (e.g., user_app.c) without modifying this file
 *
 *  ============================================================================
 *  HOW TO USE:
 *  ============================================================================
 *  1. Create your own file (e.g., user_app.c)
 *  2. Include "device_hal.h"
 *  3. Implement any of the App_* functions you need
 *  4. Your strong implementations will override these weak stubs
 *
 *  DESIGN PRINCIPLE:
 *  - App_* functions handle PURE APPLICATION LOGIC only
 *  - NO communication parsing or response formatting needed
 *  - Just implement the actual device operations and return success/failure
 *
 *  NOTE: DO NOT MODIFY THIS FILE - implement your functions elsewhere
 *  ============================================================================
 */

#include "main.h"

#if !USE_MOCK_DEVICE

#include "device_hal.h"

/*******************************************************************************
 * App_Ping
 *******************************************************************************
 * @brief  Ping check - verify device is responsive
 * @return true if device is ready, false otherwise
 *
 * @example
 *   // In your user_app.c:
 *   #include "device_hal.h"
 *
 *   bool App_Ping(void) {
 *       return true;  // Device is alive
 *   }
 ******************************************************************************/
__weak bool App_Ping(void) {
    return false;  /* Not implemented */
}

/*******************************************************************************
 * App_Move
 *******************************************************************************
 * @brief  Execute move command
 * @param  device_id  Target device ID
 * @return true on success, false on failure
 *
 * @example
 *   // In your user_app.c:
 *   bool App_Move(uint8_t device_id) {
 *       Motor_MoveToPosition(device_id, 100);
 *       return true;
 *   }
 ******************************************************************************/
__weak bool App_Move(uint8_t device_id) {
    (void)device_id;
    return false;  /* Not implemented */
}

/*******************************************************************************
 * App_MotionStart
 *******************************************************************************
 * @brief  Start motion sequence
 * @param  device_id  Target device ID
 * @return true on success, false on failure
 *
 * @example
 *   // In your user_app.c:
 *   bool App_MotionStart(uint8_t device_id) {
 *       Motion_Start(device_id);
 *       return true;
 *   }
 ******************************************************************************/
__weak bool App_MotionStart(uint8_t device_id) {
    (void)device_id;
    return false;  /* Not implemented */
}

/*******************************************************************************
 * App_MotionStop
 *******************************************************************************
 * @brief  Stop motion sequence
 * @param  device_id  Target device ID
 * @return true on success, false on failure
 *
 * @example
 *   // In your user_app.c:
 *   bool App_MotionStop(uint8_t device_id) {
 *       Motion_Stop(device_id);
 *       return true;
 *   }
 ******************************************************************************/
__weak bool App_MotionStop(uint8_t device_id) {
    (void)device_id;
    return false;  /* Not implemented */
}

/*******************************************************************************
 * App_MotionPause
 *******************************************************************************
 * @brief  Pause motion sequence
 * @param  device_id  Target device ID
 * @return true on success, false on failure
 *
 * @example
 *   // In your user_app.c:
 *   bool App_MotionPause(uint8_t device_id) {
 *       Motion_Pause(device_id);
 *       return true;
 *   }
 ******************************************************************************/
__weak bool App_MotionPause(uint8_t device_id) {
    (void)device_id;
    return false;  /* Not implemented */
}

/*******************************************************************************
 * App_GetFiles
 *******************************************************************************
 * @brief  Get file/folder list from storage as tree structure
 * @param  out_files   Output array of file info structures
 * @param  max_count   Maximum number of files to return
 * @return Number of files (>= 0) on success, -1 on failure
 *
 * @note   Tree structure rules:
 *         - depth: 0 = root, 1 = subfolder, 2 = sub-subfolder (max APP_MAX_DEPTH-1)
 *         - parent_index: index of parent folder in array (-1 for root items)
 *         - Parents MUST appear before their children in the array!
 *         - Icon is auto-generated based on is_directory (folder/file)
 *         - Communication layer handles JSON formatting - just fill the struct!
 *
 * @note   Example folder structure:
 *         Settings/           <- depth=0, parent_index=-1
 *           config.txt        <- depth=1, parent_index=0
 *           Motor/            <- depth=1, parent_index=0
 *             settings.ini    <- depth=2, parent_index=2
 *         Log/                <- depth=0, parent_index=-1
 *           boot.txt          <- depth=1, parent_index=4
 *
 * @example
 *   // In your user_app.c:
 *   #include "device_hal.h"
 *   #include <string.h>
 *
 *   int App_GetFiles(AppFileInfo *out_files, uint16_t max_count) {
 *       int idx = 0;
 *
 *       // [0] Settings folder (root)
 *       if (idx < max_count) {
 *           strcpy(out_files[idx].name, "Settings");
 *           strcpy(out_files[idx].path, "Settings");
 *           out_files[idx].is_directory = true;
 *           out_files[idx].size = 0;
 *           out_files[idx].depth = 0;
 *           out_files[idx].parent_index = -1;
 *           idx++;
 *       }
 *
 *       // [1] config.txt inside Settings
 *       if (idx < max_count) {
 *           strcpy(out_files[idx].name, "config.txt");
 *           strcpy(out_files[idx].path, "Settings/config.txt");
 *           out_files[idx].is_directory = false;
 *           out_files[idx].size = 128;
 *           out_files[idx].depth = 1;
 *           out_files[idx].parent_index = 0;  // Parent: Settings [0]
 *           idx++;
 *       }
 *
 *       // [2] Motor subfolder inside Settings
 *       if (idx < max_count) {
 *           strcpy(out_files[idx].name, "Motor");
 *           strcpy(out_files[idx].path, "Settings/Motor");
 *           out_files[idx].is_directory = true;
 *           out_files[idx].size = 0;
 *           out_files[idx].depth = 1;
 *           out_files[idx].parent_index = 0;  // Parent: Settings [0]
 *           idx++;
 *       }
 *
 *       // [3] settings.ini inside Motor (depth=2)
 *       if (idx < max_count) {
 *           strcpy(out_files[idx].name, "settings.ini");
 *           strcpy(out_files[idx].path, "Settings/Motor/settings.ini");
 *           out_files[idx].is_directory = false;
 *           out_files[idx].size = 64;
 *           out_files[idx].depth = 2;
 *           out_files[idx].parent_index = 2;  // Parent: Motor [2]
 *           idx++;
 *       }
 *
 *       // [4] Log folder (root)
 *       if (idx < max_count) {
 *           strcpy(out_files[idx].name, "Log");
 *           strcpy(out_files[idx].path, "Log");
 *           out_files[idx].is_directory = true;
 *           out_files[idx].size = 0;
 *           out_files[idx].depth = 0;
 *           out_files[idx].parent_index = -1;
 *           idx++;
 *       }
 *
 *       // [5] boot.txt inside Log
 *       if (idx < max_count) {
 *           strcpy(out_files[idx].name, "boot.txt");
 *           strcpy(out_files[idx].path, "Log/boot.txt");
 *           out_files[idx].is_directory = false;
 *           out_files[idx].size = 256;
 *           out_files[idx].depth = 1;
 *           out_files[idx].parent_index = 4;  // Parent: Log [4]
 *           idx++;
 *       }
 *
 *       return idx;
 *   }
 ******************************************************************************/
__weak int App_GetFiles(AppFileInfo *out_files, uint16_t max_count) {
    (void)out_files;
    (void)max_count;
    return -1;  /* Not implemented */
}

/*******************************************************************************
 * App_GetFile
 *******************************************************************************
 * @brief  Read file content
 * @param  path         File path to read
 * @param  out_content  Output buffer for file content
 * @param  max_len      Maximum buffer size
 * @return true on success, false on failure
 *
 * @example
 *   // In your user_app.c:
 *   bool App_GetFile(const char *path, char *out_content, uint16_t max_len) {
 *       if (path == NULL || out_content == NULL) {
 *           return false;
 *       }
 *       return SD_ReadFile(path, out_content, max_len) == 0;
 *   }
 ******************************************************************************/
__weak bool App_GetFile(const char *path, char *out_content, uint16_t max_len) {
    (void)path;
    (void)out_content;
    (void)max_len;
    return false;  /* Not implemented */
}

/*******************************************************************************
 * App_SaveFile
 *******************************************************************************
 * @brief  Save content to file
 * @param  path     File path to write
 * @param  content  Content to write
 * @return true on success, false on failure
 *
 * @example
 *   // In your user_app.c:
 *   bool App_SaveFile(const char *path, const char *content) {
 *       if (path == NULL || content == NULL) {
 *           return false;
 *       }
 *       return SD_WriteFile(path, content) == 0;
 *   }
 ******************************************************************************/
__weak bool App_SaveFile(const char *path, const char *content) {
    (void)path;
    (void)content;
    return false;  /* Not implemented */
}

/*******************************************************************************
 * App_VerifyFile
 *******************************************************************************
 * @brief  Verify file content matches expected content
 * @param  path       File path to verify
 * @param  content    Expected content
 * @param  out_match  Output: true if content matches, false otherwise
 * @return true on success (verification performed), false on failure
 *
 * @example
 *   // In your user_app.c:
 *   bool App_VerifyFile(const char *path, const char *content, bool *out_match) {
 *       if (path == NULL || content == NULL || out_match == NULL) {
 *           return false;
 *       }
 *
 *       char buffer[512];
 *       if (SD_ReadFile(path, buffer, sizeof(buffer)) != 0) {
 *           return false;  // Couldn't read file
 *       }
 *
 *       *out_match = (strcmp(buffer, content) == 0);
 *       return true;
 *   }
 ******************************************************************************/
__weak bool App_VerifyFile(const char *path, const char *content, bool *out_match) {
    (void)path;
    (void)content;
    (void)out_match;
    return false;  /* Not implemented */
}

/*******************************************************************************
 * App_GetMotors
 *******************************************************************************
 * @brief  Get list of all motors with full information
 * @param  out_motors  Output array of motor info structures
 * @param  max_count   Maximum number of motors to return
 * @return Number of motors (>= 0) on success, -1 on failure
 *
 * @note   This function returns complete motor information including:
 *         - id: Unique motor ID
 *         - group_id, sub_id: For display as "GroupId-SubId" in GUI
 *         - type: Motor type ("Servo", "DC", "Stepper")
 *         - status: Current status ("Normal", "Error")
 *         - position: Current position value
 *         - velocity: Current velocity value
 *
 * @example
 *   // In your user_app.c:
 *   #include "device_hal.h"
 *   #include <string.h>
 *
 *   int App_GetMotors(AppMotorInfo *out_motors, uint16_t max_count) {
 *       int idx = 0;
 *
 *       // Motor 1: Servo in Group 1
 *       if (idx < max_count) {
 *           out_motors[idx].id = 1;
 *           out_motors[idx].group_id = 1;
 *           out_motors[idx].sub_id = 1;
 *           strcpy(out_motors[idx].type, "Servo");
 *           strcpy(out_motors[idx].status, "Normal");
 *           out_motors[idx].position = 90.0f;
 *           out_motors[idx].velocity = 0.5f;
 *           idx++;
 *       }
 *
 *       // Motor 2: DC motor in Group 1
 *       if (idx < max_count) {
 *           out_motors[idx].id = 2;
 *           out_motors[idx].group_id = 1;
 *           out_motors[idx].sub_id = 2;
 *           strcpy(out_motors[idx].type, "DC");
 *           strcpy(out_motors[idx].status, "Error");
 *           out_motors[idx].position = 45.0f;
 *           out_motors[idx].velocity = 1.0f;
 *           idx++;
 *       }
 *
 *       // Motor 3: Stepper in Group 2
 *       if (idx < max_count) {
 *           out_motors[idx].id = 3;
 *           out_motors[idx].group_id = 2;
 *           out_motors[idx].sub_id = 1;
 *           strcpy(out_motors[idx].type, "Stepper");
 *           strcpy(out_motors[idx].status, "Normal");
 *           out_motors[idx].position = 0.0f;
 *           out_motors[idx].velocity = 0.2f;
 *           idx++;
 *       }
 *
 *       return idx;
 *   }
 ******************************************************************************/
__weak int App_GetMotors(AppMotorInfo *out_motors, uint16_t max_count) {
    (void)out_motors;
    (void)max_count;
    return -1;  /* Not implemented */
}

/*******************************************************************************
 * App_GetMotorState
 *******************************************************************************
 * @brief  Get current state of all motors (for polling)
 * @param  out_states  Output array of motor state structures
 * @param  max_count   Maximum number of motors to return
 * @return Number of motors (>= 0) on success, -1 on failure
 *
 * @note   This function is called periodically by GUI for state updates.
 *         Only runtime state is returned (no configuration like type/group).
 *         - id: Motor ID (to identify which motor)
 *         - status: Current status ("Normal", "Error")
 *         - position: Current position value
 *         - velocity: Current velocity value
 *
 * @note   You can return only a subset of motors if some haven't changed.
 *         The GUI will merge the updates with existing state.
 *
 * @example
 *   // In your user_app.c:
 *   #include "device_hal.h"
 *   #include <string.h>
 *
 *   int App_GetMotorState(AppMotorState *out_states, uint16_t max_count) {
 *       int idx = 0;
 *
 *       // Get current state from hardware
 *       for (int motor_id = 1; motor_id <= 3 && idx < max_count; motor_id++) {
 *           out_states[idx].id = motor_id;
 *           strcpy(out_states[idx].status, Motor_HasError(motor_id) ? "Error" : "Normal");
 *           out_states[idx].position = Motor_GetPosition(motor_id);
 *           out_states[idx].velocity = Motor_GetVelocity(motor_id);
 *           idx++;
 *       }
 *
 *       return idx;
 *   }
 ******************************************************************************/
__weak int App_GetMotorState(AppMotorState *out_states, uint16_t max_count) {
    (void)out_states;
    (void)max_count;
    return -1;  /* Not implemented */
}

#endif /* !USE_MOCK_DEVICE */
