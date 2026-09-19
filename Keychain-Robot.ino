/*
 * Keychain-Robot — Refined Firmware
 * Mood: Always Curious
 * Touch Short Tap: Happy Shake
 * Touch Long Hold (3s): ESP Deep Sleep / Wake Up
 * Hardware: ESP32 / ESP32-C3, SH1106 128x64 OLED
 * I2C: SDA=GPIO8, SCL=GPIO9 | Buzzer=GPIO5 | Touch=GPIO21
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "FluxGarage_RoboEyes.h"

// ── Hardware Pins & Definitions ─────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define I2C_ADDR      0x3C

#define BUZZER_PIN     5
// Set active level (HIGH for standard active buzzers, LOW for active-low modules)
#define BUZZER_ON      HIGH
#define BUZZER_OFF     LOW

#define TOUCH_PIN     21
// Set touch active level: HIGH for TTP223 capacitive touch sensor, LOW for button to GND
#define TOUCH_ACTIVE_LEVEL HIGH
#define TOUCH_THRESH  40

#define HOLD_TIME_MS 3000UL // 3 seconds required to trigger sleep / wake

// ── Hardware Handles ────────────────────────────────────────────────────────
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RoboEyes<Adafruit_SH1106G> roboEyes(display);

// ── Touch State Management ──────────────────────────────────────────────────
bool lastTouchState = false;
unsigned long touchStartTime = 0;
unsigned long happyUntil = 0;

// ── Happy Shaking Sound Effect (Non-blocking Active Buzzer) ────────────────
struct BeepStep {
  bool state;        // true = BUZZER_ON, false = BUZZER_OFF
  uint16_t duration; // Duration in milliseconds
};

// Rhythmic chirping giggle for active buzzer
const BeepStep happyPattern[] = {
  { true,  45 },  // Chirp 1
  { false, 25 },
  { true,  45 },  // Chirp 2
  { false, 25 },
  { true,  60 },  // Chirp 3
  { false, 35 },
  { true,  35 },  // Quick tap
  { false, 25 },
  { true,  110 }, // Longer joyful burst
  { false, 40 },
  { true,  40 },  // Quick tap
  { false, 30 },
  { true,  150 }  // Final celebration beep
};
const uint8_t happyPatternLength = sizeof(happyPattern) / sizeof(happyPattern[0]);

int8_t soundStepIndex = -1;
unsigned long nextBeepTime = 0;

void startHappySound() {
  soundStepIndex = 0;
  nextBeepTime = 0; // Trigger first chirp immediately on next update
}

void stopHappySound() {
  soundStepIndex = -1;
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

void updateBuzzer(unsigned long now) {
  if (soundStepIndex < 0) return;

  if (now >= nextBeepTime) {
    if (soundStepIndex < happyPatternLength) {
      bool active = happyPattern[soundStepIndex].state;
      uint16_t dur = happyPattern[soundStepIndex].duration;

      digitalWrite(BUZZER_PIN, active ? BUZZER_ON : BUZZER_OFF);

      nextBeepTime = now + dur;
      soundStepIndex++;
    } else {
      stopHappySound();
    }
  }
}

// ── Bootloader Helper ───────────────────────────────────────────────────────
void checkAutoBootloader() {
  if (millis() < 500) {
    delay(50);
#if defined(RTC_CNTL_OPTION1_REG) && defined(RTC_CNTL_FORCE_DOWNLOAD_BOOT)
    if (Serial.available() >= 0) {
      Serial.flush();
      REG_SET_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
      esp_restart();
    }
#endif
  }
}

// ── Helper to read touch status cleanly across board targets ───────────────
bool isTouched() {
#if defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C3)
  return (touchRead(TOUCH_PIN) < TOUCH_THRESH);
#else
  return (digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL);
#endif
}

// ── Sleep Handling ──────────────────────────────────────────────────────────
void goToSleep() {
  stopHappySound();

  // Play power-off chime
  digitalWrite(BUZZER_PIN, BUZZER_ON); delay(80);
  digitalWrite(BUZZER_PIN, BUZZER_OFF); delay(60);
  digitalWrite(BUZZER_PIN, BUZZER_ON); delay(150);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);

  // Clear display and close eyes
  roboEyes.close();
  display.clearDisplay();
  display.display();

  // Wait until user releases touch before sleeping
  while (isTouched()) {
    delay(20);
  }
  delay(200); // Debounce release

  // Enable interrupt wake-up on touch pin
  gpio_wakeup_enable((gpio_num_t)TOUCH_PIN, (TOUCH_ACTIVE_LEVEL == HIGH) ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  // Sleep loop: Ensure wake-up ONLY happens after holding touch for 3 seconds
  while (true) {
    esp_light_sleep_start(); // MCU sleeps here until pin goes ACTIVE

    // Woken up by touch; track how long the touch is maintained
    unsigned long wakeTouchStart = millis();
    bool validWake = false;

    while (isTouched()) {
      if (millis() - wakeTouchStart >= HOLD_TIME_MS) {
        validWake = true;
        break; // Held for 3 seconds, valid wake-up!
      }
      delay(20);
    }

    if (validWake) {
      // Play wake confirmation beep before restart
      digitalWrite(BUZZER_PIN, BUZZER_ON); delay(50);
      digitalWrite(BUZZER_PIN, BUZZER_OFF); delay(40);
      digitalWrite(BUZZER_PIN, BUZZER_ON); delay(100);
      digitalWrite(BUZZER_PIN, BUZZER_OFF);

      // Wait for release before booting to avoid immediate re-trigger
      while (isTouched()) {
        delay(20);
      }
      delay(100);
      esp_restart(); // Restart cleanly into running state
    }

    // If released before 3 seconds elapsed, loop continues and MCU goes back to sleep
  }
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(30);
  checkAutoBootloader();

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);

#if (TOUCH_ACTIVE_LEVEL == LOW)
  pinMode(TOUCH_PIN, INPUT_PULLUP);
#else
  pinMode(TOUCH_PIN, INPUT);
#endif

  // I2C fast mode setup
  Wire.begin(8, 9);
  Wire.setClock(400000);

  // OLED display setup
  display.begin(I2C_ADDR, true);
  display.setContrast(255);
  display.clearDisplay();
  display.display();

  // Boot chime / Turn-On signal
  digitalWrite(BUZZER_PIN, BUZZER_ON); delay(60);
  digitalWrite(BUZZER_PIN, BUZZER_OFF); delay(50);
  digitalWrite(BUZZER_PIN, BUZZER_ON); delay(80);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);

  // Initialize eyes to always curious
  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 40);
  roboEyes.open();
  roboEyes.setCuriosity(ON);
  roboEyes.setMood(DEFAULT);
  roboEyes.setAutoblinker(ON, 3, 1);
  roboEyes.setIdleMode(ON, 3, 2);
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  bool touched = isTouched();

  // Detect press start
  if (touched && !lastTouchState) {
    touchStartTime = now;
  }

  // Detect continuous hold for 3 seconds while ON -> Go to sleep / Turn off
  if (touched && (now - touchStartTime >= HOLD_TIME_MS)) {
    goToSleep();
  }

  // Detect single tap release (holding for less than 3 seconds) -> Trigger Happy action
  if (!touched && lastTouchState) {
    if (now - touchStartTime < HOLD_TIME_MS) {
      happyUntil = now + 800;
      roboEyes.setMood(HAPPY);
      roboEyes.setHFlicker(ON, 2);
      startHappySound();
    }
  }

  lastTouchState = touched;

  // Return eyes back to curious default after happy shake ends
  if (happyUntil > 0 && now >= happyUntil) {
    happyUntil = 0;
    roboEyes.setHFlicker(OFF, 0);
    roboEyes.setMood(DEFAULT);
    stopHappySound();
  }

  updateBuzzer(now);
  roboEyes.update();
}