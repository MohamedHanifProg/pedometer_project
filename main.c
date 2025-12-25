/*******************************************************************************
 * INCLUDES
 ******************************************************************************/
#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// Adjust includes to your project structure
#include "System/system.h"
#include "oledDriver/oledC.h"
#include "oledDriver/oledC_shapes.h"
#include "oledDriver/oledC_colors.h"
#include "System/delay.h"
#include "Accel_i2c.h"

/*******************************************************************************
 * MACROS & CONSTANTS
 ******************************************************************************/
#define MOVEMENT_THRESHOLD       50
#define MENU_CLOCK_MARGIN_RIGHT  15

// Timer / Animation
#define ANIMATION_INTERVAL       500 // milliseconds between icon toggles
#define ICON_DISPLAY_DELAY       2000 // 2000ms delay after movement stops

// For time configuration regions
#define HOUR_REGION_X       20
#define HOUR_REGION_Y       40
#define HOUR_REGION_WIDTH   30
#define HOUR_REGION_HEIGHT  30

#define MIN_REGION_X        60
#define MIN_REGION_Y        40
#define MIN_REGION_WIDTH    30
#define MIN_REGION_HEIGHT   30

#define TEXT_OFFSET         3

// For date configuration regions
#define DAY_REGION_X       20
#define DAY_REGION_Y       40
#define DAY_REGION_WIDTH   30
#define DAY_REGION_HEIGHT  30
#define MONTH_REGION_X     60
#define MONTH_REGION_Y     40
#define MONTH_REGION_WIDTH 30
#define MONTH_REGION_HEIGHT 30

// For 12H/24H configuration regions
#define OPT12H_X       20
#define OPT12H_Y       40
#define OPT12H_WIDTH   35
#define OPT12H_HEIGHT  30
#define OPT24H_X       60
#define OPT24H_Y       40
#define OPT24H_WIDTH   35
#define OPT24H_HEIGHT  30
#define GRAPH_SAMPLES 90
// For pedometer threshold, step array, etc.
#define STEP_THRESHOLD         500  // Adjust based on testing
#define SCREEN_UPDATE_INTERVAL 1000 // Update display every 1 second
#define STEP_ARRAY_SIZE        5    // Number of samples for averaging

/*******************************************************************************
 * GLOBAL VARIABLES & TYPE DEFINITIONS
 ******************************************************************************/
static volatile uint32_t msCounter = 0;  // Global milliseconds counter (Timer1)

// Timer & button globals
static bool s1Pressed = false;           
static uint32_t lastPressTimeS1 = 0;     
static uint32_t lastMovementTime = 0;
static uint32_t lastStepTime = 0;

// Time & Date globals (Watch Display)
static uint8_t seconds = 0;
static uint8_t minutes = 0;
static uint8_t hours = 12;
static uint8_t day = 1;
static uint8_t month = 1;
static char ampmStr[3] = "PM"; // "AM"/"PM" or "" for 24H

// Watch/ Menu states
typedef enum {
    STATE_TIME_DISPLAY, // Normal watch mode
    STATE_MENU          // Menu mode
} WatchState;
static WatchState currentState = STATE_TIME_DISPLAY;

// Menu definitions
typedef enum {
    MENU_PEDOMETER,
    MENU_FORMAT_12H_24H,
    MENU_SET_TIME,
    MENU_SET_DATE,
    MENU_EXIT,
    MENU_COUNT
} MenuOption;
static MenuOption selectedMenu = MENU_PEDOMETER; // Default selected item

static const char* menuItems[] = {
    "Pedometer Graph",
    "12H/24H Interval",
    "Set Time",
    "Set Date",
    "Exit"
};

// Clock display parameters structure
typedef struct {
    // Old values (for partial-update comparison)
    uint8_t oldHours;
    uint8_t oldMinutes;
    uint8_t oldSeconds;
    uint8_t oldDay;
    uint8_t oldMonth;
    char oldAmPm[3];

    // Positions/scales
    int hourX, hourY, hourScale;
    int minX,  minY,  minScale;
    int secX,  secY,  secScale;
    int ampmX, ampmY, ampmScale;
    int dateX, dateY, dateScale;

    // Whether or not to show date
    bool showDate;
} ClockDisplayParams;

// Watch mode display parameters
static ClockDisplayParams watchDisplay = {
    .oldHours = 99,
    .oldMinutes = 99,
    .oldSeconds = 99,
    .oldDay = 99,
    .oldMonth = 99,
    .oldAmPm = "XX",
    .hourX = 0,  .hourY = 30, .hourScale = 2,
    .minX  = 36, .minY  = 30, .minScale  = 2,
    .secX  = 72, .secY  = 30, .secScale  = 2,
    .ampmX = 10, .ampmY = 65, .ampmScale = 1,
    .dateX = 60, .dateY = 65, .dateScale = 1,
    .showDate = true
};

// Menu mode display parameters
static ClockDisplayParams menuDisplay = {
    .oldHours = 99,
    .oldMinutes = 99,
    .oldSeconds = 99,
    .oldDay = 0,
    .oldMonth = 0,
    .oldAmPm = "XX",
    .hourX = 45 - MENU_CLOCK_MARGIN_RIGHT, .hourY = 2, .hourScale = 1,
    .minX  = 63 - MENU_CLOCK_MARGIN_RIGHT, .minY  = 2, .minScale  = 1,
    .secX  = 81 - MENU_CLOCK_MARGIN_RIGHT, .secY  = 2, .secScale  = 1,
    .ampmX = 99 - MENU_CLOCK_MARGIN_RIGHT, .ampmY = 2, .ampmScale = 1,
    .dateX = 0, .dateY = 0, .dateScale = 0,
    .showDate = false
};

/*******************************************************************************
 * PEDOMETER / FOOT ICON
 ******************************************************************************/
static bool footToggle = false;
static bool movementDetected = false;
static uint32_t currentPace = 0;  // steps per minute
static uint32_t decayTimer = 0;

// Two foot bitmaps (16x16) for animation
static const uint16_t foot1Bitmap[16] = {
    0x7800, 0xF800, 0xFC00, 0xFC00, 0xFC00, 0x7C1E, 0x783E, 0x047F,
    0x3F9F, 0x1F3E, 0x0C3E, 0x003E, 0x0004, 0x00F0, 0x01F0, 0x00E0
};
static const uint16_t foot2Bitmap[16] = {
    0x001E, 0x003F, 0x003F, 0x007F, 0x003F, 0x383E, 0x7C1E, 0x7E10,
    0x7E7C, 0x7E78, 0x7C30, 0x3C00, 0x2000, 0x1E00, 0x1F00, 0x0E00
};

/*******************************************************************************
 * STEP-COUNTING DATA
 ******************************************************************************/
static int32_t lastX = 0, lastY = 0, lastZ = 0;
static uint32_t stepCount = 0;
static uint32_t stepArray[STEP_ARRAY_SIZE] = {0};
static uint8_t stepIndex = 0;
static uint32_t totalSteps = 0;
static uint32_t prevPaceDisplay = 0;

/*******************************************************************************
 * GRAPH DATA ARRAYS (90 SAMPLES FOR 2 MINUTES)
 ******************************************************************************/
static int stepsHistory[90] = {0};     // raw pace data
static int smoothedSteps[90] = {0};    // smoothed
static uint8_t stepsHistoryIndex = 0;  // circular index

/*******************************************************************************
 * FUNCTION PROTOTYPES
 ******************************************************************************/
