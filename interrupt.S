#include "defines.h"
/*
   interrupt.S
   kernel@kernelcrash.com 2023


*/
// Reminders (to me)
//   - Dont put instructions that change status inside an IT block!!
//   - Dont use the 1f, 2b etc style local labels. Use the 1$:, 2$: ones as they have scope between
//     regular labels, whereas 1:, 2: are actually scoped to the whole file (except in a macro
//     where you have to use 1f, 2b etc
//   - Do the PR bit clear early. Is hard to guarantee it works later. And I don't think a dsb in the
//     IRQ exit does anything 100% useful
//   - Be careful of the optimiser (in the C code, not the assembler code). Best idea is to get things
//     working with -O0, then turn the optimiser on when its all working.
//   - It is obvious to me that this code is super duper time critical. Adding features to the code
//     will eventually make it stop working because 'your code takes to long'. 
//   - Also
//       - avoid accessing RAM. No push/pop, subroutine calls
//       - try not to stuff up the pipeline


/* *********************************** */
	.syntax unified
	.cpu cortex-m4
//	.fpu softvfp
    	.fpu fpv4-sp-d16
	.thumb


	// expose init_fpu_regs so we can call it from main()
	.global	init_fpu_regs

	.global EXTI0_IRQHandler
	.global rom_base
        .global main_thread_command
        .global main_thread_data
	.global menu_ctrl_file_count
	.global	menu_ctrl_address_register


	.extern (rowcache)

/* *********************************** */

/* MACROS */

.macro mov32, reg, val
    movw \reg, #:lower16:\val
    movt \reg, #:upper16:\val
.endm

// Manage end of _MEMEN
.macro unrolled_exit_EXTI0
        dsb
#if defined(DEBUG_EXTI0_END)
        vstr     reg_bit8_high,[r11,ODR]
#endif

        vmov    r11,reg_fake_stack              // pop r11
        bx      lr
.endm







.macro databus_read_extra_delay
	.rept 1
        	nop
	.endr
.endm

// Use the 64K CCM ram as a sort of circular buffer for debugging, but use the lower 32K only as we map the top 32K of CCM to the TI's 32K expansion
.macro log_r0_r1_to_ccm_uses_r12
        vmov    r12,reg_ccmram_log
	tst	r12,#0x00008000		// effectively make it stop logging when it fills the 64K CCM
	ITTT	EQ
        strbeq  r0,[r12],#1     // store r0
        strbeq  r1,[r12],#1     // store r1
        bfceq     r12,#15,#9      // Really only need to clear bit 15
        vmov  reg_ccmram_log,r12
.endm

.section .rodata

.section ccmram

