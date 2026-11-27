// PowderReload console UI.
//
// Main screen: press = zero + fill (press again to stop), hold = menu.
// Menu: RECIPE (browse recipes and their details), TARGET, TARE / ZERO, SETUP (SPEED, CALIBRATE, WI-FI), BACK.
// Turn to pick, press to use, hold to go back.
// Target and speeds belong to the active recipe (also editable from the web UI).
// The scale is simulated from motor progress until the load cell is added.
#include <Arduino.h>
#include <Adafruit_seesaw.h>
#include <seesaw_neopixel.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "bench_motor.h"
#include "console_drawing.h"
#include "panel.h"
#include "storage.h"
#include "web.h"

namespace {
using namespace ConsoleArt;
using ConsoleDrawing::drawText;
using ConsoleDrawing::textWidth;

constexpr uint8_t EncoderAddress = 0x36;
constexpr uint8_t ButtonPin = 24;
constexpr int Width = 280;
constexpr int Height = 240;
constexpr uint32_t HoldMs = 700;

// Fake scale: grains (in tenths) dispensed per motor turn.
constexpr int FakeTenthsPerTurn = 100;

constexpr uint16_t Cyan = 0x067C;
constexpr uint16_t Blue = 0x047F;
constexpr uint16_t Amber = 0xFCA0;
constexpr uint16_t Green = 0x27E0;
constexpr uint16_t Red = 0xF9E7;
constexpr uint16_t Dim = 0x4A8C;
constexpr uint16_t Track = 0x2145;

// ---------------------------------------------------------------- settings
enum class Format { Grains, Rpm };
struct Field {
    const char* key;
    int value;
    int min, max;
    Format format;
};
// Values of the active recipe, copied here for the knob.
Field target{"target", 0, 0, 999, Format::Grains};
Field fastRpm{"fast", 120, 1, 225, Format::Rpm};
Field slowRpm{"slow", 15, 1, 225, Format::Rpm};
Field slowAt{"slowat", 20, 1, 200, Format::Grains};
// Device setting, kept in Preferences.
Field checkWeight{"calweight", 500, 1, 999, Format::Grains};

Preferences preferences;
bool storageReady = false;
uint32_t loadedRevision = 0;

void loadRecipe() {
    const auto& recipe = Recipes::active();
    target.value = recipe.charge;
    fastRpm.value = recipe.fastRpm;
    slowRpm.value = recipe.slowRpm;
    slowAt.value = recipe.slowAt;
    loadedRevision = Recipes::revision();
}

void save(const Field& field) {
    if (&field == &checkWeight) {
        if (storageReady) preferences.putUShort(field.key, field.value);
    } else {
        Recipes::updateActive(target.value, fastRpm.value, slowRpm.value, slowAt.value);
    }
    loadRecipe();
}

// Upper-case copy cut to fit `maxWidth` (the LCD fonts have capitals only).
template <size_t Count>
void fit(char* buffer, size_t size, const char* text, const Glyph (&font)[Count], int maxWidth) {
    size_t length = 0;
    for (; text[length] && length + 1 < size; ++length) buffer[length] = toupper(text[length]);
    buffer[length] = 0;
    while (length && textWidth(font, buffer) > maxWidth) buffer[--length] = 0;
}

void format(char* buffer, size_t size, const Field& field, int value) {
    if (field.format == Format::Rpm) snprintf(buffer, size, "%d RPM", value);
    else snprintf(buffer, size, "%d.%d gr", value / 10, value % 10);
}

void grains(char* buffer, size_t size, int tenths) {
    snprintf(buffer, size, "%s%d.%d", tenths < 0 ? "-" : "", abs(tenths) / 10, abs(tenths) % 10);
}

// ---------------------------------------------------------------- state
enum class Screen { Main, Menu, Recipe, Setup, Speed, Calibrate, Wifi };
enum class Status { Ready, Filling, Stopping, OnTarget, Stopped, Zeroed, NoTarget, MotorFault };

TFT_eSPI lcd;
TFT_eSprite canvas(&lcd);
bool encoderReady = false;
bool canvasReady = false;
bool motorReady = false;
bool dirty = true;

Screen screen = Screen::Main;
Status status = Status::Ready;
int focus = 0;               // highlighted row in a list screen
Field* editing = nullptr;    // field being changed with the knob
int draft = 0;
int browseIndex = 0;         // recipe shown on the recipe screen
int calibrateStep = 0;       // 0 empty, 1 set weight, 2 place weight, 3 done
int measuredTenths = 0;      // simulated scale reading
bool filling = false;
bool forgetArmed = false;    // Wi-Fi screen: first press arms, second forgets
int fillSlowFrom = 0;        // pulse where the slow phase starts
uint32_t lastTurnAt = 0;     // for the knob LED flash

void show(Screen next, int row = 0) {
    screen = next;
    focus = row;
    editing = nullptr;
    forgetArmed = false;
    dirty = true;
}

// ---------------------------------------------------------------- list screens
struct Item {
    const char* label;
    Field* field;
    void (*action)();
};

void tare() {
    measuredTenths = 0;
    status = Status::Zeroed;
    show(Screen::Main);
}
void openRecipe() {
    browseIndex = Recipes::activeIndex();
    show(Screen::Recipe);
}
void openSetup() { show(Screen::Setup); }
void openSpeed() { show(Screen::Speed); }
void openCalibrate() {
    calibrateStep = 0;
    show(Screen::Calibrate);
}
void openWifi() { show(Screen::Wifi); }
void backToMain() { show(Screen::Main); }
void backToMenu() { show(Screen::Menu, 3); }
void backToSetup() { show(Screen::Setup, screen == Screen::Calibrate ? 1 : (screen == Screen::Wifi ? 2 : 0)); }

const Item MenuItems[] = {
    {"RECIPE", nullptr, openRecipe},
    {"TARGET", &target, nullptr},
    {"TARE / ZERO", nullptr, tare},
    {"SETUP", nullptr, openSetup},
    {"BACK", nullptr, backToMain},
};
const Item SetupItems[] = {
    {"SPEED", nullptr, openSpeed},
    {"CALIBRATE", nullptr, openCalibrate},
    {"WI-FI", nullptr, openWifi},
    {"BACK", nullptr, backToMenu},
};
const Item SpeedItems[] = {
    {"FAST", &fastRpm, nullptr},
    {"SLOW", &slowRpm, nullptr},
    {"SLOW AT", &slowAt, nullptr},
    {"BACK", nullptr, backToSetup},
};

template <size_t Count>
const Item* list(const Item (&array)[Count], int& count) {
    count = Count;
    return array;
}

const Item* items(int& count) {
    if (screen == Screen::Speed) return list(SpeedItems, count);
    if (screen == Screen::Setup) return list(SetupItems, count);
    return list(MenuItems, count);
}

bool opensScreen(const Item& item) {
    return item.action == openRecipe || item.action == openSetup || item.action == openSpeed || item.action == openCalibrate ||
           item.action == openWifi;
}

// ---------------------------------------------------------------- filling (simulated scale)
bool startFill() {
    if (filling) return false;
    if (target.value <= 0) {
        status = Status::NoTarget;
        dirty = true;
        return false;
    }
    measuredTenths = 0;  // auto zero before every fill
    const int pulses = target.value * BenchMotor::PulsesPerTurn / FakeTenthsPerTurn;
    const int slowFrom = max(0, target.value - slowAt.value) * BenchMotor::PulsesPerTurn / FakeTenthsPerTurn;
    fillSlowFrom = slowFrom;
    if (!motorReady || !BenchMotor::start(pulses, fastRpm.value, slowRpm.value, slowFrom)) {
        status = Status::MotorFault;
        Serial.println("Motor start failed");
        dirty = true;
        return false;
    }
    filling = true;
    status = Status::Filling;
    dirty = true;
    return true;
}

void stopFill() {
    if (!filling) return;
    BenchMotor::stop();
    status = Status::Stopping;
    dirty = true;
}

void updateFill() {
    if (!filling) return;
    BenchMotor::update();
    const auto motion = BenchMotor::motion();
    const int next = static_cast<int>(int64_t(motion.pulses) * FakeTenthsPerTurn / BenchMotor::PulsesPerTurn);
    if (next != measuredTenths) {
        measuredTenths = next;
        dirty = true;
    }
    if (!motion.running) {
        filling = false;
        if (motion.pulseFault) status = Status::MotorFault;
        else status = status == Status::Stopping ? Status::Stopped : Status::OnTarget;
        const char* result = status == Status::Stopped ? "STOP" : (measuredTenths > target.value ? "OVER" : "OK");
        if (status != Status::MotorFault) FillLog::append(Recipes::active().name, target.value, measuredTenths, result);
        dirty = true;
    }
}

// ---------------------------------------------------------------- input
int accelerated(int delta) {
    const int speed = abs(delta);
    return delta * (speed >= 3 ? 10 : (speed == 2 ? 3 : 1));
}

void turn(int delta) {
    lastTurnAt = millis();
    if (screen == Screen::Main) return;
    if (editing) {
        draft = constrain(draft + accelerated(delta), editing->min, editing->max);
    } else if (screen == Screen::Recipe) {
        browseIndex = constrain(browseIndex + (delta > 0 ? 1 : -1), 0, Recipes::count() - 1);
    } else if (screen == Screen::Calibrate || screen == Screen::Wifi) {
        return;
    } else {
        int count;
        items(count);
        focus = constrain(focus + delta, 0, count - 1);
    }
    dirty = true;
}

void press() {
    switch (screen) {
        case Screen::Main:
            if (filling) stopFill();
            else startFill();
            break;
        case Screen::Recipe:
            Recipes::select(browseIndex);
            loadRecipe();
            status = Status::Ready;
            show(Screen::Main);
            return;
        case Screen::Wifi:
            if (forgetArmed) Web::forget();
            forgetArmed = true;
            break;
        case Screen::Menu:
        case Screen::Setup:
        case Screen::Speed: {
            if (editing) {
                editing->value = draft;
                save(*editing);
                editing = nullptr;
                break;
            }
            int count;
            const Item& item = items(count)[focus];
            if (item.field) {
                editing = item.field;
                draft = editing->value;
            } else if (item.action) {
                item.action();
            }
            break;
        }
        case Screen::Calibrate:
            if (calibrateStep == 0) {
                measuredTenths = 0;  // zero the empty scale
                editing = &checkWeight;
                draft = checkWeight.value;
            } else if (calibrateStep == 1) {
                checkWeight.value = draft;
                save(checkWeight);
                editing = nullptr;
            } else if (calibrateStep == 2) {
                // TODO: with the load cell, compute and save the scale factor here.
                measuredTenths = checkWeight.value;
            } else {
                status = Status::Ready;
                show(Screen::Main);
                return;
            }
            ++calibrateStep;
            break;
    }
    dirty = true;
}

void hold() {
    if (filling) return;
    if (editing && screen != Screen::Calibrate) {
        editing = nullptr;  // cancel the edit
    } else if (screen == Screen::Main) {
        if (status != Status::MotorFault) status = Status::Ready;
        show(Screen::Menu);
    } else if (screen == Screen::Speed || screen == Screen::Calibrate || screen == Screen::Wifi) {
        backToSetup();
    } else if (screen == Screen::Setup) {
        backToMenu();
    } else if (screen == Screen::Recipe) {
        show(Screen::Menu, 0);
    } else {
        backToMain();
    }
    dirty = true;
}

// ---------------------------------------------------------------- drawing
template <size_t Count>
void centered(const Glyph (&font)[Count], const uint8_t* pixels, const char* text, int center, int baseline,
              uint16_t ink) {
    drawText(canvas, font, pixels, text, center - textWidth(font, text) / 2, baseline, ink);
}

// Mid-size value with a small "gr" unit, centred.
void midGrains(int tenths, int center, int baseline, uint16_t ink) {
    char value[12];
    grains(value, sizeof(value), tenths);
    const int valueWidth = textWidth(Mid, value);
    const int left = center - (valueWidth + 4 + textWidth(Small, "gr")) / 2;
    drawText(canvas, Mid, MidPixels, value, left, baseline, ink);
    drawText(canvas, Small, SmallPixels, "gr", left + valueWidth + 4, baseline, ink);
}

const char* statusLabel(uint16_t& dot) {
    dot = Green;
    if (!motorReady || status == Status::MotorFault) {
        dot = Red;
        return "MOTOR FAULT";
    }
    switch (status) {
        case Status::Filling: dot = Blue; return "FILLING";
        case Status::Stopping: dot = Amber; return "STOPPING";
        case Status::OnTarget:
            if (measuredTenths > target.value) {
                dot = Red;
                return "OVER TARGET";
            }
            return "ON TARGET";
        case Status::Stopped: dot = Amber; return "STOPPED";
        case Status::Zeroed: return "ZEROED";
        case Status::NoTarget: dot = Amber; return "SET TARGET";
        default: return "READY";
    }
}

void header() {
    uint16_t dot = Green;
    const char* label = statusLabel(dot);
    switch (screen) {
        case Screen::Menu: label = "MENU"; dot = Green; break;
        case Screen::Recipe: {
            static char position[16];
            snprintf(position, sizeof(position), "RECIPE %d / %d", browseIndex + 1, Recipes::count());
            label = position;
            dot = browseIndex == Recipes::activeIndex() ? Green : Blue;
            break;
        }
        case Screen::Setup: label = "SETUP"; dot = Green; break;
        case Screen::Speed: label = "SPEED"; dot = Green; break;
        case Screen::Wifi: label = "WI-FI"; dot = Green; break;
        case Screen::Calibrate: label = "CALIBRATE"; dot = Amber; break;
        case Screen::Main: break;
    }
    if (!encoderReady) {
        label = "NO ENCODER";
        dot = Amber;
    }
    const int left = 266 - textWidth(Small, label);
    canvas.fillCircle(left - 10, 25, 4, dot);
    drawText(canvas, Small, SmallPixels, label, left, 31);
}

void hint(const char* text, int baseline = 230) { centered(Hint, HintPixels, text, Width / 2, baseline, Dim); }

void drawMain() {
    const int goal = target.value;
    uint16_t ink = TFT_WHITE;
    if (status == Status::OnTarget) ink = measuredTenths > goal ? Red : Green;

    char value[12];
    grains(value, sizeof(value), measuredTenths);
    ConsoleDrawing::reading(canvas, value, Width, 116, ink);

    // Progress toward the target.
    canvas.fillRoundRect(30, 130, 220, 6, 3, Track);
    if (goal > 0 && measuredTenths > 0) {
        const int fill = constrain(220L * measuredTenths / goal, 6L, 220L);
        canvas.fillRoundRect(30, 130, fill, 6, 3, status == Status::OnTarget ? ink : Cyan);
    }

    char name[32];
    fit(name, sizeof(name), Recipes::active().name.c_str(), Small, 250);
    centered(Small, SmallPixels, name, Width / 2, 163, Cyan);
    midGrains(goal, Width / 2, 197, TFT_WHITE);
    hint(filling ? "PRESS  STOP" : "PRESS  FILL        HOLD  MENU");
}

void drawList() {
    int count;
    const Item* rows = items(count);
    // Five rows fit between the header and the hint line, no scrolling.
    constexpr int Top = 50, Row = 34;
    for (int i = 0; i < count; ++i) {
        const int y = Top + i * Row;
        const bool focused = i == focus;
        const bool changing = focused && editing && editing == rows[i].field;
        if (focused) {
            const uint16_t outline = changing ? Amber : Blue;
            canvas.drawRoundRect(14, y, 252, Row - 4, 6, outline);
            canvas.drawRoundRect(15, y + 1, 250, Row - 6, 5, outline);
        }
        const int baseline = y + 21;
        drawText(canvas, Small, SmallPixels, rows[i].label, 28, baseline, TFT_WHITE);
        char value[32] = "";
        if (rows[i].field) {
            format(value, sizeof(value), *rows[i].field, changing ? draft : rows[i].field->value);
        } else if (rows[i].action == openRecipe) {
            char name[28];
            fit(name, sizeof(name), Recipes::active().name.c_str(), Small, 130);
            snprintf(value, sizeof(value), "%s  >", name);
        } else if (opensScreen(rows[i])) {
            strcpy(value, ">");
        }
        drawText(canvas, Small, SmallPixels, value, 252 - textWidth(Small, value), baseline, changing ? Amber : Cyan);
    }
    hint(editing ? "TURN  SET     PRESS  SAVE     HOLD  CANCEL" : "TURN  PICK     PRESS  OK     HOLD  BACK");
}

void drawCalibrate() {
    static const char* const Titles[] = {"EMPTY THE SCALE", "SET CHECK WEIGHT", "PLACE THE WEIGHT", "CALIBRATED"};
    static const char* const Hints[] = {"PRESS  WHEN EMPTY     HOLD  CANCEL", "TURN  SET     PRESS  OK     HOLD  CANCEL",
                                        "PRESS  WHEN STABLE     HOLD  CANCEL", "PRESS  DONE"};
    char step[12];
    snprintf(step, sizeof(step), "STEP %d / 3", min(calibrateStep + 1, 3));
    if (calibrateStep < 3) centered(Hint, HintPixels, step, Width / 2, 72, Cyan);
    centered(Small, SmallPixels, Titles[calibrateStep], Width / 2, 102, TFT_WHITE);

    if (calibrateStep == 1) midGrains(draft, Width / 2, 155, Amber);
    if (calibrateStep == 2) midGrains(checkWeight.value, Width / 2, 155, Cyan);
    if (calibrateStep == 3) midGrains(measuredTenths, Width / 2, 155, Green);
    if (calibrateStep == 1) {
        canvas.fillTriangle(Width / 2 - 5, 120, Width / 2, 114, Width / 2 + 5, 120, Amber);
        canvas.fillTriangle(Width / 2 - 5, 166, Width / 2, 172, Width / 2 + 5, 166, Amber);
    }
    hint(Hints[calibrateStep]);
}

// Recipe details: turn to browse, press to use.
void drawRecipe() {
    browseIndex = constrain(browseIndex, 0, Recipes::count() - 1);
    const auto& recipe = Recipes::at(browseIndex);
    const bool inUse = browseIndex == Recipes::activeIndex();

    char text[40];
    fit(text, sizeof(text), recipe.name.c_str(), Menu, 252);
    centered(Menu, MenuPixels, text, Width / 2, 70, inUse ? Green : TFT_WHITE);

    char charge[16], bullet[48];
    snprintf(charge, sizeof(charge), "%d.%d GR", recipe.charge / 10, recipe.charge % 10);
    if (recipe.bulletWeight) {
        snprintf(bullet, sizeof(bullet), "%d.%d GR %s", recipe.bulletWeight / 10, recipe.bulletWeight % 10,
                 recipe.bullet.c_str());
    } else {
        snprintf(bullet, sizeof(bullet), "%s", recipe.bullet.c_str());
    }
    const char* const Rows[][2] = {
        {"CHARGE", charge},
        {"CALIBER", recipe.caliber.c_str()},
        {"BULLET", bullet},
        {"POWDER", recipe.powder.c_str()},
        {"PRIMER", recipe.primer.c_str()},
        {"BRASS", recipe.brass.c_str()},
        {"OAL", recipe.oal.c_str()},
        {"CBTO", recipe.cbto.c_str()},
    };
    int baseline = 94;
    for (const auto& row : Rows) {
        drawText(canvas, Hint, HintPixels, row[0], 18, baseline, Cyan);
        fit(text, sizeof(text), *row[1] ? row[1] : "-", Small, 174);
        drawText(canvas, Small, SmallPixels, text, 88, baseline, row[0] == Rows[0][0] ? Amber : TFT_WHITE);
        baseline += 17;
    }
    hint(inUse ? "IN USE     TURN  BROWSE     HOLD  BACK" : "TURN  BROWSE     PRESS  USE     HOLD  BACK");
}

// Label above a value, both centred.
void infoLine(const char* label, const char* value, int baseline, uint16_t ink = TFT_WHITE) {
    char text[40];
    fit(text, sizeof(text), value, Menu, 256);
    centered(Hint, HintPixels, label, Width / 2, baseline - 24, Cyan);
    centered(Menu, MenuPixels, text, Width / 2, baseline, ink);
}

void drawWifi() {
    const String network = Web::network();
    const String address = Web::address();
    switch (Web::mode()) {
        case Web::Mode::Connected: {
            infoLine("CONNECTED TO", network.c_str(), 85, Green);
            infoLine("OPEN IN A BROWSER", "POWDERRELOAD.LOCAL", 140);
            centered(Small, SmallPixels, address.c_str(), Width / 2, 166, Cyan);
            break;
        }
        case Web::Mode::Setup: {
            infoLine("JOIN WI-FI", Web::SetupSsid, 85, Amber);
            infoLine("PASSWORD", Web::SetupPassword, 135);
            char url[32];
            snprintf(url, sizeof(url), "THEN OPEN %s", address.c_str());
            centered(Small, SmallPixels, url, Width / 2, 172, Cyan);
            break;
        }
        case Web::Mode::Connecting:
            infoLine("CONNECTING TO", network.c_str(), 110, Amber);
            break;
    }
    if (forgetArmed) hint("PRESS AGAIN TO FORGET WI-FI     HOLD  BACK");
    else hint("PRESS  FORGET WI-FI     HOLD  BACK");
}

void render() {
    if (!canvasReady) return;
    ConsoleDrawing::background(canvas);
    header();
    switch (screen) {
        case Screen::Main: drawMain(); break;
        case Screen::Recipe: drawRecipe(); break;
        case Screen::Menu:
        case Screen::Setup:
        case Screen::Speed: drawList(); break;
        case Screen::Calibrate: drawCalibrate(); break;
        case Screen::Wifi: drawWifi(); break;
    }
    canvas.pushSprite((lcd.width() - Width) / 2, 0);
}

// ---------------------------------------------------------------- encoder LED
constexpr uint8_t PixelPin = 6;
seesaw_NeoPixel pixel(1, PixelPin, NEO_GRB + NEO_KHZ800);
bool pixelReady = false;

void updatePixel(uint32_t now) {
    if (!pixelReady) return;
    uint8_t r = 0, g = 6, b = 0;  // ready: dim green
    if (!motorReady || status == Status::MotorFault) {
        const bool on = (now / 250) % 2;
        r = on ? 60 : 0; g = 0;
    } else if (screen == Screen::Main) {
        switch (status) {
            case Status::Filling:
                if (BenchMotor::motion().pulses >= fillSlowFrom) { r = 0; g = 30; b = 30; }  // slow: cyan
                else { r = 0; g = 0; b = 50; }                                               // fast: blue
                break;
            case Status::OnTarget:
                if (measuredTenths > target.value) { r = 60; g = 0; }
                else { g = 50; }
                break;
            case Status::Stopping:
            case Status::Stopped:
            case Status::NoTarget: r = 50; g = 20; break;
            default: break;
        }
    } else if (editing) {
        r = 50; g = 20;  // editing a value: amber
    } else {
        r = g = b = 8;   // menus: dim white
    }
    if (now - lastTurnAt < 80) {  // knob feedback flash
        r = min(255, r * 3 + 20); g = min(255, g * 3 + 20); b = min(255, b * 3 + 20);
    }

    static uint32_t shown = 0xFFFFFFFF;
    static uint32_t lastWrite = 0;
    const uint32_t color = pixel.Color(r, g, b);
    if (color == shown || now - lastWrite < 20) return;  // limit I2C traffic
    pixel.setPixelColor(0, color);
    pixel.show();
    shown = color;
    lastWrite = now;
}

// ---------------------------------------------------------------- encoder
class Knob : public Adafruit_seesaw {
public:
    bool sample(uint32_t& position, bool& down) {
        uint8_t count[4];
        uint8_t levels[4];
        if (!read(SEESAW_ENCODER_BASE, SEESAW_ENCODER_POSITION, count, sizeof(count)) ||
            !read(SEESAW_GPIO_BASE, SEESAW_GPIO_BULK, levels, sizeof(levels))) return false;
        position = (uint32_t(count[0]) << 24) | (uint32_t(count[1]) << 16) |
                   (uint32_t(count[2]) << 8) | count[3];
        down = (levels[0] & (1U << (ButtonPin - 24))) == 0;
        return true;
    }
};

Knob knob;
void pollEncoder(uint32_t now) {
    static uint32_t lastPoll = 0;
    static uint32_t lastPosition = 0;
    static uint32_t changedAt = 0;
    static uint32_t pressedAt = 0;
    static bool haveSample = false;
    static bool candidate = true;
    static bool pressed = true;
    static bool armed = false;
    static bool longSent = false;

    if (!encoderReady || now - lastPoll < 10) return;
    lastPoll = now;
    uint32_t position;
    bool down;
    if (!knob.sample(position, down)) {
        haveSample = false;
        armed = false;
        return;
    }
    if (!haveSample) {
        lastPosition = position;
        pressed = candidate = true;
        changedAt = now;
        haveSample = true;
    }
    const int32_t delta = static_cast<int32_t>(position - lastPosition);
    lastPosition = position;
    if (delta != 0 && delta >= -4 && delta <= 4) turn(delta);

    if (candidate != down) {
        candidate = down;
        changedAt = now;
    }
    if (candidate != pressed && now - changedAt >= 30) {
        pressed = candidate;
        if (pressed) {
            pressedAt = changedAt;
            longSent = false;
        } else {
            if (armed && !longSent) {
                if (changedAt - pressedAt >= HoldMs) hold();
                else press();
            }
            armed = true;
        }
    }
    if (armed && pressed && down && !longSent && now - pressedAt >= HoldMs) {
        longSent = true;
        hold();
    }
}

// ---------------------------------------------------------------- serial test keys
void reportState() {
    Serial.printf("UI screen=%d focus=%d editing=%d status=%d recipe=%s target=%d fast=%d slow=%d slowat=%d "
                  "cal=%d measured=%d wifi=%d %s\n",
                  static_cast<int>(screen), focus, editing != nullptr, static_cast<int>(status),
                  Recipes::active().name.c_str(), target.value, fastRpm.value, slowRpm.value, slowAt.value,
                  checkWeight.value, measuredTenths, static_cast<int>(Web::mode()), Web::address().c_str());
}

// Raw 8-bit (RGB332) frame for scripts/screenshot.py.
void dumpScreen() {
    if (!canvasReady) return;
    render();
    Serial.printf("SHOT %d %d\n", Width, Height);
    Serial.write(static_cast<const uint8_t*>(canvas.getPointer()), Width * Height);
    Serial.flush();
}

void pollSerial() {
    if (!Serial.available()) return;
    switch (Serial.read()) {
        case '+': turn(1); break;
        case '-': turn(-1); break;
        case 's': press(); break;
        case 'b': hold(); break;
        case '?': reportState(); break;
        case 'd': dumpScreen(); break;
        default: break;
    }
}
}

