/** idf example
 *
 * Timed hello world example with output every 5 seconds on the console
 *
 * Some parts are copied from the official idf examples found on 
 * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/
 * 
 * Modified by SteFam @ http://www.fambach.net
 *
 * GPLv3 
 * Blah blah blah, free to use. 
 * I invested some time and mony in those examples if you want to delete this header 
 * please do not forget to mention at least my website, thanks!
*/


#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// method declaration for timer callbacks
static void timer_callback(void* arg);

void app_main(void)
{
	//* setup timer
	
	//** create arguments for timer task
	const esp_timer_create_args_t periodic_timer_args = {
			/* name of the callback function defined before */
            .callback = &timer_callback,
            /* name is optional, but may help identify the timer when debugging */
            .name = "periodic"
    };
	
	// ** create time itself
	esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));	
	
	//** start the timer
	ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 5000000));
	
}


static void periodic_timer_callback(void* arg)
{
    // single console output 
	printf("www.fambach.net\n");
}
