/* 02 - SHT31 okuma (I2C2, J6) + UART1 cikisi, bekleme dongusu ile */
#include <xc.h>
#include <stdint.h>
#include <stdio.h>

#pragma config FNOSC    = PRIPLL
#pragma config POSCMOD  = XT
#pragma config FPLLIDIV = DIV_2
#pragma config FPLLMUL  = MUL_20
#pragma config FPLLODIV = DIV_1
#pragma config FPBDIV   = DIV_1
#pragma config FWDTEN   = OFF
#pragma config ICESEL   = ICS_PGx2

#define FPB 80000000UL
#define SHT31_ADDR 0x44

void UART1_Init(unsigned int baud) {
    U1BRG  = (FPB / (16 * baud)) - 1;
    U1MODE = 0x8000;
    U1STA  = 0x1400;
}
void UART1_PutChar(char c) {
    while (U1STAbits.UTXBF);
    U1TXREG = c;
}
void UART1_PutString(const char *s) {
    while (*s) UART1_PutChar(*s++);
}

void I2C2_Init(void) {
    I2C2BRG = 394;
    I2C2CONbits.ON = 1;
}
uint8_t I2C2_Start(void) {
    I2C2CONbits.SEN = 1;
    uint32_t t = 100000;
    while (I2C2CONbits.SEN && t--) ;
    return (t > 0);
}
uint8_t I2C2_Stop(void) {
    I2C2CONbits.PEN = 1;
    uint32_t t = 100000;
    while (I2C2CONbits.PEN && t--) ;
    return (t > 0);
}
uint8_t I2C2_Write(uint8_t data) {
    I2C2TRN = data;
    uint32_t t = 100000;
    while (I2C2STATbits.TRSTAT && t--) ;
    if (t == 0) return 0;
    return !I2C2STATbits.ACKSTAT;
}
uint8_t I2C2_Read(uint8_t ack) {
    I2C2CONbits.RCEN = 1;
    uint32_t t = 100000;
    while (!I2C2STATbits.RBF && t--) ;
    uint8_t data = I2C2RCV;
    I2C2CONbits.ACKDT = ack ? 0 : 1;
    I2C2CONbits.ACKEN = 1;
    t = 100000;
    while (I2C2CONbits.ACKEN && t--) ;
    return data;
}

int SHT31_Read(float *temperature, float *humidity) {
    uint8_t d[6];
    if (!I2C2_Start())                      { I2C2_Stop(); return -1; }
    if (!I2C2_Write((SHT31_ADDR << 1) | 0)) { I2C2_Stop(); return -2; }
    if (!I2C2_Write(0x2C))                  { I2C2_Stop(); return -3; }
    if (!I2C2_Write(0x06))                  { I2C2_Stop(); return -3; }
    I2C2_Stop();

    { volatile uint32_t w = 400000; while (w--); }  // ~15 ms

    if (!I2C2_Start())                      { I2C2_Stop(); return -1; }
    if (!I2C2_Write((SHT31_ADDR << 1) | 1)) { I2C2_Stop(); return -4; }
    d[0] = I2C2_Read(1); d[1] = I2C2_Read(1); d[2] = I2C2_Read(1);
    d[3] = I2C2_Read(1); d[4] = I2C2_Read(1); d[5] = I2C2_Read(0);
    I2C2_Stop();

    uint16_t rt = (d[0] << 8) | d[1];
    uint16_t rh = (d[3] << 8) | d[4];
    *temperature = -45.0f + 175.0f * ((float)rt / 65535.0f);
    *humidity    = 100.0f * ((float)rh / 65535.0f);
    return 0;
}

int main(void) {
    AD1PCFG = 0xFFFF;

    TRISBbits.TRISB10 = 0;
    TRISBbits.TRISB11 = 0;
    TRISBbits.TRISB12 = 0;
    TRISBbits.TRISB13 = 0;
    TRISAbits.TRISA6  = 1;
    TRISAbits.TRISA7  = 1;
    TRISFbits.TRISF8  = 0;
    TRISFbits.TRISF2  = 1;
    LATBCLR = 0x3C00;

    UART1_Init(9600);
    I2C2_Init();

    UART1_PutString("\r\nSHT31 I2C2 testi basliyor...\r\n");

    while (1) {
        if (PORTAbits.RA6) LATBSET = (1 << 10); else LATBCLR = (1 << 10);
        if (PORTAbits.RA7) LATBSET = (1 << 11); else LATBCLR = (1 << 11);

        float t, h;
        char buf[80];
        int r = SHT31_Read(&t, &h);
        if (r == 0) {
            sprintf(buf, "Sicaklik: %.2f C | Nem: %.2f %%RH\r\n", t, h);
            UART1_PutString(buf);
        } else {
            sprintf(buf, "SHT31 okuma hatasi, kod=%d\r\n", r);
            UART1_PutString(buf);
        }
        { volatile uint32_t w = 8000000; while (w--); }
    }
    return 0;
}
