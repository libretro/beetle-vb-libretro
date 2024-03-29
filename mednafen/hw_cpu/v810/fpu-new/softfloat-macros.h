
/*============================================================================

This C source fragment is part of the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2b.

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

/*----------------------------------------------------------------------------
| Shifts `a' right by the number of bits given in `count'.  If any nonzero
| bits are shifted off, they are ``jammed'' into the least significant bit of
| the result by setting the least significant bit to 1.  The value of `count'
| can be arbitrarily large; in particular, if `count' is greater than 32, the
| result will be either 0 or 1, depending on whether `a' is zero or nonzero.
| The result is stored in the location pointed to by `zPtr'.
*----------------------------------------------------------------------------*/

static INLINE uint32_t shift32RightJamming( uint32_t a, int16 count)
{
    if ( count == 0 )
        return  a;
    else if ( count < 32 )
        return ( a>>count ) | ( ( a<<( ( - count ) & 31 ) ) != 0 );
    return ( a != 0 );
}

/*----------------------------------------------------------------------------
| Shifts the 64-bit value formed by concatenating `a0' and `a1' left by the
| number of bits given in `count'.  Any bits shifted off are lost.  The value
| of `count' must be less than 32.  The result is broken into two 32-bit
| pieces which are stored at the locations pointed to by `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static INLINE void
 shortShift64Left(
     uint32_t a0, uint32_t a1, int16 count, uint32_t *z0Ptr, uint32_t *z1Ptr )
{
    *z1Ptr = a1<<count;
    *z0Ptr =
        ( count == 0 ) ? a0 : ( a0<<count ) | ( a1>>( ( - count ) & 31 ) );
}

/*----------------------------------------------------------------------------
| Shifts the 96-bit value formed by concatenating `a0', `a1', and `a2' left
| by the number of bits given in `count'.  Any bits shifted off are lost.
| The value of `count' must be less than 32.  The result is broken into three
| 32-bit pieces which are stored at the locations pointed to by `z0Ptr',
| `z1Ptr', and `z2Ptr'.
*----------------------------------------------------------------------------*/

