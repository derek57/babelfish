/*-------------------------------------------*/
/* Integer type definitions for FatFs module */
/*-------------------------------------------*/
#ifndef __INTEGER_H__
#define __INTEGER_H__


#include "types.h"


/* These types must be 16-bit, 32-bit or larger integer */
typedef int		INT;
typedef u32		UINT;

/* These types must be 8-bit integer */
typedef s8		CHAR;
typedef u8		UCHAR;
typedef u8		BYTE;

/* These types must be 16-bit integer */
typedef short		SHORT;
typedef u16		USHORT;
typedef u16		WORD;
typedef u16		WCHAR;

/* These types must be 32-bit integer */
typedef long		LONG;
typedef unsigned long	ULONG;
typedef unsigned long	DWORD;

/* Boolean type */
typedef enum
{
	FALSE = 0,
	TRUE
} BOOL;

#define _INTEGER

#endif

