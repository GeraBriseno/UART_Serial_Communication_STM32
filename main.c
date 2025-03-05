#include "stm32f303xe.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Max size of string to receive
#define RX_BUFFER_SIZE 128

// Buffer for received string
char rxString[RX_BUFFER_SIZE];

// Index for current buffer position, volatile since it is modified inside USART2 ISR
volatile uint16_t rxIndex = 0;

// Flag to indicate clear trigger char received, volatile since it is modified inside USART2 ISR
volatile uint8_t clearStringFlag = 0;

/* Function to configure PLL as System Clock with a frequency of 72 MHz, HCLK with the same 72 MHz frequency
	 and HSE as PLL input clock */
static void SysClockConfigF303re(void)
{

	// 1. ENABLE HSE and wait for the HSE to become Ready
	RCC->CR |= RCC_CR_HSEON;  // RCC->CR |= 1<<16; 
	while (!(RCC->CR & RCC_CR_HSERDY));  // while (!(RCC->CR & (1<<17)));
	
	// 2. Set the POWER ENABLE CLOCK
	RCC->APB1ENR |= RCC_APB1ENR_PWREN;  // RCC->APB1ENR |= 1<<28;
	//PWR->CR |= PWR_CR_VOS;  // PWR->CR |= 3<<14; 
	
	// 3. Configure the FLASH PREFETCH and the LATENCY Related Settings
	FLASH->ACR = 1<<4 | 2<<0;  // FLASH->ACR = (1<<8) | (1<<9)| (1<<10)| (5<<0);
	
	// 4. Configure the PRESCALERS HCLK, PCLK1, PCLK2
	// AHB = 1 PR
	RCC->CFGR |= RCC_CFGR_HPRE_DIV1;  // RCC->CFGR &= ~(0<<4);
	
	// APB1 = 2 PR
	RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;  // RCC->CFGR |= (4<<10);
	
	// APB2 = 1 PR
	RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;  // RCC->CFGR |= (0<<13);
	
	// PREDiv = 1
	RCC->CFGR2 |= (0<<0);
	
	// PLL Multiplier = 9
	RCC->CFGR |= (7<<18);
	
	// HSE as PLL input clock
	RCC->CFGR |= (1<<16);

	// 6. Enable the PLL and wait for it to become ready
	RCC->CR |= RCC_CR_PLLON;  // RCC->CR |= (1<<24);
	while (!(RCC->CR & RCC_CR_PLLRDY));  // while (!(RCC->CR & (1<<25)));
	
	// 7. Select the Clock Source and wait for it to be set
	RCC->CFGR |= RCC_CFGR_SW_PLL;  // RCC->CFGR |= (2<<0);
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);  // while (!(RCC->CFGR & (2<<2)));
	
}

//Function to configure GPIO port(s)
static void GPIO_Config(void)
{
	
	// 1. Enable the GPIOA CLOCK
	RCC->AHBENR |= (1<<17);  
	
	// 2. Set the built-in LED Pin as OUTPUT
	GPIOA->MODER |= (1<<10);  // pin PA5(bits 11:10) as Output (01)
	
	// 3. Configure the OUTPUT MODE
	GPIOA->OTYPER = 0;
	GPIOA->OSPEEDR = 0;
	
}

// Function to configure TIM2
static void configureTIM2(void){
	
	RCC->APB1ENR |= (1 << 0);   //ENABLE TIM2 PERIPHERAL CLOCK
	
	TIM2->PSC = 71;     //SET TIM2 FREQUENCY TO 1 MHZ (72000000/(71+1)) USING PRESCALER
	
	TIM2->ARR = (uint32_t)1000000;   //SET COUNTER RESET TIME
	
	TIM2->DIER |= TIM_DIER_UIE;   //(1 << 0) ENABLE TIM2 INTERRUPT
	
	TIM2->SR &= ~TIM_SR_UIF;  //CLEAR TIM2 INTERRUPT STATUS
	
	NVIC_EnableIRQ(TIM2_IRQn);  //ENABLE GLOBAL NVIC INTERRUPTS FOR TIM2
	
	TIM2->CR1 = TIM_CR1_CEN;  // |= (1 << 0);  //ENABLE COUNTER FOR TIM2
	}

