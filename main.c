/*
 * Copyright (C) 2020 Puhang Ding
 *               2020 Jan Schlichter
 *               2020 Nishchay Agrawal
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     drivers_scd30
 * @{
 * @file
 * @brief       Sensirion SCD30 sensor driver implementation
 *
 * @author      Puhang Ding      <czarsir@gmail.com>
 * @author      Jan Schlichter   <schlichter@ibr.cs.tu-bs.de>
 * @author      Nishchay Agrawal <f2016088@pilani.bits-pilani.ac.in>
 * @}
 */

#include <stdio.h>
#include "periph/gpio.h"
#include "xtimer.h"

/* capteur humidité, CO2, température */
#include "scd30.h"
#include "scd30_params.h"
#include "scd30_internal.h"

/* capteur mouvement */
#include "pir.h"
#include "pir_params.h"

/* include lora */
#include "net/loramac.h"
#include "semtech_loramac.h"

/* Add the cayenne_lpp header here */
#include "cayenne_lpp.h"

static long FREQUENCE = 5000;
static gpio_t BUZZER_PIN = GPIO_PIN(1, 10);  // PB5 -> D5

/* Declare globally the loramac descriptor */
extern semtech_loramac_t loramac;

/* Declare globally Cayenne LPP descriptor here */
static cayenne_lpp_t lpp;

