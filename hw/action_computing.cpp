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
#include <stdint.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

static void Fill(uint8_t* dst, int value, int size) {
//#pragma HLS inline
  int i,j;
  for (j = 0; j < size; ++j) {
//#pragma HLS unroll
    for(i = 0; i < size; ++i){
//#pragma HLS unroll
        dst[j * size + i] = value;
    }
  }
}

static void DCMode(uint8_t* dst, uint8_t* left, uint8_t* top,
                               int size, int round, int shift, int x, int y) {
  int DC = 0;
  int j;

  if (x != 0) {
    if (y != 0) {
  	  for (j = 0; j < size; ++j){
//#pragma HLS unroll
	    DC += top[j] + left[j];
	  }
	  DC = (DC + round) >> shift;
	  Fill(dst, DC, size);
    } else {
      for (j = 0; j < size; ++j){
//#pragma HLS unroll
		DC += left[j];
	  }
      DC = (DC + (round>>1)) >> (shift-1);
	  Fill(dst, DC, size);
    }
  } else {
    if (y != 0) {
      for (j = 0; j < size; ++j){
//#pragma HLS unroll
		DC += top[j];
	  }
	  DC = (DC + (round>>1)) >> (shift-1);
	  Fill(dst, DC, size);
    } else {
      Fill(dst, 0x80, size);
    }
  }
}

static void VerticalPred(uint8_t* dst, uint8_t* top, int size) {
  int i,j;
    for (j = 0; j < size; ++j) {
//#pragma HLS unroll
    	for(i = 0; i < size; ++i){
//#pragma HLS unroll
    		dst[j * size + i] = top[i];
    	}
    }
}

static void HorizontalPred(uint8_t* dst, uint8_t* left, int size) {
    int i,j;
    for (j = 0; j < size; ++j) {
//#pragma HLS unroll
    	for(i = 0; i < size; ++i){
//#pragma HLS unroll
    		dst[j * size + i] = left[j];
    	}
    }
}

static void TrueMotion(uint8_t* dst, uint8_t* left, uint8_t* top, uint8_t top_left, int size, int x, int y) {
  int i,j;
  int tmp;
  if (x != 0) {
    if (y != 0) {
      for (j = 0; j < size; ++j) {
//#pragma HLS unroll
        for (i = 0; i < size; ++i) {
//#pragma HLS unroll
        	tmp = top[i] + left[j] - top_left;
        	dst[j * size + i] = (!(tmp & ~0xff)) ? (uint8_t)tmp : (tmp < 0) ? 0 : 255;
        }
      }
    } else {
      HorizontalPred(dst, left, size);
    }
  } else {
    // true motion without left samples (hence: with default 129 value)
    // is equivalent to VE prediction where you just copy the top samples.
    // Note that if top samples are not available, the default value is
    // then 129, and not 127 as in the VerticalPred case.
    if (y != 0) {
      VerticalPred(dst, top, size);
    } else {
      Fill(dst, 129, size);
    }
  }
}

static void Intra16Preds_C(uint8_t YPred[4][16*16], uint8_t left_y[16],
		uint8_t* top_y, uint8_t top_left_y, int x, int y) {
////#pragma HLS PIPELINE
//#pragma HLS ARRAY_PARTITION variable=top_y complete dim=1
//#pragma HLS ARRAY_PARTITION variable=left_y complete dim=1
//#pragma HLS ARRAY_PARTITION variable=YPred complete dim=0
  DCMode(YPred[0], left_y, top_y, 16, 16, 5, x, y);
  VerticalPred(YPred[2], top_y, 16);
  HorizontalPred(YPred[3], left_y, 16);
  TrueMotion(YPred[1], left_y, top_y, top_left_y, 16, x, y);
}

static void IntraChromaPreds_C(
		uint8_t UVPred[8][8*8],
        uint8_t left_u[8], uint8_t top_u[8], uint8_t top_left_u,
		uint8_t left_v[8], uint8_t top_v[8], uint8_t top_left_v,
		int x, int y) {
////#pragma HLS PIPELINE
//#pragma HLS ARRAY_PARTITION variable=top_u complete dim=1
//#pragma HLS ARRAY_PARTITION variable=left_u complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top_v complete dim=1
//#pragma HLS ARRAY_PARTITION variable=left_v complete dim=1
//#pragma HLS ARRAY_PARTITION variable=UVPred complete dim=0
  // U block
  DCMode(UVPred[0], left_u, top_u, 8, 8, 4, x, y);
  VerticalPred(UVPred[2], top_u, 8);
  HorizontalPred(UVPred[3], left_u, 8);
  TrueMotion(UVPred[1], left_u, top_u, top_left_u, 8, x, y);
  // V block
  DCMode(UVPred[4], left_v, top_v, 8, 8, 4, x, y);
  VerticalPred(UVPred[6], top_v, 8);
  HorizontalPred(UVPred[7], left_v, 8);
  TrueMotion(UVPred[5], left_v, top_v, top_left_v, 8, x, y);
}

// luma 4x4 prediction

#define DST(x, y) dst[(x) + (y) * 4]
#define AVG3(a, b, c) ((uint8_t)(((a) + 2 * (b) + (c) + 2) >> 2))
#define AVG2(a, b) (((a) + (b) + 1) >> 1)

static void VE4(uint8_t* dst, uint8_t top_left, uint8_t* top, uint8_t* top_right) {    // vertical
  uint8_t vals[4] = {
    AVG3(top_left, top[0], top[1]),
    AVG3(top[0], top[1], top[2]),
    AVG3(top[1], top[2], top[3]),
    AVG3(top[2], top[3], top_right[0])
  };
  int i,j;
    for (j = 0; j < 4; ++j) {
//#pragma HLS unroll
    	for(i = 0; i < 4; ++i){
//#pragma HLS unroll
    		dst[j * 4 + i] = vals[i];
    	}
    }
}

static void HE4(uint8_t* dst, uint8_t* left, uint8_t top_left) {    // horizontal
  const int X = top_left;
  const int I = left[0];
  const int J = left[1];
  const int K = left[2];
  const int L = left[3];
  ((uint32_t*)dst)[0] = 0x01010101U * AVG3(X, I, J);
  ((uint32_t*)dst)[1] = 0x01010101U * AVG3(I, J, K);
  ((uint32_t*)dst)[2] = 0x01010101U * AVG3(J, K, L);
  ((uint32_t*)dst)[3] = 0x01010101U * AVG3(K, L, L);
}

static void DC4(uint8_t* dst, uint8_t* top, uint8_t* left) {
  uint32_t dc = 4;
  int i;
  for (i = 0; i < 4; ++i){
//#pragma HLS unroll
	  dc += top[i] + left[i];
  }
  dc = dc >> 3;
  Fill(dst, dc, 4);
}

static void RD4(uint8_t* dst, uint8_t* left, uint8_t top_left, uint8_t* top) {
  const int X = top_left;
  const int I = left[0];
  const int J = left[1];
  const int K = left[2];
  const int L = left[3];
  const int A = top[0];
  const int B = top[1];
  const int C = top[2];
  const int D = top[3];
  DST(0, 3)                                     = AVG3(J, K, L);
  DST(0, 2) = DST(1, 3)                         = AVG3(I, J, K);
  DST(0, 1) = DST(1, 2) = DST(2, 3)             = AVG3(X, I, J);
  DST(0, 0) = DST(1, 1) = DST(2, 2) = DST(3, 3) = AVG3(A, X, I);
  DST(1, 0) = DST(2, 1) = DST(3, 2)             = AVG3(B, A, X);
  DST(2, 0) = DST(3, 1)                         = AVG3(C, B, A);
  DST(3, 0)                                     = AVG3(D, C, B);
}

static void LD4(uint8_t* dst, uint8_t* top, uint8_t* top_right) {
  const int A = top[0];
  const int B = top[1];
  const int C = top[2];
  const int D = top[3];
  const int E = top_right[0];
  const int F = top_right[1];
  const int G = top_right[2];
  const int H = top_right[3];
  DST(0, 0)                                     = AVG3(A, B, C);
  DST(1, 0) = DST(0, 1)                         = AVG3(B, C, D);
  DST(2, 0) = DST(1, 1) = DST(0, 2)             = AVG3(C, D, E);
  DST(3, 0) = DST(2, 1) = DST(1, 2) = DST(0, 3) = AVG3(D, E, F);
  DST(3, 1) = DST(2, 2) = DST(1, 3)             = AVG3(E, F, G);
  DST(3, 2) = DST(2, 3)                         = AVG3(F, G, H);
  DST(3, 3)                                     = AVG3(G, H, H);
}

static void VR4(uint8_t* dst, uint8_t* left, uint8_t top_left, uint8_t* top) {
  const int X = top_left;
  const int I = left[0];
  const int J = left[1];
  const int K = left[2];
  const int A = top[0];
  const int B = top[1];
  const int C = top[2];
  const int D = top[3];
  DST(0, 0) = DST(1, 2) = AVG2(X, A);
  DST(1, 0) = DST(2, 2) = AVG2(A, B);
  DST(2, 0) = DST(3, 2) = AVG2(B, C);
  DST(3, 0)             = AVG2(C, D);

  DST(0, 3) =             AVG3(K, J, I);
  DST(0, 2) =             AVG3(J, I, X);
  DST(0, 1) = DST(1, 3) = AVG3(I, X, A);
  DST(1, 1) = DST(2, 3) = AVG3(X, A, B);
  DST(2, 1) = DST(3, 3) = AVG3(A, B, C);
  DST(3, 1) =             AVG3(B, C, D);
}

static void VL4(uint8_t* dst, uint8_t* top, uint8_t* top_right) {
  const int A = top[0];
  const int B = top[1];
  const int C = top[2];
  const int D = top[3];
  const int E = top_right[0];
  const int F = top_right[1];
  const int G = top_right[2];
  const int H = top_right[3];
  DST(0, 0) =             AVG2(A, B);
  DST(1, 0) = DST(0, 2) = AVG2(B, C);
  DST(2, 0) = DST(1, 2) = AVG2(C, D);
  DST(3, 0) = DST(2, 2) = AVG2(D, E);

  DST(0, 1) =             AVG3(A, B, C);
  DST(1, 1) = DST(0, 3) = AVG3(B, C, D);
  DST(2, 1) = DST(1, 3) = AVG3(C, D, E);
  DST(3, 1) = DST(2, 3) = AVG3(D, E, F);
              DST(3, 2) = AVG3(E, F, G);
              DST(3, 3) = AVG3(F, G, H);
}

static void HU4(uint8_t* dst, uint8_t* left) {
  const int I = left[0];
  const int J = left[1];
  const int K = left[2];
  const int L = left[3];
  DST(0, 0) =             AVG2(I, J);
  DST(2, 0) = DST(0, 1) = AVG2(J, K);
  DST(2, 1) = DST(0, 2) = AVG2(K, L);
  DST(1, 0) =             AVG3(I, J, K);
  DST(3, 0) = DST(1, 1) = AVG3(J, K, L);
  DST(3, 1) = DST(1, 2) = AVG3(K, L, L);
  DST(3, 2) = DST(2, 2) =
  DST(0, 3) = DST(1, 3) = DST(2, 3) = DST(3, 3) = L;
}

