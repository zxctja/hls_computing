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

/*
 * SNAP HLS_COMPUTING EXAMPLE
 *
 * Tasks for the user:
 *   1. Explore HLS pragmas to get better timing behavior.
 *   2. Try to measure the time needed to do data transfers (advanced)
 */

#include <string.h>
#include "ap_int.h"
#include "action_computing.H"

//----------------------------------------------------------------------
//--- MAIN PROGRAM -----------------------------------------------------
//----------------------------------------------------------------------
static int process_action(snap_membus_t *din_gmem,
	      snap_membus_t *dout_gmem,
	      /* snap_membus_t *d_ddrmem, *//* not needed */
	      action_reg *act_reg)
{
    uint32_t size, bytes_to_transfer;
    uint64_t i_idx, o_idx;

    /* byte address received need to be aligned with port width */
    i_idx = act_reg->Data.in.addr >> ADDR_RIGHT_SHIFT;
    o_idx = act_reg->Data.out.addr >> ADDR_RIGHT_SHIFT;
    size = act_reg->Data.in.size;
	
	#define A 0x67452301
	#define B 0xEFCDAB89
	#define C 0x98BADCFE
	#define D 0x10325476

	#define F(X,Y,Z) ((X&Y)|((~X)&Z))
	#define G(X,Y,Z) ((X&Z)|(Y&(~Z)))
	#define H(X,Y,Z) (X^Y^Z)
	#define I(X,Y,Z) (Y^(X|(~Z)))
	
	ap_uint<5> S1[4]={ 7,12,17,22};
	ap_uint<5> S2[4]={ 5, 9,14,20};
	ap_uint<5> S3[4]={ 4,11,16,23};
	ap_uint<5> S4[4]={ 6,10,15,21};

	ap_uint<32> temp[4];

	ap_uint<32> result[4] = {A,B,C,D};

	uint32_t groups[16];

	uint32_t FF(ap_uint<32> a, ap_uint<32> b, ap_uint<32> c, ap_uint<32> d, ap_uint<32> x, ap_uint<5> s,ap_uint<32> ac);
	uint32_t GG(ap_uint<32> a, ap_uint<32> b, ap_uint<32> c, ap_uint<32> d, ap_uint<32> x, ap_uint<5> s,ap_uint<32> ac);
	uint32_t HH(ap_uint<32> a, ap_uint<32> b, ap_uint<32> c, ap_uint<32> d, ap_uint<32> x, ap_uint<5> s,ap_uint<32> ac);
	uint32_t II(ap_uint<32> a, ap_uint<32> b, ap_uint<32> c, ap_uint<32> d, ap_uint<32> x, ap_uint<5> s,ap_uint<32> ac);

		int i;

		uint32_t FF_SIN[16]={
							0xd76aa478, 
							0xe8c7b756,
							0x242070db,
							0xc1bdceee,
							0xf57c0faf,
							0x4787c62a,
							0xa8304613,
							0xfd469501,
							0x698098d8,
							0x8b44f7af,
							0xffff5bb1,
							0x895cd7be,
							0x6b901122,
							0xfd987193,
							0xa679438e,
							0x49b40821};

		uint32_t GG_SIN[16]={
							0xf61e2562, 
							0xc040b340,
							0x265e5a51,
							0xe9b6c7aa,
							0xd62f105d,
							0x02441453,
							0xd8a1e681,
							0xe7d3fbc8,
							0x21e1cde6,
							0xc33707d6,
							0xf4d50d87,
							0x455a14ed,
							0xa9e3e905,
							0xfcefa3f8,
							0x676f02d9,
							0x8d2a4c8a};

		uint32_t HH_SIN[16]={
							0xfffa3942, 
							0x8771f681,
							0x6d9d6122,
							0xfde5380c,
							0xa4beea44,
							0x4bdecfa9,
							0xf6bb4b60,
							0xbebfbc70,
							0x289b7ec6,
							0xeaa127fa,
							0xd4ef3085,
							0x04881d05,
							0xd9d4d039,
							0xe6db99e5,
							0x1fa27cf8,
							0xc4ac5665};

		uint32_t II_SIN[16]={
							0xf4292244, 
							0x432aff97,
							0xab9423a7,
							0xfc93a039,
							0x655b59c3,
							0x8f0ccc92,
							0xffeff47d,
							0x85845dd1,
							0x6fa87e4f,
							0xfe2ce6e0,
							0xa3014314,
							0x4e0811a1,
							0xf7537e82,
							0xbd3af235,
							0x2ad7d2bb,
							0xeb86d391};
	main_loop:
    while (size > 0) {

	//#pragma HLS pipeline
	
	temp[0] = result[0];//a
	temp[1] = result[3];//d
	temp[2] = result[2];//c
	temp[3] = result[1];//b
	
	/* Limit the number of bytes to process to a 64B word */
	bytes_to_transfer = BPERDW;
	
    /* Read in one data */
	((snap_membus_t *)groups)[0] = (din_gmem + i_idx)[0];

		//#pragma HLS dataflow
		FF_loop:
		for(i=0;i<16;i++){
		#pragma HLS pipeline
			temp[((ap_uint<2>)i)] = FF(temp[(ap_uint<2>)i], temp[(ap_uint<2>)(3+i)], temp[(ap_uint<2>)(2+i)], temp[(ap_uint<2>)(1+i)], groups[i], S1[((ap_uint<2>)i)], FF_SIN[i]);
		}

		GG_loop:
		for(i=0;i<16;i++){
		#pragma HLS pipeline
			temp[((ap_uint<2>)i)] = GG(temp[(ap_uint<2>)i], temp[(ap_uint<2>)(3+i)], temp[(ap_uint<2>)(2+i)], temp[(ap_uint<2>)(1+i)], groups[(ap_uint<4>)(1+i*5)], S2[((ap_uint<2>)i)], GG_SIN[i]);
		}

		HH_loop:
		for(i=0;i<16;i++){
		#pragma HLS pipeline
			temp[((ap_uint<2>)i)] = HH(temp[(ap_uint<2>)i], temp[(ap_uint<2>)(3+i)], temp[(ap_uint<2>)(2+i)], temp[(ap_uint<2>)(1+i)], groups[(ap_uint<4>)(5+i*3)], S3[((ap_uint<2>)i)], HH_SIN[i]);
		}

		II_loop:
		for(i=0;i<16;i++){
		#pragma HLS pipeline
			temp[((ap_uint<2>)i)] = II(temp[(ap_uint<2>)i], temp[(ap_uint<2>)(3+i)], temp[(ap_uint<2>)(2+i)], temp[(ap_uint<2>)(1+i)], groups[(ap_uint<4>)(i*7)], S4[((ap_uint<2>)i)], II_SIN[i]);
		}

	//	FF_GG_HH_II_loop:
	//	for(i=0;i<64;i++){
	//	#pragma HLS pipeline
	//		if(i<16)
	//			temp[((ap_uint<2>)i)] = FF(temp[0], temp[3], temp[2], temp[1], groups[((ap_uint<4>)i)], S1[((ap_uint<2>)i)], FF_SIN[((ap_uint<4>)i)]);
	//		else if(i<32)
	//			temp[((ap_uint<2>)i)] = GG(temp[0], temp[3], temp[2], temp[1], groups[((ap_uint<4>)i)], S2[((ap_uint<2>)i)], GG_SIN[((ap_uint<4>)i)]);
	//		else if(i<48)
	//			temp[((ap_uint<2>)i)] = HH(temp[0], temp[3], temp[2], temp[1], groups[((ap_uint<4>)i)], S3[((ap_uint<2>)i)], HH_SIN[((ap_uint<4>)i)]);
	//		else
	//			temp[((ap_uint<2>)i)] = II(temp[0], temp[3], temp[2], temp[1], groups[((ap_uint<4>)i)], S4[((ap_uint<2>)i)], II_SIN[((ap_uint<4>)i)]);
	//			
	//		temp[0] = temp[1];
	//		temp[3] = temp[0];
	//		temp[2] = temp[3];
	//		temp[1] = temp[2];
	//	}

    result[0] += temp[0];
    result[1] += temp[3];
    result[2] += temp[2];
    result[3] += temp[1];

	size -= bytes_to_transfer;
	i_idx++;
    }

	/* Write out one word_t */
	(dout_gmem + o_idx)[0] = ((ap_uint<128> *)result)[0];
    act_reg->Control.Retc = SNAP_RETC_SUCCESS;
    return 0;
}

    uint32_t FF(ap_uint<32> a, ap_uint<32> b, ap_uint<32> c, ap_uint<32> d, ap_uint<32> x, ap_uint<5> s,ap_uint<32> ac) {
//#pragma HLS latency min=2 max=2
//#pragma HLS allocation instances=add limit=2 operation
        a = a + F(b, c, d) + x + ac;
        a = (a << s) | (a >> (32 - s));
        a = a + b;
        return a;
    }

	uint32_t GG(ap_uint<32> a, ap_uint<32> b, ap_uint<32> c, ap_uint<32> d, ap_uint<32> x, ap_uint<5> s,ap_uint<32> ac) {
//#pragma HLS latency min=2 max=2
//#pragma HLS allocation instances=add limit=2 operation
        a = a + G(b, c, d) + x + ac;
        a = (a << s) | (a >> (32 - s));
        a = a + b;
        return a;
	}

	uint32_t HH(ap_uint<32> a, ap_uint<32> b, ap_uint<32> c, ap_uint<32> d, ap_uint<32> x, ap_uint<5> s,ap_uint<32> ac) {
//#pragma HLS latency min=2 max=2
//#pragma HLS allocation instances=add limit=2 operation
        a = a + H(b, c, d) + x + ac;
        a = (a << s) | (a >> (32 - s));
        a = a + b;
        return a;
    }

	uint32_t II(ap_uint<32> a, ap_uint<32> b, ap_uint<32> c, ap_uint<32> d, ap_uint<32> x, ap_uint<5> s,ap_uint<32> ac) {
//#pragma HLS latency min=2 max=2
//#pragma HLS allocation instances=add limit=2 operation
        a = a + I(b, c, d) + x + ac;
        a = (a << s) | (a >> (32 - s));
        a = a + b;
        return a;
		}

