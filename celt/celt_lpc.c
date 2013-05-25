/* Copyright (c) 2009-2010 Xiph.Org Foundation
   Written by Jean-Marc Valin */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "celt_lpc.h"
#include "stack_alloc.h"
#include "mathops.h"
#include "pitch.h"

void _celt_lpc(
      opus_val16       *_lpc, /* out: [0...p-1] LPC coefficients      */
const opus_val32 *ac,  /* in:  [0...p] autocorrelation values  */
int          p
)
{
   int i, j;
   opus_val32 r;
   opus_val32 error = ac[0];
#ifdef FIXED_POINT
   opus_val32 lpc[LPC_ORDER];
#else
   float *lpc = _lpc;
#endif

   for (i = 0; i < p; i++)
      lpc[i] = 0;
   if (ac[0] != 0)
   {
      for (i = 0; i < p; i++) {
         /* Sum up this iteration's reflection coefficient */
         opus_val32 rr = 0;
         for (j = 0; j < i; j++)
            rr += MULT32_32_Q31(lpc[j],ac[i - j]);
         rr += SHR32(ac[i + 1],3);
         r = -frac_div32(SHL32(rr,3), error);
         /*  Update LPC coefficients and total error */
         lpc[i] = SHR32(r,3);
         for (j = 0; j < (i+1)>>1; j++)
         {
            opus_val32 tmp1, tmp2;
            tmp1 = lpc[j];
            tmp2 = lpc[i-1-j];
            lpc[j]     = tmp1 + MULT32_32_Q31(r,tmp2);
            lpc[i-1-j] = tmp2 + MULT32_32_Q31(r,tmp1);
         }

         error = error - MULT32_32_Q31(MULT32_32_Q31(r,r),error);
         /* Bail out once we get 30 dB gain */
#ifdef FIXED_POINT
         if (error<SHR32(ac[0],10))
            break;
#else
         if (error<.001f*ac[0])
            break;
#endif
      }
   }
#ifdef FIXED_POINT
   for (i=0;i<p;i++)
      _lpc[i] = ROUND16(lpc[i],16);
#endif
}

void celt_fir(const opus_val16 *_x,
         const opus_val16 *num,
         opus_val16 *_y,
         int N,
         int ord,
         opus_val16 *mem)
{
   int i,j;
   VARDECL(opus_val16, rnum);
   VARDECL(opus_val16, x);
   SAVE_STACK;

   ALLOC(rnum, ord, opus_val16);
   ALLOC(x, N+ord, opus_val16);
   for(i=0;i<ord;i++)
      rnum[i] = num[ord-i-1];
   for(i=0;i<ord;i++)
      x[i] = mem[ord-i-1];
   for (i=0;i<N;i++)
      x[i+ord]=_x[i];
   for(i=0;i<ord;i++)
      mem[i] = _x[N-i-1];
#ifdef SMALL_FOOTPRINT
   for (i=0;i<N;i++)
   {
      opus_val32 sum = SHL32(EXTEND32(_x[i]), SIG_SHIFT);
      for (j=0;j<ord;j++)
      {
         sum = MAC16_16(sum,rnum[j],x[i+j]);
      }
      _y[i] = ROUND16(sum, SIG_SHIFT);
   }
#else
   celt_assert((ord&3)==0);
   for (i=0;i<N-3;i+=4)
   {
      opus_val32 sum1=0;
      opus_val32 sum2=0;
      opus_val32 sum3=0;
      opus_val32 sum4=0;
      const opus_val16 *xx = x+i;
      const opus_val16 *z = rnum;
      opus_val16 y_0, y_1, y_2, y_3;
      y_3=0; /* gcc doesn't realize that y_3 can't be used uninitialized */
      y_0=*xx++;
      y_1=*xx++;
      y_2=*xx++;
      for (j=0;j<ord-3;j+=4)
      {
         opus_val16 tmp;
         tmp = *z++;
         y_3=*xx++;
         sum1 = MAC16_16(sum1,tmp,y_0);
         sum2 = MAC16_16(sum2,tmp,y_1);
         sum3 = MAC16_16(sum3,tmp,y_2);
         sum4 = MAC16_16(sum4,tmp,y_3);
         tmp=*z++;
         y_0=*xx++;
         sum1 = MAC16_16(sum1,tmp,y_1);
         sum2 = MAC16_16(sum2,tmp,y_2);
         sum3 = MAC16_16(sum3,tmp,y_3);
         sum4 = MAC16_16(sum4,tmp,y_0);
         tmp=*z++;
         y_1=*xx++;
         sum1 = MAC16_16(sum1,tmp,y_2);
         sum2 = MAC16_16(sum2,tmp,y_3);
         sum3 = MAC16_16(sum3,tmp,y_0);
         sum4 = MAC16_16(sum4,tmp,y_1);
         tmp=*z++;
         y_2=*xx++;
         sum1 = MAC16_16(sum1,tmp,y_3);
         sum2 = MAC16_16(sum2,tmp,y_0);
         sum3 = MAC16_16(sum3,tmp,y_1);
         sum4 = MAC16_16(sum4,tmp,y_2);
      }
      _y[i  ] = ADD16(_x[i  ], ROUND16(sum1, SIG_SHIFT));
      _y[i+1] = ADD16(_x[i+1], ROUND16(sum2, SIG_SHIFT));
      _y[i+2] = ADD16(_x[i+2], ROUND16(sum3, SIG_SHIFT));
      _y[i+3] = ADD16(_x[i+3], ROUND16(sum4, SIG_SHIFT));
   }
   for (;i<N;i++)
   {
      opus_val32 sum = 0;
      for (j=0;j<ord;j++)
         sum = MAC16_16(sum,rnum[j],x[i+j]);
      _y[i] = ADD16(_x[i  ], ROUND16(sum, SIG_SHIFT));
   }
#endif
   RESTORE_STACK;
}

