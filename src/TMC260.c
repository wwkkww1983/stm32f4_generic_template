/**
 * @file TMC260.c
 * @author Andrew K. Walker
 * @date 14 JUN 2017
 * @brief Firmware for Trinamic TMC260-PA stepper motor driver.
 *
 * Set of low level functions for controlling the the Trinamic TMC260-PA stepper
 * motor driver.  Higher level functions...like commanding a particular motor
 * profile should be done in another layer.
 */

#include "TMC260.h"
#include "debug.h"

#include "systick.h"

#include "full_duplex_usart_dma.h"
#include "generic_packet.h"
#include "gp_proj_universal.h"
#include "gp_proj_motor.h"

uint8_t TMC260_initialized = 0;
/* uint16_t TIM1_Period = 0; */

/* Global variables to store the current state of all control registers. */
uint32_t TMC260_DRVCTRL_regval = 0;
uint32_t TMC260_CHOPCONF_regval = 0;
uint32_t TMC260_SMARTEN_regval = 0;
uint32_t TMC260_SGCSCONF_regval = 0;
uint32_t TMC260_DRVCONF_regval = 0;

/* Private Function Prototypes */
void TMC260_init_gpio(void);
void TMC260_init_spi(void);
void TMC260_init_config(void);
uint8_t TMC260_spi_write_byte(uint8_t byte);
uint8_t TMC260_spi_read_byte(uint8_t *byte);
uint8_t TMC260_spi_write_read_byte(uint8_t write_byte, uint8_t *read_byte);
uint8_t TMC260_spi_write_datagram(uint32_t datagram);
uint8_t TMC260_spi_read_write_datagram(uint32_t write_datagram, uint32_t *read_datagram);
uint8_t TMC260_send_default_regs(void);

/* Public function.  Doxygen documentation is in the header file. */
void TMC260_initialize(void)
{

   TMC260_init_gpio();
   TMC260_init_spi();
   TMC260_init_config();
   TMC260_initialized = 1;

}


/**
 *
 * @fn void TMC260_init_gpio(void)
 * @brief GPIO initialization for TMC260.
 * @param None
 * @return None
 *
 * - PA0  -> MOTOR_EN
 * - PA1  -> MOTOR_DIR
 * - PA2  -> MOTOR_STEP
 * - PC0  -> HOME (covered is low...uncovered is high)
 * - PC2  -> SG_260 (step guard...did we stall)
 * - PC13 -> CS_260 (chip select...the rest of SPI initialized elsewhere)
 *
 */
void TMC260_init_gpio(void)
{
   GPIO_InitTypeDef GPIO_InitStructure;
   EXTI_InitTypeDef EXTI_InitStructure;
   NVIC_InitTypeDef NVIC_InitStructure;

   RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
   RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
   /* Enable clock for SYSCFG */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

#ifdef TOS_100_DEV_BOARD
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2;
#else
   GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;
#endif
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
   GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
   GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
   GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
   GPIO_Init(GPIOA, &GPIO_InitStructure);

   GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
   GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
   GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
   GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
   GPIO_Init(GPIOC, &GPIO_InitStructure);

#ifndef TOS_100_DEV_BOARD
   /* Enable is now an input... */
   GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
   GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
   GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
   GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
   GPIO_Init(GPIOA, &GPIO_InitStructure);
#endif

   /** @todo Make PC2 an EXTI so that we can easily catch and handle a stall condition. */
   GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
   GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
   GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
   GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
   GPIO_Init(GPIOC, &GPIO_InitStructure);

   SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource2);

   EXTI_InitStructure.EXTI_Line = EXTI_Line2;
   EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
   EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
   EXTI_InitStructure.EXTI_LineCmd = ENABLE;
   EXTI_Init(&EXTI_InitStructure);

   /** @todo Need to set the interrupt priority properly for stall guard. */
   NVIC_InitStructure.NVIC_IRQChannel = EXTI2_IRQn;
   NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x0F;
   NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x0F;
   NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
   NVIC_Init(&NVIC_InitStructure);



}

/**
 * @fn void EXTI2_Handler(void)
 * @brief Handles the external interrupt generated by the stall guard.
 *
 * @param None
 * @return None
 */
void EXTI2_IRQHandler(void)
{
   if(EXTI_GetITStatus(EXTI_Line2) != RESET)
   {
      /**
       * @todo Need to actually implement stall guard functionality here.  Maybe
       *       we should just kill the enable pin?
       */
      debug_output_toggle(DEBUG_LED_RED);

      EXTI_ClearITPendingBit(EXTI_Line2);
   }
}