//--- TOP LEVEL MODULE -------------------------------------------------
void hls_action(snap_membus_t *din_gmem,
	snap_membus_t *dout_gmem,
	/* snap_membus_t *d_ddrmem, // CAN BE COMMENTED IF UNUSED */
	action_reg *act_reg,
	action_RO_config_reg *Action_Config)
{
    // Host Memory AXI Interface - CANNOT BE REMOVED - NO CHANGE BELOW
#pragma HLS INTERFACE m_axi port=din_gmem bundle=host_mem offset=slave depth=512 \
  max_read_burst_length=64  max_write_burst_length=64
#pragma HLS INTERFACE s_axilite port=din_gmem bundle=ctrl_reg offset=0x030

#pragma HLS INTERFACE m_axi port=dout_gmem bundle=host_mem offset=slave depth=512 \
  max_read_burst_length=64  max_write_burst_length=64
#pragma HLS INTERFACE s_axilite port=dout_gmem bundle=ctrl_reg offset=0x040

/*  // DDR memory Interface - CAN BE COMMENTED IF UNUSED
 * #pragma HLS INTERFACE m_axi port=d_ddrmem bundle=card_mem0 offset=slave depth=512 \
 *   max_read_burst_length=64  max_write_burst_length=64
 * #pragma HLS INTERFACE s_axilite port=d_ddrmem bundle=ctrl_reg offset=0x050
 */
    // Host Memory AXI Lite Master Interface - NO CHANGE BELOW
#pragma HLS DATA_PACK variable=Action_Config
#pragma HLS INTERFACE s_axilite port=Action_Config bundle=ctrl_reg offset=0x010
#pragma HLS DATA_PACK variable=act_reg
#pragma HLS INTERFACE s_axilite port=act_reg bundle=ctrl_reg offset=0x100
#pragma HLS INTERFACE s_axilite port=return bundle=ctrl_reg

    /* Required Action Type Detection - NO CHANGE BELOW */
    //	NOTE: switch generates better vhdl than "if" */
    // Test used to exit the action if no parameter has been set.
    // Used for the discovery phase of the cards */
    switch (act_reg->Control.flags) {
    case 0:
	Action_Config->action_type = COMPUTING_ACTION_TYPE; //TO BE ADAPTED
	Action_Config->release_level = RELEASE_LEVEL;
	act_reg->Control.Retc = 0xe00f;
	return;
	break;
    default:
	    /* process_action(din_gmem, dout_gmem, d_ddrmem, act_reg); */
	    process_action(din_gmem, dout_gmem, act_reg);
	break;
    }
}