void haltOnError(const char *errorMsg);
int16_t readAxisValue(uint8_t regAddr);
void drawFootIcon(uint8_t x, uint8_t y, const uint16_t *bitmap,
                  uint8_t width, uint8_t height, uint16_t color);
static void checkForMovement(void);
static void setupAccelerometer(void);
static bool isDeviceFlipped(void);
static uint8_t getDaysInMonth(uint8_t m);
static void drawClockPartial(ClockDisplayParams* p,
                             uint8_t h, uint8_t m, uint8_t s,
                             const char* ampm,
                             uint8_t d, uint8_t mo);
static void drawWatchColons(void);
static void drawMenuColons(void);
static void initOLED(void);
static void drawInitialDisplay(void);
static void updateTime(void);
void TMR1_Initialize(void);
void __attribute__((interrupt, auto_psv)) _T1Interrupt(void);
static uint32_t getMillis(void);
static bool isButtonPressed(volatile unsigned int* port, uint8_t bit);
static void navigateMenuUp(void);
static void navigateMenuDown(void);
void drawRectangleOutline(uint8_t x, uint8_t y,
                          uint8_t widthRect, uint8_t heightRect,
                          uint16_t color);
static void enterMenu(void);
static void processButtons(void);
static void drawMenuStatic(void);
static void setTimeConfig(void);
static void setFormatConfig(void);
static void setDateConfig(void);
static void selectMenuOption(void);
static void drawMenu(void);

/* --- NEW GRAPH FUNCTIONS --- */
static void updateStepsHistory(void);
static void computeSmoothedSteps(void);
static void drawDashedLine(int x1, int y1, int x2, int y2, 
                           int dashLen, int gapLen, uint16_t color);
static void drawGraphGrid(void);
static void drawStepsGraph(void);
static void displayPedometerGraph(void);

/*******************************************************************************
 * MAIN
 ******************************************************************************/
int main(void)
{
    // System init
    SYSTEM_Initialize();

    TRISAbits.TRISA8 = 0; // LED1 (RA8)
    TRISAbits.TRISA9 = 0; // LED2 (RA9)
    LATAbits.LATA8 = 0;
    LATAbits.LATA9 = 0;

    // Timer1 init (1ms tick)
    TMR1_Initialize();

    // OLED init
    initOLED();
    drawInitialDisplay();

    // I2C + accelerometer check
    i2c1_open();
    uint8_t devId = 0;
    for (int i = 0; i < 3; i++)
    {
        if ((i2cReadSlaveRegister(0x3A, 0x00, &devId) == OK) && (devId == 0xE5)) {
            break;
        }
        if (i == 2)
            haltOnError("Device ID Mismatch");
        DELAY_milliseconds(10);
    }
    setupAccelerometer();

    uint32_t lastTimeUpdate = getMillis();
    uint32_t lastPedometerUpdate = getMillis();
    decayTimer = getMillis();

    while (1)
    {
        processButtons();

        // 1) Time display mode
        if (currentState == STATE_TIME_DISPLAY)
        {
            uint32_t currentTime = getMillis();
            if (currentTime - lastTimeUpdate >= 1000)
            {
                lastTimeUpdate = currentTime;
                updateTime();
                drawClockPartial(&watchDisplay, hours, minutes, seconds, ampmStr, day, month);
            }
        }
        // 2) Menu mode
        else if (currentState == STATE_MENU)
        {
            drawMenu();
        }

        // 3) Pedometer update (every ~100ms)
        if (getMillis() - lastPedometerUpdate >= 100)
        {
            lastPedometerUpdate = getMillis();
            checkForMovement();  // update stepCount/currentPace

            // If no movement for 2000ms => start decaying
            if (getMillis() - lastStepTime >= 2000)
            {
                if (getMillis() - decayTimer >= 1000)
                {
                    decayTimer = getMillis();
                    if (currentPace > 0)
                    {
                        currentPace--;
                        footToggle = !footToggle;
                    }
                }
            }
            else
            {
                // Movement detected recently => reset decay timer
                decayTimer = getMillis();
            }

            // Store the new pace in stepsHistory
            updateStepsHistory();

            // Update display only if currentPace changed
       // Update display only if currentPace changed
if (currentPace != prevPaceDisplay)
{
    // Clear old area
    oledC_DrawRectangle(0, 0, 16, 16, OLEDC_COLOR_BLACK);
    oledC_DrawRectangle(20, 0, 50, 8, OLEDC_COLOR_BLACK);

    // Only display icon and steps if movement was detected recently
    if ((getMillis() - lastStepTime < 2000) && (currentPace > 0))
    {
        // Show foot icon based on toggle state
        if (footToggle)
            drawFootIcon(0, 0, foot1Bitmap, 16, 16, OLEDC_COLOR_WHITE);
        else
            drawFootIcon(0, 0, foot2Bitmap, 16, 16, OLEDC_COLOR_WHITE);

        // Print pace value
        char paceStr[10];
        snprintf(paceStr, sizeof(paceStr), "%lu", currentPace);
        oledC_DrawString(20, 0, 1, 1, (uint8_t *) paceStr, OLEDC_COLOR_WHITE);
    }
    else
    {
        // If no recent movement, clear the area so nothing is shown
        oledC_DrawRectangle(0, 0, 16, 16, OLEDC_COLOR_BLACK);
        oledC_DrawRectangle(20, 0, 50, 8, OLEDC_COLOR_BLACK);
    }
    prevPaceDisplay = currentPace;
}

        }

        DELAY_milliseconds(20);
    }

    return 0;
}

/*******************************************************************************
 * FUNCTION IMPLEMENTATIONS
 ******************************************************************************/

/*------------------------------------------------------------------------------
 * haltOnError
 *----------------------------------------------------------------------------*/
void haltOnError(const char *errorMsg) {
    oledC_DrawString(0, 20, 1, 1, (uint8_t *) errorMsg, OLEDC_COLOR_DARKRED);
    printf("Error: %s\n", errorMsg);
    while (1);
}

/*------------------------------------------------------------------------------
 * readAxisValue: Reads a 16-bit axis value from the accelerometer
 *----------------------------------------------------------------------------*/
int16_t readAxisValue(uint8_t regAddr) {
    uint8_t lowByte = 0, highByte = 0;
    const int maxAttempts = 5;
    int attempt;

    for (attempt = 0; attempt < maxAttempts; attempt++) {
        if (i2cReadSlaveRegister(0x3A, regAddr, &lowByte) == OK) break;
        if (attempt == maxAttempts - 1) haltOnError("I2C LSB Read Fail");
        DELAY_milliseconds(2);
    }
    for (attempt = 0; attempt < maxAttempts; attempt++) {
        if (i2cReadSlaveRegister(0x3A, regAddr + 1, &highByte) == OK) break;
        if (attempt == maxAttempts - 1) {
            i2cWriteSlave(0x3A, 0x2D, 0x08);
            DELAY_milliseconds(10);
            return 0;
        }
        DELAY_milliseconds(2);
    }

    return (int16_t)((highByte << 8) | lowByte);
}

/*------------------------------------------------------------------------------
 * drawFootIcon
 *----------------------------------------------------------------------------*/
void drawFootIcon(uint8_t x, uint8_t y, const uint16_t *bitmap,
                  uint8_t width, uint8_t height, uint16_t color) {
    for (uint8_t row = 0; row < height; row++) {
        uint16_t rowData = bitmap[row];
        for (uint8_t col = 0; col < width; col++) {
            if (rowData & (1 << (width - 1 - col))) {
                oledC_DrawPoint(x + col, y + row, color);
            }
        }
    }
}

