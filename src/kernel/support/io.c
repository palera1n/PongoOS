/*
 * pongoOS - https://checkra.in
 *
 * Copyright (C) 2019-2023 checkra1n team
 *
 * This file is part of pongoOS.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <reent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <pongo.h>

int _fstat_r(struct _reent *reent, int file, struct stat *st) {
    *st = (struct stat){ .st_mode = S_IFCHR };
    return 0;
}

int _isatty_r(struct _reent *reent, int file) { return 1; }
#if 0
int _open(const char *name, int flags, int mode) { return -1; }
#endif
off_t _lseek_r(struct _reent *reent, int file, off_t ptr, int dir) { return 0; }
int _close_r(struct _reent *reent, int file) { return -1; }

static volatile uint32_t stdout_buf_off;
static volatile uint32_t stdout_buf_len;
static volatile bool stdout_blocking;
static lock stdout_lock;
#define STDOUT_BASE 0xfa0000000ULL
#define STDOUT_HEAD (char*)(STDOUT_BASE + stdout_buf_off)
#define STDOUT_TAIL (char*)(STDOUT_BASE + stdout_buf_off + stdout_buf_len)

extern char preemption_over;

#define STDIO_LOG_UART   (1 << 0)
#define STDIO_LOG_SCREEN (1 << 1)
#define STDIO_LOG_MAX    (STDIO_LOG_UART | STDIO_LOG_SCREEN)
static uint8_t stdio_flags[3] =
{
    [1] = STDIO_LOG_MAX,
    [2] = STDIO_LOG_MAX,
};

static void log_cmd(const char *cmd, char *args)
{
    uint8_t files = 0b110;
    uint8_t mask = STDIO_LOG_MAX;
    bool on;
    char *parts[3] = {};

    while(*args == ' ') ++args;
    for(size_t i = 0; i < 3; ++i)
    {
        char *delim = strchr(args, ' ');
        parts[i] = args;
        if(!delim)
        {
            goto skip;
        }
        *delim = '\0';
        args = delim + 1;
        while(*args == ' ') ++args;
    }
    if(*args != '\0')
    {
        iprintf("Too many arguments\n");
        goto help;
    }
skip:;
    if(parts[0][0] == '\0')
    {
        goto help;
    }

    size_t idx = 0;
    if(     strcmp(parts[idx], "stdout") == 0) { files = (1 << 1); ++idx; }
    else if(strcmp(parts[idx], "stderr") == 0) { files = (1 << 2); ++idx; }

    if(!parts[idx]) { iprintf("Too few arguments\n"); goto help; }
    if(     strcmp(parts[idx], "uart")   == 0) { mask = STDIO_LOG_UART;   ++idx; }
    else if(strcmp(parts[idx], "screen") == 0) { mask = STDIO_LOG_SCREEN; ++idx; }

    if(!parts[idx]) { iprintf("Too few arguments\n"); goto help; }
    if(     strcmp(parts[idx], "on")  == 0) { on = true;  ++idx; }
    else if(strcmp(parts[idx], "off") == 0) { on = false; ++idx; }
    else { iprintf("Bad arguments\n"); goto help; }

    for(uint8_t f = 0; f < 3; ++f)
    {
        if(files & (1 << f))
        {
            stdio_flags[f] = on ? (stdio_flags[f] | mask) : (stdio_flags[f] & ~mask);
        }
    }

    return;
help:;
    iprintf("Usage: log [stdout|stderr] [uart|screen] on|off\n");
}

void io_init(void)
{
    // Grab a page and map it three times, ringbuffer style
    uint64_t stdout_buf = ppage_alloc();
    map_range(STDOUT_BASE + (0 * STDOUT_BUFLEN), stdout_buf, STDOUT_BUFLEN, 3, 1, true);
    map_range(STDOUT_BASE + (1 * STDOUT_BUFLEN), stdout_buf, STDOUT_BUFLEN, 3, 1, true);
    map_range(STDOUT_BASE + (2 * STDOUT_BUFLEN), stdout_buf, STDOUT_BUFLEN, 3, 1, true);
    command_register("log", "control stdio logging", log_cmd);
}

void set_stdout_blocking(bool block)
{
    lock_take(&stdout_lock);
    stdout_blocking = block;
    lock_release(&stdout_lock);
}

void fetch_stdoutbuf(char *to, uint32_t *len)
{
    lock_take(&stdout_lock);
    uint32_t sz = *len;
    if(sz > stdout_buf_len)
    {
        sz = stdout_buf_len;
    }
    memcpy(to, STDOUT_HEAD, sz);
    *len = sz;
    stdout_buf_off = (stdout_buf_off + sz) % STDOUT_BUFLEN;
    stdout_buf_len -= sz;
    lock_release(&stdout_lock);
}

ssize_t _write_r(struct _reent *reent, int file, const void *ptr, size_t len)
{
    switch(file)
    {
        case 1: if (preemption_over) file = 2;
        case 2: break;
        default: panic("Write to unknown fd: %d", file);
    }
    const char *str = ptr;
    uint8_t flags = stdio_flags[file];
    if(flags & STDIO_LOG_MAX)
    {
        for(size_t i = 0; i < len; ++i)
        {
            if(flags & STDIO_LOG_UART)
            {
                if(str[i] == '\0')
                {
                    serial_putc('\r');
                    dockchannel_putc('\r');;
                }
                serial_putc(str[i]);
                dockchannel_putc(str[i]);
            }
            if(flags & STDIO_LOG_SCREEN)
            {
                screen_putc(str[i]);
            }
        }
    }
    if(file == 1)
    {
        extern uint64_t dis_int_count;
        if(dis_int_count != 0)
        {
            panic("write() to stdout with interrupts disabled - please use stderr instead\n");
        }
        while(len > 0)
        {
            lock_take(&stdout_lock);
            uint32_t oldlen = stdout_buf_len;
            if(stdout_blocking)
            {
                size_t max = STDOUT_BUFLEN - oldlen;
                if(max > len)
                {
                    max = len;
                }
                if(!max)
                {
                    lock_release(&stdout_lock);
                    task_yield();
                    continue;
                }
                memcpy(STDOUT_TAIL, str, max);
                stdout_buf_len = oldlen + max;
                str += max;
                len -= max;
            }
            else
            {
                size_t max = STDOUT_BUFLEN;
                if(max > len)
                {
                    max = len;
                }
                memcpy(STDOUT_TAIL, str + len - max, max);
                uint32_t newlen = oldlen + max;
                if(newlen > STDOUT_BUFLEN)
                {
                    stdout_buf_off = (stdout_buf_off + (newlen - STDOUT_BUFLEN)) % STDOUT_BUFLEN;
                    newlen = STDOUT_BUFLEN;
                }
                stdout_buf_len = newlen;
                len = 0;
            }
            lock_release(&stdout_lock);
        }
    }
    return len;
}

lock stdin_lock;
char stdin_buf[512];
struct event stdin_ev;
uint32_t bufoff = 0;
extern uint32_t uart_should_drop_rx;
void queue_rx_char(char inch) {
    lock_take(&stdin_lock);
    if (inch == '\x7f') {
        if (bufoff) {
            bufoff--;
            stdin_buf[bufoff] = 0;
            putc('\b', stderr);
            putc(' ', stderr);
            putc('\b', stderr);
            fflush(stderr);
        }
        lock_release(&stdin_lock);
        return;
    }
    if (!uart_should_drop_rx) {
        putc(inch, stderr);
        fflush(stderr);
    }
    if (bufoff < 512)
        stdin_buf[bufoff++] = inch;
    if (inch == '\n')
        event_fire(&stdin_ev);
    lock_release(&stdin_lock);
}
void queue_rx_string(char* string) {
    while (*string) queue_rx_char(*string++);
}
ssize_t _read_r(struct _reent *reent, int file, void *ptr, size_t len) {
    if (!len) return len;
    ssize_t readln = 0;
    lock_take(&stdin_lock);
    while (!bufoff) {
        lock_release(&stdin_lock);
        event_wait(&stdin_ev);
        lock_take(&stdin_lock);
    }
    if (bufoff) {
        // 1. calculate memcpy length (l o l signedness)
        if (bufoff > len) readln = len;
        else if (bufoff == len) readln = bufoff;
        else if (bufoff < len) readln = bufoff;
        else panic("_read: shouldn't be reachable!");
        // 2. perform memcpy
        memcpy(ptr, stdin_buf, readln);
        // 3. update buffer in case of under-read
        if (bufoff > len) {
            memcpy(stdin_buf, stdin_buf+len, bufoff - readln);
        }
        bufoff -= readln;
    } else panic("_read: shouldn't be reachable!");
    lock_release(&stdin_lock);
    return readln;
}
