/*
 * ti994a-rom-grom-ram-and-floppy-emulator
 * kernel@kernelcrash.com
 *
 *
 */
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "defines.h"
#include "stm32f4xx.h"
#include "util.h"

#include "stm32f4_discovery.h"
#include "stm32f4_discovery_sdio_sd.h"
#include "ff.h"
#include "diskio.h"



extern volatile uint8_t *rom_base;
extern volatile uint8_t *grom_base;
extern volatile uint8_t *dsr_base;

extern volatile uint32_t main_thread_command;
extern volatile uint32_t main_thread_data;
extern volatile uint32_t main_thread_actual_track[3];

extern volatile uint8_t *track_buffer;
extern volatile uint32_t fdc_write_flush_count;
extern volatile uint32_t fdc_write_dirty_bits;
extern volatile uint32_t menu_ctrl_file_count;

extern volatile uint8_t *disk_header;

#ifdef ENABLE_SEMIHOSTING
extern void initialise_monitor_handles(void);   /*rtt*/
#endif



// FATFS stuff
FATFS fs32;



#if _USE_LFN
    static char lfn[_MAX_LFN + 1];
        fno.lfname = lfn;
            fno.lfsize = sizeof lfn;
#endif


// Enable the FPU (Cortex-M4 - STM32F4xx and higher)
// http://infocenter.arm.com/help/topic/com.arm.doc.dui0553a/BEHBJHIG.html
// Also make sure lazy stacking is disabled
void enable_fpu_and_disable_lazy_stacking() {
  __asm volatile
  (
    "  ldr.w r0, =0xE000ED88    \n"  /* The FPU enable bits are in the CPACR. */
    "  ldr r1, [r0]             \n"  /* read CAPCR */
    "  orr r1, r1, #( 0xf << 20 )\n" /* Set bits 20-23 to enable CP10 and CP11 coprocessors */
    "  str r1, [r0]              \n" /* Write back the modified value to the CPACR */
    "  dsb                       \n" /* wait for store to complete */
    "  isb                       \n" /* reset pipeline now the FPU is enabled */
    // Disable lazy stacking (the default) and effectively have no stacking since we're not really using the FPU for anything other than a fast register store
    "  ldr.w r0, =0xE000EF34    \n"  /* The FPU FPCCR. */
    "  ldr r1, [r0]             \n"  /* read FPCCR */
    "  bfc r1, #30,#2\n" /* Clear bits 30-31. ASPEN and LSPEN. This disables lazy stacking */
    "  str r1, [r0]              \n" /* Write back the modified value to the FPCCR */
    "  dsb                       \n" /* wait for store to complete */
    "  isb"                          /* reset pipeline  */
    :::"r0","r1"
    );
}

enum sysclk_freq {
    SYSCLK_42_MHZ=0,
    SYSCLK_84_MHZ,
    SYSCLK_168_MHZ,
    SYSCLK_200_MHZ,
    SYSCLK_240_MHZ,
    SYSCLK_250_MHZ,
};
 
void rcc_set_frequency(enum sysclk_freq freq)
{
    int freqs[]   = {42, 84, 168, 200, 240, 250};
 
    /* USB freqs: 42MHz, 42Mhz, 48MHz, 50MHz, 48MHz */
    int pll_div[] = {2, 4, 7, 10, 10, 10}; 
 
    /* PLL_VCO = (HSE_VALUE / PLL_M) * PLL_N */
    /* SYSCLK = PLL_VCO / PLL_P */
    /* USB OTG FS, SDIO and RNG Clock =  PLL_VCO / PLLQ */
    uint32_t PLL_P = 2;
    uint32_t PLL_N = freqs[freq] * 2;
    uint32_t PLL_M = (HSE_VALUE/1000000);
    uint32_t PLL_Q = pll_div[freq];
 
    RCC_DeInit();
 
    /* Enable HSE osscilator */
    RCC_HSEConfig(RCC_HSE_ON);
 
    if (RCC_WaitForHSEStartUp() == ERROR) {
        return;
    }
 
    /* Configure PLL clock M, N, P, and Q dividers */
    RCC_PLLConfig(RCC_PLLSource_HSE, PLL_M, PLL_N, PLL_P, PLL_Q);
 
    /* Enable PLL clock */
    RCC_PLLCmd(ENABLE);
 
    /* Wait until PLL clock is stable */
    while ((RCC->CR & RCC_CR_PLLRDY) == 0);
 
    /* Set PLL_CLK as system clock source SYSCLK */
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
 
    /* Set AHB clock divider */
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
 
    //FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;
    FLASH->ACR =  FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;

    /* Set APBx clock dividers */
    switch (freq) {
        /* Max freq APB1: 42MHz APB2: 84MHz */
        case SYSCLK_42_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div1); /* 42MHz */
            RCC_PCLK2Config(RCC_HCLK_Div1); /* 42MHz */
            break;
        case SYSCLK_84_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div2); /* 42MHz */
            RCC_PCLK2Config(RCC_HCLK_Div1); /* 84MHz */
            break;
        case SYSCLK_168_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div4); /* 42MHz */
            RCC_PCLK2Config(RCC_HCLK_Div2); /* 84MHz */
            break;
        case SYSCLK_200_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div4); /* 50MHz */
            RCC_PCLK2Config(RCC_HCLK_Div2); /* 100MHz */
            break;
        case SYSCLK_240_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div4); /* 60MHz */
            RCC_PCLK2Config(RCC_HCLK_Div2); /* 120MHz */
            break;
        case SYSCLK_250_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div4); 
            RCC_PCLK2Config(RCC_HCLK_Div2);
            break;
    }
 
    /* Update SystemCoreClock variable */
    SystemCoreClockUpdate();
}



