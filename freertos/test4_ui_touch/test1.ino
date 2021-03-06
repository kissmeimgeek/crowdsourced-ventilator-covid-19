#define ARDUINO 10812
#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_HX8357.h>
#include <Adafruit_MPRLS.h>
#include <TouchScreen.h>
#include <SPI.h>
#include "config.h"
#include "types.h"
#include "set_screen.h"
#include "main_screen.h"
#include "mod_screen.h"
#include "alarm_screen.h"

#define RESET_PIN -1
#define EOC_PIN -1
Adafruit_MPRLS mpr = Adafruit_MPRLS(RESET_PIN, EOC_PIN);

Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, TFT_RST);
// For better pressure precision, we need to know the resistance
// between X+ and X- Use any multimeter to read it
// For the one we're using, its 300 ohms across the X plate
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

QueueHandle_t sampleQ = NULL;
QueueHandle_t settingQ = NULL;
QueueHandle_t stateQ = NULL;
Screen screen = SETSCREEN;
ModVal_t modvals;

void setup(void) {
    Serial.begin(115200);
    delay(1000);
    tft.begin();
    tft.setRotation(3);

    // touchscreen library expects 10-bit voltage measurements
    analogReadResolution(10);

    sampleQ = xQueueCreate(1000, sizeof(Sample_t));
    settingQ = xQueueCreate(1, sizeof(Settings_t));
    stateQ = xQueueCreate(1, sizeof(State_t));

    initQ();

    xTaskCreatePinnedToCore(
                    readSensor,          /* Task function. */
                    "ReadSensor",        /* String with name of task. */
                    10000,            /* Stack size in bytes. */
                    NULL,             /* Parameter passed as input of the task */
                    1,                /* Priority of the task. */
                    NULL,             /* Task handle. */
                    0);               /* pin to core 0 */
 
    xTaskCreatePinnedToCore(
                    displayResults,          /* Task function. */
                    "DisplayResults",        /* String with name of task. */
                    10000,            /* Stack size in bytes. */
                    NULL,             /* Parameter passed as input of the task */
                    1,                /* Priority of the task. */
                    NULL,             /* Task handle. */
                    1);               /* pin to core 1 */
}

void loop() {
    delay(1000);
}

// read sensor
void readSensor( void * parameter )
{
    initSensor();
    Sample_t sample;
    for(;;) {
        sample = {millis(), mpr.readPressure() * 0.0101972};
        if(xQueueSend(sampleQ, &sample, 0) != pdTRUE) {
            Serial.println("queue error\n\r");
        }
    }
}


// print sensor results
void displayResults( void * parameter)
{
    Sample_t sample;
    Settings_t settings;
    State_t state;
    Screen oldScreen = NOSCREEN;
    Phase oldPhase = INSPIRATORY;

    SetScreen setScreen = SetScreen(tft, screen, settingQ, modvals);
    MainScreen mainScreen = MainScreen(tft, screen, stateQ);
    ModScreen modScreen = ModScreen(tft, screen, modvals);
    AlarmScreen alarmScreen = AlarmScreen(tft, screen, settingQ, modvals);

    for(;;) {
        // if there was a screen transition, draw the new screen
        if (oldScreen != screen) {
            switch(screen) {
                case SETSCREEN:
                    setScreen.draw();
                    break;
                case MAINSCREEN:
                    mainScreen.draw();
                    break;
                case MODSCREEN:
                    modScreen.draw();
                    break;
                case ALARMSCREEN:
                    alarmScreen.draw();
                    break;
            }
            oldScreen = screen;
        };


        if (xQueuePeek(stateQ, &state, 10) == pdTRUE) {
            // if there was a phase transition to expiratory, update measurements
            if (oldPhase == INSPIRATORY && state.phase == EXPIRATORY) {
                if (screen == MAINSCREEN) {
                    mainScreen.updateMeas(state);
                }
                oldPhase = state.phase;
            }
        };
        // pull samples off the sample queue
        if(xQueueReceive(sampleQ, &sample,100) == pdTRUE) {
            // if we're on the main screen plot the sample, otherwise ignore it
            if (screen == MAINSCREEN) {
                mainScreen.update(sample);
            }
        }

        TSPoint p = ts.getPoint();
        // Scale from ~0->1000 to tft.width using the calibration #'s
        int y = map(p.x, TS_MINX, TS_MAXX, 0, tft.height());
        int x = map(p.y, TS_MINY, TS_MAXY, tft.width(), 0);
        p.x = x;
        p.y = y;

        if (p.z > MINPRESSURE && p.z < MAXPRESSURE) {
            //Serial.println(String(p.x) + " " + String(p.y));
            switch(screen) {
                case MAINSCREEN:
                    mainScreen.handleTouch(p);
                    break;
                case SETSCREEN:
                    setScreen.handleTouch(p);
                    break;
                case MODSCREEN:
                    modScreen.handleTouch(p);
                    break;
                case ALARMSCREEN:
                    alarmScreen.handleTouch(p);
                    break;
            }
        }
    }
}


void initSensor() {
    Serial.println("MPRLS Simple Test");
    if (! mpr.begin()) {
        Serial.println("Failed to communicate with MPRLS sensor, check wiring?");
        while (1) {
            delay(10);
        }
    }
    Serial.println("Found MPRLS sensor");
}

void initQ() {
    Settings_t init = {
        15,     // rr
        10,    // ier
        40,     // pmax
        30,     // peakAlarm
        20,     // mvhiAlarm
        1,      // mvloAlarm
        3,      // dcAlarm
        300,    // tv
        50,     // trigger
        false   // power
    };
    xQueueOverwrite(settingQ, &init);

    State_t state = {
        0, 0, 0, 0, 0, 0, 0, 0, EXPIRATORY
    };
    xQueueOverwrite(stateQ, &state);

    screen = MAINSCREEN;

    modvals = {"", 0, 0, 0, 0};
}