static void HD4(uint8_t* dst, uint8_t* left, uint8_t top_left, uint8_t* top) {
  const int X = top_left;
  const int I = left[0];
  const int J = left[1];
  const int K = left[2];
  const int L = left[3];
  const int A = top[0];
  const int B = top[1];
  const int C = top[2];

  DST(0, 0) = DST(2, 1) = AVG2(I, X);
  DST(0, 1) = DST(2, 2) = AVG2(J, I);
  DST(0, 2) = DST(2, 3) = AVG2(K, J);
  DST(0, 3)             = AVG2(L, K);

  DST(3, 0)             = AVG3(A, B, C);
  DST(2, 0)             = AVG3(X, A, B);
  DST(1, 0) = DST(3, 1) = AVG3(I, X, A);
  DST(1, 1) = DST(3, 2) = AVG3(J, I, X);
  DST(1, 2) = DST(3, 3) = AVG3(K, J, I);
  DST(1, 3)             = AVG3(L, K, J);
}

static void TM4(uint8_t* dst, uint8_t* top, uint8_t* left, uint8_t top_left) {
  int i, j;
  int tmp;
  for (j = 0; j < 4; ++j) {
//#pragma HLS unroll
    for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
      tmp = top[i] + left[j] - top_left;
      dst[j * 4 + i] = (tmp>0xff) ? 0xff : (tmp<0) ? 0 : (uint8_t)tmp;
    }
  }
}

static void Intra4Preds_C(
		uint8_t Pred[10][16], uint8_t left[4], uint8_t top_left, uint8_t top[4], uint8_t top_right[4]) {
////#pragma HLS PIPELINE
//#pragma HLS ARRAY_PARTITION variable=left complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top_right complete dim=1
//#pragma HLS ARRAY_PARTITION variable=Pred complete dim=0

  DC4(Pred[0], top, left);
  TM4(Pred[1], top, left, top_left);
  VE4(Pred[2], top_left, top, top_right);
  HE4(Pred[3], left, top_left);
  RD4(Pred[4], left, top_left, top);
  VR4(Pred[5], left, top_left, top);
  LD4(Pred[6], top, top_right);
  VL4(Pred[7], top, top_right);
  HD4(Pred[8], left, top_left, top);
  HU4(Pred[9], left);
}

static void FTransform_C(const uint8_t* src, const uint8_t* ref, int16_t* out) {
  int i;
  int tmp[16];
  for (i = 0; i < 4; ++i, src += 4, ref += 4) {
//#pragma HLS unroll
    const int d0 = src[0] - ref[0];   // 9bit dynamic range ([-255,255])
    const int d1 = src[1] - ref[1];
    const int d2 = src[2] - ref[2];
    const int d3 = src[3] - ref[3];
    const int a0 = (d0 + d3);         // 10b                      [-510,510]
    const int a1 = (d1 + d2);
    const int a2 = (d1 - d2);
    const int a3 = (d0 - d3);
    tmp[0 + i * 4] = (a0 + a1) * 8;   // 14b                      [-8160,8160]
    tmp[1 + i * 4] = (a2 * 2217 + a3 * 5352 + 1812) >> 9;      // [-7536,7542]
    tmp[2 + i * 4] = (a0 - a1) * 8;
    tmp[3 + i * 4] = (a3 * 2217 - a2 * 5352 +  937) >> 9;
  }
  for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
    const int a0 = (tmp[0 + i] + tmp[12 + i]);  // 15b
    const int a1 = (tmp[4 + i] + tmp[ 8 + i]);
    const int a2 = (tmp[4 + i] - tmp[ 8 + i]);
    const int a3 = (tmp[0 + i] - tmp[12 + i]);
    out[0 + i] = (a0 + a1 + 7) >> 4;            // 12b
    out[4 + i] = ((a2 * 2217 + a3 * 5352 + 12000) >> 16) + (a3 != 0);
    out[8 + i] = (a0 - a1 + 7) >> 4;
    out[12+ i] = ((a3 * 2217 - a2 * 5352 + 51000) >> 16);
  }
}

static void FTransformWHT_C(const int16_t* in, int16_t* out) {
  // input is 12b signed
  int32_t tmp[16];
  int i;
  for (i = 0; i < 4; ++i, in += 4) {
//#pragma HLS unroll
    const int a0 = (in[0] + in[2]);  // 13b
    const int a1 = (in[1] + in[3]);
    const int a2 = (in[1] - in[3]);
    const int a3 = (in[0] - in[2]);
    tmp[0 + i * 4] = a0 + a1;   // 14b
    tmp[1 + i * 4] = a3 + a2;
    tmp[2 + i * 4] = a3 - a2;
    tmp[3 + i * 4] = a0 - a1;
  }
  for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
    const int a0 = (tmp[0 + i] + tmp[8 + i]);  // 15b
    const int a1 = (tmp[4 + i] + tmp[12+ i]);
    const int a2 = (tmp[4 + i] - tmp[12+ i]);
    const int a3 = (tmp[0 + i] - tmp[8 + i]);
    const int b0 = a0 + a1;    // 16b
    const int b1 = a3 + a2;
    const int b2 = a3 - a2;
    const int b3 = a0 - a1;
    out[ 0 + i] = b0 >> 1;     // 15b
    out[ 4 + i] = b1 >> 1;
    out[ 8 + i] = b2 >> 1;
    out[12 + i] = b3 >> 1;
  }
}

static const uint8_t kZigzag[16] = {
  0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15
};

#define QFIX 17

static int QUANTDIV(uint32_t n, uint32_t iQ, uint32_t B) {
  return (int)((n * iQ + B) >> QFIX);
}

static int QuantizeBlock_C(int16_t* in, int16_t* out,
                           const VP8Matrix* const mtx) {
  int last = -1;
  int n;
  int16_t in_tmp[16];
  for (n = 0; n < 16; ++n) {
//#pragma HLS unroll
    const int j = kZigzag[n];
    const int sign = (in[j] < 0);
    const uint32_t coeff = (sign ? -in[j] : in[j]) + mtx->sharpen_[j];
    if (coeff > mtx->zthresh_[j]) {
      const uint32_t Q = mtx->q_[j];
      const uint32_t iQ = mtx->iq_[j];
      const uint32_t B = mtx->bias_[j];
      int level = QUANTDIV(coeff, iQ, B);
      if (level > MAX_LEVEL) level = MAX_LEVEL;
      if (sign) level = -level;
      in_tmp[j] = level * (int)Q;
      out[n] = level;
      if (level) last = n;
    } else {
      out[n] = 0;
      in_tmp[j] = 0;
    }
  }
  for (n = 0; n < 16; ++n) {
//#pragma HLS unroll
	in[n] = in_tmp[n];
  }
  return (last >= 0);
}



static void TransformWHT_C(const int16_t* in, int16_t* out) {
  int tmp[16];
  int i;
  for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
    const int a0 = in[0 + i] + in[12 + i];
    const int a1 = in[4 + i] + in[ 8 + i];
    const int a2 = in[4 + i] - in[ 8 + i];
    const int a3 = in[0 + i] - in[12 + i];
    tmp[0  + i] = a0 + a1;
    tmp[8  + i] = a0 - a1;
    tmp[4  + i] = a3 + a2;
    tmp[12 + i] = a3 - a2;
  }
  for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
    const int dc = tmp[0 + i * 4] + 3;    // w/ rounder
    const int a0 = dc             + tmp[3 + i * 4];
    const int a1 = tmp[1 + i * 4] + tmp[2 + i * 4];
    const int a2 = tmp[1 + i * 4] - tmp[2 + i * 4];
    const int a3 = dc             - tmp[3 + i * 4];
    out[0] = (a0 + a1) >> 3;
    out[1] = (a3 + a2) >> 3;
    out[2] = (a0 - a1) >> 3;
    out[3] = (a3 - a2) >> 3;
    out += 4;
  }
}

typedef int16_t fixed_t;

uint8_t clip_8b(fixed_t v) {
  return (!(v & ~0xff)) ? v : (v < 0) ? 0 : 255;
}

#define STORE(x, y, v) \
  dst[(x) + (y) * 4] = clip_8b(ref[(x) + (y) * 4] + ((v) >> 3))

static const int kC1 = 20091 + (1 << 16);
static const int kC2 = 35468;
#define MUL(a, b) (((a) * (b)) >> 16)

static void ITransformOne(const uint8_t* ref, const int16_t* in,
                                      uint8_t* dst) {
  int C[4 * 4], *tmp;
  int i;
  tmp = C;
  for (i = 0; i < 4; ++i) {    // vertical pass
//#pragma HLS unroll
    const int a = in[0] + in[8];
    const int b = in[0] - in[8];
    const int c = MUL(in[4], kC2) - MUL(in[12], kC1);
    const int d = MUL(in[4], kC1) + MUL(in[12], kC2);
    tmp[0] = a + d;
    tmp[1] = b + c;
    tmp[2] = b - c;
    tmp[3] = a - d;
    tmp += 4;
    in++;
  }

  tmp = C;
  for (i = 0; i < 4; ++i) {    // horizontal pass
//#pragma HLS unroll
    const int dc = tmp[0] + 4;
    const int a =  dc +  tmp[8];
    const int b =  dc -  tmp[8];
    const int c = MUL(tmp[4], kC2) - MUL(tmp[12], kC1);
    const int d = MUL(tmp[4], kC1) + MUL(tmp[12], kC2);
    STORE(0, i, a + d);
    STORE(1, i, b + c);
    STORE(2, i, b - c);
    STORE(3, i, a - d);
    tmp++;
  }
}