/**
 *
 * @fn void TMC260_init_spi(void)
 * @brief Initialize SPI for TMC260 configuration and control.
 * @param NONE
 * @return NONE
 *
 * Using SPI1 for configuration and control.  Speed is set by the internal
 * oscillator on the TMC260.  It is at 15 MHz.  To be stable, Trinamic suggests
 * that the speed is set to < 0.90*15000000.0/2.0 ~ 6750000 Hz...
 *
 * Baud Rate Prescaler of 32 should give us 168 MHz / 32 = 5250000 Hz...
 *
 */
void TMC260_init_spi(void)
{
   SPI_InitTypeDef  SPI_InitStructure;
   GPIO_InitTypeDef GPIO_InitStructure;
   /* NVIC_InitTypeDef NVIC_InitStructure; */

   /* Peripheral Clock Enable -------------------------------------------------*/
   /* Enable the SPI clock */
   RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);

   /* Enable GPIO clocks */
   RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

   /* Connect SPI pins to AF5 */
   GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_SPI1);
   GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_SPI1);
   GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_SPI1);

   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
   GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
   GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
   GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_DOWN;

   /* SPI SCK pin configuration */
   GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
   GPIO_Init(GPIOA, &GPIO_InitStructure);

   /* SPI  MISO pin configuration */
   GPIO_InitStructure.GPIO_Pin =  GPIO_Pin_6;
   GPIO_Init(GPIOA, &GPIO_InitStructure);

   /* SPI  MOSI pin configuration */
   GPIO_InitStructure.GPIO_Pin =  GPIO_Pin_7;
   GPIO_Init(GPIOA, &GPIO_InitStructure);

   /* SPI Chip Select was already configured in TMC260_init_gpio */

   /* SPI configuration -------------------------------------------------------*/
   SPI_I2S_DeInit(SPI1);
   SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
   SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
   SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
   SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;  /* I think this should be SPI_CPOL_High...but it does something when Low. */
   SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge; /* I think this should be SPI_CPHA_2Edge */
   SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
   SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_256;
   SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
   SPI_InitStructure.SPI_CRCPolynomial = 7;
   SPI_Init(SPI1, &SPI_InitStructure);

   /* Enable the SPI peripheral */
   SPI_Cmd(SPI1, ENABLE);

}


/**
 * @fn uint8_t TMC260_spi_write_byte(uint8_t byte)
 * @brief Reads a single byte from the SPI interface on the TMC260.
 *
 * @param uint8_t Byte that is to be written to the TMC260.
 * @return uint8_t TMC260 return status.
 *
 * @todo Because 20bit datagrams need to be written, this will likely need to be
 *       called from the write datagram function.
 */
uint8_t TMC260_spi_write_byte(uint8_t byte)
{

   while(SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_TXE) == RESET);
   SPI_I2S_SendData(SPI1, (uint16_t)byte);

   return TMC260_SUCCESS;
}

/**
 * @fn uint8_t TMC260_spi_read_byte(uint8_t *byte)
 * @brief Reads a single byte from the SPI interface on the TMC260.
 *
 * @param uint8_t* Byte that is read from the TMC260.
 * @return uint8_t TMC260 return status.
 *
 * @todo Determine if this will actually work for the TMC260.  It may be
 *       necessary to read a full 20bit datagram at a time.
 */
uint8_t TMC260_spi_read_byte(uint8_t *byte)
{
   uint16_t tmpval;

   while(SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_TXE) == RESET);
   SPI_I2S_SendData(SPI1, (uint16_t)0x00);
   while(SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_RXNE) == RESET);
   tmpval = SPI_I2S_ReceiveData(SPI1);
   *byte = (uint8_t)(tmpval & 0xFF);

   return TMC260_SUCCESS;
}

/**
 * @fn uint8_t TMC260_spi_write_read_byte(uint8_t write_byte, uint8_t *read_byte)
 * @brief Writes a byte and returns the read byte.
 *
 * @param uint8_t  Byte (write_byte) to be written to the TMC260.
 * @param uint8_t* Byte (read_byte) that is read from the TMC260.
 * @return uint8_t TMC260 return status.
 *
 * @todo Determine if this will actually work for the TMC260.  It may be
 *       necessary to read and write a full 20bit datagram at a time.
 */