// For some weird reason the optimizer likes to delete this whole function, hence set to O0
void __attribute__((optimize("O0")))  SD_NVIC_Configuration(void)
{
        NVIC_InitTypeDef NVIC_InitStructure;


        NVIC_InitStructure.NVIC_IRQChannel = SDIO_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = SDIO_IRQ_PREEMPTION_PRIORITY;    // This must be a lower priority (ie. higher number) than the _MREQ and _IORQ interrupts
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);

	// DMA2 STREAMx Interrupt ENABLE
	NVIC_InitStructure.NVIC_IRQChannel = SD_SDIO_DMA_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = SDIO_DMA_PREEMPTION_PRIORITY;
	NVIC_Init(&NVIC_InitStructure);

}

void SDIO_IRQHandler(void)
{
	/* Process All SDIO Interrupt Sources */
	SD_ProcessIRQSrc();
}

void SD_SDIO_DMA_IRQHANDLER(void)
{
	SD_ProcessDMAIRQ();
}

//
void config_PC0_cs_crom0(void) {
        EXTI_InitTypeDef EXTI_InitStruct;
        NVIC_InitTypeDef NVIC_InitStruct;

        /* Enable clock for SYSCFG */
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

        SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource0);

        EXTI_InitStruct.EXTI_Line = EXTI_Line0;
        /* Enable interrupt */
        EXTI_InitStruct.EXTI_LineCmd = ENABLE;
        /* Interrupt mode */
        EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Interrupt;
        /* Triggers on falling edge */
        EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Falling;
        /* Add to EXTI */
        EXTI_Init(&EXTI_InitStruct);

        /* Add IRQ vector to NVIC */
        /* PC0 is connected to EXTI_Line0, which has EXTI0_IRQn vector */
        NVIC_InitStruct.NVIC_IRQChannel = EXTI0_IRQn;
        /* Set priority */
        NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = CS_CROM0_PREEMPTION_PRIORITY;
        /* Set sub priority */
        NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0x00;
        /* Enable interrupt */
        NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
        /* Add to NVIC */
        NVIC_Init(&NVIC_InitStruct);
}


/*
 * Configure port C pins. Some not used as yet.
 *
 * PC0(in)         - _CS_CROM0
   PC1(in)         - _WE
*/
void config_gpio_portc(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOC Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin = GPIO_PYUUTA_CS_CROM0 | GPIO_PYUUTA_WE ;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOC, &GPIO_InitStructure);


}

/* Input/Output data GPIO pins on PD{8..15}. Also PD2 is used fo MOSI on the STM32F407VET6 board I have */
void config_gpio_data(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOD Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | 
		GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

}

/* Input Address GPIO pins on PE{0..15} */
void config_gpio_addr(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOE Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);

	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin = 
		GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | 
		GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7 | 
		GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | 
		GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOE, &GPIO_InitStructure);
}


/* Debug GPIO pins on PA6  / PA7 / PA8 */
void config_gpio_dbg(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOA Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART4,DISABLE);


	/* Configure GPIO Settings */
	//GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 ;
	GPIO_InitStructure.GPIO_Pin = GPIO_DEBUG_LED | GPIO_LOGIC_ANALYSER_DEBUG;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}

/* push buttons on PA2, PA3, PA4  */
void config_gpio_buttons(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOA Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);


	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin = GPIO_NEXT_ITEM| GPIO_PREV_ITEM | GPIO_FORCE_MENU_ROM;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}