static int ReconstructIntra16(
		const uint8_t YPred[16*16], const uint8_t Ysrc[16*16], uint8_t Yout[16*16],
		int16_t y_ac_levels[16][16], int16_t y_dc_levels[16], VP8Matrix y1, VP8Matrix y2) {
////#pragma HLS PIPELINE
//#pragma HLS ARRAY_PARTITION variable=y1.sharpen_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y1.zthresh_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y1.bias_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y1.iq_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y1.q_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y2.sharpen_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y2.zthresh_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y2.bias_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y2.iq_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y2.q_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y_ac_levels complete dim=0
//#pragma HLS ARRAY_PARTITION variable=y_dc_levels complete dim=1
//#pragma HLS ARRAY_PARTITION variable=Yout complete dim=1
//#pragma HLS ARRAY_PARTITION variable=Ysrc complete dim=1
//#pragma HLS ARRAY_PARTITION variable=YPred complete dim=1

  int nz = 0;
  int n;
  int16_t tmp[16][16], dc_tmp[16], tmp_dc[16];
  uint8_t tmp_src[16][16], tmp_pred[16][16], tmp_out[16][16];

  const uint16_t VP8Scan[16] = {  // Luma
    0 +  0 * 16,  4 +  0 * 16, 8 +  0 * 16, 12 +  0 * 16,
    0 +  4 * 16,  4 +  4 * 16, 8 +  4 * 16, 12 +  4 * 16,
    0 +  8 * 16,  4 +  8 * 16, 8 +  8 * 16, 12 +  8 * 16,
    0 + 12 * 16,  4 + 12 * 16, 8 + 12 * 16, 12 + 12 * 16,
  };

//#pragma HLS ARRAY_PARTITION variable=tmp_src complete dim=0
//#pragma HLS ARRAY_PARTITION variable=tmp_dc complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dc_tmp complete dim=1
//#pragma HLS ARRAY_PARTITION variable=tmp_out complete dim=0
//#pragma HLS ARRAY_PARTITION variable=tmp_pred complete dim=0
//#pragma HLS ARRAY_PARTITION variable=tmp complete dim=0
//#pragma HLS ARRAY_PARTITION variable=VP8Scan complete dim=1

  int i,j;
  for(n = 0; n < 16; n++){
//#pragma HLS unroll
	  for(j = 0; j < 4; j++){
//#pragma HLS unroll
		  for(i = 0; i < 4; i++){
//#pragma HLS unroll
			  tmp_src[n][j * 4 + i] = Ysrc[VP8Scan[n] + j * 16 + i];
			  tmp_pred[n][j * 4 + i] = YPred[VP8Scan[n] + j * 16 + i];
		  }
	  }
  }

  for (n = 0; n < 16; n++) {
//#pragma HLS unroll
	  FTransform_C(tmp_src[n], tmp_pred[n], tmp[n]);
  }

  for(n = 0; n < 16; n++){
//#pragma HLS unroll
	  tmp_dc[n] = tmp[n][0];
	  tmp[n][0] = 0;
  }

  FTransformWHT_C(tmp_dc, dc_tmp);

  nz |= QuantizeBlock_C(dc_tmp, y_dc_levels, &y2) << 24;

  for (n = 0; n < 16; n++) {
//#pragma HLS unroll
    // Zero-out the first coeff, so that: a) nz is correct below, and
    // b) finding 'last' non-zero coeffs in SetResidualCoeffs() is simplified.
    nz |= QuantizeBlock_C(tmp[n],y_ac_levels[n], &y1) << n;

  }

  TransformWHT_C(dc_tmp, tmp_dc);

  for(n = 0; n < 16; n++){
//#pragma HLS unroll
	  tmp[n][0] = tmp_dc[n];
  }

  for (n = 0; n < 16; n++) {
//#pragma HLS unroll
	  ITransformOne(tmp_pred[n], tmp[n], tmp_out[n]);
  }

  for(n = 0; n < 16; n++){
//#pragma HLS unroll
	  for(j = 0; j < 4; j++){
//#pragma HLS unroll
		  for(i = 0; i < 4; i++){
//#pragma HLS unroll
			  Yout[VP8Scan[n] + j * 16 + i] = tmp_out[n][j * 4 + i];
		  }
	  }
  }

  return nz;
}

static int ReconstructIntra4(int16_t levels[16], const uint8_t y_p[16],
		const uint8_t y_src[16], uint8_t y_out[16], VP8Matrix y1) {
////#pragma HLS PIPELINE
//#pragma HLS ARRAY_PARTITION variable=y1.sharpen_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y1.zthresh_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y1.bias_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y1.iq_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y1.q_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=levels complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y_out complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y_src complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y_p complete dim=1
  int nz = 0;
  int16_t tmp[16];
//#pragma HLS ARRAY_PARTITION variable=tmp complete dim=1

  FTransform_C(y_src, y_p, tmp);

  nz = QuantizeBlock_C(tmp, levels, &y1);

  ITransformOne(y_p, tmp, y_out);

  return nz;
}

#define C1 7    // fraction of error sent to the 4x4 block below
#define C2 8    // fraction of error sent to the 4x4 block on the right
#define DSHIFT 4
#define DSCALE 1   // storage descaling, needed to make the error fit int8_t

static int QuantizeSingle(int16_t* const v, const VP8Matrix* const mtx) {
  int V = *v;
  const int sign = (V < 0);
  if (sign) V = -V;
  if (V > (int)mtx->zthresh_[0]) {
    const uint32_t n = V;
    const uint32_t iQ = mtx->iq_[0];
    const uint32_t B = mtx->bias_[0];
    const int qV = QUANTDIV(n, iQ, B) * mtx->q_[0];
    const int err = (V - qV);
    *v = sign ? -qV : qV;
    return (sign ? -err : err) >> DSCALE;
  } else {
  *v = 0;
  return (sign ? -V : V) >> DSCALE;
  }
}

static void CorrectDCValues(DError top_derr[1024], DError left_derr, int x,
                            const VP8Matrix* const mtx,
                            int16_t tmp[][16], int8_t derr[2][3]) {
  //         | top[0] | top[1]
  // --------+--------+---------
  // left[0] | tmp[0]   tmp[1]  <->   err0 err1
  // left[1] | tmp[2]   tmp[3]        err2 err3
  //
  // Final errors {err1,err2,err3} are preserved and later restored
  // as top[]/left[] on the next block.
  int ch;
  for (ch = 0; ch <= 1; ++ch) {
//#pragma HLS unroll
    const int8_t* const top = top_derr[x][ch];
    const int8_t* const left = left_derr[ch];
    int16_t (* const c)[16] = &tmp[ch * 4];
    int err0, err1, err2, err3;
    c[0][0] += (C1 * top[0] + C2 * left[0]) >> (DSHIFT - DSCALE);
    err0 = QuantizeSingle(&c[0][0], mtx);
    c[1][0] += (C1 * top[1] + C2 * err0) >> (DSHIFT - DSCALE);
    err1 = QuantizeSingle(&c[1][0], mtx);
    c[2][0] += (C1 * err0 + C2 * left[1]) >> (DSHIFT - DSCALE);
    err2 = QuantizeSingle(&c[2][0], mtx);
    c[3][0] += (C1 * err1 + C2 * err2) >> (DSHIFT - DSCALE);
    err3 = QuantizeSingle(&c[3][0], mtx);
    // error 'err' is bounded by mtx->q_[0] which is 132 at max. Hence
    // err >> DSCALE will fit in an int8_t type if DSCALE>=1.
    derr[ch][0] = (int8_t)err1;
    derr[ch][1] = (int8_t)err2;
    derr[ch][2] = (int8_t)err3;
  }
}

#undef C1
#undef C2

static int ReconstructUV(int16_t uv_levels[8][16],const uint8_t uv_p[8*16],
		const uint8_t uv_src[8*16], uint8_t uv_out[8*16], VP8Matrix uv,
		DError top_derr[1024], DError left_derr, int x, int8_t derr[2][3]) {
////#pragma HLS PIPELINE
//#pragma HLS ARRAY_PARTITION variable=uv.sharpen_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=uv.zthresh_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=uv.bias_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=uv.iq_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=uv.q_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=uv_levels complete dim=0
//#pragma HLS ARRAY_PARTITION variable=uv_out complete dim=1
//#pragma HLS ARRAY_PARTITION variable=uv_src complete dim=1
//#pragma HLS ARRAY_PARTITION variable=uv_p complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top_derr complete dim=2
//#pragma HLS ARRAY_PARTITION variable=top_derr complete dim=3
//#pragma HLS ARRAY_PARTITION variable=left_derr complete dim=0
//#pragma HLS ARRAY_PARTITION variable=derr complete dim=0
  int nz = 0;
  int n;
  int16_t tmp[8][16];
  uint8_t tmp_src[8][16], tmp_p[8][16], tmp_out[8][16];
//#pragma HLS ARRAY_PARTITION variable=tmp_src complete dim=0
//#pragma HLS ARRAY_PARTITION variable=tmp_out complete dim=0
//#pragma HLS ARRAY_PARTITION variable=tmp_p complete dim=0
//#pragma HLS ARRAY_PARTITION variable=tmp complete dim=0

  static const uint16_t VP8ScanUV[4 + 4] = {
    0 + 0 * 16,   4 + 0 * 16, 0 + 4 * 16,  4 + 4 * 16,    // U
    8 + 0 * 16,  12 + 0 * 16, 8 + 4 * 16, 12 + 4 * 16     // V
  };

  int i,j;
  for(n = 0; n < 8; n++){
//#pragma HLS unroll
	  for(j = 0; j < 4; j++){
//#pragma HLS unroll
		  for(i = 0; i < 4; i++){
//#pragma HLS unroll
			  tmp_src[n][j * 4 + i] = uv_src[VP8ScanUV[n] + j * 16 + i];
			  tmp_p[n][j * 4 + i] = uv_p[VP8ScanUV[n] + j * 16 + i];
		  }
	  }
  }

  for (n = 0; n < 8; n++) {
//#pragma HLS unroll
	  FTransform_C(tmp_src[n], tmp_p[n], tmp[n]);
  }

  CorrectDCValues(top_derr, left_derr, x, &uv, tmp, derr);


  for (n = 0; n < 8; n++) {
//#pragma HLS unroll
    nz |= QuantizeBlock_C(tmp[n], uv_levels[n], &uv) << n;
  }

  for (n = 0; n < 8; n++) {
//#pragma HLS unroll
	  ITransformOne(tmp_p[n], tmp[n], tmp_out[n]);
  }

  for(n = 0; n < 8; n++){
//#pragma HLS unroll
	  for(j = 0; j < 4; j++){
//#pragma HLS unroll
		  for(i = 0; i < 4; i++){
//#pragma HLS unroll
			  uv_out[VP8ScanUV[n] + j * 16 + i] = tmp_out[n][j * 4 + i];
		  }
	  }
  }

  return (nz << 16);
}

static const uint16_t kWeightY[16] = {
  38, 32, 20, 9, 32, 28, 17, 7, 20, 17, 10, 4, 9, 7, 4, 2
};

#define FLATNESS_LIMIT_I16 10      // I16 mode
#define FLATNESS_PENALTY   140     // roughly ~1bit per block

static int GetSSE(const uint8_t* a, const uint8_t* b,
                              int w, int h) {
  int count = 0;
  int y, x;
  for (y = 0; y < h; ++y) {
//#pragma HLS unroll
    for (x = 0; x < w; ++x) {
//#pragma HLS unroll
      const int diff = (int)a[x + y * w] - b[x + y * w];
      count += diff * diff;
    }
  }
  return count;
}

static int SSE16x16_C(const uint8_t* a, const uint8_t* b) {
////#pragma HLS INLINE off
  return GetSSE(a, b, 16, 16);
}

#define MULT_8B(a, b) (((a) * (b) + 128) >> 8)