/*------------------------------------------------------------------------------
 * checkForMovement: updates stepCount/currentPace
 *----------------------------------------------------------------------------*/
static void checkForMovement(void) {
    int16_t x = readAxisValue(0x32);
    int16_t y = readAxisValue(0x34);
    int16_t z = readAxisValue(0x36);
    uint32_t now = getMillis();

    // Check significant changes
    if (abs(x - lastX) > STEP_THRESHOLD ||
        abs(y - lastY) > STEP_THRESHOLD ||
        abs(z - lastZ) > STEP_THRESHOLD)
    {
        stepCount++;
    }

    lastX = x;
    lastY = y;
    lastZ = z;

    // Update every SCREEN_UPDATE_INTERVAL (1000 ms)
  if (now - lastStepTime >= SCREEN_UPDATE_INTERVAL) {
    int stepDiff = stepCount - stepArray[stepIndex];
    stepArray[stepIndex] = stepCount;
    stepIndex = (stepIndex + 1) % STEP_ARRAY_SIZE;

    if (stepDiff > 0) {
        totalSteps += stepDiff;
        // current calculation:
        currentPace = totalSteps * (60 / (STEP_ARRAY_SIZE * (SCREEN_UPDATE_INTERVAL / 1000)));
        lastStepTime = now;
        footToggle = !footToggle;
    }
    stepCount = 0;
}

}

/*------------------------------------------------------------------------------
 * setupAccelerometer
 *----------------------------------------------------------------------------*/
static void setupAccelerometer(void) {
    for (int i = 0; i < 3; i++) {
        if (i2cWriteSlave(0x3A, 0x2D, 0x08) == OK) break;
        if (i == 2) haltOnError("Power Config Fail");
        DELAY_milliseconds(10);
    }
    for (int i = 0; i < 3; i++) {
        if (i2cWriteSlave(0x3A, 0x31, 0x0B) == OK) break;
        if (i == 2) haltOnError("Data Format Fail");
        DELAY_milliseconds(10);
    }
}

/*------------------------------------------------------------------------------
 * isDeviceFlipped
 *----------------------------------------------------------------------------*/
static bool isDeviceFlipped(void) {
    int16_t zValue = readAxisValue(0x36);
    return (zValue < 0);
}

/*------------------------------------------------------------------------------
 * getDaysInMonth
 *----------------------------------------------------------------------------*/
static uint8_t getDaysInMonth(uint8_t m) {
    static const uint8_t DaysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return DaysInMonth[m - 1];
}

/*------------------------------------------------------------------------------
 * drawClockPartial
 *----------------------------------------------------------------------------*/
static void drawClockPartial(ClockDisplayParams* p,
        uint8_t h, uint8_t m, uint8_t s,
        const char* ampm,
        uint8_t d, uint8_t mo)
{
    // Hours
    if (h != p->oldHours) {
        oledC_DrawRectangle(p->hourX, p->hourY,
                            p->hourX + 10 * p->hourScale,
                            p->hourY + 10 * p->hourScale,
                            OLEDC_COLOR_BLACK);
        char hourStr[3];
        snprintf(hourStr, sizeof(hourStr), "%02d", h);
        oledC_DrawString(p->hourX, p->hourY,
                         p->hourScale, p->hourScale,
                         (uint8_t*) hourStr, OLEDC_COLOR_WHITE);
        p->oldHours = h;
    }
    // Minutes
    if (m != p->oldMinutes) {
        oledC_DrawRectangle(p->minX, p->minY,
                            p->minX + 10 * p->minScale,
                            p->minY + 10 * p->minScale,
                            OLEDC_COLOR_BLACK);
        char minStr[3];
        snprintf(minStr, sizeof(minStr), "%02d", m);
        oledC_DrawString(p->minX, p->minY,
                         p->minScale, p->minScale,
                         (uint8_t*) minStr, OLEDC_COLOR_WHITE);
        p->oldMinutes = m;
    }
    // Seconds
    if (s != p->oldSeconds) {
        oledC_DrawRectangle(p->secX, p->secY,
                            p->secX + 10 * p->secScale,
                            p->secY + 10 * p->secScale,
                            OLEDC_COLOR_BLACK);
        char secStr[3];
        snprintf(secStr, sizeof(secStr), "%02d", s);
        oledC_DrawString(p->secX, p->secY,
                         p->secScale, p->secScale,
                         (uint8_t*) secStr, OLEDC_COLOR_WHITE);
        p->oldSeconds = s;
    }
    // AM/PM or blank for 24H
    if (ampmStr[0] != '\0') { // 12H mode
        if (strcmp(ampm, p->oldAmPm) != 0) {
            oledC_DrawRectangle(p->ampmX, p->ampmY,
                                p->ampmX + 24 * p->ampmScale,
                                p->ampmY + 10 * p->ampmScale,
                                OLEDC_COLOR_BLACK);
            oledC_DrawString(p->ampmX, p->ampmY,
                             p->ampmScale, p->ampmScale,
                             (uint8_t*) ampm, OLEDC_COLOR_WHITE);
            strcpy(p->oldAmPm, ampm);
        }
    } else {
        // 24H mode => clear am/pm if it was displayed
        if (strlen(p->oldAmPm) > 0) {
            oledC_DrawRectangle(p->ampmX, p->ampmY,
                                p->ampmX + 24 * p->ampmScale,
                                p->ampmY + 10 * p->ampmScale,
                                OLEDC_COLOR_BLACK);
            p->oldAmPm[0] = '\0';
        }
    }
    // Date
    if (p->showDate) {
        if (d != p->oldDay || mo != p->oldMonth) {
            oledC_DrawRectangle(p->dateX, p->dateY,
                                p->dateX + 30 * p->dateScale,
                                p->dateY + 16 * p->dateScale,
                                OLEDC_COLOR_BLACK);
            char dateStr[6];
            snprintf(dateStr, sizeof(dateStr), "%02d/%02d", d, mo);
            oledC_DrawString(p->dateX, p->dateY,
                             p->dateScale, p->dateScale,
                             (uint8_t*) dateStr, OLEDC_COLOR_WHITE);
            p->oldDay = d;
            p->oldMonth = mo;
        }
    }
}

/*------------------------------------------------------------------------------
 * drawWatchColons
 *----------------------------------------------------------------------------*/
static void drawWatchColons(void) {
    oledC_DrawString(24, 30, 2, 2, (uint8_t *) ":", OLEDC_COLOR_WHITE);
    oledC_DrawString(60, 30, 2, 2, (uint8_t *) ":", OLEDC_COLOR_WHITE);
}

/*------------------------------------------------------------------------------
 * drawMenuColons
 *----------------------------------------------------------------------------*/
static void drawMenuColons(void) {
    int margin = MENU_CLOCK_MARGIN_RIGHT;
    oledC_DrawString(57 - margin, 2, 1, 1, (uint8_t *) ":", OLEDC_COLOR_WHITE);
    oledC_DrawString(75 - margin, 2, 1, 1, (uint8_t *) ":", OLEDC_COLOR_WHITE);
}

/*------------------------------------------------------------------------------
 * initOLED
 *----------------------------------------------------------------------------*/
static void initOLED(void) {
    oledC_setSleepMode(false);
    oledC_DrawRectangle(0, 0, 95, 95, OLEDC_COLOR_BLACK);
}

/*------------------------------------------------------------------------------
 * drawInitialDisplay
 *----------------------------------------------------------------------------*/
