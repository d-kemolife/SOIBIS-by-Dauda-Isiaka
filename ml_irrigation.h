/*******************************************************************************
 * ml_irrigation.h
 * TensorFlow Lite Micro integration for the Hybrid Irrigation Model
 * Model: Dense(128→64→32→3), pruned 70%, clustered 16, uint8 quantized
 * Inputs : 21 features (uint8, zero_point & scale from converter)
 * Outputs: 3 uint8 values → sandy_pump / clay_pump / loamy_pump
 *
 * HOW TO USE
 * ----------
 * 1. Convert your model_hybrid.tflite to a C array:
 *      xxd -i model_hybrid.tflite > model_hybrid.h
 *    Then rename the array/length inside that file to:
 *      const unsigned char  model_hybrid_tflite[]   = { ... };
 *      const unsigned int   model_hybrid_tflite_len = ...;
 *    and place model_hybrid.h in the same folder as your .ino / main .cpp.
 *
 * 2. Add to platformio.ini (or Arduino library manager):
 *      lib_deps =
 *        https://github.com/eloquentarduino/EloquentTinyML.git
 *      OR the official:
 *        tensorflow/lite@^2.4.0   (tflite-micro for ESP32)
 *    Recommended for ESP32-S3:
 *      https://github.com/espressif/esp-tflite-micro
 *
 * 3. #include "ml_irrigation.h" at the top of your main .cpp (after Arduino.h)
 *
 * 4. In setup():   MLIrrigationModel::initialize();
 *    In loop() or wherever you call IrrigationController::update():
 *      MLIrrigationModel::predict(currentData, pumpStatus, calibration);
 *******************************************************************************/

#pragma once

#include <Arduino.h>
// Tell the compiler that 'timeClient' is defined somewhere else
#include <NTPClient.h>
#include <WiFiUdp.h>   // optional, but good for safety
extern NTPClient timeClient;
// ── TFLite Micro headers ──────────────────────────────────────────────────────
// These come from the esp-tflite-micro or arduino-library version.
// If using EloquentTinyML, replace with:  #include <EloquentTinyML.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

// ── Your converted model header (xxd -i model_hybrid.tflite > model_hybrid.h)
#include "model_hybrid.h"

// ── Forward declarations matching the main codebase ──────────────────────────
// (already declared globally in the main .cpp — included here for clarity)
#include "sensor_types.h"
struct SensorData;
struct PumpStatus;
struct AdvancedCalibration;
extern SensorData   currentData;
extern PumpStatus   pumpStatus;
extern AdvancedCalibration calibration;

// ── Model constants ───────────────────────────────────────────────────────────
// 21 input features in EXACTLY this order (must match Python feature_cols order)
//  0  id
//   1  sandy_percent
// 2  clay_percent
//   3  loamy_percent
// 4  temperature
//   5  humidity
//   6  dew_point
//   7  is_raining
//   8  IRRIGATION PATTERN 
//   9  hour
//  10  day_of_week
//  11  month
//  12  minute
//  13  temp_roll_mean
//  14  humidity_roll_mean
//  15  dew_point_roll_mean
//  16  temp_humidity
//  17  sandy_clay
//  18  sandy_loamy
//  19  clay_loamy
//  20  soil_sum
// (sandy_loamy and soil_sum were generated but the scaler selects 21 cols —
//  verify against your actual feature_cols list printed during training)