static int TTransform(const uint8_t* in, const uint16_t* w) {
  int sum = 0;
  int tmp[16];
  int i;
  // horizontal pass
  for (i = 0; i < 4; ++i, in += 4) {
//#pragma HLS unroll
    const int a0 = in[0] + in[2];
    const int a1 = in[1] + in[3];
    const int a2 = in[1] - in[3];
    const int a3 = in[0] - in[2];
    tmp[0 + i * 4] = a0 + a1;
    tmp[1 + i * 4] = a3 + a2;
    tmp[2 + i * 4] = a3 - a2;
    tmp[3 + i * 4] = a0 - a1;
  }
  // vertical pass
  for (i = 0; i < 4; ++i, ++w) {
//#pragma HLS unroll
    const int a0 = tmp[0 + i] + tmp[8 + i];
    const int a1 = tmp[4 + i] + tmp[12+ i];
    const int a2 = tmp[4 + i] - tmp[12+ i];
    const int a3 = tmp[0 + i] - tmp[8 + i];
    const int b0 = a0 + a1;
    const int b1 = a3 + a2;
    const int b2 = a3 - a2;
    const int b3 = a0 - a1;

    sum += w[ 0] * abs(b0);
    sum += w[ 4] * abs(b1);
    sum += w[ 8] * abs(b2);
    sum += w[12] * abs(b3);
  }
  return sum;
}

static int Disto4x4_C(const uint8_t* const a, const uint8_t* const b,
                      const uint16_t* const w) {
  const int sum1 = TTransform(a, w);
  const int sum2 = TTransform(b, w);
  return abs(sum2 - sum1) >> 5;
}

static int Disto16x16_C(const uint8_t* const a, const uint8_t* const b,
                        const uint16_t* const w) {
////#pragma HLS INLINE off
  int D = 0;

  uint8_t tmp_a[16][16], tmp_b[16][16];
//#pragma HLS ARRAY_PARTITION variable=tmp_a complete dim=0
//#pragma HLS ARRAY_PARTITION variable=tmp_b complete dim=0

  const uint16_t VP8Scan[16] = {
    0 +  0 * 16,  4 +  0 * 16, 8 +  0 * 16, 12 +  0 * 16,
    0 +  4 * 16,  4 +  4 * 16, 8 +  4 * 16, 12 +  4 * 16,
    0 +  8 * 16,  4 +  8 * 16, 8 +  8 * 16, 12 +  8 * 16,
    0 + 12 * 16,  4 + 12 * 16, 8 + 12 * 16, 12 + 12 * 16,
  };

  int i,j,n;
  for(n = 0; n < 16; n++){
//#pragma HLS unroll
	  for(j = 0; j < 4; j++){
//#pragma HLS unroll
		  for(i = 0; i < 4; i++){
//#pragma HLS unroll
			  tmp_a[n][j * 4 + i] = a[VP8Scan[n] + j * 16 + i];
			  tmp_b[n][j * 4 + i] = b[VP8Scan[n] + j * 16 + i];
		  }
	  }
  }


    for (i = 0; i < 16; i++) {
//#pragma HLS unroll
      D += Disto4x4_C(tmp_a[i], tmp_b[i], w);
    }

  return D;

}

const uint16_t VP8FixedCostsI16[4] = { 663, 919, 872, 919 };

// intra prediction modes
enum { B_DC_PRED = 0,   // 4x4 modes
       B_TM_PRED = 1,
       B_VE_PRED = 2,
       B_HE_PRED = 3,
       B_RD_PRED = 4,
       B_VR_PRED = 5,
       B_LD_PRED = 6,
       B_VL_PRED = 7,
       B_HD_PRED = 8,
       B_HU_PRED = 9,
       NUM_BMODES = B_HU_PRED + 1 - B_DC_PRED,  // = 10

       // Luma16 or UV modes
       DC_PRED = B_DC_PRED, V_PRED = B_VE_PRED,
       H_PRED = B_HE_PRED, TM_PRED = B_TM_PRED,
       B_PRED = NUM_BMODES,   // refined I4x4 mode
       NUM_PRED_MODES = 4,

       // special modes
       B_DC_PRED_NOTOP = 4,
       B_DC_PRED_NOLEFT = 5,
       B_DC_PRED_NOTOPLEFT = 6,
       NUM_B_DC_MODES = 7 };

#define RD_DISTO_MULT      256  // distortion multiplier (equivalent of lambda)

static void SetRDScore(int lambda, VP8ModeScore* const rd) {
  rd->score = (rd->R + rd->H) * lambda + RD_DISTO_MULT * (rd->D + rd->SD);
}

static void StoreMaxDelta(VP8SegmentInfo* const dqm, const int16_t DCs[16]) {
  // We look at the first three AC coefficients to determine what is the average
  // delta between each sub-4x4 block.
  const int v0 = abs(DCs[1]);
  const int v1 = abs(DCs[2]);
  const int v2 = abs(DCs[4]);
  int max_v = (v1 > v0) ? v1 : v0;
  max_v = (v2 > max_v) ? v2 : max_v;
  if (max_v > dqm->max_edge_) dqm->max_edge_ = max_v;
}

static void CopyScore(VP8ModeScore* const dst, const VP8ModeScore* const src) {
  dst->D  = src->D;
  dst->SD = src->SD;
  dst->R  = src->R;
  dst->H  = src->H;
  dst->nz = src->nz;      // note that nz is not accumulated, but just copied.
  dst->score = src->score;
}


static int VP8GetCostLuma16(VP8ModeScore* rd_cur){
	int64_t test_R = 0;
	int y, x;
	for (y = 1; y < 16; ++y) {
	  for (x = 1; x < 16; ++x) {
	    test_R += rd_cur->y_ac_levels[y][x] * rd_cur->y_ac_levels[y][x];
	  }
	}
	for (y = 0; y < 16; ++y) {
	  test_R += rd_cur->y_dc_levels[y] * rd_cur->y_dc_levels[y];
	}
	return test_R << 10;
}

static void copy_16x16_int16(int16_t dst[16][16], int16_t src[16][16]){
	int y, x;
	for (y = 0; y < 16; ++y) {
//#pragma HLS unroll
		for (x = 0; x < 16; ++x) {
//#pragma HLS unroll
			dst[y][x] = src[y][x];
		}
	}
}

static void copy_16_int16(int16_t dst[16], int16_t src[16]){
	int y;
	for (y = 0; y < 16; ++y) {
//#pragma HLS unroll
		dst[y] = src[y];
	}

}

static void copy_256_uint8(uint8_t dst[16*16], uint8_t src[16*16]){
	int y;
	for (y = 0; y < 256; ++y) {
//#pragma HLS unroll
		dst[y] = src[y];
	}
}

static void copy_16_uint8(uint8_t dst[16], uint8_t src[16]){
	int y;
	for (y = 0; y < 16; ++y) {
//#pragma HLS unroll
		dst[y] = src[y];
	}
}

static void PickBestIntra16(uint8_t Yin[16*16], uint8_t Yout[16*16], uint8_t YPred[4][16*16],
		VP8ModeScore* rd, VP8SegmentInfo* const dqm) {

////#pragma HLS pipeline
//#pragma HLS ARRAY_PARTITION variable=Yout complete dim=1
//#pragma HLS ARRAY_PARTITION variable=Yin complete dim=1
//#pragma HLS ARRAY_PARTITION variable=YPred complete dim=0
//#pragma HLS ARRAY_PARTITION variable=rd->y_ac_levels complete dim=0
//#pragma HLS ARRAY_PARTITION variable=rd->y_dc_levels complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.sharpen_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.zthresh_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.bias_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.iq_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.q_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y2_.sharpen_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y2_.zthresh_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y2_.bias_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y2_.iq_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y2_.q_ complete dim=1

  const int kNumBlocks = 16;
  //VP8SegmentInfo* const dqm = &it->enc_->dqm_[it->mb_->segment_];
  const int lambda = dqm->lambda_i16_;
  const int tlambda = dqm->tlambda_;
  const uint8_t* const src = Yin;
  VP8ModeScore rd_tmp;
  VP8ModeScore* rd_cur = &rd_tmp;
  VP8ModeScore* rd_best = rd;
  int mode;
  uint8_t Yout_tmp[16*16];

  //rd->mode_i16 = -1;
  for (mode = 0; mode < NUM_PRED_MODES; ++mode) {
    uint8_t* const tmp_dst = Yout_tmp;  // scratch buffer
    rd_cur->mode_i16 = mode;

    // Reconstruct
    rd_cur->nz = ReconstructIntra16(YPred[mode], src, tmp_dst, rd_cur->y_ac_levels,
    		rd_cur->y_dc_levels, dqm->y1_, dqm->y2_);

    // Measure RD-score
    rd_cur->D = SSE16x16_C(src, tmp_dst);
    rd_cur->SD = MULT_8B(tlambda, Disto16x16_C(src, tmp_dst, kWeightY));
    rd_cur->H = VP8FixedCostsI16[mode];
	rd_cur->R = VP8GetCostLuma16(rd_cur);

    // Since we always examine Intra16 first, we can overwrite *rd directly.
    SetRDScore(lambda, rd_cur);

    if (rd_cur->score < rd_best->score) {
      rd_best->mode_i16	= rd_cur->mode_i16;
      CopyScore(rd_best, rd_cur);
	  copy_16x16_int16(rd_best->y_ac_levels, rd_cur->y_ac_levels);
	  copy_16_int16(rd_best->y_dc_levels, rd_cur->y_dc_levels);
	  copy_256_uint8(Yout, tmp_dst);
    }
  }

  SetRDScore(dqm->lambda_mode_, rd);   // finalize score for mode decision.

  // we have a blocky macroblock (only DCs are non-zero) with fairly high
  // distortion, record max delta so we can later adjust the minimal filtering
  // strength needed to smooth these blocks out.
  if ((rd->nz & 0x100ffff) == 0x1000000 && rd->D > dqm->min_disto_) {
    StoreMaxDelta(dqm, rd->y_dc_levels);
  }
}

#define MAX_COST ((score_t)0x7fffffffffffffLL)

static void InitScore(VP8ModeScore* const rd) {
  rd->D  = 0;
  rd->SD = 0;
  rd->R  = 0;
  rd->H  = 0;
  rd->nz = 0;
  rd->score = MAX_COST;
}

const uint16_t VP8FixedCostsI4[NUM_BMODES] =
   {   40, 1151, 1723, 1874, 2103, 2019, 1628, 1777, 2226, 2137 };

static int SSE4x4_C(const uint8_t* a, const uint8_t* b) {
  return GetSSE(a, b, 4, 4);
}

static void AddScore(VP8ModeScore* const dst, const VP8ModeScore* const src) {
  dst->D  += src->D;
  dst->SD += src->SD;
  dst->R  += src->R;
  dst->H  += src->H;
  dst->nz |= src->nz;     // here, new nz bits are accumulated.
  dst->score += src->score;
}