static void drawInitialDisplay(void) {
    oledC_DrawRectangle(0, 0, 95, 95, OLEDC_COLOR_BLACK);
    drawWatchColons();

    // Force full update
    watchDisplay.oldHours   = 99;
    watchDisplay.oldMinutes = 99;
    watchDisplay.oldSeconds = 99;
    watchDisplay.oldDay     = 99;
    watchDisplay.oldMonth   = 99;
    strcpy(watchDisplay.oldAmPm, "XX");

    drawClockPartial(&watchDisplay, hours, minutes, seconds, ampmStr, day, month);
}

/*------------------------------------------------------------------------------
 * updateTime
 *----------------------------------------------------------------------------*/
static void updateTime(void) {
    seconds++;
    if (seconds >= 60) {
        seconds = 0;
        minutes++;
        if (minutes >= 60) {
            minutes = 0;
            hours++;
            if (ampmStr[0] != '\0') { // 12H
                if (hours == 12) {
                    strcpy(ampmStr, (strcmp(ampmStr, "AM")==0) ? "PM" : "AM");
                }
                if (hours > 12) {
                    hours -= 12;
                }
                if (hours == 0) {
                    hours = 12;
                }
            } else { // 24H
                if (hours >= 24) {
                    hours = 0;
                    day++;
                    uint8_t maxDays = getDaysInMonth(month);
                    if (day > maxDays) {
                        day = 1;
                        month++;
                        if (month > 12) {
                            month = 1;
                        }
                    }
                }
            }
        }
    }
}

/*------------------------------------------------------------------------------
 * TMR1_Initialize
 *----------------------------------------------------------------------------*/
void TMR1_Initialize(void) {
    T1CON = 0;
    TMR1 = 0;
    PR1 = 4000 - 1;   // for 1ms tick at ~4MHz
    T1CONbits.TCKPS = 0;
    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;
    T1CONbits.TON = 1;
}

/*------------------------------------------------------------------------------
 * _T1Interrupt
 *----------------------------------------------------------------------------*/
void __attribute__((interrupt, auto_psv)) _T1Interrupt(void) {
    IFS0bits.T1IF = 0;
    msCounter++;
}

/*------------------------------------------------------------------------------
 * getMillis
 *----------------------------------------------------------------------------*/
static uint32_t getMillis(void) {
    return msCounter;
}

/*------------------------------------------------------------------------------
 * isButtonPressed
 *----------------------------------------------------------------------------*/
static bool isButtonPressed(volatile unsigned int* port, uint8_t bit) {
    return ((*port & (1 << bit)) == 0);
}

/*------------------------------------------------------------------------------
 * navigateMenuUp / navigateMenuDown
 *----------------------------------------------------------------------------*/
static void navigateMenuUp(void) {
    if (selectedMenu > 0) {
        selectedMenu--;
    }
}
static void navigateMenuDown(void) {
    if (selectedMenu < MENU_COUNT - 1) {
        selectedMenu++;
    }
}

/*------------------------------------------------------------------------------
 * drawRectangleOutline
 *----------------------------------------------------------------------------*/
void drawRectangleOutline(uint8_t x, uint8_t y,
        uint8_t widthRect, uint8_t heightRect,
        uint16_t color) {
    uint8_t x2 = x + widthRect - 1;
    uint8_t y2 = y + heightRect - 1;

    oledC_DrawLine(x, y, x2, y, 1, color);
    oledC_DrawLine(x, y2, x2, y2, 1, color);

    for (uint8_t i = y + 1; i < y2; i++) {
        oledC_DrawPoint(x, i, color);
        oledC_DrawPoint(x2, i, color);
    }
}

/*------------------------------------------------------------------------------
 * enterMenu
 *----------------------------------------------------------------------------*/
static void enterMenu(void) {
    currentState = STATE_MENU;
    selectedMenu = MENU_PEDOMETER;
    oledC_DrawRectangle(0, 0, 95, 95, OLEDC_COLOR_BLACK);
}

/*------------------------------------------------------------------------------
 * processButtons
 *----------------------------------------------------------------------------*/
static void processButtons(void) {
    bool s1Down = isButtonPressed(&PORTA, 11);
    bool s2Down = isButtonPressed(&PORTA, 12);

    LATAbits.LATA8 = s1Down ? 1 : 0;
    LATAbits.LATA9 = s2Down ? 1 : 0;

    if (currentState == STATE_TIME_DISPLAY) {
        if (s1Down) {
            if (!s1Pressed) {
                s1Pressed = true;
                lastPressTimeS1 = getMillis();
            } else if ((getMillis() - lastPressTimeS1) >= 2000) {
                enterMenu();
                s1Pressed = false;
            }
        } else {
            s1Pressed = false;
        }
    }
}

/*------------------------------------------------------------------------------
 * drawMenuStatic
 *----------------------------------------------------------------------------*/
static void drawMenuStatic(void) {
    int marginLeft = 1;
    drawMenuColons();
    for (int i = 0; i < MENU_COUNT; i++) {
        oledC_DrawString(marginLeft, 15 + i * 15, 1, 1,
                (uint8_t*) menuItems[i], OLEDC_COLOR_WHITE);
    }
    oledC_DrawRectangle(0, 15 + selectedMenu * 15,
            95, 15 + (selectedMenu + 1) * 15,
            OLEDC_COLOR_WHITE);
    oledC_DrawString(marginLeft, 15 + selectedMenu * 15, 1, 1,
            (uint8_t*) menuItems[selectedMenu], OLEDC_COLOR_BLACK);
}

/*------------------------------------------------------------------------------
 * setTimeConfig
 *----------------------------------------------------------------------------*/
