#include "fmt.h"

// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

 /*
  * Oh, it's a waste of space, but oh-so-yummy for debugging.
  */

#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <ntdef.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define va_copy(destination, source) ((destination) = (source))
#define vsprintf_isdigit(c)  ( '0' <= (c) && (c) <= '9')

static int vsnprintf_skip_atoi(const char **s)
{
	int i = 0;

	while (vsprintf_isdigit(**s))
		i = i * 10 + *((*s)++) - '0';
	return i;
}

/*
 * put_dec_full4 handles numbers in the range 0 <= r < 10000.
 * The multiplier 0xccd is round(2^15/10), and the approximation
 * r/10 == (r * 0xccd) >> 15 is exact for all r < 16389.
 */
static void vsnprintf_put_dec_full4(char *end, UINT32 r)
{
	int i;

	for (i = 0; i < 3; i++) {
		unsigned int q = (r * 0xccd) >> 15;
		*--end = (char)('0' + (r - q * 10));
		r = q;
	}
	*--end = (char)('0' + r);
}

/* put_dec is copied from lib/vsprintf.c with small modifications */

/*
 * Call put_dec_full4 on x % 10000, return x / 10000.
 * The approximation x/10000 == (x * 0x346DC5D7) >> 43
 * holds for all x < 1,128,869,999.  The largest value this
 * helper will ever be asked to convert is 1,125,520,955.
 * (second call in the put_dec code, assuming n is all-ones).
 */
static unsigned int vsnprintf_put_dec_helper4(char *end, UINT32 x)
{
	unsigned int q = (x * 0x346DC5D7ULL) >> 43;

	vsnprintf_put_dec_full4(end, x - q * 10000);
	return q;
}

/* Based on code by Douglas W. Jones found at
 * <http://www.cs.uiowa.edu/~jones/bcd/decimal.html#sixtyfour>
 * (with permission from the author).
 * Performs no 64-bit division and hence should be fast on 32-bit machines.
 */
static char *vsnprintf_put_dec(char *end, UINT64 n)
{
	unsigned int d3, d2, d1, q, h;
	char *p = end;

	d1 = ((unsigned int)n >> 16); /* implicit "& 0xffff" */
	h = (n >> 32);
	d2 = (h) & 0xffff;
	d3 = (h >> 16); /* implicit "& 0xffff" */

	/* n = 2^48 d3 + 2^32 d2 + 2^16 d1 + d0
		 = 281_4749_7671_0656 d3 + 42_9496_7296 d2 + 6_5536 d1 + d0 */
	q = 656 * d3 + 7296 * d2 + 5536 * d1 + ((unsigned int)n & 0xffff);
	q = vsnprintf_put_dec_helper4(p, q);
	p -= 4;

	q += 7671 * d3 + 9496 * d2 + 6 * d1;
	q = vsnprintf_put_dec_helper4(p, q);
	p -= 4;

	q += 4749 * d3 + 42 * d2;
	q = vsnprintf_put_dec_helper4(p, q);
	p -= 4;

	q += 281 * d3;
	q = vsnprintf_put_dec_helper4(p, q);
	p -= 4;

	vsnprintf_put_dec_full4(p, q);
	p -= 4;

	/* strip off the extra 0's we printed */
	while (p < end && *p == '0')
		++p;

	return p;
}

static char *vsnprintf_number(char *end, UINT64 num, int base, char locase)
{
	/*
	 * locase = 0 or 0x20. ORing digits or letters with 'locase'
	 * produces same digits or (maybe lowercased) letters
	 */

	 /* we are called with base 8, 10 or 16, only, thus don't need "G..."  */
	UINT64 hexmap64[] = { 0x3736353433323130ULL, 0x4645444342413938ULL };
	char *digits = (char *)(hexmap64);

	switch (base) {
	case 10:
		if (num != 0)
			end = vsnprintf_put_dec(end, num);
		break;
	case 16:
		for (; num != 0; num >>= 4)
			*--end = digits[num & 0xf] | locase;
		break;
	default:
		/* unreachable(); */
		;
	}

	return end;
}

#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
#define SMALL	32		/* Must be 32 == 0x20 */
#define SPECIAL	64		/* 0x */
#define WIDE	128		/* UTF-16 string */

static int vsnprintf_get_flags(const char **fmt)
{
	int flags = 0;

	do {
		switch (**fmt) {
		case '-':
			flags |= LEFT;
			break;
		case '+':
			flags |= PLUS;
			break;
		case ' ':
			flags |= SPACE;
			break;
		case '#':
			flags |= SPECIAL;
			break;
		case '0':
			flags |= ZEROPAD;
			break;
		default:
			return flags;
		}
		++(*fmt);
	} while (1);
}

