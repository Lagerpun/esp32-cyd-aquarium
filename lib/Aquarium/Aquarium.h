#ifndef AQUARIUM_H
#define AQUARIUM_H

#include <Arduino.h>
#include <Fonts/Font4x7Fixed.h>
#include <Fonts/Font5x7Fixed.h>
#include <Matrix.h>
#include <SCD40.h>

#include <vector>

#include "AquariumSettings.h"
#include "AquariumStateManager.h"
#include "BoidManager.h"
#include "Fish.h"
#include "Food.h"
#include "Plants.h"
#include "Water.h"
#include "StateManager.h"

#ifndef CYD_AUTONOMOUS_LIFE
#define CYD_AUTONOMOUS_LIFE 0
#endif

#ifndef CYD_CURATED_BOOT_POPULATION
#define CYD_CURATED_BOOT_POPULATION 0
#endif

#ifndef CYD_KEEP_CREATURES_ON_SCREEN
#define CYD_KEEP_CREATURES_ON_SCREEN 0
#endif

#ifndef CYD_AUTONOMOUS_BOOT_MIN_MS
#define CYD_AUTONOMOUS_BOOT_MIN_MS 3000
#endif

#ifndef CYD_AUTONOMOUS_BOOT_MAX_MS
#define CYD_AUTONOMOUS_BOOT_MAX_MS 6000
#endif

#ifndef CYD_AUTONOMOUS_FOOD_MIN_MS
#define CYD_AUTONOMOUS_FOOD_MIN_MS 7000
#endif

#ifndef CYD_AUTONOMOUS_FOOD_MAX_MS
#define CYD_AUTONOMOUS_FOOD_MAX_MS 16000
#endif

#ifndef CYD_AUTONOMOUS_MAX_FOOD
#define CYD_AUTONOMOUS_MAX_FOOD 3
#endif

#ifndef CYD_FIXED_HUMIDITY_PERCENT
#define CYD_FIXED_HUMIDITY_PERCENT 50
#endif

class Aquarium {
 private:
  Matrix* matrix;
  SCD40* scd40;
  StateManager* stateManager;
  Water water;
  std::vector<std::unique_ptr<Fish>> fishArray;
  std::vector<std::unique_ptr<Plants>> plantArray;
  std::vector<std::unique_ptr<Food>> foodArray;
  BoidManager boidManager;
  AquariumStateManager aquariumStateManager;
  unsigned long lastSaveTime;
  char buffer[100];

  // Demo settings
  bool demoMode;
  int demoStep;
  unsigned long demoStartTime;
  float demoTemperature;
  float demoHumidity;
  float demoCO2;
  bool demoFinished;

  // Touch input  
  bool touchActive = false;
  unsigned long lastFoodTime = 0;
  const unsigned long FOOD_INTERVAL = 200; // Add food every 200ms while touched
  unsigned long lastAutonomousFoodTime = 0;
  unsigned long nextAutonomousFoodDelay = 0;

  enum class TextAlignment { LEFT, CENTER, RIGHT };

  float initialCreatureAge(uint8_t index = 0) {
#if CYD_CURATED_BOOT_POPULATION
    if (index == 0) {
      return random(74, 84) / 100.0f;
    }
    if (index == 1) {
      return random(66, 78) / 100.0f;
    }
    return random(52, 69) / 100.0f;
#else
    (void)index;
    return 0.5f;
#endif
  }

  const char* curatedCreatureType(uint8_t index) {
#if CYD_CURATED_BOOT_POPULATION
    switch (index) {
      case 0:
        return "Turtle";
      case 1:
        return random(0, 3) == 0 ? "Octopus" : "Fish";
      case 2:
      case 3:
      case 5:
      case 6:
        return "Fish";
      case 4:
      case 7:
        return "Star";
      default:
        return random(0, 5) == 0 ? "Turtle" : "Fish";
    }
#else
    (void)index;
    return "Fish";
#endif
  }