.section bss
	.lcomm rom_base,0x8000
	.align 4
	

        .equ CCM_BASE ,   0x10000000

        .equ BACKUP_SRAM ,   0x40024000

        .equ PERIPH_BB_BASE     ,   0x42000000                                                                

        .equ PERIPH_BASE     ,   0x40000000                                                                
        .equ PERIPH_BASE_APB1,   (PERIPH_BASE + 0x00000)
        .equ PERIPH_BASE_APB2,   (PERIPH_BASE + 0x10000)
        .equ PERIPH_BASE_AHB1,   (PERIPH_BASE + 0x20000)
        .equ PERIPH_BASE_AHB2,   0x50000000
        .equ PERIPH_BASE_AHB3,   0x60000000

        .equ GPIOA_BASE,   (PERIPH_BASE_AHB1 + 0x0000)                                           
        .equ GPIOB_BASE,   (PERIPH_BASE_AHB1 + 0x0400)
        .equ GPIOC_BASE,   (PERIPH_BASE_AHB1 + 0x0800)
        .equ GPIOD_BASE,   (PERIPH_BASE_AHB1 + 0x0C00)
        .equ GPIOE_BASE,   (PERIPH_BASE_AHB1 + 0x1000)
        .equ GPIOF_BASE,   (PERIPH_BASE_AHB1 + 0x1400)
        .equ GPIOG_BASE,   (PERIPH_BASE_AHB1 + 0x1800)
        .equ GPIOH_BASE,   (PERIPH_BASE_AHB1 + 0x1C00)
        .equ GPIOI_BASE,   (PERIPH_BASE_AHB1 + 0x2000)

	.equ A_TO_B_OFFSET,     (GPIOB_BASE - GPIOA_BASE)

	.equ C_TO_D_OFFSET,     (GPIOD_BASE - GPIOC_BASE)
	.equ C_TO_E_OFFSET,     (GPIOE_BASE - GPIOC_BASE)

        .equ GPIOA_MODER     ,   GPIOA_BASE + 0x00
        .equ GPIOA_OTYPER    ,   GPIOA_BASE + 0x04
        .equ GPIOA_OSPEEDR   ,   GPIOA_BASE + 0x08
        .equ GPIOA_PUPDR     ,   GPIOA_BASE + 0x0C
        .equ GPIOA_IDR       ,   GPIOA_BASE + 0x10
        .equ GPIOA_ODR       ,   GPIOA_BASE + 0x14
        .equ GPIOA_BSRR      ,   GPIOA_BASE + 0x18
        .equ GPIOA_LCKR      ,   GPIOA_BASE + 0x1C
        .equ GPIOA_AFRL      ,   GPIOA_BASE + 0x20
        .equ GPIOA_AFRH      ,   GPIOA_BASE + 0x24

        .equ GPIOB_MODER     ,   GPIOB_BASE + 0x00
        .equ GPIOB_OTYPER    ,   GPIOB_BASE + 0x04
        .equ GPIOB_OSPEEDR   ,   GPIOB_BASE + 0x08
        .equ GPIOB_PUPDR     ,   GPIOB_BASE + 0x0C
        .equ GPIOB_IDR       ,   GPIOB_BASE + 0x10
        .equ GPIOB_ODR       ,   GPIOB_BASE + 0x14
        .equ GPIOB_BSRR      ,   GPIOB_BASE + 0x18
        .equ GPIOB_LCKR      ,   GPIOB_BASE + 0x1C
        .equ GPIOB_AFRL      ,   GPIOB_BASE + 0x20
        .equ GPIOB_AFRH      ,   GPIOB_BASE + 0x24

        .equ GPIOC_MODER     ,   GPIOC_BASE + 0x00
        .equ GPIOC_OTYPER    ,   GPIOC_BASE + 0x04
        .equ GPIOC_OSPEEDR   ,   GPIOC_BASE + 0x08
        .equ GPIOC_PUPDR     ,   GPIOC_BASE + 0x0C
        .equ GPIOC_IDR       ,   GPIOC_BASE + 0x10
        .equ GPIOC_ODR       ,   GPIOC_BASE + 0x14
        .equ GPIOC_BSRR      ,   GPIOC_BASE + 0x18
        .equ GPIOC_LCKR      ,   GPIOC_BASE + 0x1C
        .equ GPIOC_AFRL      ,   GPIOC_BASE + 0x20
        .equ GPIOC_AFRH      ,   GPIOC_BASE + 0x24

        .equ GPIOD_MODER     ,   GPIOD_BASE + 0x00
        .equ GPIOD_OTYPER    ,   GPIOD_BASE + 0x04
        .equ GPIOD_OSPEEDR   ,   GPIOD_BASE + 0x08
        .equ GPIOD_PUPDR     ,   GPIOD_BASE + 0x0C
        .equ GPIOD_IDR       ,   GPIOD_BASE + 0x10
        .equ GPIOD_ODR       ,   GPIOD_BASE + 0x14
        .equ GPIOD_BSRR      ,   GPIOD_BASE + 0x18
        .equ GPIOD_LCKR      ,   GPIOD_BASE + 0x1C
        .equ GPIOD_AFRL      ,   GPIOD_BASE + 0x20
        .equ GPIOD_AFRH      ,   GPIOD_BASE + 0x24

        .equ GPIOE_MODER     ,   GPIOE_BASE + 0x00
        .equ GPIOE_OTYPER    ,   GPIOE_BASE + 0x04
        .equ GPIOE_OSPEEDR   ,   GPIOE_BASE + 0x08
        .equ GPIOE_PUPDR     ,   GPIOE_BASE + 0x0C
        .equ GPIOE_IDR       ,   GPIOE_BASE + 0x10
        .equ GPIOE_ODR       ,   GPIOE_BASE + 0x14
        .equ GPIOE_BSRR      ,   GPIOE_BASE + 0x18
        .equ GPIOE_LCKR      ,   GPIOE_BASE + 0x1C
        .equ GPIOE_AFRL      ,   GPIOE_BASE + 0x20
        .equ GPIOE_AFRH      ,   GPIOE_BASE + 0x24

        .equ MODER           ,   0x00
        .equ OTYPER          ,   0x04
        .equ OSPEEDR         ,   0x08
        .equ IDR             ,   0x10
        .equ ODR             ,   0x14

        .equ SCB_AIRCR       , 0xE000ED0C


        .equ    EXTI_Line0      ,       0x00001
        .equ    EXTI_Line1      ,       0x00002
        .equ    EXTI_Line2      ,       0x00004
        .equ    EXTI_Line4      ,       0x00010
        .equ    EXTI            ,       0x40013c00
        .equ    EXTI_IMR        ,       EXTI + 0x00
        .equ    EXTI_PR         ,       EXTI + 0x14

	.equ	IMR		,	0x00
	.equ	EMR		,	0x04
	.equ	RTSR		,	0x08
	.equ	FTSR		,	0x0c
	.equ	PR		,	0x14


    .equ        SYSCFG_BASE,    0x40013800
    .equ        SYSCFG_EXTICR1, SYSCFG_BASE + 0x08




