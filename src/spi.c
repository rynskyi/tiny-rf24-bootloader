#include <stdint.h>
#include <avr/io.h>

#define bit_set(reg, bit)       reg |= (1 << bit)
#define bit_clr(reg, bit)       reg &= ~(1 << bit)

/* Prepare SPI pins */
void spi_init() {
    bit_set(DDRA, 2);   // CE output
    bit_set(DDRA, 3);   // CSN output
    bit_set(DDRA, 4);   // SCK output
    bit_set(DDRA, 5);   // MOSI output
    bit_clr(DDRA, 6);   // MISO input
    
    // Init SPI (actually USI on Attiny)
    USICR = (1<<USIWM0);                // SPI Mode (3-Wire)
    USICR |= (1<<USICS1) |(1<<USICLK);  // Set up software clock counter        
};

/* CSN pin control */
void spi_csn(uint8_t state) {
    if(state) {
       bit_set(PORTA, 3);
    }
    else {
        bit_clr(PORTA, 3);
    }    
}

/* Hardware SPI */
uint8_t spi_transfer(uint8_t data) {
    USIDR = data;                           // Load data into the USI data register
    USISR = (1 << USIOIF);                  // Enable USI Counter overflow interrupt

    // Clock signal until USI data counter overflow flag to detect the end of transmission
    do {
        USICR |= (1 << USITC);
        USICR |= (1 << USITC);
    } while (!(USISR & (1 << USIOIF))); 

    USISR = (1 << USIOIF);                  // Clear interrupt flag
    return USIDR;                           // Return received data
}