  PVector safeSpawnPosition(uint8_t index) {
    const uint8_t width = matrix->getXResolution();
    const uint8_t height = matrix->getYResolution();
    const uint8_t cols = 3;
    const uint8_t rows = 3;
    const uint8_t cell = index % (cols * rows);
    const uint8_t col = cell % cols;
    const uint8_t row = cell / cols;

    float marginX = width > 24 ? 8.0f : 1.0f;
    float marginTop = height > 48 ? 18.0f : 1.0f;
    float marginBottom = height > 48 ? 18.0f : 1.0f;
    if (marginTop + marginBottom >= height) {
      marginTop = 1.0f;
      marginBottom = 1.0f;
    }
    if (marginX * 2.0f >= width) {
      marginX = 1.0f;
    }

    float usableW = width - marginX * 2.0f;
    float usableH = height - marginTop - marginBottom;
    if (usableW < 1.0f) {
      usableW = 1.0f;
    }
    if (usableH < 1.0f) {
      usableH = 1.0f;
    }

    const float jitterX = random(20, 80) / 100.0f;
    const float jitterY = random(18, 82) / 100.0f;
    const float x = marginX + (col + jitterX) * (usableW / cols);
    const float y = marginTop + (row + jitterY) * (usableH / rows);
    return PVector(x, y);
  }

 public:
  Aquarium(Matrix* m, SCD40* s, StateManager* stateManager)
      : matrix(m),
        scd40(s),
        stateManager(stateManager),
        water(matrix),
        boidManager(m),
        demoMode(false),
        demoStep(0),
        demoFinished(false),
        lastSaveTime(0) {}

  void begin() {
#if CYD_CURATED_BOOT_POPULATION
    initializeFish();
#else
    loadState();
    ensureMinimumFishPopulation();
#endif
    initializePlants();
    boidManager.initializeBoids();
    scheduleNextAutonomousFood(true);
  }

  bool isDemoFinished() const {
    return demoFinished;
  }

  void startDemo() {
    demoMode = true;
    demoStep = 0;
    demoStartTime = millis();
    demoTemperature = DEFAULT_TEMPERATURE_VALUE;
    demoHumidity = 50.0f;
    demoCO2 = DEFAULT_CO2_VALUE;
  }

  void updateDemo() {
  unsigned long currentTime = millis();
  unsigned long elapsedTime = currentTime - demoStartTime;


  // Define step durations in milliseconds
  const unsigned long stepDurations[] = {
    5000,  // 0 - Welcome to Livegrid
    5000,  // 1 - Your very own aquatic ecosystem
    5000,  // 2 - You start with 5 fish
    5000,  // 3 - They will slowly grow, and reproduce
    5000,  // 4 - But they are affected by the environment
    5000,  // 5 - Temperature affects water color
    15000, // 6 - show
    5000,  // 7 - Humidity affects plant growth
    15000, // 8 - show
    5000,  // 9 - CO2 affects fish behavior
    30000, // 10 - show
    5000,  // 11 - If your environment is good, they will thrive
    5000,  // 12 - And soon you will have an amazing ecosystem
    5000,  // 13 - Take care of them by taking care of yourself
    5000   // 14 - Enjoy
  };

  // Calculate current step and time within step
  unsigned long totalDuration = 0;
  for (demoStep = 0; demoStep < sizeof(stepDurations) / sizeof(stepDurations[0]); demoStep++) {
    if (elapsedTime < totalDuration + stepDurations[demoStep]) {
      break;
    }
    totalDuration += stepDurations[demoStep];
  }

  unsigned long stepElapsedTime = elapsedTime - totalDuration;

  auto pausingSine = [](float t, float pauseDuration) {
    float sineValue = sin(t);
    if (abs(sineValue) < 0.1) {  // Pause near the midpoint
      return 0.0f;
    }
    return sineValue;
  };

  float t;

  // Update demo values and display text based on the current step
  switch (demoStep) {
    case 0:
      snprintf(buffer, sizeof(buffer), "Welcome to\nLivegrid");
      break;
    case 1:
      snprintf(buffer, sizeof(buffer), "Your very own\naquatic\necosystem");
      break;
    case 2:
      snprintf(buffer, sizeof(buffer), "You start\nwith 5 fish");
      break;
    case 3:
      snprintf(buffer, sizeof(buffer), "They will\nslowly grow,\nand reproduce");
      break;
    case 4:
      snprintf(buffer, sizeof(buffer), "But they are\naffected by\nthe\nenvironment");
      break;
    case 5:
      snprintf(buffer, sizeof(buffer), "Temperature\naffects\nwater color");
      updateWater();
      break;
    case 6:
      demoTemperature = DEFAULT_TEMPERATURE_VALUE;
      snprintf(buffer, sizeof(buffer), "Temperature:\n%.0f C", demoTemperature);
      updateWater();
      break;
    case 7:
      snprintf(buffer, sizeof(buffer), "Humidity\naffects\nplant growth");
      updateWater();
      updatePlants();
      break;
    case 8:
      t = 2 * PI * stepElapsedTime / stepDurations[demoStep];
      demoHumidity = 50.0f + 40.0f * pausingSine(t, 0.2);
      snprintf(buffer, sizeof(buffer), "Humidity:\n%.0f %%", demoHumidity);
      updateWater();
      updatePlants();
      break;
     case 9:
      snprintf(buffer, sizeof(buffer), "CO2 affects\nfish behavior");
      break;
    case 10:
      demoCO2 = DEFAULT_CO2_VALUE;
      snprintf(buffer, sizeof(buffer), "CO2:\n%.0f ppm", demoCO2);
      break;
    case 11:
      demoCO2 = DEFAULT_CO2_VALUE;
      snprintf(buffer, sizeof(buffer), "If your\nenvironment\nis good,\nthey will\nthrive");
      break;
    case 12:
      snprintf(buffer, sizeof(buffer), "And soon you\nwill have an\namazing\necosystem");
      break;
    case 13:
      snprintf(buffer, sizeof(buffer), "Take care of\nthem by taking\ncare of\nyourself");
      break;
    case 14:
      snprintf(buffer, sizeof(buffer), "Enjoy !");
      break;
    default:
      demoMode = false;
      demoFinished = true;  // Set demoFinished to true when demo is complete
      saveState();
      return;
  }

  if (demoStep < 5 || demoStep > 8) {
    updateWater();
    boidManager.updateBoids(demoCO2);
    boidManager.renderBoids();
    updateFish();
    updateFood();
    updatePlants();
  }

  drawMultilineText(matrix->foreground, buffer, MIDDLE,
                    TextAlignment::CENTER, &Font5x7Fixed,
                    CRGB(150, 150, 150));
}