void celt_iir(const opus_val32 *_x,
         const opus_val16 *den,
         opus_val32 *_y,
         int N,
         int ord,
         opus_val16 *mem)
{
#ifdef SMALL_FOOTPRINT
   int i,j;
   for (i=0;i<N;i++)
   {
      opus_val32 sum = _x[i];
      for (j=0;j<ord;j++)
      {
         sum -= MULT16_16(den[j],mem[j]);
      }
      for (j=ord-1;j>=1;j--)
      {
         mem[j]=mem[j-1];
      }
      mem[0] = ROUND16(sum,SIG_SHIFT);
      _y[i] = sum;
   }
#else
   int i,j;
   VARDECL(opus_val16, rden);
   VARDECL(opus_val16, y);
   SAVE_STACK;

   celt_assert((ord&3)==0);
   ALLOC(rden, ord, opus_val16);
   ALLOC(y, N+ord, opus_val16);
   for(i=0;i<ord;i++)
      rden[i] = den[ord-i-1];
   for(i=0;i<ord;i++)
      y[i] = -mem[ord-i-1];
   for(;i<N+ord;i++)
      y[i]=0;
   for (i=0;i<N-3;i+=4)
   {
      opus_val32 sum1=0;
      opus_val32 sum2=0;
      opus_val32 sum3=0;
      opus_val32 sum4=0;
      const opus_val16 *yy = y+i;
      const opus_val16 *z = rden;
      opus_val16 y_0, y_1, y_2, y_3;
      sum1 = _x[i  ];
      sum2 = _x[i+1];
      sum3 = _x[i+2];
      sum4 = _x[i+3];
      y_3=0; /* gcc doesn't realize that y_3 can't be used uninitialized */
      y_0=*yy++;
      y_1=*yy++;
      y_2=*yy++;
      for (j=0;j<ord-3;j+=4)
      {
         opus_val16 tmp;
         tmp = *z++;
         y_3=*yy++;
         sum1 = MAC16_16(sum1,tmp,y_0);
         sum2 = MAC16_16(sum2,tmp,y_1);
         sum3 = MAC16_16(sum3,tmp,y_2);
         sum4 = MAC16_16(sum4,tmp,y_3);
         tmp=*z++;
         y_0=*yy++;
         sum1 = MAC16_16(sum1,tmp,y_1);
         sum2 = MAC16_16(sum2,tmp,y_2);
         sum3 = MAC16_16(sum3,tmp,y_3);
         sum4 = MAC16_16(sum4,tmp,y_0);
         tmp=*z++;
         y_1=*yy++;
         sum1 = MAC16_16(sum1,tmp,y_2);
         sum2 = MAC16_16(sum2,tmp,y_3);
         sum3 = MAC16_16(sum3,tmp,y_0);
         sum4 = MAC16_16(sum4,tmp,y_1);
         tmp=*z++;
         y_2=*yy++;
         sum1 = MAC16_16(sum1,tmp,y_3);
         sum2 = MAC16_16(sum2,tmp,y_0);
         sum3 = MAC16_16(sum3,tmp,y_1);
         sum4 = MAC16_16(sum4,tmp,y_2);
      }
      y[i+ord  ] = -ROUND16(sum1,SIG_SHIFT);
      _y[i  ] = sum1;
      sum2 = MAC16_16(sum2, y[i+ord  ], den[0]);
      y[i+ord+1] = -ROUND16(sum2,SIG_SHIFT);
      _y[i+1] = sum2;
      sum3 = MAC16_16(sum3, y[i+ord+1], den[0]);
      sum3 = MAC16_16(sum3, y[i+ord  ], den[1]);
      y[i+ord+2] = -ROUND16(sum3,SIG_SHIFT);
      _y[i+2] = sum3;

      sum4 = MAC16_16(sum4, y[i+ord+2], den[0]);
      sum4 = MAC16_16(sum4, y[i+ord+1], den[1]);
      sum4 = MAC16_16(sum4, y[i+ord  ], den[2]);
      y[i+ord+3] = -ROUND16(sum4,SIG_SHIFT);
      _y[i+3] = sum4;
   }
   for (;i<N;i++)
   {
      opus_val32 sum = _x[i];
      for (j=0;j<ord;j++)
         sum -= MULT16_16(rden[j],y[i+j]);
      y[i+ord] = ROUND16(sum,SIG_SHIFT);
      _y[i] = sum;
   }
   for(i=0;i<ord;i++)
      mem[i] = _y[N-i-1];
#endif
   RESTORE_STACK;
}

