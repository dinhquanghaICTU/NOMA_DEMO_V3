#include <string.h>
#include "driver/uart/uart.h"
#include "driver/hardware.h"
#include "gsm/gsm_send_data_queue.h"
#include "gsm/gsm_nw/gsm_nw.h"
#include "gsm/gsm_app/gsm_app.h"



int main (void){
	rcc_config();
	systick_config();
	uart_init();
	app_init();
  while (1)
  {
    gsm_send_data_queue_proces();
    app_process();

    delay_ms(50);
  }
}
