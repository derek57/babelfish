/* babelfish -- misc (C) utility functions */

#include <stdarg.h>
#include "types.h"
#include "utils.h"
#include "hollywood.h"
#include "gecko.h"


void delay(u32 d)
{
	write32(HW_TIMER, 0);

	while (read32(HW_TIMER) < d);
}

// Abort function: For "safety"...
void abort(void)
{
	printf("ABORT\n");

	while (1)
	{
		debug_output(0x20);
		delay(1000000);
		debug_output(0);
		delay(1000000);
	}
}

void panic(u8 v)
{
	printf("PANIC\n");

	while (1)
	{
		debug_output(v);
		delay(1000000);
		debug_output(0);
		delay(1000000);
	}
}

/*
void *memcpy(void *dst, const void *src, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		((unsigned char *)dst)[i] = ((unsigned char *)src)[i];

	return dst;
}
*/

void *memcpyr(void *dst, const void *src, size_t len)
{
	size_t i;

	for (i = len; i > 0; i--)
		((unsigned char *)dst)[i - 1] = ((unsigned char *)src)[i - 1];

	return dst;
}

int memcmp(const void *s1, const void *s2, size_t len)
{
	size_t i;
	const unsigned char *p1 = (const unsigned char *)s1;
	const unsigned char *p2 = (const unsigned char *)s2;

	for (i = 0; i < len; i++)
	{
		if (p1[i] != p2[i])
			return p1[i] - p2[i];
	}
	
	return 0;
}

/*
size_t strlcat(char *dest, const char *src, size_t maxlen)
{
        size_t len;

	maxlen--;

	for (len = 0; len < maxlen; len++)
	{
		if (!dest[len])
			break;
	}

	for (; len < maxlen && *src; len++)
		dest[len] = *src++;

	dest[len] = '\0';
	return len;
}
*/

s32 printf (const char* str, ...)
{
	va_list arp;
	u8 c;
	u8 f;
	u8 r;
	u32 val;
	u32 pos;
	char s[16];
	s32 i;
	s32 w;
	/*u32 cookie =*/ irq_kill();

	va_start(arp, str);

	for (pos = 0;;)
	{
		c = *str++;

		/* End of string */
		if (c == 0)
			break;

		/* Non escape cahracter */
		if (c != '%')
		{
			gecko_putc(c);
			pos++;
			continue;
		}

		w = f = 0;
		c = *str++;

		/* Flag: '0' padding */
		if (c == '0')
		{
			f = 1;
			c = *str++;
		}

		/* Precision */
		while ((c >= '0') && (c <= '9'))
		{
			w = (w * 10) + (c - '0');
			c = *str++;
		}

		/* Type is string */
		if (c == 's')
		{
			char *param = va_arg(arp, char*);

			for (i = 0; param[i]; i++)
			{
				gecko_putc(param[i]);
				pos++;
			}

			continue;
		}
		/* Type is char */
		else if (c == 'c')
		{
			char param = va_arg(arp, int);

			gecko_putc(param);
			pos++;
			continue;
		}

		r = 0;

		/* Type is signed decimal */
		if (c == 'd')
			r = 10;
		/* Type is unsigned decimal */
		if (c == 'u')
			r = 10;
		/* Type is long decimal */
		if (c == 'l')
			r = 10;
		/* Type is unsigned hexdecimal */
		if (c == 'x')
			r = 16;

		/* Type is unsigned hexdecimal */
		if (c == 'p')
			r = 16;

		/* Unknown type */
		if (r == 0)
		{
			break;
		}

		// Pointer (%p) case
		if (c == 'p')
			val = (u32)(void *)va_arg(arp, void *);
		// Long (%l) case
		else if (c == 'l')
			val = (u32)(long)va_arg(arp, int);
		else
			val = (c == 'd') ? (u32)(long)va_arg(arp, int) : (u32)va_arg(arp, unsigned int);

		/* Put numeral string */
		if (c == 'd')
		{
			if (val & 0x80000000)
			{
				val = 0 - val;
				f |= 4;
			}
		}

//		if ((maxlen - pos) <= sizeof(s))
//			continue;

 		i = sizeof(s) - 1;
		s[i] = 0;

		do
		{
//			c = (u8)(val % r + '0');
			c = (u8)((val & 15) + '0');

			if (c > '9')
				c += 7;

			s[--i] = c;
//			val /= r;
			val >>= 4;
		} while (i && val);

		if (i && (f & 4))
			s[--i] = '-';

		w = sizeof(s) - 1 - w;

		while (i && (i > w))
			s[--i] = (f & 1) ? '0' : ' ';

		for (; s[i] ; i++)
		{
			gecko_putc(s[i]);
			pos++;
		}
	}

	//irq_restore(cookie);
	va_end(arp);
	return pos;
}

int puts(const char *s)
{
	gecko_puts(s);

	return 0;
}