static int VP8IteratorRotateI4(uint8_t y_left[16], uint8_t y_top_left, uint8_t y_top[20], int* i4_, uint8_t top_mem[16],
                        uint8_t yuv_out[16][16], uint8_t left[4], uint8_t* top_left, uint8_t top[4], uint8_t top_right[4]) {
  const uint8_t* const blk = yuv_out[*i4_];
  int i;

  switch(*i4_){
  case 0 :
		*top_left = y_top[3];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = blk[3+4*i];
			top[i] = y_top[4+i];
			top_right[i] = y_top[8+i];
			top_mem[i] = blk[12+i];
		}
		break;
  case 1 :
		*top_left = y_top[7];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = blk[3+4*i];
			top[i] = y_top[8+i];
			top_right[i] = y_top[12+i];
			top_mem[4+i] = blk[12+i];
		}
		break;
  case 2 :
  		*top_left = y_top[11];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = blk[3+4*i];
			top[i] = y_top[12+i];
			top_right[i] = y_top[16+i];
			top_mem[8+i] = blk[12+i];
		}
		break;
  case 3 :
  		*top_left = y_left[3];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = y_left[4+i];
			top[i] = top_mem[i];
			top_right[i] = top_mem[4+i];
			top_mem[12+i] = blk[12+i];
		}
		break;
  case 4 :
		*top_left = top_mem[3];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = blk[3+4*i];
			top[i] = top_mem[4+i];
			top_right[i] = top_mem[8+i];
			top_mem[i] = blk[12+i];
		}
		break;
  case 5 :
  		*top_left = top_mem[7];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = blk[3+4*i];
			top[i] = top_mem[8+i];
			top_right[i] = top_mem[12+i];
			top_mem[4+i] = blk[12+i];
		}
		break;
  case 6 :
  		*top_left = top_mem[11];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = blk[3+4*i];
			top[i] = top_mem[12+i];
			top_right[i] = y_top[16+i];
			top_mem[8+i] = blk[12+i];
		}
		break;
  case 7 :
  		*top_left = y_left[7];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = y_left[8+i];
			top[i] = top_mem[i];
			top_right[i] = top_mem[4+i];
			top_mem[12+i] = blk[12+i];
		}
		break;
  case 8 :
		*top_left = top_mem[3];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = blk[3+4*i];
			top[i] = top_mem[4+i];
			top_right[i] = top_mem[8+i];
			top_mem[i] = blk[12+i];
		}
		break;
  case 9 :
  		*top_left = top_mem[7];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = blk[3+4*i];
			top[i] = top_mem[8+i];
			top_right[i] = top_mem[12+i];
			top_mem[4+i] = blk[12+i];
		}
		break;
  case 10 :
  		*top_left = top_mem[11];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = blk[3+4*i];
			top[i] = top_mem[12+i];
			top_right[i] = y_top[16+i];
			top_mem[8+i] = blk[12+i];
		}
		break;
  case 11 :
  		*top_left = y_left[11];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = y_left[12+i];
			top[i] = top_mem[i];
			top_right[i] = top_mem[4+i];
			top_mem[12+i] = blk[12+i];
		}
		break;
  case 12 :
		*top_left = top_mem[3];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = blk[3+4*i];
			top[i] = top_mem[4+i];
			top_right[i] = top_mem[8+i];
		}
		break;
  case 13 :
		*top_left = top_mem[7];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = blk[3+4*i];
			top[i] = top_mem[8+i];
			top_right[i] = top_mem[12+i];
		}
		break;
  case 14 :
		*top_left = top_mem[11];
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			left[i] = blk[3+4*i];
			top[i] = top_mem[12+i];
			top_right[i] = y_top[16+i];
		}
		break;
  case 15 :
	  	return 0;
  }
  
  // move pointers to next sub-block
  ++(*i4_);
  
  return 1;
}

static int VP8GetCostLuma4(int16_t tmp_levels[16]){
	int64_t test_R = 0;
	int y;
	for (y = 0; y < 16; ++y) {
	  test_R += tmp_levels[y] * tmp_levels[y];
	}
	return test_R << 10;
}

static int PickBestIntra4(VP8SegmentInfo* const dqm, uint8_t Yin[16*16], uint8_t Yout[16*16],
		VP8ModeScore* const rd, uint8_t y_left[16], uint8_t y_top_left, uint8_t y_top[20], uint8_t* mbtype) {
//#pragma HLS ARRAY_PARTITION variable=Yout complete dim=1
//#pragma HLS ARRAY_PARTITION variable=Yin complete dim=1
//#pragma HLS ARRAY_PARTITION variable=rd->y_ac_levels complete dim=0
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.sharpen_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.zthresh_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.bias_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.iq_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.q_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y_left complete dim=1
//#pragma HLS ARRAY_PARTITION variable=y_top complete dim=1
  const int lambda = dqm->lambda_i4_;
  const int tlambda = dqm->tlambda_;
  const uint8_t* const src0 = Yin;
  uint8_t best_blocks[16][16];
  VP8ModeScore rd_best;
  uint8_t left[4], top_left, top[4], top_right[4];
  int i, j, n;
  int i4_ = 0;
  uint8_t top_mem[16];
  uint8_t src[16][16];
  const uint16_t VP8Scan[16] = {  // Luma
    0 +  0 * 16,  4 +  0 * 16, 8 +  0 * 16, 12 +  0 * 16,
    0 +  4 * 16,  4 +  4 * 16, 8 +  4 * 16, 12 +  4 * 16,
    0 +  8 * 16,  4 +  8 * 16, 8 +  8 * 16, 12 +  8 * 16,
    0 + 12 * 16,  4 + 12 * 16, 8 + 12 * 16, 12 + 12 * 16,
  };

//#pragma HLS ARRAY_PARTITION variable=best_blocks complete dim=0
//#pragma HLS ARRAY_PARTITION variable=VP8Scan complete dim=1
//#pragma HLS ARRAY_PARTITION variable=rd_best.y_ac_levels complete dim=0
//#pragma HLS ARRAY_PARTITION variable=left complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top_right complete dim=1
//#pragma HLS ARRAY_PARTITION variable=src complete dim=0

  top_left = y_top_left;
  for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
	  left[i] = y_left[i];
  }
  for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
	  top[i] = y_top[i];
  }
  for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
	  top_right[i] = y_top[4+i];
  }
	
  for(n = 0; n < 16; n++){
//#pragma HLS unroll
    for(j = 0; j < 4; j++){
//#pragma HLS unroll
	  for(i = 0; i < 4; i++){
//#pragma HLS unroll
		  src[n][j * 4 + i] = src0[VP8Scan[n] + j * 16 + i];
	  }
    }
  }


  InitScore(&rd_best);
  rd_best.H = 211;  // '211' is the value of VP8BitCost(0, 145)
  SetRDScore(dqm->lambda_mode_, &rd_best);

  do {
    const int kNumBlocks = 1;
    VP8ModeScore rd_i4;
    int mode;
    int best_mode = -1;
    const uint16_t* const mode_costs = VP8FixedCostsI4;
    uint8_t* best_block = best_blocks[i4_];
    uint8_t tmp_pred[10][16];    // scratch buffer.

//#pragma HLS ARRAY_PARTITION variable=VP8FixedCostsI4 complete dim=1
//#pragma HLS ARRAY_PARTITION variable=tmp_pred complete dim=0

    InitScore(&rd_i4);

    Intra4Preds_C(tmp_pred, left, top_left, top, top_right);

    for (mode = 0; mode < NUM_BMODES; ++mode) {
      VP8ModeScore rd_tmp;
      int16_t tmp_levels[16];
      uint8_t tmp_dst[10][16];
//#pragma HLS ARRAY_PARTITION variable=tmp_dst complete dim=0
//#pragma HLS ARRAY_PARTITION variable=tmp_levels complete dim=1
      // Reconstruct
      rd_tmp.nz =
          ReconstructIntra4(tmp_levels, tmp_pred[mode], src[i4_], tmp_dst[mode], dqm->y1_) << i4_;

      // Compute RD-score
      rd_tmp.D = SSE4x4_C(src[i4_], tmp_dst[mode]);
      rd_tmp.SD = MULT_8B(tlambda, Disto4x4_C(src[i4_], tmp_dst[mode], kWeightY));
      rd_tmp.H = mode_costs[mode];
	  rd_tmp.R = VP8GetCostLuma4(tmp_levels);

      SetRDScore(lambda, &rd_tmp);

      if (rd_tmp.score < rd_i4.score) {
        CopyScore(&rd_i4, &rd_tmp);
        best_mode = mode;
		copy_16_int16(rd_best.y_ac_levels[i4_], tmp_levels);
		copy_16_uint8(best_block, tmp_dst[mode]);
      }
    }
    SetRDScore(dqm->lambda_mode_, &rd_i4);
    AddScore(&rd_best, &rd_i4);
    if (rd_best.score >= rd->score) {
	  *mbtype = 1;
      return 0;
    }
    rd->modes_i4[i4_] = best_mode;
  } while (VP8IteratorRotateI4(y_left, y_top_left, 
  y_top, &i4_, top_mem, best_blocks, left, &top_left, top, top_right));

  // finalize state
  CopyScore(rd, &rd_best);

  for(n = 0; n < 16; n++){
//#pragma HLS unroll
	  for(j = 0; j < 4; j++){
//#pragma HLS unroll
		  for(i = 0; i < 4; i++){
//#pragma HLS unroll
			  Yout[VP8Scan[n] + j * 16 + i] = best_blocks[n][j * 4 + i];
		  }
	  }
  }

    for (j = 0; j < 16; ++j) {
//#pragma HLS unroll
        for (i = 0; i < 16; ++i) {
//#pragma HLS unroll
        	rd->y_ac_levels[j][i] = rd_best.y_ac_levels[j][i];
        }
    }
    *mbtype = 0;
  return 1;   // select intra4x4 over intra16x16
}

static int SSE16x8_C(const uint8_t* a, const uint8_t* b) {
  return GetSSE(a, b, 16, 8);
}

const uint16_t VP8FixedCostsUV[4] = { 302, 984, 439, 642 };

static void StoreDiffusionErrors(DError top_derr[1024], DError left_derr, int x,
                                 const VP8ModeScore* const rd) {
  int ch;
  for (ch = 0; ch <= 1; ++ch) {
//#pragma HLS unroll
    int8_t* const top = top_derr[x][ch];
    int8_t* const left = left_derr[ch];
    left[0] = rd->derr[ch][0];            // restore err1
    left[1] = 3 * rd->derr[ch][2] >> 2;   //     ... 3/4th of err3
    top[0]  = rd->derr[ch][1];            //     ... err2
    top[1]  = rd->derr[ch][2] - left[1];  //     ... 1/4th of err3.
  }
}

static int64_t VP8GetCostUV(VP8ModeScore* rd_uv){
	int64_t test_R = 0;
	int x, y;
	for (y = 0; y < 8; ++y) {
	  for (x = 0; x < 16; ++x) {
	    test_R += rd_uv->uv_levels[y][x] * rd_uv->uv_levels[y][x];
	  }
	}
	return test_R << 10;
}

static void CopyUVLevel(int16_t dst[4 + 4][16], int16_t src[4 + 4][16]) {
  int i, j;
  for (j = 0; j < 8; ++j) {
//#pragma HLS unroll
	  for (i = 0; i < 16; ++i) {
//#pragma HLS unroll
		  dst[j][i] = src[j][i];
	  }
  }
}

static void CopyUVderr(int8_t dst[2][3], int8_t src[2][3]) {
  int i, j;
	for (j = 0; j < 2; ++j) {
//#pragma HLS unroll
		for (i = 0; i < 3; ++i) {
//#pragma HLS unroll
			dst[j][i] = src[j][i];
		}
	}
}

	
static void CopyUVout(uint8_t dst[8*16], uint8_t src[8*16]) {
  int j;
  for (j = 0; j < 128; ++j) {
//#pragma HLS unroll
	  dst[j] = src[j];
  }
}

