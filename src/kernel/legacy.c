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
#include <stdio.h>
#include <pongo.h>
// Legacy support - this was exported to modules

void print_register(uint64_t value)
{
    iprintf("0x%016llx\n", value);
}

void command_puts(const char* c)
{
    puts(c);
}

void command_putc(char c)
{
    putc(c, stdout);
}

void* memstr(const void* big, unsigned long blength, const char* little)
{
    return memmem(big, blength, (void*)little, strlen(little) + 1);
}
void* memstr_partial(const void* big, unsigned long blength, const char* little)
{
    return memmem(big, blength, (void*)little, strlen(little));
}

