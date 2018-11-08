#ifndef __ACTION_UPPERCASE_H__
#define __ACTION_UPPERCASE_H__

/*
 * Copyright 2017 International Business Machines
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include <string.h>
#include <ap_int.h>

#include "hls_snap.H"
#include "computing_common.h" /* Computing Job definition */

//--------------------------------------------------------------------
// 22: simplify the data casting style
#define RELEASE_LEVEL		0x00000022

typedef char word_t[BPERDW];
//---------------------------------------------------------------------
// This is generic. Just adapt names for a new action
// CONTROL is defined and handled by SNAP 
// computing_job_t is user defined in hls_computing/include/computing_common.h
typedef struct {
	CONTROL Control;	/*  16 bytes */
	computing_job_t Data;	/* up to 108 bytes */
	uint8_t padding[SNAP_HLS_JOBSIZE - sizeof(computing_job_t)];
} action_reg;

#endif  /* __ACTION_UPPERCASE_H__*/