  void loadState() {
    if (!aquariumStateManager.loadState(fishArray, matrix)) {
      log_w("Failed to load aquarium state, initializing with default values");
      initializeFish();
    }
  }

  void saveState() {
    aquariumStateManager.saveState(fishArray);
    log_i("Aquarium state saved");
  }

  void periodicSave() {
    unsigned long currentTime = millis();
    if (currentTime - lastSaveTime >= AQUARIUM_SAVE_INTERVAL*60000) {
      aquariumStateManager.saveState(fishArray);
      lastSaveTime = currentTime;
    }
  }

  void ensureMinimumFishPopulation() {
    while (fishArray.size() > NUM_FISH_IDEAL) {
      fishArray.pop_back();
    }
    while (fishArray.size() < NUM_FISH_START) {
      fishArray.emplace_back(std::make_unique<Fish>(
          matrix, safeSpawnPosition(fishArray.size()),
          initialCreatureAge(fishArray.size()), curatedCreatureType(fishArray.size())));
    }
  }

  // Initialize fish and store them in a vector of unique pointers
  void initializeFish() {
    fishArray.clear();
    for (int i = 0; i < NUM_FISH_START; i++) {
      fishArray.emplace_back(
          std::make_unique<Fish>(matrix, safeSpawnPosition(i),
                                 initialCreatureAge(i),
                                 curatedCreatureType(i)));
    }
  }

  // Initialize plants and store them in a vector of unique pointers
  void initializePlants() {
    for (int i = 0; i < NUM_PLANTS; i++) {
      plantArray.emplace_back(std::make_unique<Plants>(
          matrix, matrix->getXResolution() * i / NUM_PLANTS,
          matrix->getYResolution() + 7));
    }
  }

  void addFood(float x = -1) {
    if (x < 0) {
      x = random(0, matrix->getXResolution());
    }
    foodArray.emplace_back(std::make_unique<Food>(matrix, x));

    float minDistance = std::numeric_limits<float>::max();
    Fish* closestFish = nullptr;

    for (const auto& fish : fishArray) {
      if (fish->getFood() == nullptr && fish->getAge() > AGE_EGG &&
          fish->getAge() < AGE_SENIOR) {
        float distance = fish->getPosition().dist(PVector(x, 0));
        if (distance < minDistance) {
          minDistance = distance;
          closestFish = fish.get();
        }
      }
    }

    if (closestFish) {
      closestFish->setFood(foodArray.back().get());
    }
  }

