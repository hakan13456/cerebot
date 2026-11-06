/* 03 - Timer2 kesmesi (10 ms tik) + iki durumlu FSM, bekleme dongusu yok */
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

volatile uint32_t ticks = 0;

void __attribute__((vector(_TIMER_2_VECTOR), interrupt(IPL2SOFT), nomips16))
Timer2Handler(void) {
    ticks++;
    IFS0CLR = _IFS0_T2IF_MASK;
}

void Timer2_Init(void) {
    T2CON = 0;
    TMR2  = 0;
    T2CONbits.TCKPS = 7;          // 1:256 -> 312500 Hz
    PR2   = 3125 - 1;             // 10 ms
    IPC2bits.T2IP = 2;
    IPC2bits.T2IS = 0;
    IFS0CLR = _IFS0_T2IF_MASK;
    IEC0SET = _IEC0_T2IE_MASK;
    T2CONbits.ON = 1;
}

void UART1_Init(unsigned int baud) {
    U1BRG  = (FPB / (16 * baud)) - 1;
    U1MODE = 0x8000;
    U1STA  = 0x1400;
}
void UART1_PutChar(char c) { while (U1STAbits.UTXBF); U1TXREG = c; }
void UART1_PutString(const char *s) { while (*s) UART1_PutChar(*s++); }

void I2C2_Init(void) { I2C2BRG = 394; I2C2CONbits.ON = 1; }
uint8_t I2C2_Start(void) {
    I2C2CONbits.SEN = 1; uint32_t t = 100000;
    while (I2C2CONbits.SEN && t--) ; return (t > 0);
}
uint8_t I2C2_Stop(void) {
    I2C2CONbits.PEN = 1; uint32_t t = 100000;
    while (I2C2CONbits.PEN && t--) ; return (t > 0);
}
uint8_t I2C2_Write(uint8_t data) {
    I2C2TRN = data; uint32_t t = 100000;
    while (I2C2STATbits.TRSTAT && t--) ;
    if (t == 0) return 0;
    return !I2C2STATbits.ACKSTAT;
}
uint8_t I2C2_Read(uint8_t ack) {
    I2C2CONbits.RCEN = 1; uint32_t t = 100000;
    while (!I2C2STATbits.RBF && t--) ;
    uint8_t data = I2C2RCV;
    I2C2CONbits.ACKDT = ack ? 0 : 1;
    I2C2CONbits.ACKEN = 1; t = 100000;
    while (I2C2CONbits.ACKEN && t--) ;
    return data;
}

int SHT31_Start(void) {
    if (!I2C2_Start())                      { I2C2_Stop(); return -1; }
    if (!I2C2_Write((SHT31_ADDR << 1) | 0)) { I2C2_Stop(); return -2; }
    if (!I2C2_Write(0x2C))                  { I2C2_Stop(); return -3; }
    if (!I2C2_Write(0x06))                  { I2C2_Stop(); return -3; }
    I2C2_Stop();
    return 0;
}
int SHT31_Fetch(float *temperature, float *humidity) {
    uint8_t d[6];
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

typedef enum { ST_WAIT, ST_CONVERT } state_t;

int main(void) {
    AD1PCFG = 0xFFFF;
    TRISBbits.TRISB10 = 0; TRISBbits.TRISB11 = 0;
    TRISBbits.TRISB12 = 0; TRISBbits.TRISB13 = 0;
    TRISAbits.TRISA6  = 1; TRISAbits.TRISA7  = 1;
    TRISFbits.TRISF8  = 0; TRISFbits.TRISF2  = 1;
    LATBCLR = 0x3C00;

    UART1_Init(9600);
    I2C2_Init();

    INTCONSET = _INTCON_MVEC_MASK;     // multi-vector kesme modu
    Timer2_Init();
    __builtin_enable_interrupts();

    UART1_PutString("\r\nFSM + Timer2 testi basliyor...\r\n");

    state_t  state  = ST_WAIT;
    uint32_t t_mark = ticks;
    float t, h;
    char buf[80];
    int r;

    while (1) {
        // butonlar her turda kontrol edilir (bekleme yok)
        if (PORTAbits.RA6) LATBSET = (1 << 10); else LATBCLR = (1 << 10);
        if (PORTAbits.RA7) LATBSET = (1 << 11); else LATBCLR = (1 << 11);

        switch (state) {
        case ST_WAIT:
            if ((ticks - t_mark) >= 200) {          // 200 x 10 ms = 2 sn
                t_mark = ticks;
                LATBINV = (1 << 12);                // LD3: her ornekte yanip soner
                r = SHT31_Start();
                if (r == 0) state = ST_CONVERT;
                else {
                    LATBSET = (1 << 13);            // LD4: hata
                    sprintf(buf, "SHT31 komut hatasi, kod=%d\r\n", r);
                    UART1_PutString(buf);
                }
            }
            break;

        case ST_CONVERT:
            if ((ticks - t_mark) >= 2) {            // >= 10-20 ms donusum suresi
                r = SHT31_Fetch(&t, &h);
                if (r == 0) {
                    LATBCLR = (1 << 13);
                    sprintf(buf, "Sicaklik: %.2f C | Nem: %.2f %%RH\r\n", t, h);
                } else {
                    LATBSET = (1 << 13);
                    sprintf(buf, "SHT31 okuma hatasi, kod=%d\r\n", r);
                }
                UART1_PutString(buf);
                state = ST_WAIT;
            }
            break;
        }
    }
    return 0;
}
