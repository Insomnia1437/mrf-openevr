
#include "xparameters.h"
#include "xil_printf.h"
#include "xil_types.h"
#include "xstatus.h"
#include "xiic.h"
#include "sleep.h"
#include <string.h>

#if defined(XPAR_AXI_IIC_0_BASEADDR)
#define IIC_BASE XPAR_AXI_IIC_0_BASEADDR
#elif defined(XPAR_XIIC_0_BASEADDR)
#define IIC_BASE XPAR_XIIC_0_BASEADDR
#else
#error "AXI IIC base address not found"
#endif

#define EEPROM_ADDR  0x50
#define IDT_ADDR     0x6C
#define EEPROM_SIZE  256
#define IMAGE_SIZE   133
#define BLOCK_SIZE   8

// 0 = backup + inspection only
// 1 = program EEPROM + verify
#define ENABLE_WRITE 0

// The CRC calculated from the supplied 132 bytes.
#define EXPECTED_CRC 0xC4

// Timing Commander data, offsets 0x00-0x84.

static const u8 config_data[] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xEF,0x00,
    0x03,0x00,0x31,0x00,0x00,0x01,0x00,0x00,
    0x01,0x07,0x00,0x00,0x07,0x00,0x00,0x77,
    0x6D,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,
    0xFF,0xFF,0xFF,0x03,0x3F,0x00,0x2C,0x00,
    0x02,0x5E,0xD1,0x00,0x01,0x00,0x00,0xD0,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x0F,0x00,0x00,0x00,0x64,0x66,0x01,
    0x00,0x08,0x00,0x00,0x03,0x00,0x00,0x03,
    0x01,0x00,0x08,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xE4,0x0A,0x2B,0x20,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x27,0xCC,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xC4
};

static u8 old_image[EEPROM_SIZE];
static u8 verify_image[EEPROM_SIZE];
static u8 new_image[IMAGE_SIZE];


// Random read from 24AA025 EEPROM.
// Address-only write does not program EEPROM data.
static int eeprom_read(u8 offset, u8 *buf, u8 len)
{
    u8 addr = offset;

    if (XIic_DynSend(IIC_BASE, EEPROM_ADDR,
                     &addr, 1,
                     XIIC_REPEATED_START) != 1)
        return -1;

    if (XIic_DynRecv(IIC_BASE, EEPROM_ADDR,
                     buf, len) != len)
        return -1;

    return 0;
}

// Write up to 8 bytes, never crossing a page boundary.
static int eeprom_write(u8 offset, const u8 *data,
                        u8 len)
{
    u8 tx[BLOCK_SIZE + 1];

    if (len == 0 || len > BLOCK_SIZE)
        return -1;

    if (((offset & 0x0F) + len) > 16)
        return -1;

    tx[0] = offset;
    memcpy(&tx[1], data, len);

    int sent = XIic_DynSend(
        IIC_BASE, EEPROM_ADDR,
        tx, (u8)(len + 1), XIIC_STOP
    );

    if (sent != len + 1)
        return -1;

    // 24AA025 maximum internal write time: 5 ms.
    usleep(10000);
    return 0;
}

static int read_full_eeprom(u8 *dest)
{
    for (unsigned off = 0; off < EEPROM_SIZE;
         off += BLOCK_SIZE) {

        if (eeprom_read((u8)off, &dest[off],
                        BLOCK_SIZE) != 0) {
            xil_printf("Read error at %02X\r\n", off);
            return -1;
        }
    }

    return 0;
}

static void dump_eeprom(const u8 *data)
{
    for (unsigned off = 0; off < EEPROM_SIZE;
         off += 16) {

        xil_printf("%02X: ", off);

        for (unsigned j = 0; j < 16; j++)
            xil_printf("%02X ", data[off + j]);

        xil_printf("\r\n");
    }
}

static int idt_read(u16 reg, u8 *buf, u8 len)
{
    u8 addr[2] = {
        (u8)(reg >> 8),
        (u8)(reg & 0xFF)
    };

    if (XIic_DynSend(IIC_BASE, IDT_ADDR,
                     addr, 2,
                     XIIC_REPEATED_START) != 2)
        return -1;

    if (XIic_DynRecv(IIC_BASE, IDT_ADDR,
                     buf, len) != len)
        return -1;

    return 0;
}

static void dump_idt(void)
{
    static const u16 regs[] = {
        0x0025,0x0026,
        0x0028,0x0029,0x002A,
        0x0039,0x003D,0x003E,
        0x0045,0x0046,0x0047,
        0x0048,0x0049,0x004A,
        0x0063,0x0069,0x006C,0x006F,
        0x007A,0x007B,
        0x0200,0x0210,0x0211
    };

    xil_printf("\r\n=== 8T49N242 registers ===\r\n");

    for (unsigned i = 0;
         i < sizeof(regs)/sizeof(regs[0]); i++) {

        u8 value = 0;

        if (idt_read(regs[i], &value, 1) == 0)
            xil_printf("%04X = %02X\r\n",
                       regs[i], value);
        else
            xil_printf("%04X = READ ERROR\r\n",
                       regs[i]);
    }

    u8 status[2];

    if (idt_read(0x0210, status, 2) == 0) {
        xil_printf("BOOTFAIL = %d\r\n",
                   status[0] & 1);
        xil_printf("EEP_ERR  = %d\r\n",
                   (status[0] >> 1) & 1);
        xil_printf("EEPDONE  = %d\r\n",
                   status[1] & 1);
    }
}

