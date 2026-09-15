#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ========== PINS ==========
#define DHTPIN 15
#define DHTTYPE DHT22
#define LED_BUILTIN 2

// ========== OBJETS ==========
DHT dht(DHTPIN, DHTTYPE);
Adafruit_MPU6050 mpu;
WiFiClient espClient;
PubSubClient client(espClient);

// ========== CONFIGURATION ==========
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqttServer = "broker.emqx.io";
const int mqttPort = 1883;
const char* mqttTopicPublish = "health/patient1/sensors";
const char* mqttTopicSubscribe = "health/patient1/commands";

// ========== VARIABLES ==========
unsigned long lastTime = 0;
bool mpuOK = false;
int scenario = 0;
unsigned long lastLedBlink = 0;
unsigned long lastReconnect = 0;

struct ScenarioData {
  float bpm, spo2, temperature, humidity;
  float accel_x, accel_y, accel_z;
  bool chute;
  const char* description;
};

/*
 * ========== TABLEAU DES SCENARIOS ==========
 * Chaque scenario simule un état clinique différent du patient.
 * Format : { bpm, spo2, temp(°C), humidity(%), accel_x, accel_y, accel_z, chute, description }
 *
 * SCENARIO 1 - NORMAL
 *   BPM=75   (plage normale : 60–100 bpm)
 *   SpO2=98% (normal > 95%)
 *   Temp=37.0°C | Humidité=55%
 *   Accélération repos : x=0.1, y=-0.1, z=9.8 m/s² (gravité terrestre ~9.8)
 *   Chute=false
 *
 * SCENARIO 2 - BPM ELEVÉ
 *   BPM=105  (légèrement tachycarde, alerte > 100 bpm)
 *   SpO2=97% | Temp=37.0°C | Humidité=55%
 *   Accélération identique au repos
 *   Chute=false
 *
 * SCENARIO 3 - BPM CRITIQUE
 *   BPM=130  (tachycardie sévère, critique > 120 bpm)
 *   SpO2=96% | Temp=37.0°C | Humidité=55%
 *   Accélération identique au repos
 *   Chute=false
 *
 * SCENARIO 4 - SPO2 CRITIQUE
 *   BPM=75   (normal)
 *   SpO2=87% (hypoxémie critique < 90%, urgence médicale)
 *   Temp=37.0°C | Humidité=55%
 *   Accélération identique au repos
 *   Chute=false
 *
 * SCENARIO 5 - CHUTE DÉTECTÉE
 *   BPM=75   (normal)
 *   SpO2=97% (normal)
 *   Temp=37.0°C | Humidité=55%
 *   Accélération chute : x=8.5, y=6.2, z=2.1
 *     -> magnitude ≈ sqrt(8.5²+6.2²+2.1²) ≈ 10.6 (seuil chute > 8.0 g)
 *     -> z faible (2.1) indique que le capteur n'est plus vertical = patient au sol
 *   Chute=true
 */
ScenarioData scenarios[] = {
  {75,  98, 37.0, 55, 0.1, -0.1,  9.8, false, "NORMAL"},
  {105, 97, 37.0, 55, 0.1, -0.1,  9.8, false, "BPM ELEVE"},
  {130, 96, 37.0, 55, 0.1, -0.1,  9.8, false, "BPM CRITIQUE"},
  {75,  87, 37.0, 55, 0.1, -0.1,  9.8, false, "SPO2 CRITIQUE"},
  {75,  97, 37.0, 55, 8.5,  6.2,  2.1, true,  "CHUTE DETECTEE"}
};

