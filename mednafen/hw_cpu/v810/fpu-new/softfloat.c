
/*============================================================================

This C source file is part of the SoftFloat IEC/IEEE Floating-point Arithmetic
Package, Release 2b.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://www.cs.berkeley.edu/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.

=============================================================================*/

/*
 WARNING:  Modified for usage in Mednafen's PC-FX V810 emulation, specifically, to "wrap" the exponent(by subtracting 192
 from it) in the case of an overflow condition(only modified in the 32-bit float code, the 64-bit float code is unused in Mednafen),
 rather than returning an infinity.

 TODO: Make this configurable, and add it to float64 too(subtract 1536?)
*/


#include "softfloat.h"

/*----------------------------------------------------------------------------
| Floating-point rounding mode and exception flags.
*----------------------------------------------------------------------------*/
int8 float_exception_flags = 0;

/*----------------------------------------------------------------------------
| Primitive arithmetic functions, including multi-word arithmetic, and
| division and square root approximations.  (Can be specialized to target if
| desired.)
*----------------------------------------------------------------------------*/
#include "softfloat-macros.h"

/*----------------------------------------------------------------------------
| Functions and definitions to determine:  (1) what (if anything)
| happens when exceptions are raised, (2) how signaling NaNs are distinguished
| from quiet NaNs, (3) the default generated quiet NaNs, and (4) how NaNs
| are propagated from function inputs to output.  These details are target-
| specific.
*----------------------------------------------------------------------------*/
#include "softfloat-specialize.h"

/*----------------------------------------------------------------------------
| Returns the fraction bits of the single-precision floating-point value `a'.
*----------------------------------------------------------------------------*/
#define extractFloat32Frac(a) ((a) & 0x007FFFFF)

/*----------------------------------------------------------------------------
| Returns the exponent bits of the single-precision floating-point value `a'.
*----------------------------------------------------------------------------*/
#define extractFloat32Exp(a) (((a) >> 23 ) & 0xFF)

/*----------------------------------------------------------------------------
| Returns the sign bit of the single-precision floating-point value `a'.
*----------------------------------------------------------------------------*/
#define extractFloat32Sign(a) ((a) >> 31)

/*----------------------------------------------------------------------------
| Normalizes the subnormal single-precision floating-point value represented
| by the denormalized significand `aSig'.  The normalized exponent and
| significand are stored at the locations pointed to by `zExpPtr' and
| `zSigPtr', respectively.
*----------------------------------------------------------------------------*/

static void
 normalizeFloat32Subnormal( uint32_t aSig, int16 *zExpPtr, uint32_t *zSigPtr )
{
    int8_t shiftCount = countLeadingZeros32( aSig ) - 8;
    *zSigPtr          = aSig << shiftCount;
    *zExpPtr          = 1 - shiftCount;

}