uint8_t TMC260_spi_write_read_byte(uint8_t write_byte, uint8_t *read_byte)
{
   uint16_t tmpval;

   while(SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_TXE) == RESET);
   SPI_I2S_SendData(SPI1, (uint16_t)write_byte);
   while(SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_RXNE) == RESET);
   while(SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_BSY) == SET);
   tmpval = SPI_I2S_ReceiveData(SPI1);
   *read_byte = (uint8_t)(tmpval & 0xFF);

   return TMC260_SUCCESS;
}


/**
 * @fn uint8_t TMC260_spi_write_datagram(uint32_t datagram)
 * @brief Writes a 20 bit datagram to the TMC260.
 *
 * @param uint32_t The datagram to be written
 * @return uint8_t TMC260 return status.
 *
 * This function assumes that the uint32_t datagram value is the register data
 * to be written to the TMC260.  The incoming value is expected to have the bits
 * aligned in bit positions 0-19 as outlined in the TMC260 specification.  It
 * be shifted appropriately in this fucntion and the bytes sent in the correct
 * order to write the register.
 */
uint8_t TMC260_spi_write_datagram(uint32_t datagram)
{
   uint8_t retval;
   uint32_t sdatagram;
   uint8_t byte1, byte2, byte3;
   uint8_t ii;

   sdatagram = (datagram<<8);
   byte1 = (sdatagram>>24)&0xFF;
   byte2 = (sdatagram>>16)&0xFF;
   byte3 = (sdatagram>>8)&0xFF;


   /* TEMP */
   Delay(TMC260_SPI_DELAY_COUNT);


   GPIO_ResetBits(GPIOC, GPIO_Pin_13);

   /* TEMP */
   Delay(TMC260_SPI_DELAY_COUNT);


   /** @todo Is there any way to really check the retval here?  Not sure it
    *        really matters.  I think we'd want to write all the bytes anyway.
    */
   retval = TMC260_spi_write_byte(byte1);
   retval = TMC260_spi_write_byte(byte2);
   retval = TMC260_spi_write_byte(byte3);

   /* retval = TMC260_spi_write_byte(0xFF); */
   /* retval = TMC260_spi_write_byte(0x00); */
   /* retval = TMC260_spi_write_byte(0x55); */

   while(SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_BSY) == SET);
   for(ii=0; ii<8; ii++)
   {
      Delay(TMC260_SPI_DELAY_COUNT);
   }

   GPIO_SetBits(GPIOC, GPIO_Pin_13);

   for(ii=0; ii<8; ii++)
   {
      Delay(TMC260_SPI_DELAY_COUNT);
   }


   return TMC260_SUCCESS;
}