static void setTimeConfig(void) {
    uint8_t newHour = hours;
    uint8_t newMinute = minutes;
    uint8_t activeField = 0; 
    uint8_t prevActiveField = activeField;
    uint32_t bothPressStart = 0;
    uint32_t flipStart = 0;

    oledC_DrawRectangle(0, 0, 95, 95, OLEDC_COLOR_BLACK);
    oledC_DrawString(5, 5, 2, 2, (uint8_t*) "Set Time", OLEDC_COLOR_WHITE);

    uint8_t prevHour = newHour;
    uint8_t prevMinute = newMinute;

    // Draw initial
    {
        char hourStr[3], minStr[3];
        snprintf(hourStr, sizeof(hourStr), "%02d", newHour);
        snprintf(minStr, sizeof(minStr), "%02d", newMinute);
        oledC_DrawString(HOUR_REGION_X + TEXT_OFFSET,
                HOUR_REGION_Y + TEXT_OFFSET, 2, 2,
                (uint8_t*) hourStr, OLEDC_COLOR_WHITE);
        oledC_DrawString(MIN_REGION_X + TEXT_OFFSET,
                MIN_REGION_Y + TEXT_OFFSET, 2, 2,
                (uint8_t*) minStr, OLEDC_COLOR_WHITE);
    }
    drawRectangleOutline(HOUR_REGION_X, HOUR_REGION_Y,
            HOUR_REGION_WIDTH, HOUR_REGION_HEIGHT,
            OLEDC_COLOR_WHITE);

    while (1) {
        processButtons();

        if (newHour != prevHour) {
            char tempStr[3];
            snprintf(tempStr, sizeof(tempStr), "%02d", prevHour);
            oledC_DrawString(HOUR_REGION_X + TEXT_OFFSET,
                    HOUR_REGION_Y + TEXT_OFFSET, 2, 2,
                    (uint8_t*) tempStr, OLEDC_COLOR_BLACK);
            snprintf(tempStr, sizeof(tempStr), "%02d", newHour);
            oledC_DrawString(HOUR_REGION_X + TEXT_OFFSET,
                    HOUR_REGION_Y + TEXT_OFFSET, 2, 2,
                    (uint8_t*) tempStr, OLEDC_COLOR_WHITE);
            prevHour = newHour;
            if (activeField == 0) {
                drawRectangleOutline(HOUR_REGION_X, HOUR_REGION_Y,
                        HOUR_REGION_WIDTH, HOUR_REGION_HEIGHT,
                        OLEDC_COLOR_WHITE);
            }
        }
        if (newMinute != prevMinute) {
            char tempStr[3];
            snprintf(tempStr, sizeof(tempStr), "%02d", prevMinute);
            oledC_DrawString(MIN_REGION_X + TEXT_OFFSET,
                    MIN_REGION_Y + TEXT_OFFSET, 2, 2,
                    (uint8_t*) tempStr, OLEDC_COLOR_BLACK);
            snprintf(tempStr, sizeof(tempStr), "%02d", newMinute);
            oledC_DrawString(MIN_REGION_X + TEXT_OFFSET,
                    MIN_REGION_Y + TEXT_OFFSET, 2, 2,
                    (uint8_t*) tempStr, OLEDC_COLOR_WHITE);
            prevMinute = newMinute;
            if (activeField == 1) {
                drawRectangleOutline(MIN_REGION_X, MIN_REGION_Y,
                        MIN_REGION_WIDTH, MIN_REGION_HEIGHT,
                        OLEDC_COLOR_WHITE);
            }
        }
        if (activeField != prevActiveField) {
            drawRectangleOutline(HOUR_REGION_X, HOUR_REGION_Y,
                    HOUR_REGION_WIDTH, HOUR_REGION_HEIGHT,
                    OLEDC_COLOR_BLACK);
            drawRectangleOutline(MIN_REGION_X, MIN_REGION_Y,
                    MIN_REGION_WIDTH, MIN_REGION_HEIGHT,
                    OLEDC_COLOR_BLACK);
            char hourStr[3], minStr[3];
            snprintf(hourStr, sizeof(hourStr), "%02d", newHour);
            snprintf(minStr, sizeof(minStr), "%02d", newMinute);
            oledC_DrawString(HOUR_REGION_X + TEXT_OFFSET,
                    HOUR_REGION_Y + TEXT_OFFSET, 2, 2,
                    (uint8_t*) hourStr, OLEDC_COLOR_WHITE);
            oledC_DrawString(MIN_REGION_X + TEXT_OFFSET,
                    MIN_REGION_Y + TEXT_OFFSET, 2, 2,
                    (uint8_t*) minStr, OLEDC_COLOR_WHITE);
            if (activeField == 0) {
                drawRectangleOutline(HOUR_REGION_X, HOUR_REGION_Y,
                        HOUR_REGION_WIDTH, HOUR_REGION_HEIGHT,
                        OLEDC_COLOR_WHITE);
            } else {
                drawRectangleOutline(MIN_REGION_X, MIN_REGION_Y,
                        MIN_REGION_WIDTH, MIN_REGION_HEIGHT,
                        OLEDC_COLOR_WHITE);
            }
            prevActiveField = activeField;
        }

        bool s1Down = isButtonPressed(&PORTA, 11);
        bool s2Down = isButtonPressed(&PORTA, 12);

        // Both pressed => exit or switch field
        if (s1Down && s2Down) {
            if (bothPressStart == 0) {
                bothPressStart = getMillis();
            } else if (getMillis() - bothPressStart >= 2000) {
                break;
            }
        } else {
            if (bothPressStart != 0) {
                if (getMillis() - bothPressStart < 2000) {
                    activeField = (activeField + 1) % 2;
                }
                bothPressStart = 0;
            }
        }
        // S1 only => increment
        if (s1Down && !s2Down) {
            if (activeField == 0) {
                if (ampmStr[0] != '\0') { // 12H
                    newHour = (newHour == 12) ? 1 : newHour + 1;
                } else { // 24H
                    newHour = (newHour + 1) % 24;
                }
            } else {
                newMinute = (newMinute + 1) % 60;
            }
            DELAY_milliseconds(200);
        }
        // S2 only => decrement
        if (s2Down && !s1Down) {
            if (activeField == 0) {
                if (ampmStr[0] != '\0') { // 12H
                    newHour = (newHour == 1) ? 12 : newHour - 1;
                } else {
                    newHour = (newHour == 0) ? 23 : newHour - 1;
                }
            } else {
                newMinute = (newMinute == 0) ? 59 : newMinute - 1;
            }
            DELAY_milliseconds(200);
        }
        // Flip device => exit
        if (isDeviceFlipped()) {
            if (flipStart == 0)
                flipStart = getMillis();
            else if (getMillis() - flipStart >= 2000)
                break;
        } else {
            flipStart = 0;
        }
    }

    hours = newHour;
    minutes = newMinute;
    seconds = 0;

    // Correct AM/PM if 12H
    if (ampmStr[0] != '\0') {
        if (hours == 12) {
            strcpy(ampmStr, "PM");
        } else if (hours < 12) {
            strcpy(ampmStr, "AM");
        } else {
            strcpy(ampmStr, "PM");
            hours -= 12;
        }
    } else {
        ampmStr[0] = '\0';
    }

    oledC_DrawRectangle(0, 0, 95, 95, OLEDC_COLOR_BLACK);
    menuDisplay.oldHours   = 99;
    menuDisplay.oldMinutes = 99;
    menuDisplay.oldSeconds = 99;
    strcpy(menuDisplay.oldAmPm, "XX");
    drawMenuStatic();
}

/*------------------------------------------------------------------------------
 * setFormatConfig
 *----------------------------------------------------------------------------*/
static void setFormatConfig(void) {
    bool currentFormat = (ampmStr[0] != '\0'); // true => 12H, false => 24H
    bool prevFormat = currentFormat;

    oledC_DrawRectangle(0, 0, 95, 95, OLEDC_COLOR_BLACK);
    oledC_DrawString(5, 5, 2, 2, (uint8_t*) "12H/24H", OLEDC_COLOR_WHITE);
    oledC_DrawString(OPT12H_X + TEXT_OFFSET, OPT12H_Y + TEXT_OFFSET, 2, 2,
            (uint8_t*) "12H", OLEDC_COLOR_WHITE);
    oledC_DrawString(OPT24H_X + TEXT_OFFSET, OPT24H_Y + TEXT_OFFSET, 2, 2,
            (uint8_t*) "24H", OLEDC_COLOR_WHITE);

    drawRectangleOutline(currentFormat ? OPT12H_X : OPT24H_X,
                         currentFormat ? OPT12H_Y : OPT24H_Y,
                         currentFormat ? OPT12H_WIDTH : OPT24H_WIDTH,
                         currentFormat ? OPT12H_HEIGHT: OPT24H_HEIGHT,
                         OLEDC_COLOR_WHITE);

    while (1) {
        processButtons();
        if (currentFormat != prevFormat) {
            drawRectangleOutline(prevFormat ? OPT12H_X : OPT24H_X,
                                 prevFormat ? OPT12H_Y : OPT24H_Y,
                                 prevFormat ? OPT12H_WIDTH : OPT24H_WIDTH,
                                 prevFormat ? OPT12H_HEIGHT: OPT24H_HEIGHT,
                                 OLEDC_COLOR_BLACK);
            drawRectangleOutline(currentFormat ? OPT12H_X : OPT24H_X,
                                 currentFormat ? OPT12H_Y : OPT24H_Y,
                                 currentFormat ? OPT12H_WIDTH : OPT24H_WIDTH,
                                 currentFormat ? OPT12H_HEIGHT: OPT24H_HEIGHT,
                                 OLEDC_COLOR_WHITE);
            prevFormat = currentFormat;
        }
        bool s1Down = isButtonPressed(&PORTA, 11);
        bool s2Down = isButtonPressed(&PORTA, 12);
        if (s2Down && !s1Down) {
            currentFormat = !currentFormat;
            DELAY_milliseconds(200);
        }
        if (s1Down && !s2Down) {
            if (currentFormat) {
                // switching to 12H
                if (hours == 0) {
                    hours = 12; 
                    strcpy(ampmStr, "AM");
                } else if (hours < 12) {
                    strcpy(ampmStr, "AM");
                } else if (hours == 12) {
                    strcpy(ampmStr, "PM");
                } else {
                    hours -= 12;
                    strcpy(ampmStr, "PM");
                }
            } else {
                // switching to 24H
                if (strcmp(ampmStr, "PM") == 0 && hours != 12) {
                    hours += 12;
                } else if (strcmp(ampmStr, "AM") == 0 && hours == 12) {
                    hours = 0;
                }
                ampmStr[0] = '\0';
            }
            oledC_DrawRectangle(0, 0, 95, 95, OLEDC_COLOR_BLACK);
            drawMenuStatic();
            menuDisplay.oldHours = 99;
            menuDisplay.oldMinutes = 99;
            menuDisplay.oldSeconds = 99;
            strcpy(menuDisplay.oldAmPm, "XX");
            drawClockPartial(&menuDisplay, hours, minutes, seconds, ampmStr, day, month);
            break;
        }
        DELAY_milliseconds(100);
    }
}

