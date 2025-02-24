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
#include <pongo.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>

extern void queue_rx_char(char inch);

#define UPLOADSZ (1024 * 1024)
#define UPLOADSZ_MAX (1024 * 1024 * 128)

static bool usbloader_is_waiting_xfer = false;

uint8_t *loader_xfer_recv_data;
uint32_t loader_xfer_recv_size;
uint32_t loader_xfer_recv_count;
static uint32_t loader_xfer_size;

void usbloader_init(void)
{
    loader_xfer_recv_data = alloc_contig(UPLOADSZ);
    loader_xfer_size = UPLOADSZ;
    loader_xfer_recv_size = UPLOADSZ;
    loader_xfer_recv_count = 0;
}

void resize_loader_xfer_data(uint32_t newsz)
{
    if(newsz > UPLOADSZ_MAX)
    {
        panic("resize_loader_xfer_data");
    }
    disable_interrupts();
    if(newsz > loader_xfer_recv_size)
    {
        uint8_t *new_xfer_buffer = alloc_contig(newsz);
        memcpy(new_xfer_buffer, loader_xfer_recv_data, loader_xfer_recv_count);
        free_contig(loader_xfer_recv_data, loader_xfer_recv_size);
        loader_xfer_recv_size = newsz;
        loader_xfer_recv_data = new_xfer_buffer;
    }
    enable_interrupts();
}

static void usbloader_bulk_upload_done(void *data, uint32_t size, uint32_t transferred)
{
    if(!usbloader_is_waiting_xfer)
    {
        panic("usbloader_bulk_upload_done");
    }
    // XXX: this seems both wrong and unnecessary since ep_out_recv_data_done() should invalidate already?
    //cache_invalidate(loader_xfer_recv_data, loader_xfer_recv_count);
    loader_xfer_recv_count = transferred;
    usbloader_is_waiting_xfer = false;
}

static bool usbloader_bulk_upload_start(const void *data, uint32_t size)
{
    uint32_t newsz = 0;
    if(data)
    {
        if(size != 4)
        {
            panic("usbloader_bulk_upload_start");
        }
        // Round up to USB BULK packet size
        newsz = (*(uint32_t*)data + 0x1ff) & ~0x1ff;
        if(newsz > UPLOADSZ_MAX)
        {
            return false;
        }
    }
    loader_xfer_recv_count = 0;
    usbloader_is_waiting_xfer = true;
    if(data)
    {
        resize_loader_xfer_data(newsz);
        loader_xfer_size = newsz;
    }
    usb_out_transfer_dma(2, loader_xfer_recv_data, vatophys((uint64_t)loader_xfer_recv_data), loader_xfer_size, usbloader_bulk_upload_done); // should resolve the VA rather than doing this, but oh well.
    return true;
}

static bool should_wait_for_cmd_handler = false;

static bool usbloader_write_stdin(const void *data, uint32_t size)
{
    enable_interrupts();
    const char *cmd = (const char*)data;
    for(uint32_t i = 0; i < size && cmd[i] != '\0'; ++i)
    {
        queue_rx_char(cmd[i]);
    }
    if(should_wait_for_cmd_handler)
    {
        event_wait(&command_handler_iter);
    }
    disable_interrupts();
    return true;
}

bool ep0_device_request(struct setup_packet *setup)
{
    switch(setup->bmRequestType)
    {
        case 0x21: // OUT (HOST2DEVICE)
            switch(setup->bRequest)
            {
                case 1: // initiate bulk upload
                    // TODO: re-evaluate possibility of aborting at this level?
                    if(setup->wValue != 0 || setup->wIndex != 0 || usbloader_is_waiting_xfer)
                    {
                        return false;
                    }
                    if(setup->wLength == 4) // specify new buffer size
                    {
                        ep0_begin_data_out_stage(usbloader_bulk_upload_start);
                        return true;
                    }
                    if(setup->wLength == 0) // use existing buffer size
                    {
                        usbloader_bulk_upload_start(NULL, 0);
                        return true;
                    }
                    break;

                case 2: // discard uploaded data
                    if(setup->wValue != 0 || setup->wIndex != 0 || setup->wLength != 0)
                    {
                        return false;
                    }
                    // TODO: support for abort?
                    if(!usbloader_is_waiting_xfer)
                    {
                        loader_xfer_recv_count = 0;
                    }
                    return true;

                case 3: // write to stdin
                    if(setup->wValue != 0 || setup->wIndex != 0 || setup->wLength == 0 || setup->wLength > 512)
                    {
                        return false;
                    }
                    ep0_begin_data_out_stage(usbloader_write_stdin);
                    return true;

                case 4: // change stdio behaviour
                    if(setup->wIndex != 0 || setup->wLength != 0)
                    {
                        return false;
                    }
                    switch(setup->wValue)
                    {
                        case 0: // make stdin block until the command returns
                            should_wait_for_cmd_handler = true;
                            set_stdout_blocking(false);
                            return true;
                            break;

                        case 1: // make stdout block until async check-in
                            should_wait_for_cmd_handler = false;
                            set_stdout_blocking(true);
                            return true;
                            break;

                        case 0xffff: // reset all
                            should_wait_for_cmd_handler = false;
                            set_stdout_blocking(false);
                            return true;
                            break;
                    }
                    break;
            }
            break;

        case 0xa1: // IN (DEVICE2HOST)
            switch(setup->bRequest)
            {
                case 1: // read from stdout
                {
                    if(setup->wValue != 0 || setup->wIndex != 0 || (setup->wLength != 512 && setup->wLength != 0x1000))
                    {
                        return false;
                    }
                    static char stdoutbuf_copy[STDOUT_BUFLEN];
                    int xferlen = setup->wLength;
                    char *buf = stdoutbuf_copy;
                    fetch_stdoutbuf(buf, &xferlen);
                    if(xferlen > setup->wLength)
                    {
                        buf += xferlen - setup->wLength;
                        xferlen = setup->wLength;
                    }
                    ep0_begin_data_in_stage(buf, xferlen, NULL);
                    return true;
                }

                case 2: // check for async command completion status
                {
                    if(setup->wValue != 0 || setup->wIndex != 0 || setup->wLength != 1)
                    {
                        return false;
                    }
                    uint8_t inprog = command_in_progress;
                    ep0_begin_data_in_stage(&inprog, 1, NULL);
                    return true;
                }
            }
            break;
    }
    return false;
}
