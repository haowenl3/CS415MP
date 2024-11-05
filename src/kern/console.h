//           console.h - Console i/o
//           

#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <stddef.h>
#include <stdarg.h>

extern void console_init(void);
extern int console_initialized;

extern void console_putchar(char c);
extern char console_getchar(void);
extern void console_puts(const char * str);
extern size_t console_printf(const char * fmt, ...);
extern size_t console_vprintf(const char * fmt, va_list ap);

//           alias for console_printf
extern size_t kprintf(const char * fmt, ...);

extern void console_labeled_printf (
    const char * label,
    const char * src_flname,
    int src_lineno,
    const char * fmt, ...);

#ifdef DEBUG
#define debug(...) console_labeled_printf("DEBUG", __FILE__, __LINE__, __VA_ARGS__)
#else
#define debug(...) do {} while(0)
#endif

#ifdef TRACE
#define trace(...) console_labeled_printf("TRACE", __FILE__, __LINE__, __VA_ARGS__)
#else
#define trace(...) do {} while(0)
#endif

//           The following must be defined elsewhere, to be used for console I/O.

extern void com0_init(void);
extern void com0_putc(char c);
extern char com0_getc(void);

//           _CONSOLE_H_
#endif