uint8_t TMC260_spi_read_status(tmc260_status_types status_type, tmc260_status_struct *status_struct)
{
   uint8_t retval;
   uint32_t rd;

   /* If we are not using the SPI to configure anything...and we just want the status back... */
   if(TMC260_DRVCONF_regval == 0x00000000)
   {
      TMC260_DRVCONF_regval = 0xEF000;
   }

   /* First Clear the RDSEL bits. */
   TMC260_DRVCONF_regval &= ~TMC260_DRVCONF_RDSEL_MASK;
   /* Now set the RSDEL bits with the desired status type. */
   TMC260_DRVCONF_regval |= (status_type<<TMC260_DRVCONF_RDSEL_SHIFT)&TMC260_DRVCONF_RDSEL_MASK;
   /* Now first write the DRVCONF register with the new value. */
   retval = TMC260_spi_write_datagram(TMC260_DRVCONF_regval);
   /* Now, write it again...this time the return value will reflect the
    * status type changes from the previous write.
    */
   retval = TMC260_spi_read_write_datagram(TMC260_DRVCONF_regval, &rd);
   /* Lastly...parse the return data into the status struct. */
   status_struct->status_type = status_type;
   status_struct->position = 0;
   status_struct->stall_guard = 0;
   status_struct->current = 0;
   status_struct->status_byte = (uint8_t)(rd&0xff);

   status_struct->STST = (rd & TMC260_STATUS_STST_MASK)>>TMC260_STATUS_STST_SHIFT;
   status_struct->OLB  = (rd & TMC260_STATUS_OLB_MASK)>>TMC260_STATUS_OLB_SHIFT;
   status_struct->OLA  = (rd & TMC260_STATUS_OLA_MASK)>>TMC260_STATUS_OLA_SHIFT;
   status_struct->S2GB = (rd & TMC260_STATUS_S2GB_MASK)>>TMC260_STATUS_S2GB_SHIFT;
   status_struct->S2GA = (rd & TMC260_STATUS_S2GA_MASK)>>TMC260_STATUS_S2GA_SHIFT;
   status_struct->OTPW = (rd & TMC260_STATUS_OTPW_MASK)>>TMC260_STATUS_OTPW_SHIFT;
   status_struct->OT   = (rd & TMC260_STATUS_OT_MASK)>>TMC260_STATUS_OT_SHIFT;
   status_struct->SG   = (rd & TMC260_STATUS_SG_MASK)>>TMC260_STATUS_SG_SHIFT;

   switch(status_type)
   {
      case TMC260_STATUS_POSITION:
         status_struct->position = (rd & TMC260_STATUS_MSTEP_MASK)>>TMC260_STATUS_MSTEP_SHIFT;
         status_struct->stall_guard = 0;
         status_struct->current = 0;
         break;
      case TMC260_STATUS_STALLGUARD:
         status_struct->position = 0;
         status_struct->stall_guard = (rd & TMC260_STATUS_STALLGUARD_MASK)>>TMC260_STATUS_STALLGUARD_SHIFT;
         status_struct->current = 0;
         break;
      case TMC260_STATUS_CURRENT:
         status_struct->position = 0;
         status_struct->stall_guard = (rd & TMC260_STATUS_CUR_SG_MASK)>>TMC260_STATUS_CUR_SG_SHIFT;
         status_struct->current = (rd & TMC260_STATUS_CUR_SE_MASK)>>TMC260_STATUS_CUR_SE_SHIFT;
         break;
      default:
         status_struct->position = 0;
         status_struct->stall_guard = 0;
         status_struct->current = 0;
         break;
   }

   return TMC260_SUCCESS;
}


/**
 * @fn uint8_t TMC260_read_write_datagram(uint32_t write_datagram, uint32_t *read_datagram)
 * @brief Writes a datagram and reads the response.
 *
 * @param uint32_t  write_datagram to be written to the TMC260.
 * @param uint32_t* read_datagram that is read from the TMC260.
 * @return uint8_t TMC260 return status.
 *
 */
uint8_t TMC260_spi_read_write_datagram(uint32_t write_datagram, uint32_t *read_datagram)
{
   uint8_t retval;
   uint32_t sdatagram, t1, t2, t3;
   uint8_t byte1, byte2, byte3;
   uint8_t rb1, rb2, rb3;
   uint8_t ii;

   GenericPacket packet1, packet2, packet3;

   sdatagram = (write_datagram<<8);
   byte1 = (sdatagram>>24)&0xFF;
   byte2 = (sdatagram>>16)&0xFF;
   byte3 = (sdatagram>>8)&0xFF;

   /* TEMP */
   Delay(TMC260_SPI_DELAY_COUNT);


   GPIO_ResetBits(GPIOC, GPIO_Pin_13);

   /* TEMP */
   Delay(TMC260_SPI_DELAY_COUNT);


   /** @todo Is there any way to really check the retval here?  Not sure it
    *        really matters.  I think we'd want to write all the bytes anyway.
    */
   retval = TMC260_spi_write_read_byte(byte1, &rb1);
   retval = TMC260_spi_write_read_byte(byte2, &rb2);
   retval = TMC260_spi_write_read_byte(byte3, &rb3);

   *read_datagram = 0x00000000;
   t1 = rb1&0x000000FF;
   t2 = rb2&0x000000FF;
   t3 = rb3&0x000000FF;
   *read_datagram |= (t1<<24) & 0xFF000000;
   *read_datagram |= (t2<<16) & 0x00FF0000;
   *read_datagram |= (t3<<8)  & 0x0000FF00;
   *read_datagram = ((*read_datagram)>>12);

   create_universal_byte(&packet1, rb1);
   full_duplex_usart_dma_add_to_queue(&packet1, NULL, 0);

   create_universal_byte(&packet2, rb2);
   full_duplex_usart_dma_add_to_queue(&packet2, NULL, 0);

   create_universal_byte(&packet3, rb3);
   full_duplex_usart_dma_add_to_queue(&packet3, NULL, 0);

   while(SPI_I2S_GetFlagStatus(SPI1, SPI_FLAG_BSY) == SET);
   /* TEMP */
   for(ii=0; ii<8; ii++)
   {
      Delay(TMC260_SPI_DELAY_COUNT);
   }

   GPIO_SetBits(GPIOC, GPIO_Pin_13);

   /* TEMP */
   for(ii=0; ii<8; ii++)
   {
      Delay(TMC260_SPI_DELAY_COUNT);
   }

   return TMC260_SUCCESS;
}


