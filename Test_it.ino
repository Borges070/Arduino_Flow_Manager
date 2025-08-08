// Pin definitions
const int BOIA_PIN = 7;           // Sensor to detect water level (reed switch)
const int VALVE_PIN = 9;          // Relay to control the valve
const int USER_INPUT_NORMAL_PIN = 10; // User input for "Normal" mode
const int USER_INPUT_EXTRA_PIN = 11;  // User input for "Extra" mode
const int LED_PIN = 13;           // LED to indicate system status
const int LED_NORMAL = 4;         // LED to indicate "Normal" mode
const int LED_EXTRA = 6;          // LED to indicate "Extra" mode

// System state variables
int flowCount = 0;                  // Tracks the number of detected flows
bool saveOnSecondFlow = false;      // Save water on the 2nd flow flag
bool saveOnThirdFlow = false;       // Save water on the 3rd flow flag
int currentMode = 0;                // 0: None, 1: Normal, 2: Extra, 3: Both, 4: Failsafe

// Economy control variables
bool economiaAtiva = false;
bool aguardandoTerceiroFluxo = false;   // Indicates that 2nd flow saving occurred and 3rd flow is awaited
bool economiaNoTerceiroFluxoAtiva = false; // Indicates that the 3rd flow saving is active

// Debounce and transition detection for the float sensor
int stableBoiaState = LOW;          // Current stable state of the float sensor
int lastBoiaReading = LOW;          // Last reading from the float sensor
bool boiaPreviouslyHigh = false;    // Flag to control LOW -> HIGH transition
unsigned long lastDebounceTime = 0; // unsigned long is used to have more bits to hold more time within the milis function and the milis return a unsigned long value
const unsigned long debounceDelay = 1000;

// Variables to avoid flooding the Serial monitor (helps A LOT)
bool lastEconomiaAtivaPrinted = false;
int lastFlowCountPrinted = -1;