.section .data
// main_thread_command is actually a register now, but I left it here. These 32 bit longs must be in
// sequence. ie main_thread_command, then main_thread_data, then main_thread_actual_track
main_thread_command:
        .word   0x00000000                      // command word
main_thread_data:
        .word   0x00000000                      // command arg
menu_ctrl_file_count:
        .word   0x00000000
menu_ctrl_address_register:
        .word   0x00000000


.section .text


.type init_fpu_regs, %function
.align 4
.thumb_func
init_fpu_regs:
	movs	r0,#0
	vmov	reg_zero,r0

	movs	r0,#1
	vmov	reg_bit0_high,r0

	mov32	r0,GPIOC_BASE
	vmov	reg_gpioc_base,r0

	mov32	r0,GPIOA_BASE
	vmov	reg_gpioa_base,r0

	mov32	r0,rom_base
	vmov	reg_rom_base,r0

	mov32	r0, EXTI
	vmov	reg_exti_base,r0

	mov32	r0,0x55550020
	vmov	reg_moder_dataout,r0

	mov32	r0,0x00000020
	vmov	reg_moder_datain,r0

	movs	r0,#0x100
	vmov	reg_bit8_high,r0

	mov32	r0,#CCM_BASE
	vmov	reg_ccmram_log,r0

	// bit31 of reg_fdc_system is used to work out if the fdc is actually active. Default is off
	movs    r0,#0
	vmov    reg_fdc_system,r0

	bx	lr



// --------------------------------------------------------
// CS_CROM0 Handler. Int on -ve edge. 
.type EXTI0_IRQHandler, %function
.align 4
.thumb_func
EXTI0_IRQHandler:
	vmov	reg_fake_stack,r11		// effectively s30 is our 'stack'
	vmov	r11,reg_gpioa_base
#ifdef DEBUG_EXTI0
        //vstr     reg_zero,[r11,ODR]
	//nop
	//nop
        vstr     reg_bit8_high,[r11,ODR]
#endif

	vmov	r2,reg_gpioc_base
	vmov	r3,reg_exti_base		// s7 = EXTI
	vstr    reg_bit0_high,[r3,PR]

	
cs_crom0_loop:
	ldr	r1,[r2,IDR]

	tst	r1,#CS_CROM0_MASK
	bne	exit_EXTI0
	tst	r1,#WE_MASK
	bne	rom_cartridge_read
exit_EXTI0:
	vmov	r0,reg_moder_datain
	str	r0,[r2,C_TO_D_OFFSET + MODER]
	unrolled_exit_EXTI0


rom_cartridge_read:
// 8000 - bfff
	//databus_read_extra_delay
	// we know we are reading so open up the databus before we shove the data on
	vmov	r0,reg_moder_dataout
	str	r0,[r2, C_TO_D_OFFSET + MODER]

	ldr	r1,[r2, C_TO_E_OFFSET + IDR]
	bfc     r1,#14,#18			// and 0x3fff
	vmov	r3,reg_rom_base
	ldrb	r0,[r3,r1]
	lsls	r0,r0,#8
	str     r0,[r2, C_TO_D_OFFSET + ODR]
	b	cs_crom0_loop