  uint8_t activeFoodCount() {
    uint8_t count = 0;
    for (const auto& food : foodArray) {
      if (food && !food->isOffScreen() && !food->isEaten()) {
        count++;
      }
    }
    return count;
  }

  unsigned long randomDelayBetween(unsigned long minDelay,
                                   unsigned long maxDelay) {
    if (maxDelay <= minDelay) {
      return minDelay;
    }
    return minDelay + static_cast<unsigned long>(
                          random(0, static_cast<long>(maxDelay - minDelay + 1)));
  }

  void scheduleNextAutonomousFood(bool bootWindow = false) {
#if CYD_AUTONOMOUS_LIFE
    lastAutonomousFoodTime = millis();
    nextAutonomousFoodDelay =
        bootWindow ? randomDelayBetween(CYD_AUTONOMOUS_BOOT_MIN_MS,
                                        CYD_AUTONOMOUS_BOOT_MAX_MS)
                   : randomDelayBetween(CYD_AUTONOMOUS_FOOD_MIN_MS,
                                        CYD_AUTONOMOUS_FOOD_MAX_MS);
#else
    (void)bootWindow;
#endif
  }

  float autonomousFoodX() {
    const uint8_t width = matrix->getXResolution();
    if (width <= 8) {
      return random(0, width);
    }
    return random(4, width - 4);
  }

  void handleAutonomousLife() {
#if CYD_AUTONOMOUS_LIFE
    if (nextAutonomousFoodDelay == 0) {
      scheduleNextAutonomousFood(true);
    }

    const unsigned long now = millis();
    if (now - lastAutonomousFoodTime < nextAutonomousFoodDelay) {
      return;
    }

    if (activeFoodCount() < CYD_AUTONOMOUS_MAX_FOOD) {
      addFood(autonomousFoodX());
    }
    scheduleNextAutonomousFood(false);
#endif
  }

  float currentHumidity() {
    if (demoMode) {
      return demoHumidity;
    }
#if defined(FIXED_ENVIRONMENT)
    return CYD_FIXED_HUMIDITY_PERCENT;
#else
    return scd40->isFirstReadingReceived() ? scd40->getHumidity() : 50;
#endif
  }

  float currentTemperature() {
    if (demoMode) {
      return demoTemperature;
    }
#if defined(FIXED_ENVIRONMENT)
    return DEFAULT_TEMPERATURE_VALUE;
#else
    return scd40->isFirstReadingReceived() ? scd40->getTemperature()
                                           : DEFAULT_TEMPERATURE_VALUE;
#endif
  }

  long currentCO2() {
    if (demoMode) {
      return static_cast<long>(demoCO2);
    }
#if defined(FIXED_ENVIRONMENT)
    return static_cast<long>(DEFAULT_CO2_VALUE);
#else
    return scd40->isFirstReadingReceived()
               ? static_cast<long>(scd40->getCO2())
               : static_cast<long>(DEFAULT_CO2_VALUE);
#endif
  }

  // Update all plants in the aquarium
  void updatePlants() {
    float humidity = currentHumidity();
    for (auto& plant : plantArray) {
      plant->update(humidity);
    }
  }

  // Update the water environment
  void updateWater() {
    water.update(currentTemperature());
  }

  // Update all fish in the aquarium
  void updateFish() {
    long co2 = currentCO2();
    const bool keepCreaturesVisible =
        demoMode || (CYD_KEEP_CREATURES_ON_SCREEN != 0);
    for (auto it = fishArray.begin(); it != fishArray.end();) {
      bool destroy = (*it)->update(co2, keepCreaturesVisible);
      if (destroy) {
        it = fishArray.erase(it);
      } else {
        (*it)->display();
        ++it;
      }
    }

    // Population control
    if (fishArray.size() < NUM_FISH_IDEAL) {
      // Attempt to create new fish
      for (auto& fish : fishArray) {
        if (fish->tryReproduce()) {
          PVector newPos = fish->getPosition();
          fishArray.emplace_back(std::make_unique<Fish>(matrix, newPos));
          break;  // Only add one fish per update cycle
        }
      }
    }
  }

  void updateFood() {
    for (auto it = foodArray.begin(); it != foodArray.end();) {
      (*it)->update();
      if ((*it)->isOffScreen() || (*it)->isEaten()) {
        it = foodArray.erase(it);
      } else {
        (*it)->display();
        ++it;
      }
    }
  }

