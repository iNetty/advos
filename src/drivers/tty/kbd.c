/*_
 * Copyright (c) 2019 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <unistd.h>
#include <mki/driver.h>
#include "tty.h"

#define KBD_OK          0
#define KBD_ERROR       -1
#define KBD_MAX_RETRY   0x01000000

#define KBD_CONTROLLER  0x0064
#define KBD_CTRL_STATUS KBD_CONTROLLER
#define KBD_CTRL_COMMAND KBD_CONTROLLER

/* Keyboard encoder */
#define KBD_ENC            0x0060
#define KBD_ENC_BUF        KBD_ENC
#define KBD_ENC_CMD        KBD_ENC

/* Controller */
#define KBD_CTRL           0x0064
#define KBD_CTRL_STAT      KBD_CTRL
#define KBD_CTRL_CMD       KBD_CTRL

#define KBD_STAT_OBUF 0x01
#define KBD_STAT_IBUF 0x02

#define KBD_ENC_CMD_SETLED      0xed
#define KBD_ENC_CMD_ENABLE      0xF4
#define KBD_ENC_CMD_DISABLE     0xF5

/* LED status */
#define KBD_LED_NONE            0x00000000
#define KBD_LED_SCROLLLOCK      0x00000001
#define KBD_LED_NUMLOCK         0x00000002
#define KBD_LED_CAPSLOCK        0x00000004


/* Commands */
#define KBD_CTRL_CMD_DISABLE    0xad
#define KBD_CTRL_CMD_ENABLE     0xae
#define KBD_CTRL_CMD_SELFTEST   0xaa

/* Status of self test */
#define KBD_CTRL_STAT_SELFTEST_OK   0x55
#define KBD_CTRL_STAT_SELFTEST_NG   0xfc

/* Default keymap */
static unsigned char keymap_base[] =
    "  1234567890-=\x08\t"      /* 0x00-0x0f */
    "qwertyuiop[]\r as"         /* 0x10-0x1f */
    "dfghjkl;'` \\zxcv"         /* 0x20-0x2f */
    "bnm,./          "          /* 0x30-0x3f */
    "                "          /* 0x40-0x4f */
    "                "          /* 0x50-0x5f */
    "                "          /* 0x60-0x6f */
    "                ";         /* 0x70-0x7f */
static unsigned char keymap_shift[] =
    "  !@#$%^&*()_+\x08\t"      /* 0x00-0x0f */
    "QWERTYUIOP{}\r AS"         /* 0x10-0x1f */
    "DFGHJKL:\"~ |ZXCV"         /* 0x20-0x2f */
    "BNM<>?          "          /* 0x30-0x3f */
    "                "          /* 0x40-0x4f */
    "                "          /* 0x50-0x5f */
    "                "          /* 0x60-0x6f */
    "                ";         /* 0x70-0x7f */

/*
 * Read control status
 */
static unsigned char
_read_ctrl_status(void)
{
    return driver_in8(KBD_CTRL_STAT);
}

/*
 * Write a cntrol command
 */
static int
_write_ctrl_cmd(unsigned char cmd)
{
    int retry;

    /* Retry until it succeeds or exceeds retry max */
    for ( retry = 0; retry < KBD_MAX_RETRY; retry++ ) {
        if ( 0 == (_read_ctrl_status() & KBD_STAT_IBUF) ) {
            driver_out8(KBD_CTRL_CMD, cmd);
            return KBD_OK;
        }
    }

    return KBD_ERROR;
}

/*
 * Read from the keyboard encoder
 */
static unsigned char
_enc_read_buf(void)
{
    return driver_in8(KBD_ENC_BUF);
}

/*
 * Write command to the keyboard encoder
 */
static int
_enc_write_cmd(unsigned char cmd)
{
    int retry;

    for ( retry = 0; retry < KBD_MAX_RETRY; retry++ ) {
        if ( 0 == (_read_ctrl_status() & KBD_STAT_IBUF) ) {
            driver_out8(KBD_ENC_CMD, cmd);
            return KBD_OK;
        }
    }

    return KBD_ERROR;
}

/*
 * Wait until the output buffer becomes full
 */
static int
_wait_until_outbuf_full(void)
{
    unsigned char stat;
    int retry;

    for ( retry = 0; retry < KBD_MAX_RETRY; retry++ ) {
        stat = _read_ctrl_status();

        if ( KBD_STAT_OBUF == (stat & KBD_STAT_OBUF) ) {
            return KBD_OK;
        }
    }

    return KBD_ERROR;
}

/*
 * Initialize the keyboard
 */
int
kbd_init(kbd_t *kbd)
{
    int ret;

    kbd->disabled = 0;

    /* Initialize the key states */
    kbd->state.lctrl = 0;
    kbd->state.rctrl = 0;
    kbd->state.lshift = 0;
    kbd->state.rshift = 0;
    kbd->state.capslock = 0;
    kbd->state.numlock = 0;
    kbd->state.scrplllock = 0;
    kbd->state.insert = 0;

    /* Set LED according to the state */
    ret = kbd_set_led(kbd);
    if ( KBD_OK != ret ) {
        return -1;
    }

    return 0;
}

/*
 * Set LED by the key state
 */
int
kbd_set_led(kbd_t *kbd)
{
    int ret;
    int led;

    /* LED status from the key stateus */
    led = KBD_LED_NONE;
    if ( kbd->state.scrplllock ) {
        led |= KBD_LED_SCROLLLOCK;
    }
    if ( kbd->state.numlock ) {
        led |= KBD_LED_NUMLOCK;
    }
    if ( kbd->state.capslock ) {
        led |= KBD_LED_CAPSLOCK;
    }

    ret = _enc_write_cmd(KBD_ENC_CMD_SETLED);
    ret |= _enc_write_cmd((unsigned char)led);

    return ret;
}

/*
 * Perform self-test
 */
int
kbd_selftest(void)
{
    unsigned char encbuf;
    int stat;

    stat = _write_ctrl_cmd(KBD_CTRL_CMD_SELFTEST);
    if ( KBD_OK != stat ) {
        return stat;
    }

    /* Wait until output buffer becomes full */
    stat = _wait_until_outbuf_full();
    if ( KBD_OK != stat ) {
        return stat;
    }

    /* Check the self-test result */
    encbuf = _enc_read_buf();
    if ( KBD_CTRL_STAT_SELFTEST_OK == encbuf ) {
        /* KBD_OK */
        return stat;
    }

    return KBD_ERROR;
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