int __attribute__((optimize("O0")))  main(void) {

	FRESULT res;
	TCHAR full_filename[128];
	int next_button_debounce;
	int first_time;
	uint32_t button_state;
	int32_t	file_counter;

        TCHAR root_directory[15];
        DIR dir;
        static FILINFO fno;


	// You have to disable lazy stacking BEFORE initialising the scratch fpu registers
	enable_fpu_and_disable_lazy_stacking();
	init_fpu_regs();

	register uint32_t main_thread_command_reg asm("r10") __attribute__((unused)) = 0;
	main_thread_data = 0;

	rcc_set_frequency(SYSCLK_240_MHZ);

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_CCMDATARAMEN, ENABLE);

	/* PD{8..15}  and PD2 for SD card MOSI*/
	config_gpio_data();
	/* PE{0..15} */
	config_gpio_addr();
	
	config_gpio_portc();

	config_gpio_dbg();

	config_gpio_buttons();

	// switch on compensation cell
	RCC->APB2ENR |= 0 |  RCC_APB2ENR_SYSCFGEN ;
	SYSCFG->CMPCR |= SYSCFG_CMPCR_CMP_PD; // enable compensation cell
	while ((SYSCFG->CMPCR & SYSCFG_CMPCR_READY) == 0);  // wait until ready

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4); 
	SysTick->CTRL  = 0;

	SD_NVIC_Configuration();

	config_PC0_cs_crom0();


        memset(&fs32, 0, sizeof(FATFS));
        res = f_mount(&fs32, "",0);
        if (res != FR_OK) {
                blink_debug_led(250);
        }

	strcpy(root_directory, "pyuuta");

        res = f_opendir(&dir, root_directory);
        if (res != FR_OK) {
               blink_debug_led(100);
        }



	first_time=TRUE;		// change to false if we want to autoload the menu rom
	next_button_debounce=0;
	file_counter=-1;

	// attempt to load the menu ROM from the root of the SD card
	//res = load_rom("pyuuta-kcpfs.bin",(unsigned char *) &rom_base);
        //if (res != FR_OK) {
        //       blink_debug_led(500);
       // }
	//
	// This memset is actually really important

	while(1) {
		button_state = GPIOA->IDR;
		if ( !(button_state & FORCE_MENU_ROM_MASK) ) {
			if (next_button_debounce == 0 ) {
				res = load_rom("pyuuta-kcpfs.bin",(unsigned char *) &rom_base);
				next_button_debounce=BUTTON_DEBOUNCE;
			}
		}

		if (!(button_state & NEXT_ROM_MASK) || !(button_state & PREV_ROM_MASK) || first_time) {
			first_time = FALSE;
			if (next_button_debounce == 0 ) {
				if (!(button_state & PREV_ROM_MASK)) {
					// if we hit the prev button do the worlds worst way of finding the previous file
					if (file_counter >0) {
						res = f_closedir(&dir);
        					res = f_opendir(&dir, root_directory);
						for (uint32_t i=0;i< file_counter; i++) {
							res = f_readdir(&dir, &fno);                 
						}
						file_counter--;
					} else {
						res = f_readdir(&dir, &fno);                 
					}

				} else {
					// if we hit the next button or this is the first time through
					file_counter++;
					res = f_readdir(&dir, &fno);                 
					if (res != FR_OK || fno.fname[0] == 0) {
						// allow buttonpushes to 'wrap round'
						f_closedir(&dir);
						res = f_opendir(&dir, root_directory);
						res = f_readdir(&dir, &fno);
						file_counter=0;
					}

				}
				strcpy(full_filename,root_directory);
				strcat(full_filename,"/");
				strcat(full_filename,fno.fname);

				load_rom(full_filename, (unsigned char *) &rom_base);
				//
				//next_button_debounce=5850000;
				next_button_debounce=BUTTON_DEBOUNCE;
			} 
		}
		next_button_debounce = (next_button_debounce>0)?next_button_debounce-1:0;

		if (!(main_thread_command_reg & 0xf0000000) && (main_thread_command_reg & 0xff)) {
			switch(main_thread_command_reg & 0xff) {
				case (MAIN_THREAD_COMMAND_LOAD_DIRECTORY): {
					main_thread_command_reg |= MAIN_COMMAND_IN_PROGRESS;
					menu_ctrl_file_count = load_directory(root_directory,(uint16_t *)(CCMRAM_BASE+MENU_DIRCACHE_OFFSET+MENU_LISTING_OFFSETS), MENU_MAX_DIRECTORY_ITEMS, MENU_LISTING_OFFSETS );
	
					main_thread_command_reg |= MAIN_COMMAND_LOAD_DIRECTORY_COMPLETE;
					break;
				}
				case (MAIN_THREAD_COMMAND_LOAD_ROM): {
					main_thread_command_reg |= MAIN_COMMAND_IN_PROGRESS;
					// The filename to load will be in CCMRAM+MENU_DIRCACHE_OFFSET+MENU_LOAD_FILE_BASE
					//
					char *fname = (char *) (CCMRAM_BASE+MENU_DIRCACHE_OFFSET+MENU_LOAD_FILE_BASE);
					file_counter=0;
					res = f_closedir(&dir);
        				res = f_opendir(&dir, root_directory);
					// Search for the file that the user selected. This is really just a cheap way
					// to have the directory pointer at the right spot, such that hitting NEXT
					// will advance you to the rom/disk the user expects to be next
					while (1) {
						res = f_readdir(&dir, &fno);                 
						if (res != FR_OK || fno.fname[0] == 0) {
							f_closedir(&dir);
							res = f_opendir(&dir, root_directory);
							res = f_readdir(&dir, &fno);
							file_counter=0;
							break;
						}
						if (strcmp(fno.fname,fname)==0) {
							break;
						}
						file_counter++;
					}
					strcpy(full_filename,root_directory);
					strcat(full_filename,"/");
					strcat(full_filename,fno.fname);
					load_rom(full_filename, (unsigned char *) &rom_base);
					main_thread_command_reg |= MAIN_COMMAND_LOAD_ROM_COMPLETE;
					break;
				}
			}
		}

	}
}
