#include <avr/boot.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <string.h>
#include <nrf24.h>

#ifndef RWWSRE      // bug in AVR libc:
#define RWWSRE CTPB // RWWSRE is not defined on ATTinys, use CTBP instead
#endif

#define bit_set(reg, bit)   reg |= (1 << bit)
#define bit_clr(reg, bit)   reg &= ~(1 << bit)
#define NOOP                __asm__ volatile ("nop")
#define JUMP(addr)          __asm__ volatile ("rjmp %0" :: "i" (addr))


#define BOOT_ADDRESS            0x0400
#define PKG_MAGIK               0x7777
#define PKG_SIZE                4
#define RF24_CHANNEL            0x77
#define RF24_PIPE               {0x31, 0x31, 0x31, 0x31, 0x31}
#define RF24_PIPE_RX_TIMEOUT    1000

void write_page(uint32_t adress, uint8_t *buffer)
{
    uint16_t offset, word;
    uint8_t sreg;
    // Disable interrupts
    sreg = SREG;
    cli();
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
    // Re-enable interrupts
    SREG = sreg;
    sei();
}

volatile uint16_t time = 0;

ISR(TIM0_COMPA_vect) {
    time += 100;  // time in ms, step: 100 ms
}

// COMPARE_VALUE  = ((F_CPU / (PRESCALER * 1000)) * INTERRUPT_INTERVAL_MS)
// OCR0A = ((F_CPU / (1024 * 1000UL)) * 200);
// Interupt every 100 ms
void init_timer() {
    TCCR0A = (1 << WGM01);                  // Clear Timer on Compare Match
    TCCR0B = (1 << CS12) | (1 << CS10);     // Set prescaler to dividee to 1024
    // TCCR0B = (1 << CS01) | (1 << CS00);  // Set prescaler to dividee to 64
    OCR0A = 98;                 // Compare value
    TIMSK0 |= (1 << OCIE0A);    // Enable compare match interrupt
    sei();                      // Enable global interrupts
}

int main()
{
    // Configure pins
    DDRA = (1 << 7);   // pin output: LED (A7)
    DDRB = (1 << 1);   // pin output: LDO (B1)
    bit_set(PORTB, 1); // power on RF24 module

    // Led blink
    bit_set(PORTA, 7);
    _delay_ms(1000);
    bit_clr(PORTA, 7);
    _delay_ms(1000);

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
    uint16_t address = BOOT_ADDRESS;
    
    while (time < 1000)
    {
        if (nrf24_dataReady())
        {
            uint8_t pkg[PKG_SIZE];
            nrf24_getData(pkg);
        
            // Check for start package
            if (!started && (*(uint16_t*)pkg == PKG_MAGIK))   // first 2b of pkg: magik marker
            {
                started = 1;
                length = *(uint16_t*)(pkg + (PKG_SIZE-2));  // last 2b of pkg: payload length
                bit_set(PORTA, 7);
                continue;
            }

            // Process each payload package
            if (started && length > 0)
            {
                TCNT0 = 0;                          // reset timer
                memcpy(page + i, pkg, PKG_SIZE);    // Fill temp page
                length -= PKG_SIZE;
                i += PKG_SIZE;

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
    for(;;) NOOP;
    // JUMP(BOOT_ADDRESS);
}