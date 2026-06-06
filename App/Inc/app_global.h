/**
 * @file app_global.h
 * @brief Global variables and symbols exported from app_main.c
 *
 * @details
 * Most application globals are declared in app_main.c
 * Reference this include to gain access to these variables
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "platform.h"
#include "jobs.h"

#define JOB_QUEUE_SIZE      50

extern job_queue_t gx_job_queue;

extern NEVER_RETURNS void v_app_main(void);