void _celt_autocorr(
                   const opus_val16 *x,   /*  in: [0...n-1] samples x   */
                   opus_val32       *ac,  /* out: [0...lag-1] ac values */
                   const opus_val16       *window,
                   int          overlap,
                   int          lag,
                   int          n
                  )
{
   opus_val32 d;
   int i;
   int fastN=n-lag;
   VARDECL(opus_val16, xx);
   SAVE_STACK;
   ALLOC(xx, n, opus_val16);
   celt_assert(n>0);
   celt_assert(overlap>=0);
   for (i=0;i<n;i++)
      xx[i] = x[i];
   for (i=0;i<overlap;i++)
   {
      xx[i] = MULT16_16_Q15(x[i],window[i]);
      xx[n-i-1] = MULT16_16_Q15(x[n-i-1],window[i]);
   }
#ifdef FIXED_POINT
   {
      opus_val32 ac0;
      int shift;
      ac0 = 1+n;
      if (n&1) ac0 += SHR32(MULT16_16(xx[0],xx[0]),9);
      for(i=(n&1);i<n;i+=2)
      {
         ac0 += SHR32(MULT16_16(xx[i],xx[i]),9);
         ac0 += SHR32(MULT16_16(xx[i+1],xx[i+1]),9);
      }

      shift = celt_ilog2(ac0)-30+10;
      shift = (shift+1)/2;
      for(i=0;i<n;i++)
         xx[i] = VSHR32(xx[i], shift);
   }
#endif
   pitch_xcorr(xx, xx, ac, fastN, lag+1);
   while (lag>=0)
   {
      for (i = lag+fastN, d = 0; i < n; i++)
         d = MAC16_16(d, xx[i], xx[i-lag]);
      ac[lag] += d;
      /*printf ("%f ", ac[lag]);*/
      lag--;
   }
   /*printf ("\n");*/
   ac[0] += 10;

   RESTORE_STACK;
}