void setup() {
  pinMode(VALVE_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(USER_INPUT_NORMAL_PIN, INPUT);
  pinMode(USER_INPUT_EXTRA_PIN, INPUT);
  pinMode(BOIA_PIN, INPUT_PULLUP);
  pinMode(LED_NORMAL, OUTPUT);
  pinMode(LED_EXTRA, OUTPUT);

  Serial.begin(9600);
  digitalWrite(VALVE_PIN, HIGH);    
  digitalWrite(LED_PIN, HIGH);      
  digitalWrite(LED_NORMAL, LOW);
  digitalWrite(LED_EXTRA, LOW);

  Serial.println(F("\n===================================="));
  Serial.println(F("🔧 WATER SAVING SYSTEM STARTED"));
  Serial.println(F("▶ Valve: OPEN"));
  Serial.println(F("▶ Main LED: ON"));
  Serial.println(F("▶ Waiting for user mode selection..."));
  Serial.println(F("====================================\n"));
}

void loop() {
  lidarComEntradaUsuario();

  if (economiaAtiva) {
    if (!lastEconomiaAtivaPrinted) {
      Serial.print("⏳ [");
      Serial.print(millis() / 1000);
      Serial.println("s] Economy activated. Valve CLOSED until reservoir empties.");
      lastEconomiaAtivaPrinted = true;
    }

    int boiaAtual = digitalRead(BOIA_PIN);
    if (boiaAtual == LOW) {
      Serial.println(F("✅ Reservoir emptied. Economy finished."));
      
      // If the saving was for the second flow and we are in mode 3, wait for the third flow
      if (currentMode == 3 && flowCount == 2 && !economiaNoTerceiroFluxoAtiva) {
        Serial.println(F("⏳ Waiting for next flow before resetting (NORMAL + EXTRA mode)."));
        aguardandoTerceiroFluxo = true;
        economiaAtiva = false; // Disable saving to allow the next flow
        digitalWrite(VALVE_PIN, HIGH); // Open the valve
        digitalWrite(LED_PIN, HIGH);   // Turn on main LED
      } else {
        // If saving was for the third flow (in mode 3) or any other saving, reset
        resetSystem();
      }
    }
  } else {
    monitorarSensorBoia();
    lastEconomiaAtivaPrinted = false;
  }
  
  // Reset check for mode 3 must occur after the third flow saving.
  // This was moved into lidarComReservatorioCheio() for more precise control.
  
  delay(10);
}

void lidarComEntradaUsuario() {
  int inputNormal = digitalRead(USER_INPUT_NORMAL_PIN);
  int inputExtra = digitalRead(USER_INPUT_EXTRA_PIN);

  int newMode = 0;
  if (inputNormal == LOW && inputExtra == LOW) {
    newMode = 3;
  } else if (inputExtra == LOW) {
    newMode = 2;
  } else if (inputNormal == LOW ){
    newMode = 1;
  } else {
    newMode = 4;
  }

  if (newMode != currentMode) {
    currentMode = newMode;
    resetSystem(); // Always reset when changing mode to ensure a clean state

    Serial.println(F("\n================ MODE CHANGED ================"));
    switch (currentMode) {
      case 0:
        saveOnSecondFlow = false;
        saveOnThirdFlow = false;
        digitalWrite(LED_NORMAL, LOW);
        digitalWrite(LED_EXTRA, LOW);
        Serial.println(F("ℹ️ No mode selected. System deactivated."));
        break;
      case 1:
        saveOnSecondFlow = true;
        saveOnThirdFlow = false;
        digitalWrite(LED_NORMAL, HIGH);
        digitalWrite(LED_EXTRA, LOW);
        Serial.println(F("✅ NORMAL MODE: Saves on 2nd flow."));
        break;
      case 2:
        saveOnSecondFlow = false;
        saveOnThirdFlow = true;
        digitalWrite(LED_NORMAL, LOW);
        digitalWrite(LED_EXTRA, HIGH);
        Serial.println(F("✅ EXTRA MODE: Saves on 3rd flow."));
        break;
      case 3:
        saveOnSecondFlow = true;
        saveOnThirdFlow = true;
        digitalWrite(LED_NORMAL, HIGH);
        digitalWrite(LED_EXTRA, HIGH);
        Serial.println(F("✅ NORMAL + EXTRA MODE: Saves on 2nd and 3rd flow."));
        break;
      case 4:
        saveOnSecondFlow = false;
        saveOnThirdFlow = false;
        digitalWrite(LED_NORMAL, LOW);
        digitalWrite(LED_EXTRA, LOW);
        Serial.println(F("⚠️ MODE 0: System deactivated.")); // Former "Failsafe", but 0 is already deactivated
        break;
    }
    Serial.println(F("================================================\n"));
  }
}

void monitorarSensorBoia() {
  int currentReading = digitalRead(BOIA_PIN);

  if (currentReading != lastBoiaReading) {
    lastDebounceTime = millis();
    lastBoiaReading = currentReading;
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (stableBoiaState != currentReading) {
      stableBoiaState = currentReading;

      if (stableBoiaState == HIGH && !boiaPreviouslyHigh) {
        boiaPreviouslyHigh = true;
        lidarComReservatorioCheio();
        // We don't need the third flow reset logic here, because lidarComReservatorioCheio
        // will call resetSystem at the right moment for mode 3.
      } else if (stableBoiaState == LOW) {
        if (economiaAtiva) {
          Serial.println(F("💧 Sensor indicates reservoir is empty (LOW)."));
        }
        boiaPreviouslyHigh = false;
      }
    }
  }
}

void lidarComReservatorioCheio() {
  flowCount++;

  if (flowCount != lastFlowCountPrinted) {
    Serial.print("💦 [");
    Serial.print(millis() / 1000);
    Serial.print("s] New flow detected: ");
    Serial.println(flowCount);
    lastFlowCountPrinted = flowCount;
  }

  bool shouldSave = false;
  // Logic for mode 3 (Saves on 2nd and 3rd flow)
  if (currentMode == 3) {
    if (flowCount == 2 && saveOnSecondFlow) {
      shouldSave = true;
      economiaNoTerceiroFluxoAtiva = false; // Ensure flag is correct
    } else if (flowCount == 3 && saveOnThirdFlow && aguardandoTerceiroFluxo) {
      shouldSave = true;
      economiaNoTerceiroFluxoAtiva = true; // Activate flag to indicate saving on third flow
    }
  } 
  // Logic for modes Normal and Extra
  else if (currentMode == 1 && flowCount == 2 && saveOnSecondFlow) {
    shouldSave = true;
  } else if (currentMode == 2 && flowCount == 3 && saveOnThirdFlow) {
    shouldSave = true;
  }
  
  if (currentMode == 0 || currentMode == 4) { // Disabled modes
    shouldSave = false;
  }

  if (shouldSave) {
    Serial.print("🚫 Flow ");
    Serial.print(flowCount);
    Serial.println(" requires saving. Closing valve.");
    digitalWrite(VALVE_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    economiaAtiva = true;
    aguardandoTerceiroFluxo = false; // Reset this flag when saving is activated
  } else {
    // If the valve is closed and there is no active saving, reopen
    if (digitalRead(VALVE_PIN) == LOW && !economiaAtiva) {
      digitalWrite(VALVE_PIN, HIGH);
      digitalWrite(LED_PIN, HIGH);
      Serial.println(F("🔓 Valve reopened. No active saving."));
    }
    Serial.print("➕ Flow ");
    Serial.print(flowCount);
    Serial.println(" does not require saving.");
  }
}

void resetSystem() {
  flowCount = 0;
  economiaAtiva = false;
  digitalWrite(VALVE_PIN, HIGH); // Valve open
  digitalWrite(LED_PIN, HIGH);   // LED on
  lastFlowCountPrinted = -1; // Ensures that the next flow (0) prints!
  lastEconomiaAtivaPrinted = false; // Ensures the next cycle message will be printed
  boiaPreviouslyHigh = false;
  aguardandoTerceiroFluxo = false; 
  economiaNoTerceiroFluxoAtiva = false; 

  Serial.println(F("\n🔄 System reset: counters cleared, valve open, LED on.\n"));
}
