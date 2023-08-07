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
// #define sei() asm volatile("sei")
// #define cli() asm volatile("cli")
#define NOOP  __asm__ volatile ("nop")


#define PKG_SIZE            2
#define APP_ADDRESS         0x0400
#define RX_TIMEOUT_MS       1000


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

// TODO: as #define
void run_main_app()
{
    // jump to main program
    // cli();
    // TCCR0B = 0;
    // __asm__  volatile ("rjmp 0x0200");
    // asm volatile ("ijmp" :: "z" (APP_ADDRESS));
}

volatile uint16_t time = 0;

ISR(TIM0_COMPA_vect) {
    time += 100;  // time in ms, step: 100 ms
}

// COMPARE_VALUE  = ((F_CPU / (PRESCALER * 1000)) * INTERRUPT_INTERVAL_MS)
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

    // Timer test
    init_timer();

    // Init RF24 Radio module
    uint8_t mac[5] = {0x31, 0x31, 0x31, 0x31, 0x31}; // TODO: add to progmem
    nrf24_init();
    nrf24_config(0x77, PKG_SIZE);
    nrf24_rx_address(mac);

    // Receive data
    uint8_t start = 0, i = 0;
    uint16_t length = 0;
    uint8_t page[SPM_PAGESIZE] = {0};
    uint16_t address = APP_ADDRESS;
    
    while (time < 1000)
    {
        if (nrf24_dataReady())
        {
            uint8_t packg[PKG_SIZE];
            nrf24_getData(packg);
        
            // Check for start marker and get length
            if (!start && packg[0] == 0x77)
            {
                start = 1;
                length = packg[1];
                bit_set(PORTA, 7);
                continue;
            }

            // Receiving each package
            if (start && length > 0)
            {
                TCNT0 = 0;                          // reset timer
                memcpy(page + i, packg, PKG_SIZE);  // Fill temp page
                length -= PKG_SIZE;
                i += PKG_SIZE;

                // Write page when it's full or when there's the last package
                if (i == SPM_PAGESIZE || length == 0)
                {
                    write_page(address, page);
                    address += SPM_PAGESIZE;
                    i = 0;
                    memset(page, 0, SPM_PAGESIZE);
                }
            }

            // Finish
            if (start && length == 0) {
                bit_clr(PORTA, 7);
                break;
            }
        }
    }

    // bit_set(PORTA, 7);

    // run_main_app();

    // test page write to flash
    // bit_set(PORTA, 7);
    // uint8_t buf[SPM_PAGESIZE] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f};
    // write_page(APP_ADDRESS, buf);
    // bit_clr(PORTA, 7);

    for(;;) NOOP;

    // send
    // for(;;) {
    //   nrf24_send((uint8_t*) &data);
    //   data++;
    //   // wait for transmission
    //   while(nrf24_isSending());
    //   delay(1000);
    // }
}