/*
 * ========== VALEURS INITIALES DES VARIABLES SIMULÉES ==========
 * Ces valeurs sont le point de départ du scénario NORMAL (scénario 1).
 * Elles évoluent dynamiquement via generateSimulatedData().
 *
 * virtual_bpm      = 75.0  bpm  (repos normal)
 * virtual_spo2     = 97.0  %    (saturation normale > 95%)
 * virtual_temperature = 37.0 °C (température corporelle normale)
 * virtual_humidity = 55.0  %    (humidité ambiante confortable)
 * virtual_accel_x  =  0.1  g    (légère inclinaison axe X au repos)
 * virtual_accel_y  = -0.1  g    (légère inclinaison axe Y au repos)
 * virtual_accel_z  =  9.8  m/s² (gravité terrestre, patient debout/couché stable)
 */
float virtual_bpm = 75.0, virtual_spo2 = 97.0;
float virtual_temperature = 37.0, virtual_humidity = 55.0;
float virtual_accel_x = 0.1, virtual_accel_y = -0.1, virtual_accel_z = 9.8;

// ========== LECTURE CAPTEURS REELS ==========
float readRealTemperature() {
  float t = dht.readTemperature();
  if (isnan(t)) return virtual_temperature;
  return t;
}

float readRealHumidity() {
  float h = dht.readHumidity();
  if (isnan(h)) return virtual_humidity;
  return h;
}

void readRealAccel(float &ax, float &ay, float &az) {
  if (mpuOK) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    // Conversion m/s² -> g (divisé par 9.8) pour normaliser avec les valeurs simulées
    ax = a.acceleration.x / 9.8;
    ay = a.acceleration.y / 9.8;
    az = a.acceleration.z / 9.8;
  } else {
    ax = virtual_accel_x;
    ay = virtual_accel_y;
    az = virtual_accel_z;
  }
}

// ========== DETECTION CHUTE ==========
/*
 * Algorithme en deux phases :
 * Phase 1 - Chute libre : magnitude < 3.0 g  (le patient est en l'air, quasi apesanteur)
 * Phase 2 - Impact      : magnitude > 8.0 g  (choc à l'atterrissage détecté)
 * Une chute n'est confirmée que si les deux phases se succèdent.
 */
bool detectFall(float ax, float ay, float az) {
  float magnitude = sqrt(ax*ax + ay*ay + az*az);
  static bool falling = false;
  
  if (magnitude < 3.0 && !falling) {  // Seuil chute libre : < 3.0 g
    falling = true;
    return false;
  }
  if (falling && magnitude > 8.0) {   // Seuil impact : > 8.0 g
    falling = false;
    return true;
  }
  return false;
}

// ========== GENERATION DONNEES SIMULÉES ==========
/*
 * Ajoute une variation aléatoire aux valeurs pour simuler un signal réaliste.
 * Plages autorisées après variation :
 *   BPM         : 60  – 100  bpm   (repos normal, pas de tachycardie simulée ici)
 *   SpO2        : 94  – 100  %
 *   Température : 36.0 – 37.5 °C
 *   Humidité    : 50.0 – 60.0 %
 *   Accel X/Y   : -0.5 – +0.5 g   (légères oscillations de marche/repos)
 *   Accel Z     :  9.5 – 10.5 m/s² (autour de la gravité terrestre)
 */