__declspec(noinline)
static int vsnprintf_get_int(const char **fmt, va_list *ap)
{
	if (vsprintf_isdigit(**fmt))
		return vsnprintf_skip_atoi(fmt);
	if (**fmt == '*') {
		++(*fmt);
		/* it's the next argument */
		return va_arg(*ap, int);
	}
	return 0;
}

static UINT64 vsnprintf_get_number(int sign, int qualifier, va_list *ap)
{
	if (sign) {
		switch (qualifier) {
		case 'z':
		case 'I':
			return va_arg(*ap, size_t);
		case 'L':
			return va_arg(*ap, long long);
		case 'l':
			return va_arg(*ap, long);
		case 'h':
			return (short)va_arg(*ap, int);
		case 'H':
			return (signed char)va_arg(*ap, int);
		default:
			return va_arg(*ap, int);
		};
	} else {
		switch (qualifier) {
		case 'z':
		case 'I':
			return va_arg(*ap, size_t);
		case 'L':
			return va_arg(*ap, unsigned long long);
		case 'l':
			return va_arg(*ap, unsigned long);
		case 'h':
			return (unsigned short)va_arg(*ap, int);
		case 'H':
			return (unsigned char)va_arg(*ap, int);
		default:
			return va_arg(*ap, unsigned int);
		}
	}
}

static char vsnprintf_get_sign(INT64 *num, int flags)
{
	if (!(flags & SIGN))
		return 0;
	if (*num < 0) {
		*num = -(*num);
		return '-';
	}
	if (flags & PLUS)
		return '+';
	if (flags & SPACE)
		return ' ';
	return 0;
}

static size_t vsnprintf_strnlen(const char *s, size_t maxlen)
{
	const char *es = s;
	while (*es && maxlen) {
		es++;
		maxlen--;
	}
	return (es - s);
}

static size_t vsnprintf_wcsnlen(const UINT16* s, size_t maxlen)
{
	const UINT16* es = s;
	while (*es && maxlen) {
		es++;
		maxlen--;
	}
	return (es - s);
}

#define PUTC(c)         \
do {				    \
	if (pos < size)		\
		buf[pos] = (c);	\
	++pos;			    \
} while (0);