  void handleTouchInput() {
    // Continue adding food while touch is active
    if (touchActive) {
      unsigned long currentTime = millis();
      if (currentTime - lastFoodTime >= FOOD_INTERVAL) {
        addFood();
        lastFoodTime = currentTime;
      }
    }
  }

  // Called externally when touch is detected on pin 13 (and menu is not open)
  void onTouchStarted() {
    if (!touchActive) {
      touchActive = true;
      addFood();
      lastFoodTime = millis();
    }
  }

  // Called externally when touch is released on pin 13
  void onTouchReleased() {
    touchActive = false;
  }

  void updateSensorData(bool showSensorData) {
    if (showSensorData && !demoMode) {
      if (scd40->isFirstReadingReceived()) {
        float temperature = scd40->getTemperature();
        float humidity = scd40->getHumidity();
        float co2 = scd40->getCO2();

        if (stateManager->getState()->temperatureUnit == TemperatureUnit::FAHRENHEIT) {
            // Convert to Fahrenheit
              temperature = stateManager->getState()->environment.temperature_fahrenheit.value;
              snprintf(buffer, sizeof(buffer),
                  "%s\nTemp: %.1f F\nHumidity: %.0f %%\nCO2: %.0f ppm", "",
                  temperature, humidity, co2);
          } else {
              snprintf(buffer, sizeof(buffer),
                  "%s\nTemp: %.1f C\nHumidity: %.0f %%\nCO2: %.0f ppm", "",
                  temperature, humidity, co2);
          }
        
        drawMultilineText(matrix->foreground, buffer, MIDDLE,
                          TextAlignment::CENTER, &Font4x7Fixed,
                          CRGB(150, 150, 150));
      } else {
        drawMultilineText(matrix->foreground, "Sensors\nWarming Up...", MIDDLE,
                          TextAlignment::CENTER, &Font4x7Fixed,
                          CRGB(150, 150, 150));
      }
    }
  }

  // General update function that updates all components of the aquarium
  void update(bool showSensorData = false) {
    handleTouchInput();
    handleAutonomousLife();

    if (demoMode) {
      updateDemo();
    } else {
      updateWater();
      boidManager.updateBoids(currentCO2());
      boidManager.renderBoids();
      updateFish();
      updateFood();
      updatePlants();
      updateSensorData(showSensorData);
      periodicSave();
    }
  }

  void display() {
    matrix->compositeLayers();
  }

  // Destructor to clean up resources
  ~Aquarium() {
    // Unique pointers automatically clean up
  }

  void drawMultilineText(GFX_Layer* layer, const char* text,
                         textPosition textPos, TextAlignment alignment,
                         const GFXfont* f, CRGB color) {
    layer->setFont(f);
    layer->setTextColor(layer->color565(color.r, color.g, color.b));

    // Split text into lines
    std::vector<String> lines;
    String currentLine;
    for (const char* c = text; *c; ++c) {
      if (*c == '\n') {
        lines.push_back(currentLine);
        currentLine = "";
      } else {
        currentLine += *c;
      }
    }
    if (!currentLine.isEmpty()) {
      lines.push_back(currentLine);
    }

    // Calculate total height and maximum width
    int16_t x1, y1;
    uint16_t w, h, maxWidth = 0, totalHeight = 0;
    for (const String& line : lines) {
      layer->getTextBounds(line.c_str(), 0, 0, &x1, &y1, &w, &h);
      maxWidth = max(maxWidth, w);
      totalHeight += h;
    }

    // Calculate starting Y position
    int16_t startY;
    if (textPos == TOP) {
      startY = h;
    } else if (textPos == BOTTOM) {
      startY = layer->getHeight() - totalHeight;
    } else {  // MIDDLE
      startY = (layer->getHeight() - totalHeight) / 2 + h;
    }

    // Draw each line
    for (const String& line : lines) {
      int16_t lineX;
      layer->getTextBounds(line.c_str(), 0, 0, &x1, &y1, &w, &h);

      switch (alignment) {
        case TextAlignment::LEFT:
          lineX = 0;
          break;
        case TextAlignment::CENTER:
          lineX = (layer->getWidth() - w) / 2;
          break;
        case TextAlignment::RIGHT:
          lineX = layer->getWidth() - w;
          break;
      }

      layer->setCursor(lineX, startY);
      layer->print(line);
      startY += h;
    }
  }
};



#endif  // AQUARIUM_H