static void PickBestUV(VP8SegmentInfo* const dqm, uint8_t UVin[8*16], uint8_t UVPred[8][8*8],
		uint8_t UVout[8*16], VP8ModeScore* const rd, int x, DError top_derr[1024], DError left_derr) {
//#pragma HLS ARRAY_PARTITION variable=UVout complete dim=1
//#pragma HLS ARRAY_PARTITION variable=UVin complete dim=1
//#pragma HLS ARRAY_PARTITION variable=UVPred complete dim=0
//#pragma HLS ARRAY_PARTITION variable=rd->uv_levels complete dim=0
//#pragma HLS ARRAY_PARTITION variable=dqm->uv_.sharpen_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->uv_.zthresh_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->uv_.bias_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->uv_.iq_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->uv_.q_ complete dim=1
  const int kNumBlocks = 8;
  const int lambda = dqm->lambda_uv_;
  const uint8_t* const src = UVin;
  uint8_t tmp_dst[8*16];  // scratch buffer
  uint8_t* dst = UVout;
  VP8ModeScore rd_best;
  uint8_t tmp_p[4][8*16];
  int mode;
  int i, j, k;
//#pragma HLS ARRAY_PARTITION variable=tmp_dst complete dim=1
//#pragma HLS ARRAY_PARTITION variable=tmp_p complete dim=0
//#pragma HLS ARRAY_PARTITION variable=top_derr complete dim=2
//#pragma HLS ARRAY_PARTITION variable=top_derr complete dim=3
//#pragma HLS ARRAY_PARTITION variable=left_derr complete dim=0

  for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
	for (j = 0; j < 128; j = j + 16) {
//#pragma HLS unroll
		for (k = 0; k < 8; ++k) {
//#pragma HLS unroll
			tmp_p[i][j+k] = UVPred[i][(j>>1)+k];
		}
	}
	for (j = 8; j < 128; j = j + 16) {
//#pragma HLS unroll
		for (k = 0; k < 8; ++k) {
//#pragma HLS unroll
			tmp_p[i][j+k] = UVPred[i+4][((j-8)>>1)+k];
		}
	}
  }
  rd->mode_uv = -1;
  InitScore(&rd_best);
  for (mode = 0; mode < NUM_PRED_MODES; ++mode) {
    VP8ModeScore rd_uv;
//#pragma HLS ARRAY_PARTITION variable=rd_uv.uv_levels complete dim=0

    // Reconstruct
    rd_uv.nz = ReconstructUV(rd_uv.uv_levels, tmp_p[mode], src, tmp_dst,
    		dqm->uv_, top_derr, left_derr, x, rd_uv.derr);

    // Compute RD-score
    rd_uv.D  = SSE16x8_C(src, tmp_dst);
    rd_uv.SD = 0;    // not calling TDisto here: it tends to flatten areas.
    rd_uv.H  = VP8FixedCostsUV[mode];
	rd_uv.R  = VP8GetCostUV(&rd_uv);

    SetRDScore(lambda, &rd_uv);

    if (rd_uv.score < rd_best.score) {
      CopyScore(&rd_best, &rd_uv);
      rd->mode_uv = mode;
	  CopyUVLevel(rd->uv_levels, rd_uv.uv_levels);
	  CopyUVderr(rd->derr, rd_uv.derr);
	  CopyUVout(dst, tmp_dst);
    }
  }

  AddScore(rd, &rd_best);
  // store diffusion errors for next block
  StoreDiffusionErrors(top_derr, left_derr, x, rd);
}

void VP8Decimate_snap(uint8_t Yin[16*16], uint8_t Yout16[16*16], uint8_t Yout4[16*16],
		VP8SegmentInfo* const dqm, uint8_t UVin[8*16], uint8_t UVout[8*16], uint8_t* is_skipped,
		uint8_t left_y[16], uint8_t top_y[20], uint8_t top_left_y, uint8_t* mbtype, uint8_t left_u[8], 
		uint8_t top_u[8], uint8_t top_left_u,uint8_t left_v[8], uint8_t top_v[8], uint8_t top_left_v, 
		int x, int y, VP8ModeScore* const rd, DError top_derr[1024], DError left_derr) {

  uint8_t YPred[4][16*16];
  uint8_t UVPred[8][8*8];

//#pragma HLS ARRAY_PARTITION variable=UVout complete dim=1
//#pragma HLS ARRAY_PARTITION variable=UVin complete dim=1
//#pragma HLS ARRAY_PARTITION variable=UVPred complete dim=0
//#pragma HLS ARRAY_PARTITION variable=rd->uv_levels complete dim=0
//#pragma HLS ARRAY_PARTITION variable=dqm->uv_.sharpen_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->uv_.zthresh_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->uv_.bias_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->uv_.iq_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->uv_.q_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=Yout16 complete dim=1
//#pragma HLS ARRAY_PARTITION variable=Yout4 complete dim=1
//#pragma HLS ARRAY_PARTITION variable=Yin complete dim=1
//#pragma HLS ARRAY_PARTITION variable=YPred complete dim=0
//#pragma HLS ARRAY_PARTITION variable=rd->y_ac_levels complete dim=0
//#pragma HLS ARRAY_PARTITION variable=rd->y_dc_levels complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.sharpen_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.zthresh_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.bias_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.iq_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y1_.q_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y2_.sharpen_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y2_.zthresh_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y2_.bias_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y2_.iq_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=dqm->y2_.q_ complete dim=1
//#pragma HLS ARRAY_PARTITION variable=left_y complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top_y complete dim=1
//#pragma HLS ARRAY_PARTITION variable=left_u complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top_u complete dim=1
//#pragma HLS ARRAY_PARTITION variable=left_v complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top_v complete dim=1

  InitScore(rd);

  // We can perform predictions for Luma16x16 and Chroma8x8 already.
  // Luma4x4 predictions needs to be done as-we-go.

  Intra16Preds_C( YPred, left_y, top_y, top_left_y, x, y);

  IntraChromaPreds_C(UVPred, left_u, top_u, top_left_u, left_v, top_v, top_left_v, x,  y);

  PickBestIntra16(Yin, Yout16, YPred, rd, dqm);
  PickBestIntra4(dqm, Yin, Yout4, rd, left_y, top_left_y, top_y, mbtype);
  PickBestUV(dqm, UVin, UVPred, UVout, rd, x, top_derr, left_derr);

  *is_skipped = (rd->nz == 0);
}

void VP8IteratorSaveBoundary_snap(uint8_t mbtype, int x, int y, int mb_w, int mb_h, 
	uint8_t Yout16[16*16], uint8_t Yout4[16*16], uint8_t UVout[8*16],uint8_t top_y_tmp1[16], 
	uint8_t top_y_tmp2[16], uint8_t mem_top_y[1024][16], uint8_t mem_top_u[1024][8], 
	uint8_t mem_top_v[1024][8], uint8_t* top_left_y, uint8_t* top_left_u, uint8_t* top_left_v,
	uint8_t top_y[20], uint8_t top_u[8], uint8_t top_v[8], uint8_t left_y[16], uint8_t left_u[8],
	uint8_t left_v[8]) {
  const uint8_t* const ysrc = mbtype ? Yout16 : Yout4;
  const uint8_t* const uvsrc = UVout;
  int i;

  if (x < mb_w - 1) {   // left
    for (i = 0; i < 16; ++i) {
//#pragma HLS unroll
    	left_y[i] = ysrc[15 + i * 16];
    }
    for (i = 0; i < 8; ++i) {
//#pragma HLS unroll
    	left_u[i] = uvsrc[7 + i * 16];
    	left_v[i] = uvsrc[15 + i * 16];
    }
    // top-left (before 'top'!)
    *top_left_y = top_y[15];
    *top_left_u = top_u[7];
    *top_left_v = top_v[7];
  }
  else{
	for (i = 0; i < 16; ++i) {
//#pragma HLS unroll
		left_y[i] = 129;
	}
	for (i = 0; i < 8; ++i) {
//#pragma HLS unroll
		left_u[i] = 129;
		left_v[i] = 129;
	}
	// top-left (before 'top'!)
	*top_left_y = 129;
	*top_left_u = 129;
	*top_left_v = 129;
  }

  if (y < mb_h - 1) {  // top mem
	for (i = 0; i < 16; ++i) {
//#pragma HLS unroll
		mem_top_y[x][i] = ysrc[15 * 16 + i];
	}
	for (i = 0; i < 8; ++i) {
//#pragma HLS unroll
	  	mem_top_u[x][i] = uvsrc[7 * 16 + i];
	  	mem_top_v[x][i] = uvsrc[7 * 16 + i + 8];
	}
  }

  int tmp = (x < mb_w - 1) ? x + 1 : 0;
  int tmp_p = (x + 1 < mb_w - 1) ?  x + 2 : (x < mb_w - 1) ? 0 : 1;

	for (i = 0; i < 16; ++i) {
//#pragma HLS unroll
		top_y_tmp2[i] = top_y_tmp1[i];
		top_y_tmp1[i] = mem_top_y[tmp_p][i];
	}

  if ((y == 0)&&(x < mb_w - 1)) {  // top
	for (i = 0; i < 20; ++i) {
//#pragma HLS unroll
		top_y[i] = 127;
	}
	for (i = 0; i < 8; ++i) {
//#pragma HLS unroll
	  	top_u[i] = 127;
	  	top_v[i] = 127;
	}
  }
  else {  // top
	for (i = 0; i < 16; ++i) {
//#pragma HLS unroll
		top_y[i] = top_y_tmp2[i];
	}
	for (i = 0; i < 8; ++i) {
//#pragma HLS unroll
		top_u[i] = mem_top_u[tmp][i];
		top_v[i] = mem_top_v[tmp][i];
	}
	if (x == mb_w - 2) {
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			top_y[16 + i] = top_y[15];
		}
	} else {
		for (i = 0; i < 4; ++i) {
//#pragma HLS unroll
			top_y[16 + i] = top_y_tmp1[i];
		}		
	}
  }
}