static INLINE void
 shortShift96Left(
     uint32_t a0,
     uint32_t a1,
     uint32_t a2,
     int16 count,
     uint32_t *z0Ptr,
     uint32_t *z1Ptr,
     uint32_t *z2Ptr
 )
{
    int8 negCount;
    uint32_t z2 = a2<<count;
    uint32_t z1 = a1<<count;
    uint32_t z0 = a0<<count;
    if ( 0 < count ) {
        negCount = ( ( - count ) & 31 );
        z1 |= a2>>negCount;
        z0 |= a1>>negCount;
    }
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Adds the 64-bit value formed by concatenating `a0' and `a1' to the 64-bit
| value formed by concatenating `b0' and `b1'.  Addition is modulo 2^64, so
| any carry out is lost.  The result is broken into two 32-bit pieces which
| are stored at the locations pointed to by `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static INLINE void
 add64(
     uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1, uint32_t *z0Ptr, uint32_t *z1Ptr )
{
    uint32_t z1 = a1 + b1;
    *z1Ptr = z1;
    *z0Ptr = a0 + b0 + ( z1 < a1 );

}

/*----------------------------------------------------------------------------
| Adds the 96-bit value formed by concatenating `a0', `a1', and `a2' to the
| 96-bit value formed by concatenating `b0', `b1', and `b2'.  Addition is
| modulo 2^96, so any carry out is lost.  The result is broken into three
| 32-bit pieces which are stored at the locations pointed to by `z0Ptr',
| `z1Ptr', and `z2Ptr'.
*----------------------------------------------------------------------------*/

static INLINE void
 add96(
     uint32_t a0,
     uint32_t a1,
     uint32_t a2,
     uint32_t b0,
     uint32_t b1,
     uint32_t b2,
     uint32_t *z0Ptr,
     uint32_t *z1Ptr,
     uint32_t *z2Ptr
 )
{
    uint32_t z2 = a2 + b2;
    int8 carry1 = ( z2 < a2 );
    uint32_t z1 = a1 + b1;
    int8 carry0 = ( z1 < a1 );
    uint32_t z0 = a0 + b0;
    z1 += carry1;
    z0 += ( z1 < carry1 );
    z0 += carry0;
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Subtracts the 64-bit value formed by concatenating `b0' and `b1' from the
| 64-bit value formed by concatenating `a0' and `a1'.  Subtraction is modulo
| 2^64, so any borrow out (carry out) is lost.  The result is broken into two
| 32-bit pieces which are stored at the locations pointed to by `z0Ptr' and
| `z1Ptr'.
*----------------------------------------------------------------------------*/

static INLINE void
 sub64(
     uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1, uint32_t *z0Ptr, uint32_t *z1Ptr )
{

    *z1Ptr = a1 - b1;
    *z0Ptr = a0 - b0 - ( a1 < b1 );

}

/*----------------------------------------------------------------------------
| Subtracts the 96-bit value formed by concatenating `b0', `b1', and `b2' from
| the 96-bit value formed by concatenating `a0', `a1', and `a2'.  Subtraction
| is modulo 2^96, so any borrow out (carry out) is lost.  The result is broken
| into three 32-bit pieces which are stored at the locations pointed to by
| `z0Ptr', `z1Ptr', and `z2Ptr'.
*----------------------------------------------------------------------------*/

static INLINE void
 sub96(
     uint32_t a0,
     uint32_t a1,
     uint32_t a2,
     uint32_t b0,
     uint32_t b1,
     uint32_t b2,
     uint32_t *z0Ptr,
     uint32_t *z1Ptr,
     uint32_t *z2Ptr
 )
{
    uint32_t z2  = a2 - b2;
    int8 borrow1 = ( a2 < b2 );
    uint32_t z1  = a1 - b1;
    int8 borrow0 = ( a1 < b1 );
    uint32_t z0  = a0 - b0;
    z0 -= ( z1 < borrow1 );
    z1 -= borrow1;
    z0 -= borrow0;
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Multiplies `a' by `b' to obtain a 64-bit product.  The product is broken
| into two 32-bit pieces which are stored at the locations pointed to by
| `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static INLINE void mul32To64( uint32_t a, uint32_t b, uint32_t *z0Ptr, uint32_t *z1Ptr )
{
    uint16_t aLow     = a;
    uint16_t aHigh    = a>>16;
    uint16_t bLow     = b;
    uint16_t bHigh    = b>>16;
    uint32_t z1       = ( (uint32_t) aLow ) * bLow;
    uint32_t zMiddleA = ( (uint32_t) aLow ) * bHigh;
    uint32_t zMiddleB = ( (uint32_t) aHigh ) * bLow;
    uint32_t z0       = ( (uint32_t) aHigh ) * bHigh;

    zMiddleA         += zMiddleB;
    z0               += ( ( (uint32_t) ( zMiddleA < zMiddleB ) )<<16 ) + ( zMiddleA>>16 );
    zMiddleA        <<= 16;
    z1               += zMiddleA;
    z0               += ( z1 < zMiddleA );
    *z1Ptr            = z1;
    *z0Ptr            = z0;
}

/*----------------------------------------------------------------------------
| Multiplies the 64-bit value formed by concatenating `a0' and `a1' by `b'
| to obtain a 96-bit product.  The product is broken into three 32-bit pieces
| which are stored at the locations pointed to by `z0Ptr', `z1Ptr', and
| `z2Ptr'.
*----------------------------------------------------------------------------*/

static INLINE void
 mul64By32To96(
     uint32_t a0,
     uint32_t a1,
     uint32_t b,
     uint32_t *z0Ptr,
     uint32_t *z1Ptr,
     uint32_t *z2Ptr
 )
{
    uint32_t z0, z1, z2, more1;

    mul32To64( a1, b, &z1, &z2 );
    mul32To64( a0, b, &z0, &more1 );
    add64( z0, more1, 0, z1, &z0, &z1 );
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Multiplies the 64-bit value formed by concatenating `a0' and `a1' to the
| 64-bit value formed by concatenating `b0' and `b1' to obtain a 128-bit
| product.  The product is broken into four 32-bit pieces which are stored at
| the locations pointed to by `z0Ptr', `z1Ptr', `z2Ptr', and `z3Ptr'.
*----------------------------------------------------------------------------*/

static INLINE void
 mul64To128(
     uint32_t a0,
     uint32_t a1,
     uint32_t b0,
     uint32_t b1,
     uint32_t *z0Ptr,
     uint32_t *z1Ptr,
     uint32_t *z2Ptr,
     uint32_t *z3Ptr
 )
{
    uint32_t z0, z1, z2, z3;
    uint32_t more1, more2;

    mul32To64( a1, b1, &z2, &z3 );
    mul32To64( a1, b0, &z1, &more2 );
    add64( z1, more2, 0, z2, &z1, &z2 );
    mul32To64( a0, b0, &z0, &more1 );
    add64( z0, more1, 0, z1, &z0, &z1 );
    mul32To64( a0, b1, &more1, &more2 );
    add64( more1, more2, 0, z2, &more1, &z2 );
    add64( z0, z1, 0, more1, &z0, &z1 );
    *z3Ptr = z3;
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Returns an approximation to the 32-bit integer quotient obtained by dividing
| `b' into the 64-bit value formed by concatenating `a0' and `a1'.  The
| divisor `b' must be at least 2^31.  If q is the exact quotient truncated
| toward zero, the approximation returned lies between q and q + 2 inclusive.
| If the exact quotient q is larger than 32 bits, the maximum positive 32-bit
| unsigned integer is returned.
*----------------------------------------------------------------------------*/

static uint32_t estimateDiv64To32( uint32_t a0, uint32_t a1, uint32_t b )
{
    uint32_t b0, b1;
    uint32_t rem0, rem1, term0, term1;
    uint32_t z;

    if ( b <= a0 ) return 0xFFFFFFFF;
    b0 = b>>16;
    z = ( b0<<16 <= a0 ) ? 0xFFFF0000 : ( a0 / b0 )<<16;
    mul32To64( b, z, &term0, &term1 );
    sub64( a0, a1, term0, term1, &rem0, &rem1 );
    while ( ( (int32_t) rem0 ) < 0 ) {
        z -= 0x10000;
        b1 = b<<16;
        add64( rem0, rem1, b0, b1, &rem0, &rem1 );
    }
    rem0 = ( rem0<<16 ) | ( rem1>>16 );
    z |= ( b0<<16 <= rem0 ) ? 0xFFFF : rem0 / b0;
    return z;

}

/*----------------------------------------------------------------------------
| Returns an approximation to the square root of the 32-bit significand given
| by `a'.  Considered as an integer, `a' must be at least 2^31.  If bit 0 of
| `aExp' (the least significant bit) is 1, the integer returned approximates
| 2^31*sqrt(`a'/2^31), where `a' is considered an integer.  If bit 0 of `aExp'
| is 0, the integer returned approximates 2^31*sqrt(`a'/2^30).  In either
| case, the approximation returned lies strictly within +/-2 of the exact
| value.
*----------------------------------------------------------------------------*/

static uint32_t estimateSqrt32( int16 aExp, uint32_t a )
{
    static const uint16_t sqrtOddAdjustments[] = {
        0x0004, 0x0022, 0x005D, 0x00B1, 0x011D, 0x019F, 0x0236, 0x02E0,
        0x039C, 0x0468, 0x0545, 0x0631, 0x072B, 0x0832, 0x0946, 0x0A67
    };
    static const uint16_t sqrtEvenAdjustments[] = {
        0x0A2D, 0x08AF, 0x075A, 0x0629, 0x051A, 0x0429, 0x0356, 0x029E,
        0x0200, 0x0179, 0x0109, 0x00AF, 0x0068, 0x0034, 0x0012, 0x0002
    };
    uint32_t z;
    int8 index = ( a>>27 ) & 15;
    if ( aExp & 1 ) {
        z = 0x4000 + ( a>>17 ) - sqrtOddAdjustments[ index ];
        z = ( ( a / z )<<14 ) + ( z<<15 );
        a >>= 1;
    }
    else {
        z = 0x8000 + ( a>>17 ) - sqrtEvenAdjustments[ index ];
        z = a / z + z;
        z = ( 0x20000 <= z ) ? 0xFFFF8000 : ( z<<15 );
        if ( z <= a ) return (uint32_t) ( ( (int32_t) a )>>1 );
    }
    return ( ( estimateDiv64To32( a, 0, z ) )>>1 ) + ( z>>1 );

}

/*----------------------------------------------------------------------------
| Returns the number of leading 0 bits before the most-significant 1 bit of
| `a'.  If `a' is zero, 32 is returned.
*----------------------------------------------------------------------------*/

static int8 countLeadingZeros32( uint32_t a )
{
    static const int8 countLeadingZerosHigh[] = {
        8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    int8 shiftCount = 0;
    if ( a < 0x10000 ) {
        shiftCount += 16;
        a <<= 16;
    }
    if ( a < 0x1000000 ) {
        shiftCount += 8;
        a <<= 8;
    }
    return shiftCount + countLeadingZerosHigh[ a>>24 ];
}

/*----------------------------------------------------------------------------
| Returns 1 if the 64-bit value formed by concatenating `a0' and `a1' is
| equal to the 64-bit value formed by concatenating `b0' and `b1'.  Otherwise,
| returns 0.
*----------------------------------------------------------------------------*/

static INLINE char eq64( uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1 )
{
    return ( a0 == b0 ) && ( a1 == b1 );
}

/*----------------------------------------------------------------------------
| Returns 1 if the 64-bit value formed by concatenating `a0' and `a1' is less
| than or equal to the 64-bit value formed by concatenating `b0' and `b1'.
| Otherwise, returns 0.
*----------------------------------------------------------------------------*/

static INLINE char le64( uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1 )
{
    return ( a0 < b0 ) || ( ( a0 == b0 ) && ( a1 <= b1 ) );
}

/*----------------------------------------------------------------------------
| Returns 1 if the 64-bit value formed by concatenating `a0' and `a1' is less
| than the 64-bit value formed by concatenating `b0' and `b1'.  Otherwise,
| returns 0.
*----------------------------------------------------------------------------*/

static INLINE char lt64( uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1 )
{
    return ( a0 < b0 ) || ( ( a0 == b0 ) && ( a1 < b1 ) );
}

/*----------------------------------------------------------------------------
| Returns 1 if the 64-bit value formed by concatenating `a0' and `a1' is not
| equal to the 64-bit value formed by concatenating `b0' and `b1'.  Otherwise,
| returns 0.
*----------------------------------------------------------------------------*/

static INLINE char ne64( uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1 )
{
    return ( a0 != b0 ) || ( a1 != b1 );
}