/**
 * @fn void TMC260_init_config(void)
 * @brief Sets up the initial state of all control registers for the TMC260.
 *
 * @param NONE
 * @return NONE
 *
 * @todo Determine desired state of the control registers and write them.  Put
 *       some debugging in to make sure that things are set properly.
 *
 */
void TMC260_init_config(void)
{

   debug_output_set(DEBUG_LED_RED);

   /* */
   TMC260_send_drvconf(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
   /* No step interpoloation, step on both edges, full stepping... */
   TMC260_send_drvctrl_sdon(0x00, 0x01, MICROSTEP_CONFIG_64);

   /* /\* /\\* *\\/ *\/ */
   /* /\* TMC260_send_drvconf(0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00); *\/ */
   /* /\* /\\* No step interpoloation, step on both edges, full stepping... *\\/ *\/ */
   /* /\* TMC260_send_drvctrl_sdoff(0x01, 0xF0, 0x00, 0xF0); *\/ */

   /* */
   TMC260_send_chopconf(0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x04);
   /* */
   TMC260_send_smarten(0x00, 0x00, 0x02, 0x00, 0x00);


   /* /\* Lower Current *\/ */
   TMC260_send_sgcsconf(0x01, 0x3F, 0x05);

   /* Mid Current */
   /* TMC260_send_sgcsconf(0x01, 0x3F, 0x12); */

   /* /\* High Current *\/ */
   /* TMC260_send_sgcsconf(0x01, 0x3F, 0x1F); */

   /* TMC260_send_default_regs(); */

   debug_output_clear(DEBUG_LED_RED);
}

/**
 * @fn uint8_t TMC260_send_drvctrl_sdoff(uint8_t ph_a_dir, uint8_t ph_a_cur, uint8_t ph_b_dir, uint8_t ph_b_cur)
 * @brief Packs outgoing data and writes drvctrl reg when not using step/dir.
 *
 * @param
 * @return uint8_t TMC260 return status.
 */
uint8_t TMC260_send_drvctrl_sdoff(uint8_t ph_a_dir, uint8_t ph_a_cur, uint8_t ph_b_dir, uint8_t ph_b_cur)
{
   uint8_t retval;
   uint32_t regval;

   if((ph_a_dir > 1)||(ph_b_dir > 1))
   {
      return TMC260_ERROR_INVALID_INPUT;
   }

   regval = TMC260_DRVCTRL_SDOFF_INIT;
   regval |= ((uint32_t)ph_a_dir << TMC260_DRVCTRL_SDOFF_PHA_DIR_SHIFT)&TMC260_DRVCTRL_SDOFF_PHA_DIR_MASK;
   regval |= ((uint32_t)ph_a_cur << TMC260_DRVCTRL_SDOFF_PHA_CUR_SHIFT)&TMC260_DRVCTRL_SDOFF_PHA_CUR_MASK;
   regval |= ((uint32_t)ph_b_dir << TMC260_DRVCTRL_SDOFF_PHB_DIR_SHIFT)&TMC260_DRVCTRL_SDOFF_PHB_DIR_MASK;
   regval |= ((uint32_t)ph_b_cur << TMC260_DRVCTRL_SDOFF_PHB_CUR_SHIFT)&TMC260_DRVCTRL_SDOFF_PHB_CUR_MASK;

   /** @todo Call the spi send register value function here once it is set up! */
   retval = TMC260_spi_write_datagram(regval);

   /** @todo Should only set this variable if the SPI register write was a
    *        success.  Unfortunately, I don't think the TMC260 allows you to
    *        read the config registers back.  The status datagram returned
    *        doesn't actually contain this info.
    */
   TMC260_DRVCTRL_regval = regval;

   return TMC260_SUCCESS;

}

/**
 * @fn uint8_t TMC260_send_drvctrl_sdon(uint8_t intpol, uint8_t dedge, microstep_config mres)
 * @brief Packs outgoing data and writes drvctrl reg when using step/dir mode.
 *
 * @param uint8_t intpol -> enable(1)/disable(0) step pulse interpolation
 * @param uint8_t dedge  -> 0 == Step on rising edges, 1 == Step on both edges
 * @param microstep_config mres -> microstep resolution
 * @return uint8_t TMC260 return status.
 */
uint8_t TMC260_send_drvctrl_sdon(uint8_t intpol, uint8_t dedge, microstep_config mres)
{
   uint8_t retval;
   uint32_t regval;

   if((intpol > 1)||(dedge > 1))
   {
      return TMC260_ERROR_INVALID_INPUT;
   }

   regval = TMC260_DRVCTRL_SDON_INIT;
   regval |= ((uint32_t)intpol << TMC260_DRVCTRL_SDON_INTPOL_SHIFT)&TMC260_DRVCTRL_SDON_INTPOL_MASK;
   regval |= ((uint32_t)dedge << TMC260_DRVCTRL_SDON_DEDGE_SHIFT)&TMC260_DRVCTRL_SDON_DEDGE_MASK;
   regval |= ((uint32_t)mres << TMC260_DRVCTRL_SDON_MRES_SHIFT)&TMC260_DRVCTRL_SDON_MRES_MASK;

   /** @todo Call the spi send register value function here once it is set up! */
   /* regval = 0x110; */
   /* regval = 0x00000100; */
   retval = TMC260_spi_write_datagram(regval);

   /** @todo Should only set this variable if the SPI register write was a
    *        success.  Unfortunately, I don't think the TMC260 allows you to
    *        read the config registers back.  The status datagram returned
    *        doesn't actually contain this info.
    */
   TMC260_DRVCTRL_regval = regval;

   return TMC260_SUCCESS;

}


/**
 * @fn uint8_t TMC260_send_chopconf(uint8_t tbl, uin8_t chm, uint8_t rndtf, uin8_t hdec, uin8_t hend, uint8_t hstrt, uint8_t toff)
 * @brief Packs outgoing data and writes chopconf register.
 *
 * @param uint8_t tbl -> blanking time
 * @param uint8_t chm -> chopper mode: 1=constant toff, 0=spreadCycle
 * @param uint8_t rndtf -> random toff time enable (1) or disable (0)
 * @param uint8_t hdec -> hysteresis decrement interval or fast decay mode
 * @param uint8_t hend -> hysteresis end value or sine wave offset
 * @param uint8_t hstrt -> hysteresis start value or fast decay time setting
 * @param uint8_t toff -> duration of slow decay phase (Nclk = 12*(32*toff))
 * @return uint8_t TMC260 return status.
 */
uint8_t TMC260_send_chopconf(uint8_t tbl, uint8_t chm, uint8_t rndtf, uint8_t hdec, uint8_t hend, uint8_t hstrt, uint8_t toff)
{
   uint8_t retval;
   uint32_t regval;

   /** @todo Should we check the input parameters??? */

   regval = TMC260_CHOPCONF_INIT;
   regval |= ((uint32_t)tbl << TMC260_CHOPCONF_TBL_SHIFT)&TMC260_CHOPCONF_TBL_MASK;
   regval |= ((uint32_t)chm << TMC260_CHOPCONF_CHM_SHIFT)&TMC260_CHOPCONF_CHM_MASK;
   regval |= ((uint32_t)rndtf << TMC260_CHOPCONF_RNDTF_SHIFT)&TMC260_CHOPCONF_RNDTF_MASK;
   regval |= ((uint32_t)hdec << TMC260_CHOPCONF_HDEC_SHIFT)&TMC260_CHOPCONF_HDEC_MASK;
   regval |= ((uint32_t)hend << TMC260_CHOPCONF_HEND_SHIFT)&TMC260_CHOPCONF_HEND_MASK;
   regval |= ((uint32_t)hstrt << TMC260_CHOPCONF_HSTRT_SHIFT)&TMC260_CHOPCONF_HSTRT_MASK;
   regval |= ((uint32_t)toff << TMC260_CHOPCONF_TOFF_SHIFT)&TMC260_CHOPCONF_TOFF_MASK;

   /* TEMP */
   /* regval = 0x94552; */
   /* regval = 0x901B4; */

   retval = TMC260_spi_write_datagram(regval);

   TMC260_CHOPCONF_regval = regval;

   return TMC260_SUCCESS;
}

/**
 * @fn uint8_t TMC260_send_smarten(uint8_t seimin, uint8_t sedn, uint8_t semax, uint8_t seup, uint8_t semin)
 * @brief Packs outgoing data and writes smarten register.
 *
 * @param uint8_t seimin -> minimum coolStep current
 * @param uint8_t sedn -> current decrement speed
 * @param uint8_t semax -> upper coolStep threshold
 * @param uint8_t seup -> current increment size
 * @param uint8_t semin -> lower coolStep threshold
 * @return uint8_t TMC260 return status.
 */
uint8_t TMC260_send_smarten(uint8_t seimin, uint8_t sedn, uint8_t semax, uint8_t seup, uint8_t semin)
{
   uint8_t retval;
   uint32_t regval;

   regval = TMC260_SMARTEN_INIT;
   regval |= ((uint32_t)seimin << TMC260_SMARTEN_SEIMIN_SHIFT)&TMC260_SMARTEN_SEIMIN_MASK;
   regval |= ((uint32_t)sedn << TMC260_SMARTEN_SEDN_SHIFT)&TMC260_SMARTEN_SEDN_MASK;
   regval |= ((uint32_t)semax << TMC260_SMARTEN_SEMAX_SHIFT)&TMC260_SMARTEN_SEMAX_MASK;
   regval |= ((uint32_t)seup << TMC260_SMARTEN_SEUP_SHIFT)&TMC260_SMARTEN_SEUP_MASK;
   regval |= ((uint32_t)semin << TMC260_SMARTEN_SEMIN_SHIFT)&TMC260_SMARTEN_SEMIN_MASK;

   /* regval = 0xA8202; */
   /* regval = 0xA8200; */

   retval = TMC260_spi_write_datagram(regval);

   TMC260_SMARTEN_regval = regval;

   return TMC260_SUCCESS;
}


/**
 * @fn uint8_t TMC260_send_sgcsconf(uint8_t sfilt, uint8_t sgt, uint8_t cs)
 * @brief Packs outgoing data and writes sgcs register.
 *
 * @param uint8_t sfilt -> stallGuard2 filter enable, 0=none, 1=4 full steps
 * @param uint8_t sgt -> stallGuard2 threshold
 * @param uint8_t cs -> current scale
 * @return uint8_t TMC260 return status.
 */
uint8_t TMC260_send_sgcsconf(uint8_t sfilt, uint8_t sgt, uint8_t cs)
{
   uint8_t retval;
   uint32_t regval;

   regval = TMC260_SGCSCONF_INIT;
   regval |= ((uint32_t)sfilt << TMC260_SGCSCONF_SFILT_SHIFT)&TMC260_SGCSCONF_SFILT_MASK;
   regval |= ((uint32_t)sgt << TMC260_SGCSCONF_SGT_SHIFT)&TMC260_SGCSCONF_SGT_MASK;
   regval |= ((uint32_t)cs << TMC260_SGCSCONF_CS_SHIFT)&TMC260_SGCSCONF_CS_MASK;

   /* regval = 0xD001F; */
   /* regval = 0xD3F1F; */
   retval = TMC260_spi_write_datagram(regval);

   TMC260_SGCSCONF_regval = regval;

   return TMC260_SUCCESS;
}


/**
 * @fn uint8_t TMC260_send_drvconf(uint8_t tst, uint8_t slph, uint8_t slpl, uint8_t diss2g, uint8_t ts2g, uint8_t sdoff, uint8_t vsense, uint8_t rdsel)
 * @brief Packs outgoing data and writes drvconf register.
 *
 * @param uint8_t tst -> reserved test mode
 * @param uint8_t slph -> slope control high side
 * @param uint8_t slpl -> slope control low side
 * @param uint8_t diss2g -> 0=short to ground protection enabled,
 *                          1=short to ground protection disabled
 * @param uint8_t ts2g -> short to ground protection timer
 * @param uint8_t sdoff -> 0=enable step/dir interface
 *                         1=disable step/dir interface
 * @param uint8_t vsense -> sense resistor voltage-biased current scaling
 * @param uint8_t rdsel -> select value for status output
 *                         0x00 - microstep position readout
 *                         0x01 - stallGuard2 level readout
 *                         0x02 - stallGuard2 and current level redout
 *                         0x03 - INVALID
 * @return uint8_t TMC260 return status.
 */
uint8_t TMC260_send_drvconf(uint8_t tst, uint8_t slph, uint8_t slpl, uint8_t diss2g, uint8_t ts2g, uint8_t sdoff, uint8_t vsense, uint8_t rdsel)
{
   uint8_t retval;
   uint32_t regval;

   regval = TMC260_DRVCONF_INIT;
   regval |= ((uint32_t)tst << TMC260_DRVCONF_TST_SHIFT)&TMC260_DRVCONF_TST_MASK;
   regval |= ((uint32_t)slph << TMC260_DRVCONF_SLPH_SHIFT)&TMC260_DRVCONF_SLPH_MASK;
   regval |= ((uint32_t)slpl << TMC260_DRVCONF_SLPL_SHIFT)&TMC260_DRVCONF_SLPL_MASK;
   regval |= ((uint32_t)diss2g << TMC260_DRVCONF_DISS2G_SHIFT)&TMC260_DRVCONF_DISS2G_MASK;
   regval |= ((uint32_t)ts2g << TMC260_DRVCONF_TS2G_SHIFT)&TMC260_DRVCONF_TS2G_MASK;
   regval |= ((uint32_t)sdoff << TMC260_DRVCONF_SDOFF_SHIFT)&TMC260_DRVCONF_SDOFF_MASK;
   regval |= ((uint32_t)vsense << TMC260_DRVCONF_VSENSE_SHIFT)&TMC260_DRVCONF_VSENSE_MASK;
   regval |= ((uint32_t)rdsel << TMC260_DRVCONF_RDSEL_SHIFT)&TMC260_DRVCONF_RDSEL_MASK;

   /* regval = 0xE0000; */
   /* regval = 0xEF010; */
   /* regval = 0xEF000; */
   retval = TMC260_spi_write_datagram(regval);

   TMC260_DRVCONF_regval = regval;

   return TMC260_SUCCESS;
}


uint8_t TMC260_send_default_regs(void)
{
   uint8_t retval;

   /* /\* From Datasheet *\/ */
   /* retval = TMC260_spi_write_datagram(0x94557); */
   /* retval = TMC260_spi_write_datagram(0xD001F); */
   /* retval = TMC260_spi_write_datagram(0xE0010); */
   /* retval = TMC260_spi_write_datagram(0x00000); */
   /* retval = TMC260_spi_write_datagram(0xA8202); */

   /* From "Getting Started" */
   retval = TMC260_spi_write_datagram(0x00000);
   retval = TMC260_spi_write_datagram(0x90131);
   retval = TMC260_spi_write_datagram(0xA0000);
   retval = TMC260_spi_write_datagram(0xD0505);
   retval = TMC260_spi_write_datagram(0xEF440);


   return TMC260_SUCCESS;
}

/* Public Interface Functions - Doxygen Documentation in Header */
void TMC260_enable(void)
{
#ifdef TOS_100_DEV_BOARD
   GPIO_ResetBits(GPIOA, GPIO_Pin_0);
#endif
}

void TMC260_disable(void)
{
#ifdef TOS_100_DEV_BOARD
   GPIO_SetBits(GPIOA, GPIO_Pin_0);
#endif
}

void TMC260_dir_CW(void)
{
   /* CCW looking in on the pinion. Rotating LIDAR radians Increasing. */
   GPIO_ResetBits(GPIOA, GPIO_Pin_1);
}

void TMC260_dir_CCW(void)
{
   /* CW looking in on the pinion. Rotating LIDAR radians Decreasing. */
   GPIO_SetBits(GPIOA, GPIO_Pin_1);
}


void TMC260_step(void)
{
   /**
    * @todo We are curerntly assuming that the DEDGE bit is set to 1  such that
    *       steps are taken on both rising and falling edges.  We would need to
    *       modify this function if we wish to behave properly for only rising
    *       edge active.
    *
    */
   if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_2) == Bit_SET)
   {
      GPIO_ResetBits(GPIOA, GPIO_Pin_2);
   }
   else
   {
      GPIO_SetBits(GPIOA, GPIO_Pin_2);
   }
}

void TMC260_status(tmc260_status_types status_type, tmc260_status_struct *status, uint8_t send_packet)
{
   GenericPacket packet;

   TMC260_spi_read_status(status_type, status);

   if(send_packet)
   {
      /**
       * @todo Create MOTOR_TMC260_RESP_STATUS GenericPacket and ship it!
       */
      create_motor_tmc260_resp_status(&packet, status->position, status->stall_guard, status->current, status->status_byte);
      full_duplex_usart_dma_add_to_queue(&packet, NULL, 0);
   }

}
