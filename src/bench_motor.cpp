#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/pulse_cnt.h>
#include <driver/rmt_tx.h>
#include "bench_motor.h"

namespace BenchMotor {
namespace {
constexpr int StepPin = 27;
constexpr int DirectionPin = 16;
constexpr gpio_num_t EnablePin = GPIO_NUM_17;
constexpr uint32_t TimerHz = 1000000;

constexpr uint32_t StartHz = 50;
constexpr uint32_t Acceleration = 50000;  // steps/s^2

// Move profile. The encoder callback runs in the RMT interrupt, so it uses integer math only.
struct Profile {
    uint32_t total;
    uint32_t slowFrom;
    uint32_t fastSq;  // Hz^2
    uint32_t slowSq;
    volatile uint32_t end;      // shortened by stop()
    volatile uint32_t encoded;  // steps handed to the RMT so far
};
Profile profile{};

rmt_channel_handle_t transmitter = nullptr;
rmt_encoder_handle_t encoder = nullptr;
pcnt_unit_handle_t counter = nullptr;
bool running = false;
bool pulseFault = false;

uint32_t isqrt(uint32_t value) {
    uint32_t root = 0;
    for (uint32_t bit = 1UL << 30; bit; bit >>= 2) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
    }
    return root;
}

// Speed limit at step s: accelerate from start, slow down before the slow zone, stop at the end.
uint32_t hzAt(uint32_t step) {
    const uint64_t baseSq = uint64_t(StartHz) * StartHz;
    const uint32_t end = profile.end;
    uint64_t limit = step < profile.slowFrom ? profile.fastSq : profile.slowSq;
    limit = min<uint64_t>(limit, baseSq + 2ULL * Acceleration * step);
    if (step + 1 < end) limit = min<uint64_t>(limit, baseSq + 2ULL * Acceleration * (end - 1 - step));
    else limit = min<uint64_t>(limit, baseSq);
    if (step < profile.slowFrom) {
        limit = min<uint64_t>(limit, profile.slowSq + 2ULL * Acceleration * (profile.slowFrom - step));
    }
    return max<uint32_t>(isqrt(static_cast<uint32_t>(limit)), StartHz);
}

size_t encodeSteps(const void*, size_t, size_t written, size_t space, rmt_symbol_word_t* symbols,
                   bool* done, void*) {
    // Always emit at least one more step, so a late stop() can never leave the encoder empty.
    const uint32_t end = max<uint32_t>(uint32_t(profile.end), written + 1);
    size_t count = 0;
    while (count < space && written + count < end) {
        const uint32_t period = TimerHz / hzAt(written + count);
        symbols[count].level0 = 0;
        symbols[count].duration0 = period / 2;
        symbols[count].level1 = 1;
        symbols[count].duration1 = period - period / 2;
        ++count;
    }
    profile.encoded = written + count;
    *done = profile.encoded >= end;
    return count;
}

int readPulses() {
    int count = 0;
    if (counter) pcnt_unit_get_count(counter, &count);
    return max(count, 0);
}

uint32_t hzSquared(float rpm) {
    const float hz = constrain(rpm, 1.0f, 225.0f) * PulsesPerTurn / 60.0f;
    return static_cast<uint32_t>(hz * hz);
}
}

bool begin() {
    gpio_set_level(EnablePin, 1);
    gpio_set_direction(EnablePin, GPIO_MODE_OUTPUT);
    pinMode(DirectionPin, OUTPUT);
    digitalWrite(DirectionPin, LOW);

    pcnt_unit_config_t countConfig{};
    countConfig.low_limit = -1;
    countConfig.high_limit = 32767;
    if (pcnt_new_unit(&countConfig, &counter) != ESP_OK) return false;

    pcnt_channel_handle_t countChannel = nullptr;
    pcnt_chan_config_t countInput{};
    countInput.edge_gpio_num = StepPin;
    countInput.level_gpio_num = -1;
    countInput.flags.io_loop_back = true;
    if (pcnt_new_channel(counter, &countInput, &countChannel) != ESP_OK ||
        pcnt_channel_set_edge_action(countChannel, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_HOLD) != ESP_OK ||
        pcnt_unit_enable(counter) != ESP_OK ||
        pcnt_unit_start(counter) != ESP_OK) return false;

    rmt_tx_channel_config_t output{};
    output.gpio_num = static_cast<gpio_num_t>(StepPin);
    output.clk_src = RMT_CLK_SRC_DEFAULT;
    output.resolution_hz = TimerHz;
    output.mem_block_symbols = 128;
    output.trans_queue_depth = 1;
    output.flags.io_loop_back = true;
    if (rmt_new_tx_channel(&output, &transmitter) != ESP_OK) return false;

    rmt_simple_encoder_config_t encoderConfig{};
    encoderConfig.callback = encodeSteps;
    encoderConfig.min_chunk_size = 1;
    return rmt_new_simple_encoder(&encoderConfig, &encoder) == ESP_OK &&
           rmt_enable(transmitter) == ESP_OK;
}

bool start(float rpm) {
    return start(PulsesPerTurn, rpm, rpm, PulsesPerTurn);
}

bool start(int pulses, float fastRpm, float slowRpm, int slowFrom) {
    if (running || !transmitter || pulses <= 0 || pulses > MaxPulses ||
        pcnt_unit_clear_count(counter) != ESP_OK) return false;
    pulseFault = false;
    profile.total = pulses;
    profile.slowFrom = constrain(slowFrom, 0, pulses);
    profile.fastSq = hzSquared(fastRpm);
    profile.slowSq = hzSquared(slowRpm);
    profile.end = pulses;
    profile.encoded = 0;

    static const uint8_t dummy = 0;  // the simple encoder needs a non-empty payload
    rmt_transmit_config_t transaction{};
    gpio_set_level(EnablePin, 0);
    if (rmt_transmit(transmitter, encoder, &dummy, sizeof(dummy), &transaction) != ESP_OK) {
        gpio_set_level(EnablePin, 1);
        return false;
    }
    running = true;
    return true;
}

void stop() {
    if (!running) return;
    const uint32_t from = profile.encoded;
    const uint32_t hz = hzAt(from);
    const uint32_t rampSteps = (uint64_t(hz) * hz - uint64_t(StartHz) * StartHz) / (2ULL * Acceleration);
    profile.end = min<uint32_t>(uint32_t(profile.end), from + rampSteps + 1);
}

void update() {
    if (!running) return;
    const esp_err_t result = rmt_tx_wait_all_done(transmitter, 0);
    if (result == ESP_ERR_TIMEOUT) return;

    gpio_set_level(EnablePin, 1);
    running = false;
    pulseFault = result != ESP_OK || readPulses() != static_cast<int>(profile.encoded);
}

Motion motion() {
    const int pulses = readPulses();
    float rpm = 0.0f;
    if (running) rpm = hzAt(pulses) * 60.0f / PulsesPerTurn;
    return {running, pulseFault, pulses, rpm};
}
}
