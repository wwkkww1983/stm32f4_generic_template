/**
 * @file tilt_motor_control_stepper.c
 * @author Andrew K. Walker
 * @date 26 JUN 2017
 * @brief Stepper control of rotating LIDAR unit.
 *
 * Handles step timing, flag sensor for homing, and high level state machine.
 * Lower level motor control is handled in TMC260.c.
 */
#include "stm32f4xx_conf.h"
#include "tilt_stepper_motor_control.h"

#include "debug.h"

#include "full_duplex_usart_dma.h"

#include "generic_packet.h"
#include "gp_proj_motor.h"

#include "TMC260.h"

/* Note that this can only bee included one time from one file. */
#include "tilt_stepper_motor_profile.h"

uint32_t ts_state_timer = 0;
tilt_stepper_states ts_state = TILT_STEPPER_INITIALIZE;

tmc260_status_struct stat_struct;

uint8_t last_dir = 0;

volatile uint32_t tilt_index = 0;
volatile int32_t steps_from_home = 0;
tilt_stepper_dirs current_step_dir = TILT_STEPPER_DIR_STOPPED;
float current_pos_rad = 0.0f;

GenericPacket gp_pos_rad;
float pos_rad = 0.0f;

uint8_t home_dir = 0;

volatile uint8_t tilt_stepper_motor_send_angle = 0;

uint32_t TimerPeriod = 0;
uint16_t pscale = 0;

/* Private functions. */
void tilt_stepper_motor_init_state_machine(void);
void tilt_stepper_motor_init_step_timer(void);
void tilt_stepper_motor_init_home_sensor(void);
void tilt_stepper_motor_state_change(tilt_stepper_states new_state, uint8_t reset_timer);
void tilt_stepper_motor_set_CCW(void);
void tilt_stepper_motor_set_CW(void);
void tilt_stepper_motor_step(void);

/* Public function.  Doxygen documentation is in the header file. */
void tilt_stepper_motor_init(void)
{

   tilt_stepper_motor_init_state_machine();
   tilt_stepper_motor_init_step_timer();
   tilt_stepper_motor_init_home_sensor();
   /* Set initial state and stuch... */


}


/**
 * @fn void tilt_stepper_motor_init_home_sensor(void)
 * @brief Initialize the home sensor.
 * @param None
 * @return None
 *
 */
void tilt_stepper_motor_init_home_sensor(void)
{

   GPIO_InitTypeDef GPIO_InitStructure;
   EXTI_InitTypeDef EXTI_InitStructure;
   NVIC_InitTypeDef NVIC_InitStructure;

   RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
   /* Enable clock for SYSCFG */
   RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);


   /** @todo Make PC1 an EXTI so that we can easily catch HOME. */
   GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
   GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
   GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
   GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
   GPIO_Init(GPIOC, &GPIO_InitStructure);

   SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource1);

   EXTI_InitStructure.EXTI_Line = EXTI_Line1;
   EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
   EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
   /* EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling; */
   EXTI_InitStructure.EXTI_LineCmd = ENABLE;
   EXTI_Init(&EXTI_InitStructure);

   /** @todo Need to set the interrupt priority properly to catch HOME. */
   NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;
   NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x0F;
   NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x0F;
   NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
   NVIC_Init(&NVIC_InitStructure);



}


/**
 * @fn void tilt_stepper_motor_init_state_machine(void)
 * @brief Initialize the state machine timer
 * @param None
 * @return None
 *
 */
void tilt_stepper_motor_init_state_machine(void)
{
   TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
   NVIC_InitTypeDef   NVIC_InitStructure;

   uint32_t TimerPeriod = 0;
   uint16_t pscale = 0;

   /* Turn the timer clock on! */
   RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM11, ENABLE);

   /* TIM11 on APB2 runs at SystemCoreClock. */
   pscale = 2;
   TimerPeriod = (SystemCoreClock / (TILT_STEPPER_STATE_MACHINE_HZ * (pscale+1))) - 1;

   /* Time Base configuration */
   TIM_TimeBaseStructure.TIM_Prescaler = pscale;
   TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
   TIM_TimeBaseStructure.TIM_Period = TimerPeriod;
   TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
   TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;

   TIM_TimeBaseInit(TIM11, &TIM_TimeBaseStructure);

   /* Set up interrupt. */
   NVIC_InitStructure.NVIC_IRQChannel = TIM1_TRG_COM_TIM11_IRQn;
   NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x01;
   NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x00;
   NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
   NVIC_Init(&NVIC_InitStructure);

   TIM_ITConfig(TIM11, TIM_IT_Update, ENABLE);

   TIM_Cmd(TIM11, ENABLE);

}

