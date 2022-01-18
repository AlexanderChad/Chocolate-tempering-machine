/*
* CTM.c
*
* Created: 13.01.2022 0:34:28
* Author : Alexander.Chad
*/
#define F_CPU 1200000UL
#include <avr/io.h>
#include <avr/interrupt.h>

#define LED_BIT _BV(PB0)
#define FAN_BIT _BV(PB1)
#define TR_BIT _BV(PB2)
#define BUTTON_BIT _BV(PB3)
#define HEATER_BIT _BV(PB4)
#define SP_BIT _BV(PB5)

const uint16_t T_Sh[2][4] = { {661, 616, 625, 625}, {649, 610, 619, 619} };

#define BT_ReFrash 0b1111 //16 = 12.5 ms CONSTANT!!!
#define BT_Thr 12 //ms*1.28/BT_ReFrash
#define BT_Hold 120 //ms*1.28/BT_ReFrash

volatile uint32_t millis = 0;
volatile uint16_t TR_Val;
uint8_t Mode_WB = 2;
uint8_t Run_CTM = 0;

int16_t T_target = 600;

volatile int16_t computePID() {
	int16_t error = T_target-(int16_t)TR_Val;
	if (error>0){
		int16_t error_for_heater=error>>3;
		if (error_for_heater>0){return error_for_heater;}else{return 1;}
		}else{
		return 5 * error;
	}
}
/*
volatile int16_t computePID() {
int16_t error = T_target-(int16_t)TR_Val;			// ошибка регулирования
return kp * error;
//if (T_target<TR_Val) {return -50;} else {return 50;}
}

volatile int16_t computePID() {
int16_t error = T_target - TR_Val;			// ошибка регулирования
static int16_t integral = 0, prevErr = 0;
integral += (error * ki)<<10;
int16_t d_term = (kd * (error - prevErr))>>10;
prevErr = error;
return kp * error + integral + d_term;
}*/

uint8_t Led_St=0;
void Led_En(uint8_t id_led){
	Led_St=id_led;
	if (id_led){
		DDRB |= LED_BIT;
		switch (id_led) {
			case 1:
			PORTB |= LED_BIT;
			break;
			case 2:
			PORTB &= ~LED_BIT;
			break;
			case 3:
			for (uint8_t i = 0; i<50; i++){
				PORTB |= LED_BIT;
				PORTB &= ~LED_BIT;
			}
			DDRB &= ~ LED_BIT;
			break;
		}
		}else{
		DDRB &= ~ LED_BIT;
	}
}

volatile uint8_t LED_RF = 0;
void Anim_led(uint8_t id_led){
	if (LED_RF) {
		LED_RF=0;
		if (!Led_St){
			Led_En(id_led);
			}else{
			Led_En(0);
		}
		}else if (Led_St==3){
		Led_En(3);
	}
}

volatile uint8_t SP_EN = 0;
volatile uint8_t SP_T = 0;
volatile uint8_t BT_RF = 0;
volatile uint8_t G_CN = 0;
ISR(TIM0_COMPA_vect){
	static int16_t PWM_FAN = 0;
	static int16_t PWM_HEATER = 0;
	millis++;
	if (millis&0b1000000000) {
		int16_t PID_ST=computePID();
		PWM_FAN=-PID_ST;
		PWM_HEATER=PID_ST;
	}
	int16_t PWM_CN = millis&0b1111111;
	if (PWM_CN<PWM_FAN) {PORTB |= FAN_BIT;} else {PORTB &= ~FAN_BIT;}
	if (PWM_CN<PWM_HEATER) {PORTB |= HEATER_BIT;} else {PORTB &= ~HEATER_BIT;}
	if (SP_EN){
		if (PORTB & SP_BIT) {PORTB &= ~SP_BIT;} else {PORTB |= SP_BIT;}
	}
	if (!(PWM_CN&BT_ReFrash)){
		G_CN++;
		BT_RF=1;
		if (G_CN&0b10000){
			LED_RF=1;
		}
		if (G_CN==SP_T){
			SP_EN=0;
		}
	}
}
void SpSignal(uint8_t t_dl){
	SP_T=G_CN+t_dl;
	SP_EN=1;
}

// Обработчик прерывания по завершению преобразования АЦП
ISR(ADC_vect)
{
	uint8_t ADCL_t = ADCL;
	uint16_t ADC_Rez = (ADCL_t|(uint16_t)(ADCH << 8));
	TR_Val = (31 * TR_Val + ADC_Rez) >> 5; //A = (2^k) – 1
	//TR_Val=ADC_Rez;
}

int main(void)
{
	DDRB |= 0b110010;
	PORTB |= BUTTON_BIT;

	// Настраиваем таймер на тик каждую миллисекунду
	TCCR0A = 0x2; //сбрасываем по совпадению
	TCCR0B = 0x2; // Пределитель таймера 8
	OCR0A=0x75; //значение регистра сравнения 146, 128 тактов - примерно 1 с
	TIMSK0=0x04;//Разрешаем выполнение прерываний по совпадению в OCR0A
	
	// Настройка АЦП:
	ADMUX = 0b00000001; // опорное напряжение - VCC, левое ориентирование данных, выбран вход ADC1
	ADCSRA = 0b11111111; // АЦП включен, запуск преобразования, режим автоизмерения, прерывание по окончанию преобразования, частота CLK/16
	ADCSRB = 0x00; // режим автоизмерения: постоянно запущено
	DIDR0 |= TR_BIT; // запрещаем цифровой вход на ноге аналогового входа
	
	sei(); // Разрешаем глобальные прерывания
	
	uint8_t BT_Trig=0;
	uint16_t BT_St=0;
	uint8_t StageT=0;
	uint8_t TT_Complete;
	while (1)
	{
		if (BT_RF) {
			if (PINB & BUTTON_BIT){
				if ((BT_St>BT_Thr)&&(BT_St<BT_Hold)&&(!Run_CTM)){
					if (Mode_WB){Mode_WB=0;}else{Mode_WB=1;}
					SpSignal(5); //сигнал о нажатой кнопке
				}
				BT_St=0;
				BT_Trig=0;
				}else{
				BT_St++;
				if ((BT_St>BT_Hold)&&(!BT_Trig)&&(Mode_WB<2)){
					BT_Trig=0xFF;
					if (Run_CTM){
						Run_CTM=0;
						}else{
						Run_CTM=0xFF;
						StageT=0;
					}
					SpSignal(20);//тут нужно подать звуковой сигнал о начале работы
				}
			}
			BT_RF=0;
		}
		if (Run_CTM) {
			T_target=T_Sh[Mode_WB][StageT];
			TT_Complete = !(TR_Val-T_target);//abs((int16_t)TR_Val-T_target)<1;
			switch (StageT) {
				case 0:
				if (TT_Complete){StageT++;}else{Led_En(1);}
				break;
				case 1:
				if (TT_Complete){StageT++;}else{Anim_led(1);}
				break;
				case 2:
				if (TT_Complete){
					Led_En(2);
					SpSignal(30);//звуковой мигнал о готовности
					StageT++;
					}else{
					Anim_led(2);
				}
				break;
				default:
				//
				break;
			}
		}
		else{
			switch (Mode_WB) {
				case 0:
				Anim_led(1);
				break;
				case 1:
				Anim_led(2);
				break;
				default:
				Anim_led(3);
				break;
			}
		}
	}
}
//if (TR_Val>650) {Led_red();} else {Led_green();}
//if (PORTB & LED_BIT) {PORTB &= ~LED_BIT;} else {PORTB |= LED_BIT;}
//if (Led_St==1) {Led_green();} else {Led_red();}