/*------------------------------------------------------------------------------
 * setDateConfig
 *----------------------------------------------------------------------------*/
static void setDateConfig(void) {
    uint8_t newDay = day;
    uint8_t newMonth = month;
    uint8_t activeField = 0;
    uint8_t prevActiveField = activeField;
    uint32_t bothPressStart = 0;
    uint32_t flipStart = 0;

    oledC_DrawRectangle(0, 0, 95, 95, OLEDC_COLOR_BLACK);
    oledC_DrawString(5, 5, 2, 2, (uint8_t*) "Set Date", OLEDC_COLOR_WHITE);

    uint8_t prevDay = newDay;
    uint8_t prevMonth = newMonth;

    {
        char dayStr[3], monthStr[3];
        snprintf(dayStr, sizeof(dayStr), "%02d", newDay);
        snprintf(monthStr, sizeof(monthStr), "%02d", newMonth);
        oledC_DrawString(DAY_REGION_X + TEXT_OFFSET,
                DAY_REGION_Y + TEXT_OFFSET, 2, 2,
                (uint8_t*) dayStr, OLEDC_COLOR_WHITE);
        oledC_DrawString(MONTH_REGION_X + TEXT_OFFSET,
                MONTH_REGION_Y + TEXT_OFFSET, 2, 2,
                (uint8_t*) monthStr, OLEDC_COLOR_WHITE);
    }
    drawRectangleOutline(DAY_REGION_X, DAY_REGION_Y,
            DAY_REGION_WIDTH, DAY_REGION_HEIGHT,
            OLEDC_COLOR_WHITE);

    while (1) {
        processButtons();

        if (newDay != prevDay) {
            char tempStr[3];
            snprintf(tempStr, sizeof(tempStr), "%02d", prevDay);
            oledC_DrawString(DAY_REGION_X + TEXT_OFFSET,
                    DAY_REGION_Y + TEXT_OFFSET, 2, 2,
                    (uint8_t*) tempStr, OLEDC_COLOR_BLACK);
            snprintf(tempStr, sizeof(tempStr), "%02d", newDay);
            oledC_DrawString(DAY_REGION_X + TEXT_OFFSET,
                    DAY_REGION_Y + TEXT_OFFSET, 2, 2,
                    (uint8_t*) tempStr, OLEDC_COLOR_WHITE);
            prevDay = newDay;
            if (activeField == 0)
                drawRectangleOutline(DAY_REGION_X, DAY_REGION_Y,
                                     DAY_REGION_WIDTH, DAY_REGION_HEIGHT,
                                     OLEDC_COLOR_WHITE);
        }
        if (newMonth != prevMonth) {
            char tempStr[3];
            snprintf(tempStr, sizeof(tempStr), "%02d", prevMonth);
            oledC_DrawString(MONTH_REGION_X + TEXT_OFFSET,
                    MONTH_REGION_Y + TEXT_OFFSET, 2, 2,
                    (uint8_t*) tempStr, OLEDC_COLOR_BLACK);
            snprintf(tempStr, sizeof(tempStr), "%02d", newMonth);
            oledC_DrawString(MONTH_REGION_X + TEXT_OFFSET,
                    MONTH_REGION_Y + TEXT_OFFSET, 2, 2,
                    (uint8_t*) tempStr, OLEDC_COLOR_WHITE);
            prevMonth = newMonth;
            if (activeField == 1)
                drawRectangleOutline(MONTH_REGION_X, MONTH_REGION_Y,
                                     MONTH_REGION_WIDTH, MONTH_REGION_HEIGHT,
                                     OLEDC_COLOR_WHITE);
        }
        if (activeField != prevActiveField) {
            drawRectangleOutline(DAY_REGION_X, DAY_REGION_Y,
                    DAY_REGION_WIDTH, DAY_REGION_HEIGHT,
                    OLEDC_COLOR_BLACK);
            drawRectangleOutline(MONTH_REGION_X, MONTH_REGION_Y,
                    MONTH_REGION_WIDTH, MONTH_REGION_HEIGHT,
                    OLEDC_COLOR_BLACK);
            char dayStr[3], monthStr[3];
            snprintf(dayStr, sizeof(dayStr), "%02d", newDay);
            snprintf(monthStr, sizeof(monthStr), "%02d", newMonth);
            oledC_DrawString(DAY_REGION_X + TEXT_OFFSET,
                    DAY_REGION_Y + TEXT_OFFSET, 2, 2,
                    (uint8_t*) dayStr, OLEDC_COLOR_WHITE);
            oledC_DrawString(MONTH_REGION_X + TEXT_OFFSET,
                    MONTH_REGION_Y + TEXT_OFFSET, 2, 2,
                    (uint8_t*) monthStr, OLEDC_COLOR_WHITE);
            if (activeField == 0)
                drawRectangleOutline(DAY_REGION_X, DAY_REGION_Y,
                                     DAY_REGION_WIDTH, DAY_REGION_HEIGHT,
                                     OLEDC_COLOR_WHITE);
            else
                drawRectangleOutline(MONTH_REGION_X, MONTH_REGION_Y,
                                     MONTH_REGION_WIDTH, MONTH_REGION_HEIGHT,
                                     OLEDC_COLOR_WHITE);
            prevActiveField = activeField;
        }
        bool s1Down = isButtonPressed(&PORTA, 11);
        bool s2Down = isButtonPressed(&PORTA, 12);
        if (s1Down && s2Down) {
            if (bothPressStart == 0)
                bothPressStart = getMillis();
            else if (getMillis() - bothPressStart >= 2000)
                break;
        } else {
            if (bothPressStart != 0) {
                if (getMillis() - bothPressStart < 2000)
                    activeField = (activeField + 1) % 2;
                bothPressStart = 0;
            }
        }
        if (s1Down && !s2Down) {
            if (activeField == 0) {
                uint8_t maxDays = getDaysInMonth(newMonth);
                newDay = (newDay == maxDays) ? 1 : newDay + 1;
            } else {
                newMonth = (newMonth == 12) ? 1 : newMonth + 1;
                uint8_t maxDays = getDaysInMonth(newMonth);
                if (newDay > maxDays)
                    newDay = maxDays;
            }
            DELAY_milliseconds(200);
        }
        if (s2Down && !s1Down) {
            if (activeField == 0) {
                uint8_t maxDays = getDaysInMonth(newMonth);
                newDay = (newDay == 1) ? maxDays : newDay - 1;
            } else {
                newMonth = (newMonth == 1) ? 12 : newMonth - 1;
                uint8_t maxDays = getDaysInMonth(newMonth);
                if (newDay > maxDays)
                    newDay = maxDays;
            }
            DELAY_milliseconds(200);
        }
        if (isDeviceFlipped()) {
            if (flipStart == 0)
                flipStart = getMillis();
            else if (getMillis() - flipStart >= 2000)
                break;
        } else {
            flipStart = 0;
        }
    }
    day = newDay;
    month = newMonth;
    oledC_DrawRectangle(0, 0, 95, 95, OLEDC_COLOR_BLACK);
    drawMenuStatic();
    menuDisplay.oldHours   = 99;
    menuDisplay.oldMinutes = 99;
    menuDisplay.oldSeconds = 99;
    strcpy(menuDisplay.oldAmPm, "XX");
    drawClockPartial(&menuDisplay, hours, minutes, seconds, ampmStr, day, month);
}

