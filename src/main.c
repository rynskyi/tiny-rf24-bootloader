#include <avr/boot.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>
#include <nrf24.h>


#define BOOTLOADER_ADDRESS      0x0400
#define PKG_MAGIK               0x7777
#define PKG_SIZE                4
#define RF24_CHANNEL            0x77
#define RF24_PIPE               {0x31, 0x31, 0x31, 0x31, 0x31}


#ifndef RWWSRE              // Bug in AVR libc:
    #define RWWSRE CTPB     // RWWSRE is not defined on ATTinys, use CTBP instead
#endif

#define BOOTLOADER_RJMP         0xC000 + (BOOTLOADER_ADDRESS / 2) - 1
#define bit_set(reg, bit)       reg |= (1 << bit)
#define bit_clr(reg, bit)       reg &= ~(1 << bit)
#define NOOP                    __asm__ volatile ("nop")
#define JUMP(addr)              __asm__ volatile ("rjmp %0" :: "i" (addr))
#define ijmp(addr)              asm volatile ("ijmp" :: "z" (addr));

#define TIMER0_OVRFLOW          (TIFR0 & (1 << OCF0A))
#define init_timer()                            \
    {                                           \
        TCCR0B = (1 << CS12) | (1 << CS10);     \
        OCR0A = 98;                             \
    }
// OCR0A: ((F_CPU / (PRESCALER * 1000)) * TIME_MS)


void write_page(uint32_t adress, uint8_t *buffer)
{
    uint16_t offset, word;
    // Erase the memory
    boot_page_erase_safe(adress);
    // Fill a temporary page
    for (offset = 0; offset < SPM_PAGESIZE; offset += 2)
    {
        // Create a block of data to store in the memory
        word = *buffer++;
        word += (*buffer++) << 8;
        // Write the word
        boot_page_fill_safe(adress + offset, word);
    }
    // Write the page into the flash memory
    boot_page_write_safe(adress);
    boot_rww_enable_safe();
    boot_spm_busy_wait();
}


int main()
{
    // Uploaded payload test program (long blink)
    // DDRA = (1 << 7);   // pin output: LED (A7)
    // for(;;) {
    //     _delay_ms(1500);
    //     bit_set(PORTA, 7);
    //     _delay_ms(1500);
    //     bit_clr(PORTA, 7);
    // }
    // return 0;
    
    // Disable all interrupts; we don't need them
    cli();

    // Configure pins
    DDRA = (1 << 7);   // pin output: LED (A7)
    DDRB = (1 << 1);   // pin output: LDO (B1)
    bit_set(PORTB, 1); // power on RF24 module

    // Wait for power on Rf24
    _delay_ms(10);

    // Init timer for timeout check
    init_timer();

    // Init RF24 Radio module
    uint8_t mac[5] = RF24_PIPE; // TODO: add to progmem
    nrf24_init();
    nrf24_config(RF24_CHANNEL, PKG_SIZE);
    nrf24_rx_address(mac);

    // Receive data
    uint8_t started = 0, i = 0;
    uint8_t page[SPM_PAGESIZE] = {0};
    int16_t length = 0;
    uint16_t address = 0x0000;
    
    while (!TIMER0_OVRFLOW)
    {
        if (nrf24_dataReady())
        {
            uint8_t pkg[PKG_SIZE];
            nrf24_getData(pkg);
        
            // Check for start package
            if (!started && (*(uint16_t*)pkg == PKG_MAGIK)) // First 2B of pkg: magik marker
            {
                started = 1;
                length = *(uint16_t*)(pkg + (PKG_SIZE-2));  // Last 2B of pkg: payload length
                bit_set(PORTA, 7);
                continue;
            }

            // Process each payload package
            if (started && length > 0)
            {
                TCNT0 = 0;                          // Rreset timer counter
                memcpy(page + i, pkg, PKG_SIZE);    // Fill temp page
                length -= PKG_SIZE;
                i += PKG_SIZE;

                // Patch reset vector in first word (rjmp to Bootloader)
                if (i == PKG_SIZE && address == 0) {
                    uint16_t app_addr = *(uint16_t*)page - 0xC000 + 1;
                    eeprom_write_word(0x00, app_addr);
                    *(uint16_t*)page = (uint16_t)BOOTLOADER_RJMP;
                }

                // Write page when it's full or when it's the last package
                if (i == SPM_PAGESIZE || length <= 0)
                {
                    write_page(address, page);
                    address += SPM_PAGESIZE;
                    i = 0;
                    memset(page, 0, SPM_PAGESIZE);
                }
            }

            // Finish
            if (started && length <= 0) {
                bit_clr(PORTA, 7);
                break;
            }
        }
    }

    nrf24_powerDown();

    for(uint32_t i = 0 ; i < 3; i++) {
        _delay_ms(500);
        bit_set(PORTA, 7);
        _delay_ms(500);
        bit_clr(PORTA, 7);
    }

    uint16_t app = eeprom_read_word(0x00);
    ijmp(app);
}