/**
 * @fn void tilt_stepper_motor_init_step_timer(void)
 * @brief Initialize the step timer.
 * @param None
 * @return None
 *
 * This function will reload the ARR value each time such that the next step
 * will be taken at the appropriate time.  It will do nothing if we aren't
 * in a moving state.
 */
void tilt_stepper_motor_init_step_timer(void)
{
   TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
   NVIC_InitTypeDef   NVIC_InitStructure;



   /* Turn the timer clock on! */
   RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5, ENABLE);

   /* Start with a 1ms timer for the state machine.  TIM2 is a 32bit counter!
    * And APB1 is counting at 84 MHz...SystemCoreClock is 168 MHz so the
    * factor of 2 in the denominator. Assumes the Prescaler is 0 and  that
    * TimerPeriod wouldn't roll a 32 bit number.
    */
   pscale = 0;
   TimerPeriod = (SystemCoreClock / (DEFAULT_STEP_FREQ_HZ * 2 * (pscale + 1))) - 1;

   /* Time Base configuration */
   TIM_TimeBaseStructure.TIM_Prescaler = pscale;
   TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
   TIM_TimeBaseStructure.TIM_Period = TimerPeriod;
   TIM_TimeBaseStructure.TIM_ClockDivision = 0;
   TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;

   TIM_TimeBaseInit(TIM5, &TIM_TimeBaseStructure);

   /* Set up interrupt. */
   NVIC_InitStructure.NVIC_IRQChannel = TIM5_IRQn;
   NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x00;
   NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x00;
   NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
   NVIC_Init(&NVIC_InitStructure);

   TIM_ITConfig(TIM5, TIM_IT_Update, ENABLE);

   TIM_Cmd(TIM5, ENABLE);
}


/**
 * @fn void EXTI1_Handler(void)
 * @brief Handles the external interrupt generated by the HOME flag.
 *
 * @param None
 * @return None
 */
void EXTI1_IRQHandler(void)
{
   if(EXTI_GetITStatus(EXTI_Line1) != RESET)
   {
      /**
       * @todo Need to actually implement home functionality.
       */
      /* Reset our position to zero. */
      if(GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_1) == Bit_SET)
      {
         if(current_step_dir == TILT_STEPPER_DIR_CW)
         {
            /* Flag is covered. We just crossed home. */
            current_pos_rad = 0.0f;
            steps_from_home = 0;

            debug_output_set(DEBUG_LED_RED);
         }
         else
         {
            /* Flag is uncovered.  We need to go CCW until we cover it. */
            current_pos_rad = 3.14f;
            steps_from_home = (int32_t)((current_pos_rad * (float)micro_steps_per_rev * stepper_gear_ratio_num) / (stepper_gear_ratio_den * TILT_STEPPER_TWO_PI));

            debug_output_clear(DEBUG_LED_RED);
         }
      }
      else
      {
         if(current_step_dir == TILT_STEPPER_DIR_CW)
         {
            /* Flag is uncovered.  We need to go CCW until we cover it. */
            current_pos_rad = 3.14f;
            steps_from_home = (int32_t)((current_pos_rad * (float)micro_steps_per_rev * stepper_gear_ratio_num) / (stepper_gear_ratio_den * TILT_STEPPER_TWO_PI));

            debug_output_clear(DEBUG_LED_ORANGE);
         }
         else
         {
            /* Flag is covered. We just crossed home. */
            current_pos_rad = 0.0f;
            steps_from_home = 0;

            debug_output_set(DEBUG_LED_ORANGE);
         }
      }


      if(steps_from_home == 0)
      {
         if(ts_state == TILT_STEPPER_HOME)
         {
            /* Get us tilting in the correct direction. */
            last_dir = 1;

            /* Now start us tilting. */
            tilt_stepper_motor_state_change(TILT_STEPPER_TEST_DELAY, 1);
         }
      }

      EXTI_ClearITPendingBit(EXTI_Line1);
   }
}