/*------------------------------------------------------------------------------
 * selectMenuOption
 *----------------------------------------------------------------------------*/
static void selectMenuOption(void) {
    switch (selectedMenu) {
        case MENU_PEDOMETER:
            displayPedometerGraph();
            break;
        case MENU_FORMAT_12H_24H:
            setFormatConfig();
            break;
        case MENU_SET_TIME:
            setTimeConfig();
            break;
        case MENU_SET_DATE:
            setDateConfig();
            break;
        case MENU_EXIT:
            currentState = STATE_TIME_DISPLAY;
            drawInitialDisplay();
            break;
        default:
            break;
    }
}

/*------------------------------------------------------------------------------
 * drawMenu
 *----------------------------------------------------------------------------*/
static void drawMenu(void) {
 int marginLeft = 1;
    
    // Clear screen and draw the menu items
    oledC_DrawRectangle(0, 0, 95, 95, OLEDC_COLOR_BLACK);
    drawMenuColons();
    for (int i = 0; i < MENU_COUNT; i++) {
        oledC_DrawString(marginLeft, 15 + i * 15, 1, 1,
                         (uint8_t*) menuItems[i], OLEDC_COLOR_WHITE);
    }
    oledC_DrawRectangle(0, 15 + selectedMenu * 15,
                        95, 15 + (selectedMenu + 1) * 15,
                        OLEDC_COLOR_WHITE);
    oledC_DrawString(marginLeft, 15 + selectedMenu * 15, 1, 1,
                     (uint8_t*) menuItems[selectedMenu], OLEDC_COLOR_BLACK);

    // Update time once right away
    updateTime();

    // (1) Clear the clock area so it will redraw from scratch
    oledC_DrawRectangle(menuDisplay.hourX, menuDisplay.hourY, 
                        50, 16, OLEDC_COLOR_BLACK);

    // (2) Force old values to something invalid
    menuDisplay.oldHours   = 99;
    menuDisplay.oldMinutes = 99;
    menuDisplay.oldSeconds = 99;
    strcpy(menuDisplay.oldAmPm, "XX");

    // (3) Now immediately draw the clock so it appears right away
    drawClockPartial(&menuDisplay, hours, minutes, seconds, ampmStr, day, month);

    uint32_t lastUpdateTime = getMillis();
    MenuOption prevMenuSelection = MENU_COUNT;

    // Main loop for the menu
    while (currentState == STATE_MENU) 
    {
        processButtons();
        uint32_t currentTime = getMillis();

        // Update the time display once a second
        if (currentTime - lastUpdateTime >= 1000) {
            lastUpdateTime = currentTime;
            updateTime();
            drawClockPartial(&menuDisplay, hours, minutes, seconds, ampmStr, day, month);
        }

        // Highlight the selected menu item if changed
        if (selectedMenu != prevMenuSelection) {
            // First, re-draw the previously selected item in white text on black
            if (prevMenuSelection < MENU_COUNT) {
                oledC_DrawRectangle(0, 15 + prevMenuSelection * 15,
                                    95, 15 + (prevMenuSelection + 1) * 15,
                                    OLEDC_COLOR_BLACK);
                oledC_DrawString(marginLeft, 15 + prevMenuSelection * 15, 1, 1,
                                 (uint8_t*) menuItems[prevMenuSelection], OLEDC_COLOR_WHITE);
            }
            // Now highlight the new selection
            oledC_DrawRectangle(0, 15 + selectedMenu * 15,
                                95, 15 + (selectedMenu + 1) * 15,
                                OLEDC_COLOR_WHITE);
            oledC_DrawString(marginLeft, 15 + selectedMenu * 15, 1, 1,
                             (uint8_t*) menuItems[selectedMenu], OLEDC_COLOR_BLACK);

            prevMenuSelection = selectedMenu;
        }
bool s1Down = isButtonPressed(&PORTA, 11);  // true if S1 is pressed
bool s2Down = isButtonPressed(&PORTA, 12);  // true if S2 is pressed

// First, handle the case both are already pressed at once
if (s1Down && s2Down)
{
    // Both pressed => select current menu item
    selectMenuOption();
    DELAY_milliseconds(150); // small delay to avoid rapid re-trigger
}
else if (s1Down && !s2Down)
{
    // S1 is down alone so far. Let's wait a tiny bit and check again.
    DELAY_milliseconds(50); // wait 50ms
    bool s2Recheck = isButtonPressed(&PORTA, 12);

    if (s2Recheck) 
    {
        // Now S2 is also pressed => treat as "both pressed"
        selectMenuOption();
    }
    else
    {
        // It's still just S1 => move up
        navigateMenuUp();
    }
}
else if (!s1Down && s2Down)
{
    // S2 is down alone so far. Wait and check again.
    DELAY_milliseconds(50);
    bool s1Recheck = isButtonPressed(&PORTA, 11);

    if (s1Recheck)
    {
        // Both pressed after recheck => select
        selectMenuOption();
    }
    else
    {
        // Still just S2 => move down
        navigateMenuDown();
    }
}
else
{
    // No buttons pressed; do nothing (or handle other logic)
}


    }
}


/*******************************************************************************
 * NEW FUNCTIONS FOR PEDOMETER GRAPH
 ******************************************************************************/

/*------------------------------------------------------------------------------
 * updateStepsHistory: store currentPace into stepsHistory[] in circular manner
 *----------------------------------------------------------------------------*/
static void updateStepsHistory(void)
{
    stepsHistory[stepsHistoryIndex] = (int)currentPace;
    stepsHistoryIndex++;
    if (stepsHistoryIndex >= 90) {
        stepsHistoryIndex = 0;
    }
}


/*------------------------------------------------------------------------------
 * computeSmoothedSteps: simple neighbor average
 *----------------------------------------------------------------------------*/
