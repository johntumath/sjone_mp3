#include <LCDdisplay.h>
#include "FreeRTOS.h"
#include "handlers.hpp"
#include "tasks.hpp"
#include <LPC17xx.h>
#include <stdint.h>
#include <stdio.h>
#include "VS1053.h"
#include <queue.h>
#include <stdlib.h>
#include <string.h>
#include "storage.hpp"
#include "semphr.h"
#include "uart0_min.h"
#include "LabGPIOInterrupt.h"
#include <string>
#include "mp3_struct.h"
#include "gpio.hpp"
#include "soft_timer.hpp"
#include "task.h"
#include "Controller.h"
#include "ViewController.h"

Controller ctrl;
VS1053 MP3;
LabGpioInterrupts interrupt;
QueueHandle_t mp3Bytes;
SemaphoreHandle_t sem_start_playback, sem_dreq_high, sem_btn, sem_click, sem_held, sem_view_update;
SoftTimer debouncer(200);

enum buttonList{
  singlePressLeft,    //0
  doublePressLeft,    //1
  heldLeft,           //2
  singlePressCenter,  //3
  doublePressCenter,  //4
  heldCenter,         //5
  singlePressRight,   //6
  doublePressRight,   //7
  heldRight,          //8
  singlePressUp,      //9
  doublePressUp,      //10
  heldUp,             //11
  singlePressDown,    //12
  doublePressDown,    //13
  heldDown            //14
};



void Eint3Handler(void)
{
    interrupt.HandleInterrupt();
}

void DReqISR(void)
{
    long yield = 0;
    if (MP3.DREQ->read() == 1)
    {
        xSemaphoreGiveFromISR(sem_dreq_high, &yield);
    }
    portYIELD_FROM_ISR(yield);
}

void ButtonPushISR()
{
  long yield = 0;
  xSemaphoreGiveFromISR(sem_btn, &yield);
  portYIELD_FROM_ISR(yield);
}

void resetMP3()
{
    MP3.soft_reset();
    MP3.soft_reset();
    // reset playback
    MP3.sciWrite(SCI_MODE, SM_LINE1 | SM_SDINEW);
    // resync
    MP3.sciWrite(SCI_CLOCKF,0x6000);
    MP3.sciWrite(SCI_WRAMADDR, 0x1e29);
    MP3.sciWrite(SCI_WRAM, 0);
    MP3.sciWrite(SCI_DECODE_TIME, 0x00);
    MP3.sciWrite(SCI_DECODE_TIME, 0x00);
}

void Reader(void* pvParameters)
{
    while (1)
    {
        //Wait for signal to start playback
        while(xSemaphoreTake(sem_start_playback, portMAX_DELAY)!= pdTRUE);
        while (ctrl.is_playing_song()){
            if (ctrl.is_stop_requested())
            {
                break;
            }
            else if (!ctrl.is_paused())
            {
                //Push music into Queue
                xQueueSend(mp3Bytes, ctrl.get_next_block(), portMAX_DELAY);
            }
            else if (ctrl.is_paused())
            {
                vTaskDelay(10);
            }
            else
            {
                break;
            }
        }
    }
}

void Player(void * pvParameters)
{
    unsigned char playerBuffer[512];
    u_int8 *bufP;
    while(1)
    {
        resetMP3();
        while (1)
        {
            //Read off queue
            xQueueReceive(mp3Bytes, &playerBuffer, portMAX_DELAY);
            bufP = playerBuffer;
            //Begin playing the block received
            if(MP3.DREQ->read()==0)
            {
                while(xSemaphoreTake(sem_dreq_high, portMAX_DELAY)!= pdTRUE);
            }
            MP3._DCS->setLow();
            for (int i = 0; i < 512; i = i+32)
            {
                if(MP3.DREQ->read()==0)
                {
                    while(xSemaphoreTake(sem_dreq_high, portMAX_DELAY)!= pdTRUE);
                }
                MP3.spiwrite(bufP, 32);
                bufP += 32;
            }
            MP3._DCS->setHigh();
        }
    }
}