void SegmentInfoLoad(VP8SegmentInfo* dqm, snap_membus_t dqm_tmp[12]){
	int i;

	for(i=0;i<16;i++){
#pragma HLS unroll
		dqm->y1_.q_[i] = dqm_tmp[0] >> (16 * i);
		dqm->y1_.iq_[i] = dqm_tmp[0] >> (16 * i + 256);
		dqm->y1_.bias_[i] = dqm_tmp[1] >> (32 * i);
		dqm->y1_.zthresh_[i] = dqm_tmp[2] >> (32 * i);
		dqm->y1_.sharpen_[i] = dqm_tmp[3] >> (16 * i);
		
		dqm->y2_.q_[i] = dqm_tmp[3] >> (16 * i + 256);
		dqm->y2_.iq_[i] = dqm_tmp[4] >> (16 * i);
		dqm->y2_.sharpen_[i] = dqm_tmp[6] >> (16 * i + 256);
		
		dqm->uv_.q_[i] = dqm_tmp[7] >> (16 * i);
		dqm->uv_.iq_[i] = dqm_tmp[7] >> (16 * i + 256);
		dqm->uv_.bias_[i] = dqm_tmp[8] >> (32 * i);
		dqm->uv_.zthresh_[i] = dqm_tmp[9] >> (32 * i);
		dqm->uv_.sharpen_[i] = dqm_tmp[10] >> (16 * i);
	}
	
	for(i=0;i<8;i++){
#pragma HLS unroll
		dqm->y2_.bias_[i] = dqm_tmp[4] >> (32 * i + 256);
		dqm->y2_.bias_[8 + i] = dqm_tmp[5] >> (32 * i);
		dqm->y2_.zthresh_[i] = dqm_tmp[5] >> (32 * i + 256);
		dqm->y2_.zthresh_[8 + i] = dqm_tmp[6] >> (32 * i);
	}

	dqm->max_edge_ = dqm_tmp[10] >> 384;
	dqm->lambda_i16_ = dqm_tmp[10] >> 448;
	dqm->lambda_i4_ = dqm_tmp[10] >> 480;
	dqm->lambda_uv_ = dqm_tmp[11];
	dqm->lambda_mode_ = dqm_tmp[11] >> 32;
	dqm->tlambda_ = dqm_tmp[11] >> 96;
}

void DATALoad(DATA_O* data_o, snap_membus_t data_tmp[14]){

	data_tmp[0] = ((snap_membus_t)(ap_uint<64>)(data_o->info.D));
	data_tmp[0] |= ((snap_membus_t)(ap_uint<64>)(data_o->info.SD)) << 64;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<64>)(data_o->info.H)) << 128;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<64>)(data_o->info.R)) << 192;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<64>)(data_o->info.score)) << 256;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[0 ])) << 320;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[1 ])) << 336;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[2 ])) << 352;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[3 ])) << 368;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[4 ])) << 384;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[5 ])) << 400;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[6 ])) << 416;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[7 ])) << 432;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[8 ])) << 448;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[9 ])) << 464;
	data_tmp[0] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[10])) << 480;
	
	data_tmp[1] = ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[12])) << 0  ;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[13])) << 16 ;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[14])) << 32 ;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_dc_levels[15])) << 48 ;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][0 ])) << 64 ;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][1 ])) << 80 ;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][2 ])) << 96 ;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][3 ])) << 112;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][4 ])) << 128;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][5 ])) << 144;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][6 ])) << 160;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][7 ])) << 176;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][8 ])) << 192;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][9 ])) << 208;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][10])) << 224;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][11])) << 240;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][12])) << 256;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][13])) << 272;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][14])) << 288;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[0 ][15])) << 304;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][0 ])) << 320;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][1 ])) << 336;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][2 ])) << 352;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][3 ])) << 368;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][4 ])) << 384;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][5 ])) << 400;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][6 ])) << 416;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][7 ])) << 432;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][8 ])) << 448;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][9 ])) << 464;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][10])) << 480;
	data_tmp[1] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][11])) << 496;
	
	data_tmp[2] = ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][12])) << 0  ;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][13])) << 16 ;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][14])) << 32 ;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[1 ][15])) << 48 ;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][0 ])) << 64 ;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][1 ])) << 80 ;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][2 ])) << 96 ;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][3 ])) << 112;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][4 ])) << 128;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][5 ])) << 144;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][6 ])) << 160;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][7 ])) << 176;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][8 ])) << 192;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][9 ])) << 208;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][10])) << 224;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][11])) << 240;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][12])) << 256;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][13])) << 272;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][14])) << 288;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[2 ][15])) << 304;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][0 ])) << 320;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][1 ])) << 336;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][2 ])) << 352;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][3 ])) << 368;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][4 ])) << 384;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][5 ])) << 400;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][6 ])) << 416;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][7 ])) << 432;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][8 ])) << 448;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][9 ])) << 464;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][10])) << 480;
	data_tmp[2] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][11])) << 496;
	
	data_tmp[3] = ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][12])) << 0  ;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][13])) << 16 ;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][14])) << 32 ;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[3 ][15])) << 48 ;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][0 ])) << 64 ;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][1 ])) << 80 ;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][2 ])) << 96 ;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][3 ])) << 112;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][4 ])) << 128;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][5 ])) << 144;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][6 ])) << 160;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][7 ])) << 176;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][8 ])) << 192;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][9 ])) << 208;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][10])) << 224;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][11])) << 240;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][12])) << 256;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][13])) << 272;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][14])) << 288;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[4 ][15])) << 304;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][0 ])) << 320;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][1 ])) << 336;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][2 ])) << 352;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][3 ])) << 368;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][4 ])) << 384;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][5 ])) << 400;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][6 ])) << 416;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][7 ])) << 432;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][8 ])) << 448;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][9 ])) << 464;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][10])) << 480;
	data_tmp[3] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][11])) << 496;
	
	data_tmp[4] = ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][12])) << 0  ;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][13])) << 16 ;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][14])) << 32 ;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[5 ][15])) << 48 ;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][0 ])) << 64 ;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][1 ])) << 80 ;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][2 ])) << 96 ;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][3 ])) << 112;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][4 ])) << 128;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][5 ])) << 144;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][6 ])) << 160;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][7 ])) << 176;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][8 ])) << 192;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][9 ])) << 208;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][10])) << 224;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][11])) << 240;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][12])) << 256;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][13])) << 272;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][14])) << 288;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[6 ][15])) << 304;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][0 ])) << 320;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][1 ])) << 336;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][2 ])) << 352;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][3 ])) << 368;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][4 ])) << 384;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][5 ])) << 400;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][6 ])) << 416;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][7 ])) << 432;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][8 ])) << 448;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][9 ])) << 464;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][10])) << 480;
	data_tmp[4] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][11])) << 496;
	
	data_tmp[5] = ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][12])) << 0  ;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][13])) << 16 ;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][14])) << 32 ;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[7 ][15])) << 48 ;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][0 ])) << 64 ;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][1 ])) << 80 ;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][2 ])) << 96 ;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][3 ])) << 112;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][4 ])) << 128;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][5 ])) << 144;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][6 ])) << 160;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][7 ])) << 176;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][8 ])) << 192;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][9 ])) << 208;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][10])) << 224;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][11])) << 240;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][12])) << 256;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][13])) << 272;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][14])) << 288;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[8 ][15])) << 304;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][0 ])) << 320;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][1 ])) << 336;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][2 ])) << 352;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][3 ])) << 368;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][4 ])) << 384;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][5 ])) << 400;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][6 ])) << 416;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][7 ])) << 432;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][8 ])) << 448;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][9 ])) << 464;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][10])) << 480;
	data_tmp[5] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][11])) << 496;
	
	data_tmp[6] = ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][12])) << 0  ;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][13])) << 16 ;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][14])) << 32 ;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[9 ][15])) << 48 ;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][0 ])) << 64 ;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][1 ])) << 80 ;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][2 ])) << 96 ;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][3 ])) << 112;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][4 ])) << 128;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][5 ])) << 144;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][6 ])) << 160;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][7 ])) << 176;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][8 ])) << 192;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][9 ])) << 208;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][10])) << 224;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][11])) << 240;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][12])) << 256;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][13])) << 272;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][14])) << 288;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[10][15])) << 304;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][0 ])) << 320;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][1 ])) << 336;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][2 ])) << 352;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][3 ])) << 368;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][4 ])) << 384;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][5 ])) << 400;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][6 ])) << 416;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][7 ])) << 432;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][8 ])) << 448;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][9 ])) << 464;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][10])) << 480;
	data_tmp[6] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][11])) << 496;
	
	data_tmp[7] = ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][12])) << 0  ;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][13])) << 16 ;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][14])) << 32 ;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[11][15])) << 48 ;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][0 ])) << 64 ;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][1 ])) << 80 ;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][2 ])) << 96 ;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][3 ])) << 112;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][4 ])) << 128;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][5 ])) << 144;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][6 ])) << 160;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][7 ])) << 176;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][8 ])) << 192;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][9 ])) << 208;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][10])) << 224;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][11])) << 240;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][12])) << 256;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][13])) << 272;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][14])) << 288;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[12][15])) << 304;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][0 ])) << 320;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][1 ])) << 336;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][2 ])) << 352;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][3 ])) << 368;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][4 ])) << 384;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][5 ])) << 400;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][6 ])) << 416;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][7 ])) << 432;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][8 ])) << 448;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][9 ])) << 464;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][10])) << 480;
	data_tmp[7] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][11])) << 496;
	
	data_tmp[8] = ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][12])) << 0  ;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][13])) << 16 ;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][14])) << 32 ;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[13][15])) << 48 ;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][0 ])) << 64 ;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][1 ])) << 80 ;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][2 ])) << 96 ;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][3 ])) << 112;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][4 ])) << 128;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][5 ])) << 144;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][6 ])) << 160;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][7 ])) << 176;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][8 ])) << 192;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][9 ])) << 208;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][10])) << 224;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][11])) << 240;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][12])) << 256;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][13])) << 272;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][14])) << 288;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[14][15])) << 304;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][0 ])) << 320;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][1 ])) << 336;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][2 ])) << 352;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][3 ])) << 368;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][4 ])) << 384;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][5 ])) << 400;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][6 ])) << 416;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][7 ])) << 432;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][8 ])) << 448;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][9 ])) << 464;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][10])) << 480;
	data_tmp[8] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][11])) << 496;
	
	data_tmp[9] = ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][12])) << 0  ;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][13])) << 16 ;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][14])) << 32 ;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.y_ac_levels[15][15])) << 48 ;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][0 ])) << 64 ;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][1 ])) << 80 ;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][2 ])) << 96 ;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][3 ])) << 112;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][4 ])) << 128;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][5 ])) << 144;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][6 ])) << 160;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][7 ])) << 176;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][8 ])) << 192;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][9 ])) << 208;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][10])) << 224;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][11])) << 240;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][12])) << 256;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][13])) << 272;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][14])) << 288;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[0 ][15])) << 304;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][0 ])) << 320;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][1 ])) << 336;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][2 ])) << 352;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][3 ])) << 368;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][4 ])) << 384;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][5 ])) << 400;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][6 ])) << 416;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][7 ])) << 432;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][8 ])) << 448;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][9 ])) << 464;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][10])) << 480;
	data_tmp[9] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][11])) << 496;

	data_tmp[10] = ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][12])) << 0  ;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][13])) << 16 ;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][14])) << 32 ;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[1 ][15])) << 48 ;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][0 ])) << 64 ;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][1 ])) << 80 ;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][2 ])) << 96 ;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][3 ])) << 112;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][4 ])) << 128;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][5 ])) << 144;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][6 ])) << 160;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][7 ])) << 176;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][8 ])) << 192;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][9 ])) << 208;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][10])) << 224;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][11])) << 240;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][12])) << 256;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][13])) << 272;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][14])) << 288;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[2 ][15])) << 304;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][0 ])) << 320;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][1 ])) << 336;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][2 ])) << 352;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][3 ])) << 368;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][4 ])) << 384;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][5 ])) << 400;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][6 ])) << 416;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][7 ])) << 432;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][8 ])) << 448;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][9 ])) << 464;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][10])) << 480;
	data_tmp[10] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][11])) << 496;
	
	data_tmp[11] = ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][12])) << 0  ;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][13])) << 16 ;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][14])) << 32 ;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[3 ][15])) << 48 ;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][0 ])) << 64 ;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][1 ])) << 80 ;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][2 ])) << 96 ;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][3 ])) << 112;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][4 ])) << 128;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][5 ])) << 144;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][6 ])) << 160;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][7 ])) << 176;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][8 ])) << 192;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][9 ])) << 208;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][10])) << 224;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][11])) << 240;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][12])) << 256;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][13])) << 272;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][14])) << 288;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[4 ][15])) << 304;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][0 ])) << 320;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][1 ])) << 336;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][2 ])) << 352;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][3 ])) << 368;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][4 ])) << 384;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][5 ])) << 400;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][6 ])) << 416;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][7 ])) << 432;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][8 ])) << 448;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][9 ])) << 464;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][10])) << 480;
	data_tmp[11] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][11])) << 496;
	
	data_tmp[12] = ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][12])) << 0  ;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][13])) << 16 ;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][14])) << 32 ;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[5 ][15])) << 48 ;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][0 ])) << 64 ;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][1 ])) << 80 ;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][2 ])) << 96 ;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][3 ])) << 112;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][4 ])) << 128;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][5 ])) << 144;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][6 ])) << 160;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][7 ])) << 176;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][8 ])) << 192;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][9 ])) << 208;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][10])) << 224;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][11])) << 240;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][12])) << 256;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][13])) << 272;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][14])) << 288;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[6 ][15])) << 304;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][0 ])) << 320;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][1 ])) << 336;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][2 ])) << 352;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][3 ])) << 368;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][4 ])) << 384;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][5 ])) << 400;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][6 ])) << 416;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][7 ])) << 432;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][8 ])) << 448;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][9 ])) << 464;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][10])) << 480;
	data_tmp[12] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][11])) << 496;
	
	data_tmp[13] = ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][12])) << 0  ;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][13])) << 16 ;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][14])) << 32 ;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<16>)(data_o->info.uv_levels[7 ][15])) << 48 ;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<32>)(data_o->info.mode_i16)) << 64 ;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[0 ])) << 96 ;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[1 ])) << 104;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[2 ])) << 112;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[3 ])) << 120;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[4 ])) << 128;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[5 ])) << 136;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[6 ])) << 144;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[7 ])) << 152;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[8 ])) << 160;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[9 ])) << 168;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[10])) << 176;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[11])) << 184;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[12])) << 192;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[13])) << 200;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[14])) << 208;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.modes_i4[15])) << 216;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<32>)(data_o->info.mode_uv)) << 224;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<32>)(data_o->info.nz)) << 256;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.derr[0][0])) << 288;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.derr[0][1])) << 296;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.derr[0][2])) << 304;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.derr[1][0])) << 312;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.derr[1][1])) << 320;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->info.derr[1][2])) << 328;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->mbtype)) << 384;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<8>)(data_o->is_skipped)) << 392;
	data_tmp[13] |= ((snap_membus_t)(ap_uint<32>)(data_o->max_edge_)) << 416;

}