/**
 * @fn void TIM5_IRQHandler(void)
 * @brief Tilt stepper step timer.
 * @param None
 * @return None
 *
 * This function will reload the ARR value each time such that the next step
 * will be taken at the appropriate time.  It will do nothing if we aren't
 * in a moving state.
 */
void TIM5_IRQHandler(void)
{
   if(TIM_GetITStatus(TIM5, TIM_IT_Update) != RESET)
   {

      debug_output_toggle(DEBUG_LED_GREEN);

      /* if(rotating)
       *   {step through the table...flip dir at end...keep stepping until state changes...}
       * else if(homing)
       *   {move until we see the flag...dir determined by current state of flag}
       * else
       *   {do nothing}
       */

      if((ts_state == TILT_STEPPER_TEST_CW)||(ts_state == TILT_STEPPER_TEST_CCW))
      {
         tilt_stepper_motor_step();
      }

      if(ts_state == TILT_STEPPER_HOME)
      {
         tilt_stepper_motor_step();
      }

      if(ts_state == TILT_STEPPER_TILT_TABLE)
      {
         tilt_index++;
         if((tilt_index < tilt_elements)&&(stepper_profile[tilt_index] > 0))
         {
            tilt_stepper_motor_step();
            TIM_SetAutoreload(TIM5, stepper_profile[tilt_index]);
         }
         else
         {
            tilt_stepper_motor_state_change(TILT_STEPPER_TILT_TABLE, 1);
         }
      }

      TIM_ClearITPendingBit(TIM5, TIM_IT_Update);
   }
}


/**
 * @fn void TIM1_TRG_COM_TIM11_IRQHandler(void)
 * @brief Tilt stepper motor state machine interrupt handler.
 * @param None
 * @return None
 *
 */
void TIM1_TRG_COM_TIM11_IRQHandler(void)
{
   if(TIM_GetITStatus(TIM11, TIM_IT_Update) != RESET)
   {
      debug_output_toggle(DEBUG_LED_BLUE);

      ts_state_timer++;


      if(ts_state_timer%25 == 0)
      {
         if(tilt_stepper_motor_send_angle == 0)
         {
            tilt_stepper_motor_send_angle = 1;
            /* TMC260_status(TMC260_STATUS_CURRENT, &stat_struct, 1); */
         }
      }


      /* Not perfect...but at least some protection from overrotation. */
      if((ts_state != TILT_STEPPER_HOME)&&(ts_state != TILT_STEPPER_INITIALIZE))
      {
         if(current_pos_rad > 3.5f)
         {
            tilt_stepper_motor_state_change(TILT_STEPPER_HOME, 1);
         }

         if(current_pos_rad < -0.5f)
         {
            tilt_stepper_motor_state_change(TILT_STEPPER_HOME, 1);
         }
      }

      switch(ts_state)
      {
         case TILT_STEPPER_INITIALIZE:

            TMC260_initialize();

            tilt_stepper_motor_state_change(TILT_STEPPER_HOME, 1);
            /* tilt_stepper_motor_state_change(TILT_STEPPER_TEST_CW, 1); */

            break;
         case TILT_STEPPER_HOME:
            /**
             * @todo Continue to poll the pin here just in case the EXTI doesn't
             *       work?
             */
            if(ts_state_timer == 1)
            {
               TIM_Cmd(TIM5, DISABLE);
               TimerPeriod = (SystemCoreClock / (HOME_STEP_FREQ_HZ * 2 * (pscale + 1))) - 1;
               TIM_SetAutoreload(TIM5, TimerPeriod);
               TIM_Cmd(TIM5, ENABLE);

               if(steps_from_home == 0)
               {
                  if(GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_1) == Bit_SET)
                  {
                     /* Flag is uncovered.  We need to go CCW until we cover it. */
                     tilt_stepper_motor_set_CCW();
                  }
                  else
                  {
                     /* Flag is covered. We need to go CW until we uncover it. */
                     tilt_stepper_motor_set_CW();
                  }
               }
               else
               {
                  if(current_pos_rad > 0.0f)
                  {
                     tilt_stepper_motor_set_CCW();
                  }
                  else
                  {
                     tilt_stepper_motor_set_CW();
                  }
               }

            }

            break;
         case TILT_STEPPER_TILT_TABLE:
            if(ts_state_timer == 1)
            {
               if(last_dir)
               {
                  last_dir = 0;
                  tilt_stepper_motor_set_CW();
               }
               else
               {
                  last_dir = 1;
                  tilt_stepper_motor_set_CCW();
               }
               tilt_index = 0;
               TIM_SetAutoreload(TIM5, stepper_profile[tilt_index]);
            }


            break;
         case TILT_STEPPER_TEST_CW:
            if(ts_state_timer == 1)
            {
               TimerPeriod = (SystemCoreClock / (DEFAULT_STEP_FREQ_HZ * 2 * (pscale + 1))) - 1;
               TIM_SetAutoreload(TIM5, TimerPeriod);

               tilt_stepper_motor_set_CW();
            }


            /* if(ts_state_timer%8 == 0) */
            /* { */
            /*    TMC260_status(TMC260_STATUS_CURRENT, &stat_struct, 1); */
            /* } */

            if(ts_state_timer > 80000)
            {
               TMC260_disable();
               tilt_stepper_motor_state_change(TILT_STEPPER_TEST_CCW, 1);
            }
            break;
         case TILT_STEPPER_TEST_CCW:
            if(ts_state_timer == 1)
            {
               TimerPeriod = (SystemCoreClock / (DEFAULT_STEP_FREQ_HZ * 2 * (pscale + 1))) - 1;
               TIM_SetAutoreload(TIM5, TimerPeriod);

               tilt_stepper_motor_set_CCW();
            }

            if(ts_state_timer > 80000)
            {
               TMC260_disable();
               tilt_stepper_motor_state_change(TILT_STEPPER_TEST_CW, 1);
            }

            break;
         case TILT_STEPPER_TEST_DELAY:
            if(ts_state_timer == 1)
            {
               TMC260_status(TMC260_STATUS_POSITION, &stat_struct, 1);
            }

            if(ts_state_timer > 200)
            {
               tilt_stepper_motor_state_change(TILT_STEPPER_TILT_TABLE, 1);
            }
            break;
         case TILT_STEPPER_ERROR:
            break;
         default:
            break;
      }

      TIM_ClearITPendingBit(TIM11, TIM_IT_Update);
   }
}