static void computeSmoothedSteps(void)
{
    for (int i = 0; i < 90; i++)
    {
        int prev = (i == 0) ? 89 : i - 1;
        int next = (i == 89) ? 0 : i + 1;
        int sum = stepsHistory[i] + stepsHistory[prev] + stepsHistory[next];
        smoothedSteps[i] = sum / 3;
    }
}
static void drawLineSmooth(int x1, int y1, int x2, int y2, uint16_t color)
{
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);

    // Decide how many small steps to take. 
    // Using the larger of |dx| or |dy| ensures we don't skip big vertical/horizontal leaps.
    float steps = (fabsf(dx) > fabsf(dy)) ? fabsf(dx) : fabsf(dy);
    if (steps < 1.0f) steps = 1.0f;  // avoid division by 0 if points are the same

    // Step through from 0..1 in small increments
    for (float i = 0.0f; i <= steps; i += 0.5f)
    {
        float t = i / steps;  // fraction from 0..1
        int px = (int)(x1 + dx * t);
        int py = (int)(y1 + dy * t);

        oledC_DrawPoint(px, py, color);
    }
}


/*------------------------------------------------------------------------------
 * drawDashedLine
 *----------------------------------------------------------------------------*/
static void drawDashedLine(int x1, int y1, int x2, int y2, 
                           int dashLen, int gapLen, uint16_t color)
{
    int dx = x2 - x1;
    int dy = y2 - y1;
    float length = sqrtf((float)(dx*dx + dy*dy));
    if (length <= 0) return;

    float stepX = dx / length;
    float stepY = dy / length;
    float pos = 0.0f;
    float totalCycle = dashLen + gapLen;

    while (pos < length) {
        float endPos = pos + dashLen;
        if (endPos > length) endPos = length;
        // Start
        int sx = x1 + (int)(stepX * pos);
        int sy = y1 + (int)(stepY * pos);
        // End
        int ex = x1 + (int)(stepX * endPos);
        int ey = y1 + (int)(stepY * endPos);

        oledC_DrawLine(sx, sy, ex, ey, 1, color);
        pos += totalCycle;
    }
}


/*------------------------------------------------------------------------------
 * drawGraphGrid
 *----------------------------------------------------------------------------*/
static void drawGraphGrid(void)
{
    // Clear the screen
    oledC_DrawRectangle(0, 0, 95, 95, OLEDC_COLOR_BLACK);

    // Draw Y-axis labels and dashed horizontal lines
    char label[4];
    // Draw "100" at top (y = 0)
    snprintf(label, sizeof(label), "100");
    oledC_DrawString(0, 0, 1, 1, (uint8_t*)label, OLEDC_COLOR_WHITE);
    drawDashedLine(20, 5, 95, 5, 3, 2, OLEDC_COLOR_WHITE);

    // Draw "60" label (y = 30)
    snprintf(label, sizeof(label), "60");
    oledC_DrawString(0, 30, 1, 1, (uint8_t*)label, OLEDC_COLOR_WHITE);
    drawDashedLine(20, 35, 95, 35, 3, 2, OLEDC_COLOR_WHITE);

    // Draw "30" label (y = 60)
    snprintf(label, sizeof(label), "30");
    oledC_DrawString(0, 60, 1, 1, (uint8_t*)label, OLEDC_COLOR_WHITE);
    drawDashedLine(20, 65, 95, 65, 3, 2, OLEDC_COLOR_WHITE);

    // Draw the bottom dashed line (x-axis) at y = 95
    drawDashedLine(20, 95, 95, 95, 3, 2, OLEDC_COLOR_WHITE);

    // Now add x-axis markers (small 2x2 squares) along the bottom line.
    int charWidth = 6;
    int margin = 2;
    int fixedLeftMargin = (3 * charWidth) + margin;  // Same left margin used for labels
    int x2_end = 95;  // End of the screen (width - 1)
    int yBaseline = 95;  // y-coordinate for the bottom axis
    int squareSpacing = 10;  // Adjust spacing as needed

    for (int xSquare = fixedLeftMargin; xSquare <= x2_end; xSquare += squareSpacing) {
        // Draw a 2x2 pixel square at (xSquare, yBaseline - 2)
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                oledC_DrawPoint(xSquare + i, yBaseline - 2 + j, OLEDC_COLOR_WHITE);
            }
        }
    }
}

/*------------------------------------------------------------------------------
 * drawStepsGraph
 *----------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------
 * drawStepsGraph
 *----------------------------------------------------------------------------*/
static void drawStepsGraph(void)
{
    // 1) ????? ??????? ?????? ?????? ????? ?????
    int orderedSteps[GRAPH_SAMPLES];
    for (int i = 0; i < GRAPH_SAMPLES; i++)
    {
        int idx = (stepsHistoryIndex + i) % GRAPH_SAMPLES;
        orderedSteps[i] = stepsHistory[idx];
    }

    // 2) ????? ????? ?????
    int orderedSmoothed[GRAPH_SAMPLES];
    for (int i = 0; i < GRAPH_SAMPLES; i++)
    {
        int prev = (i == 0) ? GRAPH_SAMPLES - 1 : i - 1;
        int next = (i == GRAPH_SAMPLES - 1) ? 0 : i + 1;
        orderedSmoothed[i] = (orderedSteps[i] + orderedSteps[prev] + orderedSteps[next]) / 3;
    }

    // 3) ???? ????
    int xStart = 20;      // ?????? ???????
    int baseline = 95;    // ?? ????? ????
    int maxVal = 100;     // ??? ???????

    // ?????? ?? ?????? ??????? (i=0) ???? ????? ???? ??
    int xPrev = xStart;
    int yPrev = baseline - (orderedSmoothed[0] * baseline / maxVal);

    // ??????? ??? ???? ??????? ????
    if (yPrev < 0)       yPrev = 0;
    if (yPrev > baseline) yPrev = baseline;

    // ??? ??????? ????? ?? ??????? ?????? (i=1)
    for (int i = 1; i < GRAPH_SAMPLES; i++)
    {
        int xCur = xStart + i;
        int yCur = baseline - (orderedSmoothed[i] * baseline / maxVal);

        if (yCur < 0)         yCur = 0;
        if (yCur > baseline)  yCur = baseline;

        // ??????? ?? ?????? i-1 ?? ????? i
        drawLineSmooth(xPrev, yPrev, xCur, yCur, OLEDC_COLOR_WHITE);

        xPrev = xCur;
        yPrev = yCur;
    }
}

/*------------------------------------------------------------------------------
 * displayPedometerGraph
 *----------------------------------------------------------------------------*/
static void displayPedometerGraph(void)
{
    computeSmoothedSteps();
    drawGraphGrid();
    drawStepsGraph();

    // Turn these LEDs off as soon as we enter pedometer.
    LATAbits.LATA8 = 0;
    LATAbits.LATA9 = 0;

    uint32_t s1DownStart = 0;

    while (1)
    {
        bool s1 = isButtonPressed(&PORTA, 11);

        // NEW: Turn RA8 ON if s1 is pressed
        LATAbits.LATA8 = s1 ? 1 : 0;

        // If S1 is held for 2 seconds, exit pedometer
        if (s1)
        {
            if (s1DownStart == 0)
            {
                s1DownStart = getMillis();
            }
            else if ((getMillis() - s1DownStart) >= 2000)
            {
                break; // leave pedometer
            }
        }
        else
        {
            s1DownStart = 0;
        }

        DELAY_milliseconds(50);
    }

    // Clear screen, return to watch display
    oledC_DrawRectangle(0, 0, 95, 95, OLEDC_COLOR_BLACK);
    currentState = STATE_TIME_DISPLAY;
    drawInitialDisplay();
}