/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
| single-precision floating-point value, returning the result.  After being
| shifted into the proper positions, the three fields are simply added
| together to form the result.  This means that any integer portion of `zSig'
| will be added into the exponent.  Since a properly normalized significand
| will have an integer portion equal to 1, the `zExp' input should be 1 less
| than the desired result exponent whenever `zSig' is a complete, normalized
| significand.
*----------------------------------------------------------------------------*/

static INLINE float32 packFloat32( char zSign, int16 zExp, uint32_t zSig )
{
    return ( ( (uint32_t) zSign )<<31 ) + ( ( (uint32_t) zExp )<<23 ) + zSig;
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper single-precision floating-
| point value corresponding to the abstract input.  Ordinarily, the abstract
| value is simply rounded and packed into the single-precision format, with
| the inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal single-
| precision floating-point number.
|     The input significand `zSig' has its binary point between bits 30
| and 29, which is 7 bits to the left of the usual location.  This shifted
| significand must be normalized or smaller.  If `zSig' is not normalized,
| `zExp' must be 0; in that case, the result returned is a subnormal number,
| and it must not require rounding.  In the usual case that `zSig' is
| normalized, `zExp' must be 1 less than the ``true'' floating-point exponent.
| The handling of underflow and overflow follows the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float32 roundAndPackFloat32( char zSign, int16 zExp, uint32_t zSig )
{
   char isTiny;
   char roundNearestEven = 1;
   int8 roundIncrement   = 0x40;
   int8 roundBits        = zSig & 0x7F;
   if ( 0xFD <= (uint16_t) zExp )
   {
      if (    ( 0xFD < zExp )
            || (    ( zExp == 0xFD )
               && ( (int32_t) ( zSig + roundIncrement ) < 0 ) )
         )
      {
         // Mednafen hack
         //float_raise( float_flag_overflow | float_flag_inexact );
         float_raise( float_flag_overflow );

         // Mednafen hack
         zExp -= 192;
      }
      if ( zExp < 0 )
      {
         isTiny =
               ( zExp < -1 )
            || ( zSig + roundIncrement < 0x80000000 );
         zSig = shift32RightJamming( zSig, - zExp);
         zExp = 0;
         roundBits = zSig & 0x7F;
         if ( isTiny && roundBits ) float_raise( float_flag_underflow );
      }
   }
   if ( roundBits )
      float_exception_flags |= float_flag_inexact;
   zSig = ( zSig + roundIncrement )>>7;
   zSig &= ~ ( ( ( roundBits ^ 0x40 ) == 0 ) & roundNearestEven );
   if ( zSig == 0 )
      zExp = 0;
   return packFloat32( zSign, zExp, zSig );
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper single-precision floating-
| point value corresponding to the abstract input.  This routine is just like
| `roundAndPackFloat32' except that `zSig' does not have to be normalized.
| Bit 31 of `zSig' must be zero, and `zExp' must be 1 less than the ``true''
| floating-point exponent.
*----------------------------------------------------------------------------*/

static float32
 normalizeRoundAndPackFloat32( char zSign, int16 zExp, uint32_t zSig )
{
   int8 shiftCount = countLeadingZeros32( zSig ) - 1;
   return roundAndPackFloat32( zSign, zExp - shiftCount, zSig<<shiftCount );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the 32-bit two's complement integer `a' to
| the single-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 int32_to_float32( int32 a )
{
    char zSign;

    if ( a == 0 ) return 0;
    if ( a == (int32_t) 0x80000000 ) return packFloat32( 1, 0x9E, 0 );
    zSign = ( a < 0 );
    return normalizeRoundAndPackFloat32( zSign, 0x9C, zSign ? - a : a );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 32-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  Otherwise, if the conversion overflows, the
| largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int32 float32_to_int32( float32 a )
{
    uint32_t aSigExtra;
    int32 z;
    uint32_t aSig    = extractFloat32Frac( a );
    int16 aExp       = extractFloat32Exp( a );
    char aSign       = extractFloat32Sign( a );
    int16 shiftCount = aExp - 0x96;
    if ( 0 <= shiftCount ) {
        if ( 0x9E <= aExp ) {
            if ( a != 0xCF000000 ) {
                float_raise( float_flag_invalid );
                if ( ! aSign || ( ( aExp == 0xFF ) && aSig ) ) {
                    return 0x7FFFFFFF;
                }
            }
            return (int32_t) 0x80000000;
        }
        z = ( aSig | 0x00800000 )<<shiftCount;
        if ( aSign ) z = - z;
    }
    else {
        if ( aExp < 0x7E ) {
            aSigExtra = aExp | aSig;
            z = 0;
        }
        else {
            aSig |= 0x00800000;
            aSigExtra = aSig<<( shiftCount & 31 );
            z = aSig>>( - shiftCount );
        }
        if ( aSigExtra ) float_exception_flags |= float_flag_inexact;
        {
            if ( (int32_t) aSigExtra < 0 ) {
                ++z;
                if ( (uint32_t) ( aSigExtra<<1 ) == 0 ) z &= ~1;
            }
            if ( aSign ) z = - z;
        }
    }
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 32-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.
| If `a' is a NaN, the largest positive integer is returned.  Otherwise, if
| the conversion overflows, the largest integer with the same sign as `a' is
| returned.
*----------------------------------------------------------------------------*/

int32 float32_to_int32_round_to_zero( float32 a )
{
   int32 z;
   uint32_t aSig     = extractFloat32Frac( a );
   int16 aExp        = extractFloat32Exp( a );
   char aSign        = extractFloat32Sign( a );
   int16 shiftCount  = aExp - 0x9E;
   if ( 0 <= shiftCount ) {
      if ( a != 0xCF000000 ) {
         float_raise( float_flag_invalid );
         if ( ! aSign || ( ( aExp == 0xFF ) && aSig ) ) return 0x7FFFFFFF;
      }
      return (int32_t) 0x80000000;
   }
   else if ( aExp <= 0x7E ) {
      if ( aExp | aSig ) float_exception_flags |= float_flag_inexact;
      return 0;
   }
   aSig = ( aSig | 0x00800000 )<<8;
   z = aSig>>( - shiftCount );
   if ( (uint32_t) ( aSig<<( shiftCount & 31 ) ) ) {
      float_exception_flags |= float_flag_inexact;
   }
   if ( aSign ) z = - z;
   return z;
}

/*----------------------------------------------------------------------------
| Rounds the single-precision floating-point value `a' to an integer,
| and returns the result as a single-precision floating-point value.  The
| operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_round_to_int( float32 a )
{
    char aSign;
    float32 z;
    uint32_t lastBitMask, roundBitsMask;
    int16 aExp = extractFloat32Exp( a );
    if ( 0x96 <= aExp ) {
        if ( ( aExp == 0xFF ) && extractFloat32Frac( a ) ) {
            return propagateFloat32NaN( a, a );
        }
        return a;
    }
    if ( aExp <= 0x7E ) {
        if ( (uint32_t) ( a<<1 ) == 0 ) return a;
        float_exception_flags |= float_flag_inexact;
        aSign = extractFloat32Sign( a );
	if ( ( aExp == 0x7E ) && extractFloat32Frac( a ) )
		return packFloat32( aSign, 0x7F, 0 );
        return packFloat32( aSign, 0, 0 );
    }
    lastBitMask = 1;
    lastBitMask <<= 0x96 - aExp;
    roundBitsMask = lastBitMask - 1;
    z = a;
    {
        z += lastBitMask>>1;
        if ( ( z & roundBitsMask ) == 0 )
		z &= ~ lastBitMask;
    }
    z &= ~ roundBitsMask;
    if ( z != a ) float_exception_flags |= float_flag_inexact;
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of adding the absolute values of the single-precision
| floating-point values `a' and `b'.  If `zSign' is 1, the sum is negated
| before being returned.  `zSign' is ignored if the result is a NaN.
| The addition is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float32 addFloat32Sigs( float32 a, float32 b, char zSign )
{
   int16 zExp;
   uint32_t zSig;
   uint32_t aSig   = extractFloat32Frac( a );
   int16 aExp    = extractFloat32Exp( a );
   uint32_t bSig   = extractFloat32Frac( b );
   int16 bExp    = extractFloat32Exp( b );
   int16 expDiff = aExp - bExp;
   aSig <<= 6;
   bSig <<= 6;
   if ( 0 < expDiff ) {
      if ( aExp == 0xFF ) {
         if ( aSig ) return propagateFloat32NaN( a, b );
         return a;
      }
      if ( bExp == 0 )
         --expDiff;
      else {
         bSig |= 0x20000000;
      }
      bSig = shift32RightJamming( bSig, expDiff);
      zExp = aExp;
   }
   else if ( expDiff < 0 ) {
      if ( bExp == 0xFF ) {
         if ( bSig ) return propagateFloat32NaN( a, b );
         return packFloat32( zSign, 0xFF, 0 );
      }
      if ( aExp == 0 ) {
         ++expDiff;
      }
      else {
         aSig |= 0x20000000;
      }
      aSig = shift32RightJamming( aSig, - expDiff);
      zExp = bExp;
   }
   else {
      if ( aExp == 0xFF ) {
         if ( aSig | bSig ) return propagateFloat32NaN( a, b );
         return a;
      }
      if ( aExp == 0 ) return packFloat32( zSign, 0, ( aSig + bSig )>>6 );
      zSig = 0x40000000 + aSig + bSig;
      zExp = aExp;
      goto roundAndPack;
   }
   aSig |= 0x20000000;
   zSig = ( aSig + bSig )<<1;
   --zExp;
   if ( (int32_t) zSig < 0 ) {
      zSig = aSig + bSig;
      ++zExp;
   }
roundAndPack:
   return roundAndPackFloat32( zSign, zExp, zSig );

}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the absolute values of the single-
| precision floating-point values `a' and `b'.  If `zSign' is 1, the
| difference is negated before being returned.  `zSign' is ignored if the
| result is a NaN.  The subtraction is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float32 subFloat32Sigs( float32 a, float32 b, char zSign )
{
   int16 zExp;
   uint32_t zSig;
   uint32_t aSig   = extractFloat32Frac( a );
   int16 aExp      = extractFloat32Exp( a );
   uint32_t bSig   = extractFloat32Frac( b );
   int16 bExp      = extractFloat32Exp( b );
   int16 expDiff   = aExp - bExp;
   aSig <<= 7;
   bSig <<= 7;
   if ( 0 < expDiff ) goto aExpBigger;
   if ( expDiff < 0 ) goto bExpBigger;
   if ( aExp == 0xFF ) {
      if ( aSig | bSig ) return propagateFloat32NaN( a, b );
      float_raise( float_flag_invalid );
      return float32_default_nan;
   }
   if ( aExp == 0 ) {
      aExp = 1;
      bExp = 1;
   }
   if ( bSig < aSig ) goto aBigger;
   if ( aSig < bSig ) goto bBigger;
   return packFloat32( 0, 0, 0 );
bExpBigger:
   if ( bExp == 0xFF ) {
      if ( bSig ) return propagateFloat32NaN( a, b );
      return packFloat32( zSign ^ 1, 0xFF, 0 );
   }
   if ( aExp == 0 ) {
      ++expDiff;
   }
   else {
      aSig |= 0x40000000;
   }
   aSig  = shift32RightJamming( aSig, - expDiff);
   bSig |= 0x40000000;
bBigger:
   zSig = bSig - aSig;
   zExp = bExp;
   zSign ^= 1;
   goto normalizeRoundAndPack;
aExpBigger:
   if ( aExp == 0xFF ) {
      if ( aSig ) return propagateFloat32NaN( a, b );
      return a;
   }
   if ( bExp == 0 )
      --expDiff;
   else
      bSig |= 0x40000000;
   bSig  = shift32RightJamming( bSig, expDiff);
   aSig |= 0x40000000;
aBigger:
   zSig = aSig - bSig;
   zExp = aExp;
normalizeRoundAndPack:
   --zExp;
   return normalizeRoundAndPackFloat32( zSign, zExp, zSig );
}

/*----------------------------------------------------------------------------
| Returns the result of adding the single-precision floating-point values `a'
| and `b'.  The operation is performed according to the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_add( float32 a, float32 b )
{
   char aSign = extractFloat32Sign( a );
   char bSign = extractFloat32Sign( b );
   if ( aSign == bSign )
      return addFloat32Sigs( a, b, aSign );
   return subFloat32Sigs( a, b, aSign );
}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the single-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_sub( float32 a, float32 b )
{
   char aSign = extractFloat32Sign( a );
   char bSign = extractFloat32Sign( b );
   if ( aSign == bSign )
      return subFloat32Sigs( a, b, aSign );
   return addFloat32Sigs( a, b, aSign );
}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the single-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_mul( float32 a, float32 b )
{
   int16 zExp;
   uint32_t zSig0, zSig1;
   uint32_t aSig = extractFloat32Frac( a );
   int16 aExp    = extractFloat32Exp( a );
   char aSign    = extractFloat32Sign( a );
   uint32_t bSig = extractFloat32Frac( b );
   int16 bExp    = extractFloat32Exp( b );
   char bSign    = extractFloat32Sign( b );
   char zSign    = aSign ^ bSign;
   if ( aExp == 0xFF )
   {
      if ( aSig || ( ( bExp == 0xFF ) && bSig ) )
         return propagateFloat32NaN( a, b );
      if ( ( bExp | bSig ) == 0 )
      {
         float_raise( float_flag_invalid );
         return float32_default_nan;
      }
      return packFloat32( zSign, 0xFF, 0 );
   }
   if ( bExp == 0xFF )
   {
      if ( bSig )
         return propagateFloat32NaN( a, b );
      if ( ( aExp | aSig ) == 0 )
      {
         float_raise( float_flag_invalid );
         return float32_default_nan;
      }
      return packFloat32( zSign, 0xFF, 0 );
   }
   if ( aExp == 0 )
   {
      if ( aSig == 0 )
         return packFloat32( zSign, 0, 0 );
      normalizeFloat32Subnormal( aSig, &aExp, &aSig );
   }
   if ( bExp == 0 )
   {
      if ( bSig == 0 )
         return packFloat32( zSign, 0, 0 );
      normalizeFloat32Subnormal( bSig, &bExp, &bSig );
   }
   zExp = aExp + bExp - 0x7F;
   aSig = ( aSig | 0x00800000 )<<7;
   bSig = ( bSig | 0x00800000 )<<8;
   mul32To64( aSig, bSig, &zSig0, &zSig1 );
   zSig0 |= ( zSig1 != 0 );
   if ( 0 <= (int32_t) ( zSig0<<1 ) )
   {
      zSig0 <<= 1;
      --zExp;
   }
   return roundAndPackFloat32( zSign, zExp, zSig0 );

}

/*----------------------------------------------------------------------------
| Returns the result of dividing the single-precision floating-point value `a'
| by the corresponding value `b'.  The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_div( float32 a, float32 b )
{
   int16 zExp;
   uint32_t zSig, rem0, rem1, term0, term1;
   uint32_t aSig = extractFloat32Frac( a );
   int16 aExp    = extractFloat32Exp( a );
   char aSign    = extractFloat32Sign( a );
   uint32_t bSig = extractFloat32Frac( b );
   int16 bExp    = extractFloat32Exp( b );
   char bSign    = extractFloat32Sign( b );
   char zSign    = aSign ^ bSign;
   if ( aExp == 0xFF )
   {
      if ( aSig )
         return propagateFloat32NaN( a, b );
      if ( bExp == 0xFF )
      {
         if ( bSig )
            return propagateFloat32NaN( a, b );
         float_raise( float_flag_invalid );
         return float32_default_nan;
      }
      return packFloat32( zSign, 0xFF, 0 );
   }
   if ( bExp == 0xFF )
   {
      if ( bSig )
         return propagateFloat32NaN( a, b );
      return packFloat32( zSign, 0, 0 );
   }
   if ( bExp == 0 )
   {
      if ( bSig == 0 )
      {
         if ( ( aExp | aSig ) == 0 )
         {
            float_raise( float_flag_invalid );
            return float32_default_nan;
         }
         float_raise( float_flag_divbyzero );
         return packFloat32( zSign, 0xFF, 0 );
      }
      normalizeFloat32Subnormal( bSig, &bExp, &bSig );
   }
   if ( aExp == 0 )
   {
      if ( aSig == 0 )
         return packFloat32( zSign, 0, 0 );
      normalizeFloat32Subnormal( aSig, &aExp, &aSig );
   }
   zExp = aExp - bExp + 0x7D;
   aSig = ( aSig | 0x00800000 )<<7;
   bSig = ( bSig | 0x00800000 )<<8;
   if ( bSig <= ( aSig + aSig ) )
   {
      aSig >>= 1;
      ++zExp;
   }
   zSig = estimateDiv64To32( aSig, 0, bSig );
   if ( ( zSig & 0x3F ) <= 2 )
   {
      mul32To64( bSig, zSig, &term0, &term1 );
      sub64( aSig, 0, term0, term1, &rem0, &rem1 );
      while ( (int32_t) rem0 < 0 )
      {
         --zSig;
         add64( rem0, rem1, 0, bSig, &rem0, &rem1 );
      }
      zSig |= ( rem1 != 0 );
   }
   return roundAndPackFloat32( zSign, zExp, zSig );
}

/*----------------------------------------------------------------------------
| Returns the remainder of the single-precision floating-point value `a'
| with respect to the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_rem( float32 a, float32 b )
{
   char zSign;
   int16 expDiff;
   uint32_t q, alternateASig;
   int32_t sigMean;
   uint32_t aSig = extractFloat32Frac( a );
   int16_t aExp  = extractFloat32Exp( a );
   char aSign    = extractFloat32Sign( a );
   uint32_t bSig = extractFloat32Frac( b );
   int16_t bExp  = extractFloat32Exp( b );

   if ( aExp == 0xFF )
   {
      if ( aSig || ( ( bExp == 0xFF ) && bSig ) )
         return propagateFloat32NaN( a, b );
      float_raise( float_flag_invalid );
      return float32_default_nan;
   }
   if ( bExp == 0xFF )
   {
      if ( bSig ) return propagateFloat32NaN( a, b );
      return a;
   }
   if ( bExp == 0 )
   {
      if ( bSig == 0 )
      {
         float_raise( float_flag_invalid );
         return float32_default_nan;
      }
      normalizeFloat32Subnormal( bSig, &bExp, &bSig );
   }
   if ( aExp == 0 ) {
      if ( aSig == 0 ) return a;
      normalizeFloat32Subnormal( aSig, &aExp, &aSig );
   }
   expDiff = aExp - bExp;
   aSig = ( aSig | 0x00800000 )<<8;
   bSig = ( bSig | 0x00800000 )<<8;
   if ( expDiff < 0 ) {
      if ( expDiff < -1 ) return a;
      aSig >>= 1;
   }
   q = ( bSig <= aSig );
   if ( q ) aSig -= bSig;
   expDiff -= 32;
   while ( 0 < expDiff ) {
      q = estimateDiv64To32( aSig, 0, bSig );
      q = ( 2 < q ) ? q - 2 : 0;
      aSig = - ( ( bSig>>2 ) * q );
      expDiff -= 30;
   }
   expDiff += 32;
   if ( 0 < expDiff ) {
      q = estimateDiv64To32( aSig, 0, bSig );
      q = ( 2 < q ) ? q - 2 : 0;
      q >>= 32 - expDiff;
      bSig >>= 2;
      aSig = ( ( aSig>>1 )<<( expDiff - 1 ) ) - bSig * q;
   }
   else {
      aSig >>= 2;
      bSig >>= 2;
   }
   do {
      alternateASig = aSig;
      ++q;
      aSig -= bSig;
   } while ( 0 <= (int32_t) aSig );
   sigMean = aSig + alternateASig;
   if ( ( sigMean < 0 ) || ( ( sigMean == 0 ) && ( q & 1 ) ) ) {
      aSig = alternateASig;
   }
   zSign = ( (int32_t) aSig < 0 );
   if ( zSign ) aSig = - aSig;
   return normalizeRoundAndPackFloat32( aSign ^ zSign, bExp, aSig );
}

/*----------------------------------------------------------------------------
| Returns the square root of the single-precision floating-point value `a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_sqrt( float32 a )
{
    int16 zExp;
    uint32_t zSig, rem0, rem1, term0, term1;
    uint32_t aSig = extractFloat32Frac( a );
    int16 aExp    = extractFloat32Exp( a );
    char aSign    = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if ( aSig ) return propagateFloat32NaN( a, 0 );
        if ( ! aSign ) return a;
        float_raise( float_flag_invalid );
        return float32_default_nan;
    }
    if ( aSign ) {
        if ( ( aExp | aSig ) == 0 ) return a;
        float_raise( float_flag_invalid );
        return float32_default_nan;
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return 0;
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    zExp = ( ( aExp - 0x7F )>>1 ) + 0x7E;
    aSig = ( aSig | 0x00800000 )<<8;
    zSig = estimateSqrt32( aExp, aSig ) + 2;
    if ( ( zSig & 0x7F ) <= 5 ) {
        if ( zSig < 2 ) {
            zSig = 0x7FFFFFFF;
            goto roundAndPack;
        }
        else {
            aSig >>= aExp & 1;
            mul32To64( zSig, zSig, &term0, &term1 );
            sub64( aSig, 0, term0, term1, &rem0, &rem1 );
            while ( (int32_t) rem0 < 0 ) {
                --zSig;
                shortShift64Left( 0, zSig, 1, &term0, &term1 );
                term1 |= 1;
                add64( rem0, rem1, term0, term1, &rem0, &rem1 );
            }
            zSig |= ( ( rem0 | rem1 ) != 0 );
        }
    }
    zSig = shift32RightJamming( zSig, 1);
 roundAndPack:
    return roundAndPackFloat32( 0, zExp, zSig );

}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is equal to
| the corresponding value `b', and 0 otherwise.  The comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

char float32_eq( float32 a, float32 b )
{
   if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
      )
   {
      if ( float32_is_signaling_nan( a ) || float32_is_signaling_nan( b ) )
         float_raise( float_flag_invalid );
      return 0;
   }
   return ( a == b ) || ( (uint32_t) ( ( a | b )<<1 ) == 0 );

}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is less than
| or equal to the corresponding value `b', and 0 otherwise.  The comparison
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

char float32_le( float32 a, float32 b )
{
   char aSign, bSign;

   if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
      )
   {
      float_raise( float_flag_invalid );
      return 0;
   }
   aSign = extractFloat32Sign( a );
   bSign = extractFloat32Sign( b );
   if ( aSign != bSign )
      return aSign || ( (uint32_t) ( ( a | b )<<1 ) == 0 );
   return ( a == b ) || ( aSign ^ ( a < b ) );
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  The comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

char float32_lt( float32 a, float32 b )
{
   char aSign, bSign;

   if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
      ) {
      float_raise( float_flag_invalid );
      return 0;
   }
   aSign = extractFloat32Sign( a );
   bSign = extractFloat32Sign( b );
   if ( aSign != bSign )
      return aSign && ( (uint32_t) ( ( a | b )<<1 ) != 0 );
   return ( a != b ) && ( aSign ^ ( a < b ) );
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is equal to
| the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  Otherwise, the comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

char float32_eq_signaling( float32 a, float32 b )
{

   if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
      ) {
      float_raise( float_flag_invalid );
      return 0;
   }
   return ( a == b ) || ( (uint32_t) ( ( a | b )<<1 ) == 0 );
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is less than or
| equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
| cause an exception.  Otherwise, the comparison is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

char float32_le_quiet( float32 a, float32 b )
{
   char aSign, bSign;

   if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
      ) {
      if ( float32_is_signaling_nan( a ) || float32_is_signaling_nan( b ) )
         float_raise( float_flag_invalid );
      return 0;
   }
   aSign = extractFloat32Sign( a );
   bSign = extractFloat32Sign( b );
   if ( aSign != bSign )
      return aSign || ( (uint32_t) ( ( a | b )<<1 ) == 0 );
   return ( a == b ) || ( aSign ^ ( a < b ) );
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.  Otherwise, the comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

char float32_lt_quiet( float32 a, float32 b )
{
   char aSign, bSign;

   if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
      )
   {
      if ( float32_is_signaling_nan( a ) || float32_is_signaling_nan( b ) )
         float_raise( float_flag_invalid );
      return 0;
   }
   aSign = extractFloat32Sign( a );
   bSign = extractFloat32Sign( b );
   if ( aSign != bSign )
      return aSign && ( (uint32_t) ( ( a | b )<<1 ) != 0 );
   return ( a != b ) && ( aSign ^ ( a < b ) );
}