// Function to configure and enable USART2
void enableUART2(void)
{	
	// Enable USART2 Clock
	RCC->APB1ENR |= (1<<17);
	
	// Set PA2 and PA3 pins as alternate mode (these will be our TX and RX pins respectively)
	GPIOA->MODER |= (2<<4);
	
	GPIOA->MODER |= (2<<6);
	
	// Set High speed for PA2 and PA3
	GPIOA->OSPEEDR |= (2<<4) | (2<<6);
	
	//Select AF7 for PA2 and PA3 (USART2 TX and RX respectively)
	GPIOA->AFR[0] |= (7<<8);
	
	GPIOA->AFR[0] |= (7<<12);
	
	// Reset USART2
	USART2->CR1 = 0x00;
	
	// Set word length as 8 bit
	USART2->CR1 &= ~(1U<<12);
	
	USART2->CR1 &= ~(1U<<28);
	
	// Set Baud Rate
	USART2->BRR = 313;
	
	// Enable USART2 receiver
	USART2->CR1 |= (1U<<2);
	
	// Enable USART2 transmitter
	USART2->CR1 |= (1U<<3);
	
	// Enable Receive data register not empty (data ready to be read) Interrupt
	USART2->CR1 |= (1U<<5);
	
	// Enable USART2
	USART2->CR1 |= (1U<<0);
	
	// ENABLE GLOBAL NVIC INTERRUPTS FOR USART2
	NVIC_EnableIRQ(USART2_IRQn);
	
}

// Function to send single char over UART2
void sendCharUART2(uint8_t charToSend)
{
	// Write char to USART2 transmission data register
	USART2->TDR = charToSend;
	
	// Wait till transmission complete bit is set
	while(!(USART2->ISR & (1<<7))); 
}

// Function to send string over UART2
void sendStringUART2(char *charArrayToSend)
{
	// Write char to USART2 transmission data register
	while(*charArrayToSend)
	{
		sendCharUART2(*charArrayToSend++);
	}
	// Wait till transmission complete bit is set
	while(!(USART2->ISR & (1<<6)));
}

// Toggle LED pin using XOR bitwise operator	
static void toggleLEDGPIOA5(void){
	GPIOA->ODR ^= (1<<5);
}

// Turn LED pin on using OR bitwise operator	
static void turnOnLEDGPIOA5(void){
	GPIOA->ODR |= (1U<<5);
}

// Turn LED pin off using AND and NOT bitwise operators
static void turnOffLEDGPIOA5(void){
	GPIOA->ODR &= ~(1U<<5);
}
	
// TIM2 ISR funtion	
void TIM2_IRQHandler(void)
{	
	//If UIF flag is set
  if(TIM2->SR & TIM_SR_UIF)
  {	
		// Clear interrupt status
    TIM2->SR &= ~TIM_SR_UIF;
  }
}

// USART2 ISR function
void USART2_IRQHandler(void){

	// 'Receive register not empty' RXNE interrupt
	if(USART2->ISR & (1U<<5))
    {
			// Copy new char into the buffer
			char receivedChar = USART2->RDR;

			// If buffer is not full
			if(rxIndex < RX_BUFFER_SIZE - 1)
			{
				// Add char to string	
				rxString[rxIndex++] = receivedChar;

					// Check for string clearing trigger char
					if(receivedChar == 'x')
					{
						// Set flag to clear string
						clearStringFlag = 1;
						// Reset buffer index
						rxIndex = 0;
					}
			}
			// Buffer is full
			else
			{
				// Reset buffer index	
				rxIndex = 0;
			}
	}

	// Clear ICR register flags
	USART2->ICR = 0xFFFFFFFF;

}

// Function to process the string received, will run in main while loop
void processReceivedString(void)
{	
	// Conditionals to control built-in LED
	if(!strcmp(rxString,"off"))
		{
			turnOffLEDGPIOA5();
			//sendStringUART2(rxString);
		}
	else if(!strcmp(rxString,"on"))
		{
			turnOnLEDGPIOA5();
			//sendStringUART2(rxString);
		}
	
	// If clear string trigger char is detected
	if(clearStringFlag)
	{	
		// Reset flag
		clearStringFlag = 0;
	
		// Clear the string's memory
		memset(rxString, 0, RX_BUFFER_SIZE);
		
		// The string memory has been cleared, new commands can be sent
		sendStringUART2("Cleared");
	}
}

int main (void){
	
	SysClockConfigF303re();
	configureTIM2();
	GPIO_Config();
	enableUART2();
	
	while (1)
	{
		processReceivedString();
	}
}
