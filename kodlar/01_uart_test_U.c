/* 01 - UART1 testi: PuTTY'de (COM6, 9600) surekli "U" gorulur.
   LD3 yanik = baslangic bitti, LD4 yanip soner = dongu calisiyor. */
#include <xc.h>
#include <stdint.h>

#pragma config FNOSC    = PRIPLL
#pragma config POSCMOD  = XT
#pragma config FPLLIDIV = DIV_2
#pragma config FPLLMUL  = MUL_20
#pragma config FPLLODIV = DIV_1
#pragma config FPBDIV   = DIV_1
#pragma config FWDTEN   = OFF
#pragma config ICESEL   = ICS_PGx2

#define FPB 80000000UL

int main(void) {
    AD1PCFG = 0xFFFF;

    TRISBbits.TRISB10 = 0;
    TRISBbits.TRISB11 = 0;
    TRISBbits.TRISB12 = 0;
    TRISBbits.TRISB13 = 0;
    LATBCLR = 0x3C00;

    TRISFbits.TRISF8 = 0;   // U1TX cikis
    TRISFbits.TRISF2 = 1;   // U1RX giris

    U1BRG  = (FPB / (16 * 9600UL)) - 1;
    U1MODE = 0x8000;
    U1STA  = 0x1400;

    LATBSET = (1 << 12);

    while (1) {
        while (U1STAbits.UTXBF);
        U1TXREG = 'U';
        LATBINV = (1 << 13);
        for (volatile uint32_t d = 0; d < 4000000; d++);
    }
    return 0;
}