/**
 * @fn void tilt_stepper_motor_state_change(tilt_stepper_states new_state, uint8_t reset_timer)
 * @brief Used to change the state of the tilt stepper motor control.
 * @param tilt_stepper_states new_state -> Must be of type tilt_stepper_states.
 * @param uint8_t reset_timer -> Anything other than 0 will result in the timer being reset.
 * @return None
 *
 */
void tilt_stepper_motor_state_change(tilt_stepper_states new_state, uint8_t reset_timer)
{

   if(reset_timer)
   {
      ts_state_timer = 0;
   }

   ts_state = new_state;

}

void tilt_stepper_motor_pos(float *rad)
{

   /* *rad = (((float)tilt_index/(float)micro_steps_per_rev)*stepper_gear_ratio_den / stepper_gear_ratio_num) * TILT_STEPPER_TWO_PI; */

   /* if(last_dir == 0) */
   /* { */
   /*    *rad = (TILT_STEPPER_TWO_PI / 2.0f) - *rad; */
   /* } */

   *rad = current_pos_rad;

}


void tilt_stepper_motor_step(void)
{

   if(current_step_dir == TILT_STEPPER_DIR_CW)
   {
      steps_from_home++;
   }

   if(current_step_dir == TILT_STEPPER_DIR_CCW)
   {
      steps_from_home--;
   }

   /* current_pos_rad = (((float)steps_from_home/(float)micro_steps_per_rev)*stepper_gear_ratio_den / stepper_gear_ratio_num) * TILT_STEPPER_TWO_PI; */

   current_pos_rad = (((float)steps_from_home/(float)micro_steps_per_rev)*(stepper_gear_ratio_den / stepper_gear_ratio_num)) * TILT_STEPPER_TWO_PI;

   TMC260_enable();
   TMC260_step();
}


void tilt_stepper_motor_set_CW(void)
{
   current_step_dir = TILT_STEPPER_DIR_CW;
   TMC260_dir_CW();
}

void tilt_stepper_motor_set_CCW(void)
{
   current_step_dir = TILT_STEPPER_DIR_CCW;
   TMC260_dir_CCW();
}