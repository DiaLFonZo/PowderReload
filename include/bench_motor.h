#pragma once

#include <stdint.h>

namespace BenchMotor {
constexpr int FullStepsPerTurn = 200;
constexpr int Microsteps = 16;
constexpr int PulsesPerTurn = FullStepsPerTurn * Microsteps;
// Pulse counter limit: longest single move.
constexpr int MaxPulses = 32000;

struct Motion {
    bool running;
    bool pulseFault;
    int pulses;   // STEP pulses sent since start
    float rpm;    // commanded speed at the current pulse
};

bool begin();
// One revolution at a constant speed (with ramps).
bool start(float rpm);
// `pulses` steps: fastRpm until `slowFrom`, then slowRpm to the end.
bool start(int pulses, float fastRpm, float slowRpm, int slowFrom);
// Ramp down and stop early.
void stop();
void update();
Motion motion();
}