/* Device and application informations required for OTAA activation */
static const uint8_t deveui[LORAMAC_DEVEUI_LEN] = { 0x64, 0x05, 0xe0, 0xfd, 0xa2, 0x58, 0x65, 0x82 };
static const uint8_t appeui[LORAMAC_APPEUI_LEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t appkey[LORAMAC_APPKEY_LEN] = { 0x01, 0x3B, 0xF2, 0x33, 0x0C, 0x6D, 0xA2, 0x03, 0x4D, 0xDE, 0x8E, 0xD0, 0x07, 0xB6, 0x4E, 0x6A };

#define MEASUREMENT_INTERVAL_SECS (5)

scd30_t scd30_dev_scd;
scd30_params_t params = SCD30_PARAMS;
scd30_measurement_t result;

static pir_t dev_pir;

#define PANIC_BUTTON_PIN GPIO_PIN(0, 9) //PA9 -> D0
int flag_panic = 0;


#define NBR_MEAS 4

static void sender(scd30_measurement_t result, uint8_t status_pir)
{
    /* TODO: prepare cayenne lpp payload here */
    cayenne_lpp_add_temperature(&lpp, 0, result.temperature);
    cayenne_lpp_add_analog_input(&lpp, 1, result.co2_concentration);
    cayenne_lpp_add_relative_humidity(&lpp, 2, result.relative_humidity);
    cayenne_lpp_add_presence(&lpp, 3, status_pir);

    printf("Sending LPP data\n");

    /* send the LoRaWAN message */
    // uint8_t ret = semtech_loramac_send(&loramac, lpp.buffer, lpp.cursor);
    // //printf("Payload length %d\n",lpp.cursor);
    
    // if (ret != SEMTECH_LORAMAC_TX_DONE) {
    //     printf("Cannot send lpp message, ret code: %d\n", ret);
    // }

    /* TODO: clear buffer once done here */
    cayenne_lpp_reset(&lpp);

    /* this should never be reached */
    return;
}

void tone (long int duration) {
    float NBR_iter = duration*FREQUENCE;
    //float demi_periode = 1000/(2*FREQUENCE);
    printf("NBR_iter : %f\n", NBR_iter);
    for (int i=0; i < 20000; i++) {
        gpio_write(BUZZER_PIN, 1);
        xtimer_usleep(100);
        gpio_clear(BUZZER_PIN);
        xtimer_usleep(100);
    }
}

void tone_gendarmerie (long int duration) {
    for (int l = 0; l < duration; l++) {

        // Boucle pour une seconde
        //float NBR_iter = duration*435;
        for (int i=0; i < 435; i++) {
            if (flag_panic < 1)
                return;
            // 1 er ton
            gpio_write(BUZZER_PIN, 1);
            xtimer_usleep(1149);
            gpio_clear(BUZZER_PIN);
            xtimer_usleep(1149);

        }
        //NBR_iter = duration*735;
        for (int i=0; i < 735; i++) {
            if (flag_panic < 1)
                return;
            // 2 eme ton
            gpio_write(BUZZER_PIN, 1);
            xtimer_usleep(683);
            gpio_clear(BUZZER_PIN);
            xtimer_usleep(683);
        }
    } 
}

/*
static void cb_panic_button (void *arg)
{
    printf("Panic button n%d pressed\n", (int)arg);
    if (flag_panic) 
    {
        flag_panic = 0;
    }
    else 
    {
        flag_panic = 1;
    }
}
*/

float moyenne_CO2 (scd30_t* scd30_dev_scd) 
{
    float mean_meas = 0.0;

    for (int i=0; i<NBR_MEAS; i++) {
        scd30_read_triggered(scd30_dev_scd, &result);
        mean_meas += result.co2_concentration;
        xtimer_usleep(300);
    }

    mean_meas = mean_meas/NBR_MEAS;
    return mean_meas;

}

int main(void)
{
    /*===================INITIALISATION BUZZER========================*/
    /*
    if (gpio_init(BUZZER_PIN, GPIO_OUT)) {
        printf("Error to initialize GPIO_PIN(%d %d)\n", PORT_B, 5);
        return -1;
    }
    */
    
    /*===================INITIALISATION LORA========================*/
    /* use a fast datarate so we don't use the physical layer too much */
    semtech_loramac_set_dr(&loramac, 5);

    /* set the LoRaWAN keys */
    semtech_loramac_set_deveui(&loramac, deveui);
    semtech_loramac_set_appeui(&loramac, appeui);
    semtech_loramac_set_appkey(&loramac, appkey);

    /* start the OTAA join procedure */
    puts("Starting join procedure");
    /*
    if (semtech_loramac_join(&loramac, LORAMAC_JOIN_OTAA) != SEMTECH_LORAMAC_JOIN_SUCCEEDED) {
        puts("Join procedure failed");
        // enlever le commentaire ci dessous une fois à proximité d'une gateway campus iot
        //return 1;
    }
    */
    puts("Join procedure succeeded");

    /*===================INITIALISATION PIR========================*/
    puts("PIR motion sensor test application\n");
    pir_params_t pir_parametres;

    pir_parametres.gpio = GPIO_PIN(1, 10);
    pir_parametres.active_high = 1;
    if (pir_init(&dev_pir, &pir_parametres) == 0) {
        puts("[OK]\n");
    }
    else {
        puts("[Failed]");
        return 1;
    }

   /* if (gpio_init_int(PANIC_BUTTON_PIN, GPIO_IN_PD, GPIO_RISING, cb_panic_button, (void *)1) < 0) {
        puts("Error on button init");
        return 1;
    }*/

    /*===================INITIALISATION SCD========================*/
    printf("SCD30 Test:\n");
    //int i = 0;

    scd30_init(&scd30_dev_scd, &params);
    //scd30_reset(&scd30_dev_scd);
    uint16_t pressure_compensation = SCD30_DEF_PRESSURE;
    scd30_set_param(&scd30_dev_scd, SCD30_INTERVAL, MEASUREMENT_INTERVAL_SECS);
    scd30_set_param(&scd30_dev_scd, SCD30_START, pressure_compensation);

/*    scd30_get_param(&scd30_dev_scd, SCD30_INTERVAL, &value);
    printf("[test][dev-%d] Interval: %u s\n", i, value);
    scd30_get_param(&scd30_dev_scd, SCD30_T_OFFSET, &value);
    printf("[test][dev-%d] Temperature Offset: %u.%02u C\n", i, value / 100u,
           value % 100u);
    scd30_get_param(&scd30_dev_scd, SCD30_A_OFFSET, &value);
    printf("[test][dev-%d] Altitude Compensation: %u m\n", i, value);
    scd30_get_param(&scd30_dev_scd, SCD30_ASC, &value);
    printf("[test][dev-%d] ASC: %u\n", i, value);
    scd30_get_param(&scd30_dev_scd, SCD30_FRC, &value);
    printf("[test][dev-%d] FRC: %u ppm\n", i, value);*/

    while (1) {
        
        xtimer_sleep(MEASUREMENT_INTERVAL_SECS);
        //scd30_read_triggered(&scd30_dev_scd, &result);
        // printf("[scd30_test-%d] Triggered measurements co2: %.02fppm," " temp: %.02f°C, hum: %.02f%%. \n", ++i, 
        //     result.co2_concentration, result.temperature, result.relative_humidity);
        printf("Status: %s\n", pir_get_status(&dev_pir) == PIR_STATUS_INACTIVE ? "inactive" : "active");
        //uint8_t status_pir = pir_get_status(&dev_pir);
        // printf("Status: %s\n", pir_get_status(&dev_pir) == PIR_STATUS_INACTIVE ?
        //        "inactive" : "active");
        // xtimer_usleep(1000 * 1000);
        //sender(result, status_pir);
        float mean = moyenne_CO2(&scd30_dev_scd);
        printf("Moyenne CO2 : %3.02f\n", mean);
    }

    uint8_t status_pir;
    sender(result, status_pir);
    return 0;
}