void ButtonReaderTask(void * pvParameters)
{
  GPIO nextButton(P2_1);
  GPIO playPauseButton(P2_2);
  GPIO prevButton(P2_5);
  GPIO volumeUpButton(P2_3);
  GPIO volumeDownButton(P2_4);
  nextButton.setAsInput();
  playPauseButton.setAsInput();
  prevButton.setAsInput();
  volumeUpButton.setAsInput();
  volumeDownButton.setAsInput();
  
  buttonList buttonStatus;

  while(1)
  {
    while(xSemaphoreTake(sem_btn, portMAX_DELAY)!= pdTRUE);
    if(debouncer.expired())
    {
      if(prevButton.read() == 1)
      {
        buttonStatus = singlePressLeft;
        vTaskDelay(200);
        if(prevButton.read() == 1)
        {
          while(prevButton.read() == 1)
          {
            // printf("\nleft button is held\n");
            xSemaphoreGive(sem_held);
            vTaskDelay(50);
            buttonStatus = heldLeft;
          }
        }
        else
        {
          printf("\nleft button released\n");
          xSemaphoreGive(sem_click);
        }
      }
      else if(playPauseButton.read() == 1)
      {
        buttonStatus = singlePressCenter;
        vTaskDelay(200);
        if(playPauseButton.read() == 1)
        {
          while(playPauseButton.read() == 1)
          {
            // printf("\ncenter button is held\n");
            xSemaphoreGive(sem_held);
            vTaskDelay(50);
            buttonStatus = heldCenter;
          }
        }
        else
        {
          printf("\ncenter button released\n");
          xSemaphoreGive(sem_click);
        }
      }
      else if(nextButton.read() == 1)
      {
        buttonStatus = singlePressRight;
        vTaskDelay(200);
        if(nextButton.read() == 1)
        {
          while(nextButton.read() == 1)
          {
            // printf("\nright button is held\n");
            xSemaphoreGive(sem_held);
            vTaskDelay(50);
            buttonStatus = heldRight;
          }
        }
        else
        {
          printf("\nright button released\n");
          xSemaphoreGive(sem_click);
        }
      }
      else if(volumeUpButton.read() == 1)
      {
        buttonStatus = singlePressUp;
        vTaskDelay(200);
        if(volumeUpButton.read() == 1)
        {
          while(volumeUpButton.read() == 1)
          {
            // printf("\nup button is held\n");
            xSemaphoreGive(sem_held);
            vTaskDelay(50);
            buttonStatus = heldUp;
          }
        }
        else
        {
          printf("\nup button released\n");
          xSemaphoreGive(sem_click);
        }
      }
      else if(volumeDownButton.read() == 1)
      {
        buttonStatus = singlePressDown;
        vTaskDelay(200);
        if(volumeDownButton.read() == 1)
        {
          while(volumeDownButton.read() == 1)
          {
            // printf("\ndown button is held\n");
            xSemaphoreGive(sem_held);
            vTaskDelay(50);
            buttonStatus = heldDown;
          }
        }
        else
        {
          printf("\ndown button released\n");
          xSemaphoreGive(sem_click);
        }
      }
      vTaskDelay(50);
      debouncer.reset();
    }
  }
}

void View(void * pvParameters)
{
    ViewController VC(&ctrl);
    while(1)
    {
        // Wait for signal from controller to update the view.
        while(xSemaphoreTake(sem_view_update, portMAX_DELAY)!= pdTRUE);
        VC.update_view();
    }
}

void Control(void * pvParameters)
{
    while(1)
    {
        // Wait for signal from button task.
        while(xSemaphoreTake(sem_click, portMAX_DELAY)!= pdTRUE);
        ctrl.on_click();
    }
}

int main(void)
{
    MP3_Handler handler;
    scheduler_add_task(new terminalTask(3));
    MP3.init(P2_7, P1_29, P1_23);
    interrupt.Initialize();
    interrupt.AttachInterruptHandler(2,7,DReqISR,InterruptCondition::kRisingEdge);
    interrupt.AttachInterruptHandler(2,1, ButtonPushISR, InterruptCondition::kRisingEdge);
    interrupt.AttachInterruptHandler(2,2, ButtonPushISR, InterruptCondition::kRisingEdge);
    interrupt.AttachInterruptHandler(2,3, ButtonPushISR, InterruptCondition::kRisingEdge);
    interrupt.AttachInterruptHandler(2,4, ButtonPushISR, InterruptCondition::kRisingEdge);
    interrupt.AttachInterruptHandler(2,5, ButtonPushISR, InterruptCondition::kRisingEdge);
    isr_register(EINT3_IRQn, Eint3Handler);
    sem_click = xSemaphoreCreateBinary();
    sem_view_update = xSemaphoreCreateBinary();
    sem_start_playback = xSemaphoreCreateBinary();
    sem_dreq_high = xSemaphoreCreateBinary();
    sem_btn = xSemaphoreCreateBinary();
    sem_click = xSemaphoreCreateBinary();
    sem_held = xSemaphoreCreateBinary();
    mp3Bytes = xQueueCreate(2, 512);
    xTaskCreate(Control, "Control", STACK_BYTES(2096), NULL, 3, NULL);
    xTaskCreate(View, "View", STACK_BYTES(2096), NULL, 1, NULL);
    xTaskCreate(Reader, "Reader", STACK_BYTES(2096), NULL, 1, NULL);
    xTaskCreate(Player, "Player", STACK_BYTES(1048), NULL, 2, NULL);
    xTaskCreate(ButtonReaderTask, "button", STACK_BYTES(1048), NULL, 2, NULL);
    scheduler_start();
    return -1;
}
