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

/* Alarm esikleri: test icin TEMP_HIGH dusuk, final icin 55.0f yap */
#define TEMP_HIGH 25.0f
#define TEMP_LOW   5.0f

/* ---------- Timer2: 10 ms tick ---------- */
volatile uint32_t ticks = 0;
volatile uint8_t  silenced = 0;      // 1: alarm susturuldu
volatile uint32_t last_cn = 0;

void __attribute__((vector(_TIMER_2_VECTOR), interrupt(IPL2SOFT), nomips16))
Timer2Handler(void) {
    ticks++;
    IFS0CLR = _IFS0_T2IF_MASK;
}

/* ---------- CN kesmesi: RB5 (CN7) harici buton, GND'ye ---------- */
void __attribute__((vector(_CHANGE_NOTICE_VECTOR), interrupt(IPL3SOFT), nomips16))
CNHandler(void) {
    volatile uint32_t dummy = PORTB;         // uyumsuzlugu temizle
    (void)dummy;
    IFS1CLR = _IFS1_CNIF_MASK;
    if (PORTBbits.RB5 == 0 && (ticks - last_cn) > 20) {   // basildi + 200 ms debounce
        last_cn = ticks;
        silenced = 1;
    }
}

void Timer2_Init(void) {
    T2CON = 0; TMR2 = 0;
    T2CONbits.TCKPS = 7;
    PR2 = 3125 - 1;
    IPC2bits.T2IP = 2; IPC2bits.T2IS = 0;
    IFS0CLR = _IFS0_T2IF_MASK;
    IEC0SET = _IEC0_T2IE_MASK;
    T2CONbits.ON = 1;
}

void CN_Init(void) {
    TRISBbits.TRISB5 = 1;
    CNPUEbits.CNPUE7 = 1;                    // dahili pull-up
    CNENbits.CNEN7   = 1;
    CNCONbits.ON     = 1;
    volatile uint32_t dummy = PORTB; (void)dummy;
    IPC6bits.CNIP = 3; IPC6bits.CNIS = 0;
    IFS1CLR = _IFS1_CNIF_MASK;
    IEC1SET = _IEC1_CNIE_MASK;
}

/* ---------- UART1 ---------- */
void UART1_Init(unsigned int baud) {
    U1BRG = (FPB / (16 * baud)) - 1; U1MODE = 0x8000; U1STA = 0x1400;
}
void UART1_PutChar(char c) { while (U1STAbits.UTXBF); U1TXREG = c; }
void UART1_PutString(const char *s) { while (*s) UART1_PutChar(*s++); }

/* ---------- I2C2 ---------- */
void I2C2_Init(void) { I2C2BRG = 394; I2C2CONbits.ON = 1; }
uint8_t I2C2_Start(void) { I2C2CONbits.SEN = 1; uint32_t t = 100000; while (I2C2CONbits.SEN && t--) ; return (t > 0); }
uint8_t I2C2_Stop(void)  { I2C2CONbits.PEN = 1; uint32_t t = 100000; while (I2C2CONbits.PEN && t--) ; return (t > 0); }
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

/* ---------- Alarm cikisi: LD4 (RB13) + JA-01 (RE0, role/buzzer icin) ---------- */
void Alarm_Set(uint8_t on) {
    if (on) { LATBSET = (1 << 13); LATESET = (1 << 0); }
    else    { LATBCLR = (1 << 13); LATECLR = (1 << 0); }
}

typedef enum { ST_WAIT, ST_CONVERT } state_t;

int main(void) {
    AD1PCFG = 0xFFFF;
    TRISBbits.TRISB10 = 0; TRISBbits.TRISB11 = 0;
    TRISBbits.TRISB12 = 0; TRISBbits.TRISB13 = 0;
    TRISAbits.TRISA6  = 1; TRISAbits.TRISA7  = 1;
    TRISEbits.TRISE0  = 0;
    TRISFbits.TRISF8  = 0; TRISFbits.TRISF2  = 1;
    LATBCLR = 0x3C00; LATECLR = (1 << 0);

    UART1_Init(9600);
    I2C2_Init();

    INTCONSET = _INTCON_MVEC_MASK;
    Timer2_Init();
    CN_Init();
    __builtin_enable_interrupts();

    UART1_PutString("\r\nAlarm + CN testi basliyor...\r\n");

    state_t  state  = ST_WAIT;
    uint32_t t_mark = ticks;
    uint32_t t_alarm = 0;          // asiri deger ilk goruldugu an
    uint8_t  alarm_on = 0, prev_btn2 = 0;
    float t, h;
    char buf[100];
    int r;

    while (1) {
        // kart butonlari -> LD1/LD2 (anlik)
        if (PORTAbits.RA6) LATBSET = (1 << 10); else LATBCLR = (1 << 10);
        if (PORTAbits.RA7) LATBSET = (1 << 11); else LATBCLR = (1 << 11);

        // yedek susturma: kart uzerindeki BTN2 (yukselen kenar)
        uint8_t b2 = PORTAbits.RA7;
        if (b2 && !prev_btn2) silenced = 1;
        prev_btn2 = b2;

        switch (state) {
        case ST_WAIT:
            if ((ticks - t_mark) >= 200) {
                t_mark = ticks;
                LATBINV = (1 << 12);
                r = SHT31_Start();
                if (r == 0) state = ST_CONVERT;
                else {
                    sprintf(buf, "SHT31 komut hatasi, kod=%d\r\n", r);
                    UART1_PutString(buf);
                }
            }
            break;

        case ST_CONVERT:
            if ((ticks - t_mark) >= 2) {
                r = SHT31_Fetch(&t, &h);
                if (r == 0) {
                    uint8_t out_of_range = (t > TEMP_HIGH) || (t < TEMP_LOW);
                    if (out_of_range) {
                        if (!alarm_on && !silenced) {
                            t_alarm = ticks;
                            alarm_on = 1;
                            Alarm_Set(1);
                        }
                    } else {
                        alarm_on = 0; silenced = 0;      // normale dondu: sifirla
                        Alarm_Set(0);
                    }
                    sprintf(buf, "Sicaklik: %.2f C | Nem: %.2f %%RH | %s\r\n", t, h,
                            out_of_range ? (silenced ? "ALARM-SUSTURULDU" : "ALARM!") : "normal");
                } else {
                    sprintf(buf, "SHT31 okuma hatasi, kod=%d\r\n", r);
                }
                UART1_PutString(buf);
                state = ST_WAIT;
            }
            break;
        }

        // susturma istegi gelince alarmi hemen kapat (kesme bayragi)
        if (silenced && alarm_on) {
            alarm_on = 0;
            Alarm_Set(0);
            UART1_PutString("Alarm susturuldu.\r\n");
        }
    }
    return 0;
}