int main(void)
{
    xil_printf("\r\n");
    xil_printf("====================================\r\n");
    xil_printf("PicoZed FMC2 EEPROM Programmer\r\n");
    xil_printf("====================================\r\n");

    if (XIic_DynInit(IIC_BASE) != XST_SUCCESS) {
        xil_printf("AXI IIC init failed\r\n");
        return -1;
    }

    if (sizeof(config_data) != IMAGE_SIZE) {
        xil_printf("ERROR: wrong data length!\r\n");
        return -1;
    }

    memcpy(new_image, config_data,
           IMAGE_SIZE);

    xil_printf("Image bytes = %d\r\n",
               (int)sizeof(config_data));
    xil_printf("Calculated CRC = %02X\r\n",
               new_image[0x84]);

    if (new_image[0x84] != EXPECTED_CRC) {
        xil_printf("ERROR: CRC unexpectedly changed\r\n");
        return -1;
    }

    // Check key target settings.
    if ((new_image[0x39] & 0x0C) != 0x0C ||
        new_image[0x3D] != 0x64 ||
        new_image[0x45] != 0x00 ||
        new_image[0x46] != 0x00 ||
        new_image[0x47] != 0x03 ||
        new_image[0x48] != 0x01 ||
        new_image[0x49] != 0x00 ||
        new_image[0x4A] != 0x08 ||
        (new_image[0x6F] & 0x0C) != 0) {
        xil_printf("ERROR: unexpected Q2/Q3 settings\r\n");
        return -1;
    }

    xil_printf("Target OUTEN(39) = %02X\r\n",
               new_image[0x39]);
    xil_printf("Target factory(7B) = %02X\r\n",
               new_image[0x7B]);

    xil_printf("\r\n=== BEFORE WRITE ===\r\n");

    if (read_full_eeprom(old_image) != 0)
        return -1;

    dump_eeprom(old_image);

    xil_printf("\r\nSave the above 256 bytes!\r\n");

    // Check if EEPROM already contains this image.
    int already_programmed =
        (memcmp(old_image, new_image,
                IMAGE_SIZE) == 0);

    if (already_programmed) {
        xil_printf("Image already programmed.\r\n");
        dump_idt();
        return 0;
    }

#if ENABLE_WRITE == 0
    xil_printf("\r\nREAD ONLY MODE\r\n");
    xil_printf("Set ENABLE_WRITE to 1 to program.\r\n");
    dump_idt();
    return 0;
#else
    xil_printf("\r\n=== WRITING EEPROM ===\r\n");

    for (unsigned off = 0; off < IMAGE_SIZE;
         off += BLOCK_SIZE) {

        unsigned len = IMAGE_SIZE - off;

        if (len > BLOCK_SIZE)
            len = BLOCK_SIZE;

        // Skip unchanged blocks.
        if (memcmp(&old_image[off],
                   &new_image[off], len) == 0) {
            xil_printf("Skip %02X\r\n", off);
            continue;
        }

        xil_printf("Write %02X (%d bytes)\r\n",
                   off, len);

        if (eeprom_write((u8)off,
                         &new_image[off],
                         (u8)len) != 0) {
            xil_printf("WRITE ERROR at %02X\r\n", off);
            return -1;
        }

        u8 check[BLOCK_SIZE];

        if (eeprom_read((u8)off, check,
                        (u8)len) != 0 ||
            memcmp(check, &new_image[off], len) != 0) {
            xil_printf("VERIFY ERROR at %02X\r\n", off);
            return -1;
        }
    }

    xil_printf("\r\n=== FINAL VERIFICATION ===\r\n");

    if (read_full_eeprom(verify_image) != 0)
        return -1;

    for (unsigned i = 0; i < EEPROM_SIZE; i++) {
        u8 expected = (i < IMAGE_SIZE) ?
                      new_image[i] : old_image[i];

        if (verify_image[i] != expected) {
            xil_printf("Mismatch at %02X: %02X != %02X\r\n",
                       i, verify_image[i], expected);
            return -1;
        }
    }

    dump_eeprom(verify_image);

    xil_printf("\r\nEEPROM WRITE + VERIFY PASS\r\n");

    xil_printf("\r\nCurrent clock chip state:\r\n");
    xil_printf("(May still reflect old configuration)\r\n");

    dump_idt();

    xil_printf("\r\nPower cycle the carrier.\r\n");
    xil_printf("Then run again in read-only mode.\r\n");

    return 0;
#endif
}

