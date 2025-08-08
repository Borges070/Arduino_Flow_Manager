// Definições de pinos
const int BOIA_PIN = 7;           // Sensor para detectar o nível de água (reed switch)
const int VALVE_PIN = 9;          // Relé para controlar a válvula
const int USER_INPUT_NORMAL_PIN = 10; // Entrada do usuário para o modo "Normal"
const int USER_INPUT_EXTRA_PIN = 11;  // Entrada do usuário para o modo "Extra"
const int LED_PIN = 13;           // LED para indicar o status do sistema
const int LED_NORMAL = 4;         // LED indicar modo "Normal"
const int LED_EXTRA = 6;          // LED indicar modo "Extra"

// Variáveis de estado do sistema
int flowCount = 0;                  // Rastrea o número de fluxos detectados
bool saveOnSecondFlow = false;      // Economiza no 2º fluxo flag
bool saveOnThirdFlow = false;       // Economiza no 3º fluxo flag
int currentMode = 0;                // 0: Nenhum, 1: Normal, 2: Extra, 3: Ambos, 4: Failsafe

// Variáveis para controle de economia
bool economiaAtiva = false;
bool aguardandoTerceiroFluxo = false;   // NOVA VARIÁVEL: Sinaliza que já economizou no 2º e aguarda o 3º
bool economiaNoTerceiroFluxoAtiva = false; // Sinaliza que a economia do terceiro fluxo está ativa

// Debounce e detecção de transição do sensor boia
int stableBoiaState = LOW;          // Estado estável atual do sensor boia
int lastBoiaReading = LOW;          // Última leitura do sensor boia
bool boiaPreviouslyHigh = false;    // Flag para controlar a transição LOW -> HIGH
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 1000;

// Variáveis para evitar flood no Serial (isso ajuda BASTANTE)
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
  Serial.println(F("🔧 SISTEMA DE ECONOMIA DE ÁGUA INICIADO"));
  Serial.println(F("▶ Válvula: ABERTA"));
  Serial.println(F("▶ LED Principal: LIGADO"));
  Serial.println(F("▶ Aguardando seleção de modo pelo usuário..."));
  Serial.println(F("====================================\n"));
}