int vsprintf_s(char *buf, size_t size, const char *fmt, va_list ap)
{
	/* The maximum space required is to print a 64-bit number in octal */
	char tmp[(sizeof(UINT64) * 8 + 2) / 3];
	char *tmp_end = &tmp[ARRAY_SIZE(tmp)];
	INT64 num;
	int base;
	const char *s;
	size_t len, pos;
	char sign;
	int flags;		/* flags to number() */
	int field_width;	/* width of output field */
	int precision;		/* min. # of digits for integers; max number of chars for from string */
	int qualifier;		/* 'h', 'hh', 'l' or 'll' for integer fields */
	va_list args;
	PUNICODE_STRING ustr;

	/*
	 * We want to pass our input va_list to helper functions by reference,
	 * but there's an annoying edge case. If va_list was originally passed
	 * to us by value, we could just pass &ap down to the helpers. This is
	 * the case on, for example, X86_32.
	 * However, on X86_64 (and possibly others), va_list is actually a
	 * size-1 array containing a structure. Our function parameter ap has
	 * decayed from T[1] to T*, and &ap has type T** rather than T(*)[1],
	 * which is what will be expected by a function taking a va_list *
	 * parameter.
	 * One standard way to solve this mess is by creating a copy in a local
	 * variable of type va_list and then passing a pointer to that local
	 * copy instead, which is what we do here.
	 */
	va_copy(args, ap);

	for (pos = 0; *fmt; ++fmt) {
		if (*fmt != '%' || *++fmt == '%') {
			PUTC(*fmt);
			continue;
		}

		/* process flags */
		flags = vsnprintf_get_flags(&fmt);

		/* get field width */
		field_width = vsnprintf_get_int(&fmt, &args);
		if (field_width < 0) {
			field_width = -field_width;
			flags |= LEFT;
		}

		if (flags & LEFT)
			flags &= ~ZEROPAD;

		/* get the precision */
		precision = -1;
		if (*fmt == '.') {
			++fmt;
			precision = vsnprintf_get_int(&fmt, &args);
			if (precision >= 0)
				flags &= ~ZEROPAD;
		}

		/* get the conversion qualifier */
		qualifier = -1;
		if (*fmt == 'h' || *fmt == 'l') {
			qualifier = *fmt;
			++fmt;
			if (qualifier == *fmt) {
				// L to l
				// H to h
				qualifier -= 'a' - 'A';
				++fmt;
			}
		} else if (*fmt == 'w' || *fmt == 'z' || *fmt == 'I') {
			qualifier = *fmt;
			++fmt;
		}

		sign = 0;

		switch (*fmt) {
		case 'c':
			flags &= LEFT;
			s = tmp;
			if (qualifier == 'l') {
				((UINT16*)tmp)[0] = (UINT16)va_arg(args, unsigned int);
				((UINT16*)tmp)[1] = L'\0';
				precision = INT_MAX;
				goto wstring;
			} else {
				tmp[0] = (unsigned char)va_arg(args, int);
				precision = 1;
				len = 1;
			}
			goto output;

		case 'Z':
			/* ANSI_STRING or UNICODE_STRING */
			flags &= LEFT;
			if (precision < 0)
				precision = INT_MAX;
			ustr = (PUNICODE_STRING)va_arg(args, void *);
			s = (const char *)ustr->Buffer;
			len = ustr->Length;
			if (qualifier == 'w') {
				flags |= WIDE;
				len >>= 1;
			}
			if (len > (size_t)precision)
				len = precision;
			precision = (int)len;
			goto output;
			
		case 's':
			flags &= LEFT;
			if (precision < 0)
				precision = INT_MAX;
			s = va_arg(args, void *);
			if (!s) {
				tmp[0] = '\0';
				s = tmp;
			} else if (qualifier == 'l') {
			wstring:
				flags |= WIDE;
				len = vsnprintf_wcsnlen((const UINT16*)s, (size_t)precision);
				precision = (int)len;
				goto output;
			}
			len = vsnprintf_strnlen(s, (size_t)precision);
			precision = (int)len;
			goto output;

			/* integer number formats - set up the flags and "break" */
		case 'p':
			if (precision < 0)
				precision = 2 * sizeof(void *);
			/* fallthrough */
		case 'x':
			flags |= SMALL;
			/* fallthrough */
		case 'X':
			base = 16;
			break;

		case 'd':
		case 'i':
			flags |= SIGN;
			/* fallthrough */
		case 'u':
			flags &= ~SPECIAL;
			base = 10;
			break;

		default:
			/*
			 * Bail out if the conversion specifier is invalid.
			 * There's probably a typo in the format string and the
			 * remaining specifiers are unlikely to match up with
			 * the arguments.
			 */
			goto fail;
		}
		if (*fmt == 'p') {
			num = (UINT64)va_arg(args, void *);
		} else {
			num = vsnprintf_get_number(flags & SIGN, qualifier, &args);
		}

		sign = vsnprintf_get_sign(&num, flags);
		if (sign)
			--field_width;

		s = vsnprintf_number(tmp_end, num, base, flags & SMALL);
		len = tmp_end - s;
		/* default precision is 1 */
		if (precision < 0)
			precision = 1;
		/* precision is minimum number of digits to print */
		if ((size_t)precision < len)
			precision = (int)len;
		if (flags & SPECIAL) {
			/*
			 * For octal, a leading 0 is printed only if necessary,
			 * i.e. if it's not already there because of the
			 * precision.
			 */
			if (base == 8 && precision == len)
				++precision;
			/*
			 * For hexadecimal, the leading 0x is skipped if the
			 * output is empty, i.e. both the number and the
			 * precision are 0.
			 */
			if (base == 16 && precision > 0)
				field_width -= 2;
			else
				flags &= ~SPECIAL;
		}
		/*
		 * For zero padding, increase the precision to fill the field
		 * width.
		 */
		if ((flags & ZEROPAD) && field_width > precision)
			precision = field_width;

	output:
		/* Calculate the padding necessary */
		field_width -= precision;
		/* Leading padding with ' ' */
		if (!(flags & LEFT))
			while (field_width-- > 0)
				PUTC(' ');
		/* sign */
		if (sign)
			PUTC(sign);
		/* 0x/0X for hexadecimal */
		if (flags & SPECIAL) {
			PUTC('0');
			PUTC('x');
		}
		/* Zero padding and excess precision */
		while (precision-- > len)
			PUTC('0');
		/* Actual output */
		if (flags & WIDE) {
			const UINT16* ws = (const UINT16*)s;

			while (len-- > 0) {
				UINT8 c8 = (UINT8)*ws++ & 0x7F;
				PUTC(c8);
			}
		} else {
			while (len-- > 0)
				PUTC(*s++);
		}
		/* Trailing padding with ' ' */
		while (field_width-- > 0)
			PUTC(' ');
	}
fail:
	va_end(args);

#define min____(x,y) ((x) < (y) ? x : y)
	if (size)
		buf[min____(pos, size - 1)] = '\0';

	return (int)pos;
}