void generateSimulatedData() {
  virtual_bpm += random(-3, 4);       // Variation BPM : ±3 bpm par cycle
  virtual_spo2 += random(-2, 3);      // Variation SpO2 : ±2% par cycle
  if (virtual_bpm < 60) virtual_bpm = 60;
  if (virtual_bpm > 100) virtual_bpm = 100;
  if (virtual_spo2 < 94) virtual_spo2 = 94;
  if (virtual_spo2 > 100) virtual_spo2 = 100;

  virtual_temperature += random(-5, 6) / 10.0;   // Variation temp : ±0.5°C par cycle
  if (virtual_temperature < 36.0) virtual_temperature = 36.0;
  if (virtual_temperature > 37.5) virtual_temperature = 37.5;

  virtual_humidity += random(-10, 15) / 10.0;    // Variation humidité : ±1.0–1.5% par cycle
  if (virtual_humidity < 50.0) virtual_humidity = 50.0;
  if (virtual_humidity > 60.0) virtual_humidity = 60.0;

  virtual_accel_x += random(-5, 6) / 100.0;      // Variation accel X : ±0.05 g par cycle
  virtual_accel_y += random(-5, 6) / 100.0;      // Variation accel Y : ±0.05 g par cycle
  virtual_accel_z += random(-3, 4) / 100.0;      // Variation accel Z : ±0.03–0.04 m/s² par cycle
  if (virtual_accel_x < -0.5) virtual_accel_x = -0.5;
  if (virtual_accel_x > 0.5) virtual_accel_x = 0.5;
  if (virtual_accel_y < -0.5) virtual_accel_y = -0.5;
  if (virtual_accel_y > 0.5) virtual_accel_y = 0.5;
  if (virtual_accel_z < 9.5) virtual_accel_z = 9.5;
  if (virtual_accel_z > 10.5) virtual_accel_z = 10.5;
}

// ========== TRAITEMENT COMMANDES RECUES ==========
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];

  Serial.println("\n===================================");
  Serial.print("📩 Message recu ! Topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  Serial.println(message);

  StaticJsonDocument<200> doc;
  if (!deserializeJson(doc, message)) {
    if (doc.containsKey("command")) {
      String cmd = doc["command"];
      if (cmd == "ALERT") {
        Serial.println("🔴🔴🔴 ALERTE DU MEDECIN ! 🔴🔴🔴");
        for (int i = 0; i < 10; i++) {
          digitalWrite(LED_BUILTIN, HIGH);
          delay(100);
          digitalWrite(LED_BUILTIN, LOW);
          delay(100);
        }
        client.publish("health/patient1/response", "ALERT_RECEIVED");
      } else if (cmd == "STATUS") {
        Serial.println("📊 Commande: Demande de status");
        String statusMsg = "ESP32_OK_SCENARIO_" + String(scenario + 1);
        client.publish("health/patient1/response", statusMsg.c_str());
      }
    }
  } else {
    Serial.println("❌ Erreur parsing JSON");
  }
  Serial.println("===================================\n");
}

// ========== CONNEXION WiFi ==========
void connectWiFi() {
  Serial.print("Connexion WiFi");
  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ✅");
    Serial.print("IP : ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" ❌ ECHEC");
  }
}

// ========== CONNEXION MQTT ==========
void connectMQTT() {
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  int tries = 0;
  while (!client.connected() && tries < 5) {
    Serial.print("Connexion MQTT...");
    String clientId = "ESP32-TeleHealth-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println(" ✅ Connecte !");
      if (client.subscribe(mqttTopicSubscribe)) {
        Serial.print("✅ Abonne au topic: ");
        Serial.println(mqttTopicSubscribe);
      }
      client.publish("health/patient1/status", "ESP32_STARTED");
    } else {
      Serial.print(" ❌ ECHEC rc=");
      Serial.println(client.state());
      delay(2000);
      tries++;
    }
  }
}