namespace MLIrrigationModel {

// ── Tensor arena  ─────────────────────────────────────────────────────────────
// For Dense(128,64,32,3) uint8: ~20 KB is usually sufficient on ESP32-S3.
// Increase to 30*1024 if you see kTfLiteError on AllocateTensors().
constexpr int   kTensorArenaSize = 50 * 1024;
static uint8_t  tensorArena[kTensorArenaSize];

// ── TFLite runtime objects ────────────────────────────────────────────────────
static const tflite::Model*         tfModel     = nullptr;
static tflite::MicroInterpreter*    interpreter = nullptr;
static TfLiteTensor*                inputTensor  = nullptr;
static TfLiteTensor*                outputTensor = nullptr;
static bool                         modelReady   = false;

// ── Quantization parameters (read back from tensors after AllocateTensors) ───
// They are stored here for the dequantisation step.
static float    inputScale      = 1.0f;
static int32_t  inputZeroPoint  = 0;
static float    outputScale     = 1.0f;
static int32_t  outputZeroPoint = 0;

// ── Rolling buffers for the 1-hour window features ───────────────────────────
// 30 rows × 2-min sensor interval = ~1 hour of history
constexpr int kRollingWindow = 30;
static float rollingTemp     [kRollingWindow] = {};
static float rollingHumidity [kRollingWindow] = {};
static float rollingDewPoint [kRollingWindow] = {};
static int   rollingIdx      = 0;
static int   rollingSamples  = 0;  // number of samples accumulated so far

// ── StandardScaler parameters ─────────────────────────────────────────────────
// Copy these 21 pairs from the scaler printed in Colab:
//   print(list(zip(scaler.mean_.tolist(), scaler.scale_.tolist())))
// Until you have the exact values, the defaults below are placeholders.
// The model WON'T give correct results until you fill in the real numbers!
//
// Order: same 21-feature order listed above.
static const float kScalerMean[21] = {
    /*  0  id                        */ 5147.97440585f,
    /*  1  sandy_percent             */ 5.18396266f,
    /*  2  clay_percent              */ 59.83407680f,
    /*  3  loamy_percent             */ 15.15024980f,
    /*  4  temperature               */ 31.13546618f,
    /*  5  humidity                  */ 74.98452163f,
    /*  6  dew_point                 */ 25.84886093f,
    /*  7  is_raining                */ 0.00048751f,
    /*  8  IRRIGATION PATTERN        */ 99.96087751f,
    /*  9  hour                      */ 11.37684339f,
    /* 10  day_of_week               */ 2.95124924f,
    /* 11  month                     */ 6.00000000f,
    /* 12  minute                    */ 29.99463742f,
    /* 13  temp_roll_mean            */ 31.12328458f,
    /* 14  humidity_roll_mean        */ 75.01385708f,
    /* 15  dew_point_roll_mean       */ 25.84772248f,
    /* 16  temp_humidity             */ 2296.30375503f,
    /* 17  sandy_clay                */ 306.00719225f,
    /* 18  sandy_loamy               */ 138.21920147f,
    /* 19  clay_loamy                */ 918.26580686f,
    /* 20  soil_sum                  */ 80.16828926f
};

static const float kScalerScale[21] = {
    /*  0  id                        */ 2959.22428141f,
    /*  1  sandy_percent             */ 13.42286769f,
    /*  2  clay_percent              */ 7.01790161f,
    /*  3  loamy_percent             */ 15.54891417f,
    /*  4  temperature               */ 3.06266821f,
    /*  5  humidity                  */ 13.73838835f,
    /*  6  dew_point                 */ 0.83255168f,
    /*  7  is_raining                */ 0.02207419f,
    /*  8  IRRIGATION PATTERN        */ 13.89753766f,
    /*  9  hour                      */ 6.94711283f,
    /* 10  day_of_week               */ 2.01470825f,
    /* 11  month                     */ 1.00000000f,
    /* 12  minute                    */ 17.31318767f,
    /* 13  temp_roll_mean            */ 2.98961868f,
    /* 14  humidity_roll_mean        */ 13.59818529f,
    /* 15  dew_point_roll_mean       */ 0.62722908f,
    /* 16  temp_humidity             */ 224.59767732f,
    /* 17  sandy_clay                */ 792.73699160f,
    /* 18  sandy_loamy               */ 461.30501981f,
    /* 19  clay_loamy                */ 922.39182342f,
    /* 20  soil_sum                  */ 24.61184948f
};

// ── Decision thresholds (uint8 equivalent of sigmoid ≈ 0.5 after dequant) ───
// After uint8 dequantisation the raw uint8 value maps to float via:
//   float_val = (raw - outputZeroPoint) * outputScale
// We threshold at 0.5 in float space. Adjust per soil type if needed.
static const float kPumpThreshold[3] = { 0.50f, 0.50f, 0.50f };

// ── Confidence struct returned by predict() ───────────────────────────────────
struct MLPrediction {
    float  sandyProb;       // 0.0 – 1.0
    float  clayProb;
    float  loamyProb;
    bool   sandyPump;
    bool   clayPump;
    bool   loamyPump;
    bool   valid;           // false if model not ready or inference failed
};

// ─────────────────────────────────────────────────────────────────────────────
// INTERNAL HELPERS
// ─────────────────────────────────────────────────────────────────────────────

static float rollingMean(const float* buf, int count) {
    if (count == 0) return 0.0f;
    float sum = 0.0f;
    int   n   = min(count, kRollingWindow);
    for (int i = 0; i < n; i++) sum += buf[i];
    return sum / n;
}

// Standard-scale one feature value
static float standardScale(float raw, float mean, float scale) {
    if (scale == 0.0f) return 0.0f;
    return (raw - mean) / scale;
}

// Quantise a float to uint8 using the input tensor's quant params
static uint8_t quantiseInput(float scaled_value) {
    int32_t q = static_cast<int32_t>(
        roundf(scaled_value / inputScale) + inputZeroPoint);
    return static_cast<uint8_t>(constrain(q, 0, 255));
}

// Dequantise a uint8 output to float probability
static float dequantiseOutput(uint8_t raw) {
    return (static_cast<float>(raw) - static_cast<float>(outputZeroPoint))
           * outputScale;
}

// ─────────────────────────────────────────────────────────────────────────────
// PUBLIC API
// ─────────────────────────────────────────────────────────────────────────────

/**
 * initialize() — call once in setup(), AFTER WiFi / sensors are ready.
 * Returns true on success.
 */
static bool initialize() {
    tflite::InitializeTarget();

    tfModel = tflite::GetModel(model_hybrid_tflite);
    if (tfModel->version() != TFLITE_SCHEMA_VERSION) {
        Serial.printf("[ML] Model schema mismatch: got %u, expected %u\n",
                      tfModel->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    // Register only the ops the hybrid model uses:
    //   FULLY_CONNECTED (Dense), LOGISTIC (sigmoid output), RESHAPE
    static tflite::MicroMutableOpResolver<4> resolver;
    resolver.AddFullyConnected();
    resolver.AddLogistic();
    resolver.AddReshape();
    resolver.AddQuantize();   // needed for uint8 I/O conversion internally

    static tflite::MicroInterpreter staticInterpreter(
        tfModel, resolver, tensorArena, kTensorArenaSize);
    interpreter = &staticInterpreter;

    TfLiteStatus allocStatus = interpreter->AllocateTensors();
    if (allocStatus != kTfLiteOk) {
        Serial.println("[ML] AllocateTensors() FAILED — try increasing kTensorArenaSize");
        return false;
    }

    inputTensor  = interpreter->input(0);
    outputTensor = interpreter->output(0);

    // Validate tensor shapes
    if (inputTensor->dims->data[1] != 21) {
        Serial.printf("[ML] Unexpected input size: %d (expected 21)\n",
                      inputTensor->dims->data[1]);
        return false;
    }
    if (outputTensor->dims->data[1] != 3) {
        Serial.printf("[ML] Unexpected output size: %d (expected 3)\n",
                      outputTensor->dims->data[1]);
        return false;
    }

    // Cache quantisation parameters
    inputScale      = inputTensor->params.scale;
    inputZeroPoint  = inputTensor->params.zero_point;
    outputScale     = outputTensor->params.scale;
    outputZeroPoint = outputTensor->params.zero_point;

    Serial.printf("[ML] Model ready. Input quant: scale=%.6f zp=%d\n",
                  inputScale, inputZeroPoint);
    Serial.printf("[ML] Output quant: scale=%.6f zp=%d\n",
                  outputScale, outputZeroPoint);

    // Arena usage diagnostic
    Serial.printf("[ML] Arena used: %u / %u bytes\n",
                  interpreter->arena_used_bytes(), kTensorArenaSize);

    modelReady = true;
    return true;
}

/**
 * updateRollingBuffers() — call every time new sensor data is read
 * (inside your sensor-read loop, before calling predict).
 */
static void updateRollingBuffers(const SensorData& data) {
    rollingTemp    [rollingIdx] = data.temperature;
    rollingHumidity[rollingIdx] = data.humidity;
    rollingDewPoint[rollingIdx] = data.dewPoint;
    rollingIdx = (rollingIdx + 1) % kRollingWindow;
    if (rollingSamples < kRollingWindow) rollingSamples++;
}

/**
 * buildFeatureVector() — assemble and scale the 21-feature input.
 * Returns false if critical sensor data is NaN.
 */
static bool buildFeatureVector(const SensorData& data, float features[21]) {
    if (isnan(data.temperature) || isnan(data.humidity)) return false;

    float dewPt = isnan(data.dewPoint) ? (data.temperature - 2.5f) : data.dewPoint;

    // Time features — derive from NTP if available, else millis()-based
    time_t epoch = timeClient.getEpochTime();
    struct tm* ptm = gmtime(&epoch);
    int hour       = ptm ? ptm->tm_hour   : (int)((millis() / 3600000UL) % 24);
    int dayOfWeek  = ptm ? ptm->tm_wday   : 0;
    int month      = ptm ? (ptm->tm_mon+1): 1;
    int minute     = ptm ? ptm->tm_min    : (int)((millis() / 60000UL) % 60);

    // Rolling means (use current value as fallback if buffer not yet warm)
    float tRoll  = (rollingSamples > 0) ? rollingMean(rollingTemp,     rollingSamples) : data.temperature;
    float hRoll  = (rollingSamples > 0) ? rollingMean(rollingHumidity, rollingSamples) : data.humidity;
    float dpRoll = (rollingSamples > 0) ? rollingMean(rollingDewPoint, rollingSamples) : dewPt;

    // Interaction features
    float tempHumidity = data.temperature * data.humidity;
    float sandyClay    = data.sandyPercent * data.clayPercent;
    float clayLoamy    = data.clayPercent  * data.loamyPercent;

    // Pack raw (unscaled) values in feature order
    features[0]  = data.sandyPercent;
    features[1]  = data.clayPercent;
    features[2]  = data.loamyPercent;
    features[3]  = data.temperature;
    features[4]  = data.humidity;
    features[5]  = isnan(data.pressure) ? 1013.0f : data.pressure;
    features[6]  = isnan(data.altitude) ? 0.0f    : data.altitude;
    features[7]  = dewPt;
    features[8]  = data.isRaining    ? 1.0f : 0.0f;
    features[9]  = data.deviceMoved  ? 1.0f : 0.0f;
    features[10] = static_cast<float>(data.movementIntensity);
    features[11] = static_cast<float>(hour);
    features[12] = static_cast<float>(dayOfWeek);
    features[13] = static_cast<float>(month);
    features[14] = static_cast<float>(minute);
    features[15] = tRoll;
    features[16] = hRoll;
    features[17] = dpRoll;
    features[18] = tempHumidity;
    features[19] = sandyClay;
    features[20] = clayLoamy;

    // Apply StandardScaler
    for (int i = 0; i < 21; i++) {
        features[i] = standardScale(features[i], kScalerMean[i], kScalerScale[i]);
    }
    return true;
}

/**
 * predict() — run a full inference cycle.
 * Returns an MLPrediction struct; check .valid before using pump booleans.
 *
 * The pump booleans here are ADVISORY — they should feed into
 * IrrigationController logic, not bypass it.
 */
static MLPrediction predict(const SensorData& data) {
    MLPrediction result = {0.0f, 0.0f, 0.0f, false, false, false, false};

    if (!modelReady) {
        Serial.println("[ML] predict() called before initialize()");
        return result;
    }

    // ── 1. Build & scale features ────────────────────────────────────────────
    float features[21];
    if (!buildFeatureVector(data, features)) {
        Serial.println("[ML] Skipping inference — NaN in sensor data");
        return result;
    }

    // ── 2. Quantise and write to input tensor ─────────────────────────────────
    for (int i = 0; i < 21; i++) {
        inputTensor->data.uint8[i] = quantiseInput(features[i]);
    }

    // ── 3. Run inference ──────────────────────────────────────────────────────
    unsigned long t0 = micros();
    TfLiteStatus invokeStatus = interpreter->Invoke();
    unsigned long inferUs = micros() - t0;

    if (invokeStatus != kTfLiteOk) {
        Serial.println("[ML] Invoke() FAILED");
        return result;
    }

    // ── 4. Dequantise outputs ─────────────────────────────────────────────────
    result.sandyProb = constrain(dequantiseOutput(outputTensor->data.uint8[0]), 0.0f, 1.0f);
    result.clayProb  = constrain(dequantiseOutput(outputTensor->data.uint8[1]), 0.0f, 1.0f);
    result.loamyProb = constrain(dequantiseOutput(outputTensor->data.uint8[2]), 0.0f, 1.0f);

    // ── 5. Apply thresholds ───────────────────────────────────────────────────
    result.sandyPump = (result.sandyProb >= kPumpThreshold[0]);
    result.clayPump  = (result.clayProb  >= kPumpThreshold[1]);
    result.loamyPump = (result.loamyProb >= kPumpThreshold[2]);
    result.valid     = true;

    // ── 6. Diagnostics (printed every 10 calls) ───────────────────────────────
    static uint32_t callCount = 0;
    if (++callCount % 10 == 0) {
        Serial.printf("[ML] #%u  %.1fms  S:%.3f C:%.3f L:%.3f  →  S:%d C:%d L:%d\n",
                      callCount, inferUs / 1000.0f,
                      result.sandyProb, result.clayProb, result.loamyProb,
                      result.sandyPump, result.clayPump, result.loamyPump);
    }

    return result;
}

/**
 * printScalerHint() — prints a snippet to paste into the Serial monitor
 * to verify your scaler values match what Colab produced.
 * Call once from setup() after initialize().
 */
static void printScalerHint() {
    Serial.println(F("\n[ML] === PASTE THESE LINES INTO COLAB TO VERIFY SCALER ==="));
    Serial.println(F("import numpy as np"));
    Serial.println(F("mean  = scaler.mean_"));
    Serial.println(F("scale = scaler.scale_"));
    Serial.println(F("for i,(m,s) in enumerate(zip(mean,scale)):"));
    Serial.println(F("    print(f'    /* {i:2d} */ {m:.6f}f,  // mean   {s:.6f}f  // scale')"));
    Serial.println(F("[ML] =========================================================\n"));
}

} // namespace MLIrrigationModel