void loop() {
  lidarComEntradaUsuario();

  if (economiaAtiva) {
    if (!lastEconomiaAtivaPrinted) {
      Serial.print("⏳ [");
      Serial.print(millis() / 1000);
      Serial.println("s] Economia ativada. Válvula FECHADA até reservatório esvaziar.");
      lastEconomiaAtivaPrinted = true;
    }

    int boiaAtual = digitalRead(BOIA_PIN);
    if (boiaAtual == LOW) {
      Serial.println(F("✅ Reservatório esvaziou. Economia finalizada."));
      
      // Se a economia era do segundo fluxo e estamos no modo 3, aguardamos o terceiro fluxo
      if (currentMode == 3 && flowCount == 2 && !economiaNoTerceiroFluxoAtiva) {
        Serial.println(F("⏳ Aguardando próximo fluxo antes de resetar (modo NORMAL + EXTRA)."));
        aguardandoTerceiroFluxo = true;
        economiaAtiva = false; // Desativa a economia para permitir o próximo fluxo
        digitalWrite(VALVE_PIN, HIGH); // Abre a válvula
        digitalWrite(LED_PIN, HIGH);   // Acende o LED principal
      } else {
        // Se a economia era do terceiro fluxo (no modo 3) ou qualquer outra economia, reseta
        resetSystem();
      }
    }
  } else {
    monitorarSensorBoia();
    lastEconomiaAtivaPrinted = false;
  }
  
  // A verificação de reset para o modo 3 precisa ocorrer após a economia do terceiro fluxo.
  // Isso foi movido para dentro de lidarComReservatorioCheio() para um controle mais preciso.
  
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
    resetSystem(); // Sempre reseta ao mudar o modo para garantir um estado limpo

    Serial.println(F("\n================ MODO ALTERADO ================"));
    switch (currentMode) {
      case 0:
        saveOnSecondFlow = false;
        saveOnThirdFlow = false;
        digitalWrite(LED_NORMAL, LOW);
        digitalWrite(LED_EXTRA, LOW);
        Serial.println(F("ℹ️ Nenhum modo selecionado. Sistema desativado."));
        break;
      case 1:
        saveOnSecondFlow = true;
        saveOnThirdFlow = false;
        digitalWrite(LED_NORMAL, HIGH);
        digitalWrite(LED_EXTRA, LOW);
        Serial.println(F("✅ MODO NORMAL: Economiza no 2º fluxo."));
        break;
      case 2:
        saveOnSecondFlow = false;
        saveOnThirdFlow = true;
        digitalWrite(LED_NORMAL, LOW);
        digitalWrite(LED_EXTRA, HIGH);
        Serial.println(F("✅ MODO EXTRA: Economiza no 3º fluxo."));
        break;
      case 3:
        saveOnSecondFlow = true;
        saveOnThirdFlow = true;
        digitalWrite(LED_NORMAL, HIGH);
        digitalWrite(LED_EXTRA, HIGH);
        Serial.println(F("✅ MODO NORMAL + EXTRA: Economiza no 2º e 3º fluxo."));
        break;
      case 4:
        saveOnSecondFlow = false;
        saveOnThirdFlow = false;
        digitalWrite(LED_NORMAL, LOW);
        digitalWrite(LED_EXTRA, LOW);
        Serial.println(F("⚠️ MODO 0: Sistema desativado.")); // Antigo "Failsafe", mas 0 já é desativado
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
        // Não precisamos da lógica de reset do terceiro fluxo aqui, pois lidarComReservatorioCheio
        // vai chamar resetSystem no momento certo para o modo 3.
      } else if (stableBoiaState == LOW) {
        if (economiaAtiva) {
          Serial.println(F("💧 Sensor indica que reservatório está vazio (LOW)."));
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
    Serial.print("s] Novo fluxo detectado: ");
    Serial.println(flowCount);
    lastFlowCountPrinted = flowCount;
  }

  bool shouldSave = false;
  // Lógica para modo 3 (Economiza no 2º e 3º fluxo)
  if (currentMode == 3) {
    if (flowCount == 2 && saveOnSecondFlow) {
      shouldSave = true;
      economiaNoTerceiroFluxoAtiva = false; // Garante que a flag esteja correta
    } else if (flowCount == 3 && saveOnThirdFlow && aguardandoTerceiroFluxo) {
      shouldSave = true;
      economiaNoTerceiroFluxoAtiva = true; // Ativa a flag para indicar economia no terceiro fluxo
    }
  } 
  // Lógica para outros modos (Normal e Extra)
  else if (currentMode == 1 && flowCount == 2 && saveOnSecondFlow) {
    shouldSave = true;
  } else if (currentMode == 2 && flowCount == 3 && saveOnThirdFlow) {
    shouldSave = true;
  }
  
  if (currentMode == 0 || currentMode == 4) { // Modos desativados
    shouldSave = false;
  }

  if (shouldSave) {
    Serial.print("🚫 Fluxo ");
    Serial.print(flowCount);
    Serial.println(" requer economia. Válvula fechando.");
    digitalWrite(VALVE_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    economiaAtiva = true;
    aguardandoTerceiroFluxo = false; // Zera essa flag quando a economia é ativada
  } else {
    // Se a válvula estiver fechada e não houver economia ativa, reabrir
    if (digitalRead(VALVE_PIN) == LOW && !economiaAtiva) {
      digitalWrite(VALVE_PIN, HIGH);
      digitalWrite(LED_PIN, HIGH);
      Serial.println(F("🔓 Válvula reaberta. Sem economia ativa."));
    }
    Serial.print("➕ Fluxo ");
    Serial.print(flowCount);
    Serial.println(" não requer economia.");
  }
}

void resetSystem() {
  flowCount = 0;
  economiaAtiva = false;
  digitalWrite(VALVE_PIN, HIGH); // Válvula aberta
  digitalWrite(LED_PIN, HIGH);   // Led ligado
  lastFlowCountPrinted = -1;
  lastEconomiaAtivaPrinted = false;
  boiaPreviouslyHigh = false;
  aguardandoTerceiroFluxo = false; 
  economiaNoTerceiroFluxoAtiva = false; 

  Serial.println(F("\n🔄 Sistema resetado: contadores zerados, válvula aberta, LED ligado.\n"));
}