// ---------------------------------------------------------------- web UI hooks (panel.h)
PanelStatus panelStatus() {
    uint16_t dot;
    return {statusLabel(dot), filling, measuredTenths, target.value};
}

bool panelFill() {
    if (filling || editing) return false;
    show(Screen::Main);
    return startFill();
}

void panelStop() { stopFill(); }

void panelTare() {
    if (!filling) tare();
}

void setup() {
    // Disables the driver (ENABLE high) before anything else.
    motorReady = BenchMotor::begin();
    Serial.begin(115200);
    storageReady = preferences.begin("powder-panel", false);
    if (storageReady) {
        const int stored = preferences.getUShort(checkWeight.key, checkWeight.value);
        checkWeight.value = constrain(stored, checkWeight.min, checkWeight.max);
    }

    // First boot with recipes: carry over the values set before recipes existed.
    Recipes::Recipe first;
    first.name = "Default";
    if (storageReady) {
        first.charge = constrain(static_cast<int>(preferences.getUShort("target", 0)), 0, 999);
        first.fastRpm = constrain(static_cast<int>(preferences.getUShort("fast", 120)), 1, 225);
        first.slowRpm = constrain(static_cast<int>(preferences.getUShort("slow", 15)), 1, 225);
        first.slowAt = constrain(static_cast<int>(preferences.getUShort("slowat", 20)), 1, 200);
    }
    if (!Recipes::begin(first)) Serial.println("Storage: could not mount flash file system");
    loadRecipe();

    Wire.begin(21, 22);
    encoderReady = knob.begin(EncoderAddress);
    if (encoderReady) {
        knob.pinMode(ButtonPin, INPUT_PULLUP);
        pixelReady = pixel.begin(EncoderAddress);
    }

    lcd.init();
    lcd.setRotation(1);
    lcd.fillScreen(TFT_BLACK);
    canvas.setColorDepth(8);
    canvasReady = canvas.createSprite(Width, Height) != nullptr;
    if (!canvasReady) {
        lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        lcd.drawString("Display buffer unavailable", 22, 100, 2);
    }
    Serial.printf("PowderReload: motor %s, encoder %s, display %s\n",
                  motorReady ? "ok" : "FAILED", encoderReady ? "ok" : "FAILED",
                  canvasReady ? "ok" : "FAILED");
    Web::begin();
    reportState();
}

void loop() {
    pollEncoder(millis());
    pollSerial();
    Web::loop();
    updateFill();
    updatePixel(millis());

    // Recipes changed from the web page: pick up the new values.
    if (Recipes::revision() != loadedRevision && !editing) {
        loadRecipe();
        dirty = true;
    }
    // Keep the Wi-Fi screen current (address appears after connecting).
    static Web::Mode shownMode = Web::Mode::Connecting;
    if (Web::mode() != shownMode) {
        shownMode = Web::mode();
        dirty = true;
    }

    // Cap redraws at 20 fps; a full frame push takes about 30 ms.
    static uint32_t lastRender = 0;
    if (dirty && millis() - lastRender >= 50) {
        dirty = false;
        lastRender = millis();
        render();
    }
    delay(1);
}
