#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_seesaw.h>
#include <seesaw_neopixel.h>
#include <TFT_eSPI.h>
#include "bench_motor.h"
#include "console_drawing.h"

namespace {
constexpr uint8_t EncoderAddress = 0x36;
constexpr uint8_t SwitchPin = 24;
constexpr uint8_t PixelPin = 6;
constexpr uint32_t FlashMs = 100;
constexpr uint32_t PollMs = 10;
constexpr uint32_t DebounceMs = 30;
constexpr int MinRpm = 1;
constexpr int MaxRpm = 225;

class BenchEncoder : public Adafruit_seesaw {
public:
    bool sample(uint32_t& position, bool& pressed) {
        uint8_t count[4];
        uint8_t levels[4];
        if (!read(SEESAW_ENCODER_BASE, SEESAW_ENCODER_POSITION, count, sizeof(count)) ||
            !read(SEESAW_GPIO_BASE, SEESAW_GPIO_BULK, levels, sizeof(levels))) return false;

        position = (uint32_t(count[0]) << 24) | (uint32_t(count[1]) << 16) |
                   (uint32_t(count[2]) << 8) | count[3];
        pressed = (levels[0] & (1U << (SwitchPin - 24))) == 0;
        return true;
    }
};

BenchEncoder encoder;
TFT_eSPI display;
TFT_eSprite canvas(&display);
bool canvasReady = false;
seesaw_NeoPixel encoderPixel(1, PixelPin, NEO_GRB + NEO_KHZ800);
bool pixelReady = false;
bool knobFlash = false;
uint32_t lastKnobTurn = 0;
bool motorReady = false;
bool encoderReady = false;
bool haveSample = false;
bool stablePressed = true;
bool candidatePressed = true;
bool eligiblePress = false;
uint32_t candidateSince = 0;
uint32_t lastPoll = 0;
uint32_t lastPosition = 0;
int selectedRpm = 8;

void pollEncoder(uint32_t now) {
    if (!encoderReady || now - lastPoll < PollMs) return;
    lastPoll = now;

    const bool runningAtSampleStart = BenchMotor::motion().running;
    uint32_t position;
    bool pressed;
    if (!encoder.sample(position, pressed)) {
        haveSample = false;
        return;
    }

    if (!haveSample) {
        lastPosition = position;
        stablePressed = candidatePressed = true;
        eligiblePress = false;
        candidateSince = now;
        haveSample = true;
    }

    const int32_t delta = static_cast<int32_t>(position - lastPosition);
    lastPosition = position;
    if (delta != 0 && delta >= -4 && delta <= 4) {
        lastKnobTurn = now;
        knobFlash = true;
        selectedRpm = constrain(selectedRpm + delta, MinRpm, MaxRpm);
    }

    if (pressed != candidatePressed) {
        candidatePressed = pressed;
        candidateSince = now;
        eligiblePress = pressed && !runningAtSampleStart;
    }
    if (pressed == stablePressed || now - candidateSince < DebounceMs) return;

    stablePressed = pressed;
    if (pressed && eligiblePress && motorReady && !BenchMotor::start(selectedRpm)) {
        Serial.println("Motor start failed");
    }
}

void refreshPixel(uint32_t now) {
    if (!pixelReady) return;
    if (knobFlash && now - lastKnobTurn >= FlashMs) knobFlash = false;

    const bool running = BenchMotor::motion().running;
    const uint8_t red = running ? (knobFlash ? 120 : 32) : 0;
    const uint8_t blue = !running && knobFlash ? 80 : 0;
    const uint32_t color = encoderPixel.Color(red, 0, blue);

    static uint32_t previousColor = 0;
    if (color == previousColor) return;
    encoderPixel.setPixelColor(0, color);
    encoderPixel.show();
    previousColor = color;
}

void refreshStatus() {
    const auto motion = BenchMotor::motion();
    const bool inputFault = !haveSample;
    const char* status = motion.running ? "Running" : "Idle";
    if (!motorReady) status = "Motor setup failed";
    else if (motion.pulseFault) status = "STEP count mismatch";
    else if (!motion.running && inputFault) status = "Encoder unavailable";

    const float angle = motion.pulses * 360.0f / BenchMotor::PulsesPerTurn;
    static int loggedRpm = -1;
    static const char* loggedStatus = nullptr;
    if (loggedRpm != selectedRpm || loggedStatus != status) {
        Serial.printf("Next: %d RPM | Angle: %.1f deg | %s\n", selectedRpm, angle, status);
        loggedRpm = selectedRpm;
        loggedStatus = status;
    }

    static uint32_t lastRefresh = 0;
    static int shownTargetRpm = -1;
    static int shownPulses = -1;
    static float shownRpm = -1.0f;
    static const char* shownStatus = nullptr;
    const uint32_t now = millis();
    if (now - lastRefresh < 100) return;
    if (shownTargetRpm == selectedRpm && shownPulses == motion.pulses &&
        shownRpm == motion.rpm && shownStatus == status) return;
    lastRefresh = now;
    shownTargetRpm = selectedRpm;
    shownPulses = motion.pulses;
    shownRpm = motion.rpm;
    shownStatus = status;

    if (!canvasReady) return;
    using namespace ConsoleArt;
    constexpr uint16_t Cyan = 0x067C;
    canvas.fillSprite(TFT_BLACK);
    canvas.setTextColor(Cyan);
    canvas.drawString("Stepper bench", 17, 22, 2);
    const bool ready = motorReady && !inputFault && !motion.pulseFault;
    const bool complete = motion.pulses == BenchMotor::PulsesPerTurn && !motion.running;
    const char* label = !ready ? "FAULT" : (motion.running ? "RUNNING" : (complete ? "COMPLETE" : "READY"));
    canvas.fillCircle(118, 31, 5, !ready ? TFT_RED : (motion.running ? 0x047F : TFT_GREEN));
    ConsoleDrawing::drawText(canvas, Small, SmallPixels, label, 130, 37);

    char value[12];
    snprintf(value, sizeof(value), "%.1f", angle);
    const int numberWidth = ConsoleDrawing::textWidth(Large, value);
    ConsoleDrawing::drawText(canvas, Large, LargePixels, value, (280 - numberWidth) / 2, 133);
    canvas.drawFastHLine(60, 152, 161, Cyan);

    canvas.drawCentreString("NEXT RPM", 49, 174, 2);
    canvas.drawCentreString("DRIVE RPM", 234, 174, 2);
    canvas.setTextColor(TFT_WHITE);
    snprintf(value, sizeof(value), "%d", selectedRpm);
    canvas.drawCentreString(value, 49, 193, 4);
    snprintf(value, sizeof(value), "%.1f", motion.rpm);
    canvas.drawCentreString(value, 234, 193, 4);
    canvas.drawCentreString(motion.running ? "TURNING" : "PRESS TO RUN", 145, 190, 2);
    canvas.setTextColor(Cyan);
    canvas.drawCentreString("ANGLE IN DEGREES", 145, 217, 2);
    canvas.pushSprite((display.width() - 280) / 2, 0);
}
}

void setup() {
    motorReady = BenchMotor::begin();
    Serial.begin(115200);
    display.init();
    display.setRotation(1);
    display.fillScreen(TFT_BLACK);
    canvas.setColorDepth(8);
    canvasReady = canvas.createSprite(280, 240) != nullptr;
    if (!canvasReady) {
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.drawString("Display buffer unavailable", 20, 100, 2);
        motorReady = false;
    }

    Wire.begin(21, 22);
    encoderReady = encoder.begin(EncoderAddress);
    if (encoderReady) {
        encoder.pinMode(SwitchPin, INPUT_PULLUP);
        pixelReady = encoderPixel.begin(EncoderAddress);
        if (pixelReady) encoderPixel.show();
    }

    Serial.printf("PowderReload: motor %s, encoder %s, display %s\n",
                  motorReady ? "ok" : "FAILED", encoderReady ? "ok" : "FAILED",
                  canvasReady ? "ok" : "FAILED");
}

void loop() {
    BenchMotor::update();
    pollEncoder(millis());
    refreshPixel(millis());
    refreshStatus();
    delay(1);
}
