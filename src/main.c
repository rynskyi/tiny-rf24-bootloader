#include <avr/boot.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>
#include <nrf24.h>


#ifndef RWWSRE              // Bug in AVR libc:
    #define RWWSRE CTPB     // RWWSRE is not defined on ATTinys, use CTBP instead
#endif


#define APP_ADDR                0x0000
#define BOOTLOADER_ADDR         0x0400
#define PKG_MAGIK               0x7777
#define PKG_SIZE                4
#define RF24_CHANNEL            0x77
#define RF24_NODE_ADDR          "12345"

#define EEPROM_ADDR             (uint16_t *)(E2END - 2)     // Last 2 bytes in EEPROM memory
#define RJMP_OPCODE             (uint16_t)0xC000            // RJMP opcode - 1100LLLLLLLLLLLL
#define TIMEOUT                 (TIFR0 & (1 << OCF0A))      // TIMER0 overflow check macros

#define bit_set(reg, bit)       reg |= (1 << bit)
#define bit_clr(reg, bit)       reg &= ~(1 << bit)
#define ijmp(addr)              asm volatile ("ijmp" :: "z" (addr));


// Write one page to Flash memory
void write_page(uint16_t adress, uint8_t *buffer)
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
    // Disable all interrupts; we don't need them
    cli();

    // Configure pins    
    bit_set(DDRB, 1);   // LDO output
    bit_set(DDRA, 7);   // LED output
    bit_set(DDRA, 2);   // CE output
    bit_set(PORTB, 1);  // Power On RF24
    bit_set(PORTA, 2);  // CE high permanently

    // Wait for power on RF24
    _delay_ms(10);

    // Init timer for timeout check (100ms on 1Mhz CPU)
    TCCR0B = (1 << CS12) | (1 << CS10);
    OCR0A = 98; // Calc: (F_CPU / (PRESCALER * 1000)) * ms

    // Init RF24 Radio module
    nrf24_init();
    nrf24_config(RF24_CHANNEL, PKG_SIZE);
    nrf24_rx_address((uint8_t*) RF24_NODE_ADDR);

    // Receive data
    uint8_t started = 0, i = 0;
    uint8_t page[SPM_PAGESIZE] = {0};
    int16_t length = 1;  // signed!
    uint16_t address = APP_ADDR;
    
    while (!TIMEOUT && i < length)
    {
        if (nrf24_dataReady())
        {
            uint8_t pkg[PKG_SIZE];
            nrf24_getData(pkg);
        
            // Check for start package
            if (!started && *(uint16_t*)pkg == PKG_MAGIK)   // First 2B of pkg: magik marker
            {
                started = 1;
                length = *(uint16_t*)(pkg + (PKG_SIZE-2));  // Last 2B of pkg: payload length
                bit_set(PORTA, 7);                          // On LED during flashing
                continue;
            }

            // Process each payload package
            if (started)
            {
                TCNT0 = 0;                                          // Rreset timer counter
                memcpy(page + (i % SPM_PAGESIZE), pkg, PKG_SIZE);   // Fill temp page

                // Patch app reset vector and save original to eeprom
                if (i == 0) {
                    uint16_t app_addr = *(uint16_t*)page - RJMP_OPCODE + 1;
                    eeprom_write_word(EEPROM_ADDR, app_addr);
                    *(uint16_t*)page = RJMP_OPCODE + (BOOTLOADER_ADDR / 2) - 1;
                }

                i += PKG_SIZE;

                // Write page when it's full or when it's the end
                if (i % SPM_PAGESIZE == 0 || i >= length)
                {
                    write_page(address, page);
                    address += SPM_PAGESIZE;
                    memset(page, 0, SPM_PAGESIZE);
                }
            }
        }
    }

    // Reset: pins, timer, interrupt
    PORTA = PORTB = DDRA = DDRB = TCCR0B = OCR0A = 0;
    sei();

    // for(uint8_t i = 0 ; i < 3; i++) {
    //     _delay_ms(500);
    //     bit_set(PORTA, 7);
    //     _delay_ms(500);
    //     bit_clr(PORTA, 7);
    // }

    // Jump to main app code
    uint16_t app = eeprom_read_word(EEPROM_ADDR);
    ijmp(app);
}