void YUVLoad(snap_membus_t YUVin[6], uint8_t Yin[16*16], uint8_t UVin[8*16]){
	int i, j;

	for(j=0;j<4;j++){
#pragma HLS unroll
		for(i=0;i<64;i++){
#pragma HLS unroll
			Yin[64 * j + i] = YUVin[j] >> (8 * i);
		}
	}
	
	for(j=0;j<2;j++){
#pragma HLS unroll
		for(i=0;i<64;i++){
#pragma HLS unroll
			UVin[64 * j + i] = YUVin[4 + j] >> (8 * i);
		}
	}
}

//----------------------------------------------------------------------
//--- MAIN PROGRAM -----------------------------------------------------
//----------------------------------------------------------------------
static int process_action(snap_membus_t *din_gmem,
	      snap_membus_t *dout_gmem,
	      /* snap_membus_t *d_ddrmem, *//* not needed */
	      action_reg *act_reg)
{
	uint8_t Yin[16*16];
	uint8_t UVin[8*16];
	uint8_t Yout16[16*16];
	uint8_t Yout4[16*16];
	uint8_t UVout[8*16];
	uint8_t top_y[20];
	uint8_t top_u[8];
	uint8_t top_v[8];
	uint8_t left_y[16];
	uint8_t left_u[8];
	uint8_t left_v[8];
	uint8_t top_left_y = 127;
	uint8_t top_left_u = 127;
	uint8_t top_left_v = 127;
	uint8_t top_y_tmp1[16];
	uint8_t top_y_tmp2[16];
	uint8_t mem_top_y[1024][16];
	uint8_t mem_top_u[1024][8];
	uint8_t mem_top_v[1024][8];
	snap_membus_t YUVin[6];
	snap_membus_t dqm_tmp[12];
	snap_membus_t data_tmp[14];
	DError top_derr[1024] = {0};
	DError left_derr = {0};
	VP8SegmentInfo dqm;
	DATA_O data_o;
	int x = 0;
	int y = 0;
	int mb_w;
	int mb_h;
	uint64_t i_idx, o_idx, dqm_idx;		
	int i, j;
	
#pragma HLS ARRAY_PARTITION variable=YUVin complete dim=1
#pragma HLS ARRAY_PARTITION variable=UVin complete dim=1
//#pragma HLS ARRAY_PARTITION variable=UVout complete dim=1
//#pragma HLS ARRAY_PARTITION variable=Yout16 complete dim=1
//#pragma HLS ARRAY_PARTITION variable=Yout4 complete dim=1
#pragma HLS ARRAY_PARTITION variable=Yin complete dim=1
#pragma HLS ARRAY_PARTITION variable=data_o.info.y_ac_levels complete dim=0
#pragma HLS ARRAY_PARTITION variable=data_o.info.y_dc_levels complete dim=1
#pragma HLS ARRAY_PARTITION variable=data_o.info.uv_levels complete dim=0
//#pragma HLS ARRAY_PARTITION variable=left_y complete dim=1
//#pragma HLS ARRAY_PARTITION variable=left_u complete dim=1
//#pragma HLS ARRAY_PARTITION variable=left_v complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top_y complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top_u complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top_v complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top_y_tmp1 complete dim=1
//#pragma HLS ARRAY_PARTITION variable=top_y_tmp2 complete dim=1
//#pragma HLS ARRAY_PARTITION variable=mem_top_y complete dim=2
//#pragma HLS ARRAY_PARTITION variable=mem_top_u complete dim=2
//#pragma HLS ARRAY_PARTITION variable=mem_top_v complete dim=2
//#pragma HLS ARRAY_PARTITION variable=top_derr complete dim=2
//#pragma HLS ARRAY_PARTITION variable=top_derr complete dim=3
//#pragma HLS ARRAY_PARTITION variable=left_derr complete dim=0
#pragma HLS ARRAY_PARTITION variable=dqm_tmp complete dim=1
#pragma HLS ARRAY_PARTITION variable=data_tmp complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.y1_.sharpen_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.y1_.zthresh_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.y1_.bias_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.y1_.iq_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.y1_.q_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.y2_.sharpen_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.y2_.zthresh_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.y2_.bias_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.y2_.iq_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.y2_.q_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.uv_.sharpen_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.uv_.zthresh_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.uv_.bias_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.uv_.iq_ complete dim=1
#pragma HLS ARRAY_PARTITION variable=dqm.uv_.q_ complete dim=1

	i_idx = act_reg->Data.in >> ADDR_RIGHT_SHIFT;
	o_idx = act_reg->Data.out >> ADDR_RIGHT_SHIFT;
	dqm_idx = act_reg->Data.dqm >> ADDR_RIGHT_SHIFT;
	mb_w = act_reg->Data.mb_w;
	mb_h = act_reg->Data.mb_h;
	
	for(i=0;i<20;i++){
//#pragma HLS unroll
	  top_y[i] = 127;
	}
	
	for(i=0;i<16;i++){
//#pragma HLS unroll
	  left_y[i] = 129;
	}

	for(i=0;i<8;i++){
//#pragma HLS unroll
	  top_u[i] = 127;
	  top_v[i] = 127;
	  left_u[i] = 129;
	  left_v[i] = 129;
	}

	for(i=0;i<12;i++){//12 is sizeof(dqm)/64
#pragma HLS pipeline
		dqm_tmp[i] = (din_gmem + dqm_idx)[i];
	}

	SegmentInfoLoad(&dqm, dqm_tmp);


	for(y = 0; y < mb_h; y++){
	  for(x = 0; x < mb_w; x++){

		for(i=0;i<6;i++){//6 is sizeof(Yin + UVin)/64
#pragma HLS pipeline
			YUVin[i] = (din_gmem + i_idx + (y * mb_w + x) * 6)[i];
		}

		YUVLoad(YUVin, Yin, UVin);

		VP8Decimate_snap(Yin, Yout16, Yout4, &dqm, UVin, UVout, &data_o.is_skipped, left_y,
			top_y, top_left_y, &data_o.mbtype, left_u, top_u, top_left_u, left_v, top_v, 
			top_left_v, x, y, &data_o.info, top_derr, left_derr);

		VP8IteratorSaveBoundary_snap(data_o.mbtype, x, y, mb_w, mb_h, Yout16, Yout4, UVout, 
			top_y_tmp1, top_y_tmp2, mem_top_y, mem_top_u, mem_top_v, &top_left_y, &top_left_u, 
			&top_left_v, top_y, top_u, top_v, left_y, left_u, left_v);

		data_o.max_edge_ = dqm.max_edge_;

		DATALoad(&data_o, data_tmp);

		for(i=0;i<14;i++){//14 is sizeof(data_o)/64
#pragma HLS pipeline
		  (dout_gmem + o_idx + (y * mb_w + x) * 14)[i] = data_tmp[i];
		}
		
	  }
	  
	  for(j=0;j<2;j++){
//#pragma HLS unroll
		for(i=0;i<2;i++){
//#pragma HLS unroll
		  left_derr[j][i] = 0;
		}
	  }
	  
	}
	
	act_reg->Control.Retc = SNAP_RETC_SUCCESS;
    return 0;
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
#define MEMORY_LINES 2048
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
    for(i=0;i<2048*64;i++){
    	((uint8_t*)din_gmem)[i]=(uint8_t)i;
    }
    printf("Input is : %s\n", (char *)((unsigned long)din_gmem + 0));

    // set flags != 0 to have action processed
    act_reg.Control.flags = 0x1; /* just not 0x0 */


	act_reg.Data.in = (unsigned long)0;
	act_reg.Data.out = (unsigned long)0;
	act_reg.Data.dqm= (unsigned long)0;
	act_reg.Data.mb_w= 2;
	act_reg.Data.mb_h= 2;


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