// ========== PUBLICATION DES DONNEES ==========
void publishData() {
  if (!client.connected()) {
    digitalWrite(LED_BUILTIN, LOW);
    Serial.println("MQTT deconnecte, tentative de reconnexion...");
    connectMQTT();
    if (!client.connected()) return;
  }
  
  digitalWrite(LED_BUILTIN, HIGH);

  ScenarioData s = scenarios[scenario];
  float realTemp = readRealTemperature();
  float realHum = readRealHumidity();
  float realAx, realAy, realAz;
  readRealAccel(realAx, realAy, realAz);

  // Scénario 1 uniquement : on remplace les valeurs statiques par les données
  // dynamiques (simulées + capteurs réels DHT22 et MPU6050 si disponibles)
  if (scenario == 0) {
    generateSimulatedData();
    s.bpm         = virtual_bpm;
    s.spo2        = virtual_spo2;
    s.temperature = realTemp;       // Température lue du DHT22 (ou virtuelle si erreur)
    s.humidity    = realHum;        // Humidité lue du DHT22 (ou virtuelle si erreur)
    s.accel_x     = realAx;         // Accélération X du MPU6050 (ou virtuelle si absent)
    s.accel_y     = realAy;
    s.accel_z     = realAz;
    s.chute       = detectFall(realAx, realAy, realAz);
  }

  StaticJsonDocument<300> doc;
  doc["bpm"]       = (int)s.bpm;
  doc["spo2"]      = (int)s.spo2;
  doc["temp"]      = s.temperature;
  doc["humidity"]  = s.humidity;
  doc["chute"]     = s.chute;
  doc["scenario"]  = scenario + 1;
  doc["accel_x"]   = s.accel_x;
  doc["accel_y"]   = s.accel_y;
  doc["accel_z"]   = s.accel_z;
  doc["timestamp"] = millis();      // Temps en ms depuis le démarrage de l'ESP32
  
  String payload;
  serializeJson(doc, payload);

  Serial.println("==========================================");
  Serial.print("Scenario ");
  Serial.print(scenario + 1);
  Serial.print("/5 : ");
  Serial.println(s.description);
  Serial.print("   BPM=");
  Serial.print(s.bpm, 0);
  Serial.print(" | SpO2=");
  Serial.print(s.spo2, 0);
  Serial.print("% | Temp=");
  Serial.print(s.temperature, 1);
  Serial.print("°C | Chute=");
  Serial.println(s.chute ? "OUI" : "NON");
  Serial.print("   Payload: ");
  Serial.println(payload);

  if (client.publish(mqttTopicPublish, payload.c_str())) {
    Serial.println("✅ MQTT publie avec succes !");
  } else {
    Serial.println("❌ Erreur publication MQTT !");
  }
  Serial.println("==========================================\n");

  scenario = (scenario + 1) % 5;
  
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
}

// ========== LED STATUS (clignotement) ==========
void updateLED() {
  if (client.connected()) {
    if (millis() - lastLedBlink > 5000) {   // Clignote toutes les 5 secondes si MQTT connecté
      lastLedBlink = millis();
      digitalWrite(LED_BUILTIN, HIGH);
      delay(50);
      digitalWrite(LED_BUILTIN, LOW);
    }
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  randomSeed(analogRead(0));
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println("\n==========================================");
  Serial.println("🏥 Smart-TeleHealth - ESP32 (Version Corrigee)");
  Serial.println("==========================================\n");

  Serial.print("Initialisation DHT22... ");
  dht.begin();
  delay(500);
  Serial.println("✅");

  Serial.print("Initialisation MPU6050... ");
  if (mpu.begin()) {
    mpuOK = true;
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);   // Plage ±8g (adapté détection chute)
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);        // Plage ±500°/s (mouvements rapides)
    Serial.println("✅");
  } else {
    Serial.println("⚠️ (Mode simulation)");
  }

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }

  Serial.println("\n✅ Systeme pret !");
  Serial.print("📡 MQTT Publish: ");
  Serial.println(mqttTopicPublish);
  Serial.print("📡 MQTT Subscribe: ");
  Serial.println(mqttTopicSubscribe);
  Serial.println("⏱️ Envoi toutes les 8 secondes");
  Serial.println("💡 LED clignote toutes les 5 secondes si MQTT connecte\n");
}

// ========== LOOP ==========
void loop() {
  if (client.connected()) {
    client.loop();
    updateLED();
  } else if (WiFi.status() == WL_CONNECTED) {
    if (millis() - lastReconnect > 5000) {  // Tentative de reconnexion MQTT toutes les 5s
      lastReconnect = millis();
      connectMQTT();
    }
  }

  if (millis() - lastTime >= 8000) {  // Publication toutes les 8 secondes
    lastTime = millis();
    publishData();
  }

  delay(10);
}