//-----------------------------------------------------------------------------
//-- TESTBENCH BELOW IS USED ONLY TO DEBUG THE HARDWARE ACTION WITH HLS TOOL --
//-----------------------------------------------------------------------------

#ifdef NO_SYNTH

int main(void)
{
#define MEMORY_LINES 1
    int rc = 0;
    unsigned int i;
    static snap_membus_t  din_gmem[MEMORY_LINES];
    static snap_membus_t  dout_gmem[MEMORY_LINES];

    //snap_membus_t  dout_gmem[2048];
    //snap_membus_t  d_ddrmem[2048];
    action_reg act_reg;
    action_RO_config_reg Action_Config;

    // Discovery Phase .....
    // when flags = 0 then action will just return action type and release
    act_reg.Control.flags = 0x0;
    printf("Discovery : calling action to get config data\n");
    hls_action(din_gmem, dout_gmem, &act_reg, &Action_Config);
    fprintf(stderr,
	"ACTION_TYPE:	%08x\n"
	"RELEASE_LEVEL: %08x\n"
	"RETC:		%04x\n",
	(unsigned int)Action_Config.action_type,
	(unsigned int)Action_Config.release_level,
	(unsigned int)act_reg.Control.Retc);

    // Processing Phase .....
    // Fill the memory with 'c' characters
    memset(din_gmem,  'c', sizeof(din_gmem[0]));
    printf("Input is : %s\n", (char *)((unsigned long)din_gmem + 0));

    // set flags != 0 to have action processed
    act_reg.Control.flags = 0x1; /* just not 0x0 */

    act_reg.Data.in.addr = 0;
    act_reg.Data.in.size = 64;
    act_reg.Data.in.type = SNAP_ADDRTYPE_HOST_DRAM;

    act_reg.Data.out.addr = 0;
    act_reg.Data.out.size = 64;
    act_reg.Data.out.type = SNAP_ADDRTYPE_HOST_DRAM;

    printf("Action call \n");
    hls_action(din_gmem, dout_gmem, &act_reg, &Action_Config);
    if (act_reg.Control.Retc == SNAP_RETC_FAILURE) {
	fprintf(stderr, " ==> RETURN CODE FAILURE <==\n");
	return 1;
    }

    printf("Output is : %s\n", (char *)((unsigned long)dout_gmem + 0));

    printf(">> ACTION TYPE = %08lx - RELEASE_LEVEL = %08lx <<\n",
		    (unsigned int)Action_Config.action_type,
		    (unsigned int)Action_Config.release_level);
    return 0;
}

#endif
