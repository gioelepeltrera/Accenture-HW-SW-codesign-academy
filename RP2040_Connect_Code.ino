/*

+-----------+---------------------------------------------------------+
| Operation |                    Description                          |
+-----------+---------------------------------------------------------+
|     0     | "I'm Alive" message sent by any node                    |
|     1     | "I'm the Master" declaration message                    |
|     2     | Temperature reading below the threshold sent by a slave |
|     3     | Temperature reading above the threshold sent by a slave |
|     4     | ACK message (Acknowledgement)                           |
|     5     | Ultimatum message: "Is the Master Alive?"               |
|     6     | "ATTENTION PHASE" broadcast by master                   |
|     7     | "STOP" message sent by master                           |
|     8     | "Normal" broadcast by master                            |
+-----------+---------------------------------------------------------+

*/

#include <Scheduler.h>
#include <Arduino_LSM6DSOX.h>
#include <WiFiNINA.h>
#include <MQTTPubSubClient_Generic.h>

const char ssid[] = "XXXXXXXXX";
const char pass[] = "XXXXXXXXX";


const char broker[] =   "XXX.XXX.XXX.XXX";
const char username[] = "XXXXXX";
const char password[] = "XXXXXX";
const int mqttPort = 8884;
WiFiClient wifiClient;
MQTTPubSubClient mqttClient;
WiFiUDP udp;

bool killed = false;
bool master = false;
bool masterSet = false;
IPAddress masterIP;
int lastTemperatureRead = 0;

const int localPort = 8888;
const IPAddress broadcastIP(192, 168, 26, 255);
const IPAddress defaultIP(255, 255, 255, 255);
const int remotePort = 8888;
int THRESHOLD = 40;
unsigned long lastPrintTime = millis();
unsigned long lastSentTime = 0;
unsigned long lastRecvTime = 0;
unsigned long samplingPeriod = 10000;
unsigned long NORMAL_samplingPeriod = 10000;
unsigned long ATTENTION_samplingPeriod = 5000;
IPAddress nodeAboveThreshold(255, 255, 255, 255);
bool timerTaStarted = false;
unsigned long timerTaStartTime;
const unsigned long ULTIMATUM_WAIT_TIME = 7500;
const unsigned long ACK_WAIT_TIME = 5000;
const unsigned long Ta = 20000;

struct UDPPacket {
  uint8_t operation : 4;
  IPAddress senderIP;
  unsigned long timestamp;
  int payload;
} packet;

struct TempReading {
  IPAddress nodeIP;
  int tempValue;
  TempReading *next;
};

TempReading *head = NULL;


void subscribeToThreshold();
void mqttCallback(char *topic, byte *payload, unsigned int length);
int computeDelayFromIP();
void sendUDPMessage(uint8_t operation, IPAddress targetIP, int payload);
void addReading(IPAddress node, int temp);
void printReadings();
void flushReadings();
void loop2();
void startTimerTa();
void attentionPhase(IPAddress node);
bool ensureMQTTConnection();


void setup() {
  Serial.begin(9600);

  // Connect to WiFi
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");

  udp.begin(localPort);
  while (!wifiClient.connect(broker, mqttPort)) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nConnected to broker!");

  mqttClient.begin(wifiClient);


  while (!mqttClient.connect(WiFi.localIP().toString(), username, password)) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("MQTT connected!");

  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1)
      ;
  }
  Serial.print("Node started. IP address: ");
  Serial.println(WiFi.localIP());

  Scheduler.startLoop(loop2);
  sendUDPMessage(0, broadcastIP, 0);  // Broadcast "I'm Alive" message
}

void loop() {
  if (killed) {
    Serial.println("Killed....");
    while (true)
      ;
  }
  if (IMU.temperatureAvailable()) {
    IMU.readTemperature(lastTemperatureRead);
  }

  if (master) {
    addReading(WiFi.localIP(), lastTemperatureRead);
    if (lastTemperatureRead > THRESHOLD && !timerTaStarted) {
      sendUDPMessage(6, broadcastIP, 0);
      attentionPhase(WiFi.localIP());
      startTimerTa();
      nodeAboveThreshold = WiFi.localIP();
    }

    if (millis() - lastPrintTime > 60000) {
      printReadings();
      flushReadings();
      lastPrintTime = millis();
      mqttClient.update();
    }

  } else if (masterSet && !master) {
    sendUDPMessage((lastTemperatureRead > THRESHOLD ? 3 : 2), masterIP, lastTemperatureRead);
    if (lastSentTime == 0) {
      lastSentTime = millis();
    }
    Serial.println("Temp sent to master...");
  }

  if (masterSet && !master && millis() - lastSentTime > ACK_WAIT_TIME) {
    if (millis() - lastSentTime > ACK_WAIT_TIME + ULTIMATUM_WAIT_TIME) {
      sendUDPMessage(1, broadcastIP, THRESHOLD);
      masterIP = WiFi.localIP();
      master = true;
      masterSet = true;
      Serial.println("No UltimatumACK received. I'm the master now.");
      subscribeToThreshold();
      lastSentTime = millis();
    } else {
      sendUDPMessage(5, masterIP, 0);
      Serial.println("Sent ultimatum Message...");
    }
  }

  if (master && timerTaStarted && millis() - timerTaStartTime > Ta) {
    timerTaStarted = false;
    samplingPeriod = NORMAL_samplingPeriod;
    faultPhase(nodeAboveThreshold);
    normalPhase(nodeAboveThreshold);

    if (nodeAboveThreshold != masterIP) {
      sendUDPMessage(7, nodeAboveThreshold, 0);
    } else {
      killed = true;
    }
    sendUDPMessage(8, broadcastIP, 0);
    nodeAboveThreshold = defaultIP;
  }

  delay(samplingPeriod);
}

void loop2() {
  if (killed) {
    Serial.println("Killed....");
    while (true)
      ;
  }
  int packetSize = udp.parsePacket();
  if (packetSize) {
    udp.read((uint8_t *)&packet, sizeof(packet));
    if (packet.timestamp != lastRecvTime) {
      lastRecvTime = packet.timestamp;
      switch (packet.operation) {
        case 0:
          if (master) {
            sendUDPMessage(1, packet.senderIP, THRESHOLD);
          }
          break;
        case 1:
          master = false;
          masterSet = true;
          masterIP = packet.senderIP;
          THRESHOLD = packet.payload;
          Serial.print("I'm not master... threshold set to: ");
          Serial.println(packet.payload);
          break;
        case 2:
          if (master) {
            addReading(packet.senderIP, packet.payload);
            if (timerTaStarted && packet.senderIP == nodeAboveThreshold) {
              timerTaStarted = false;
              sendUDPMessage(8, broadcastIP, 0);
              normalPhase(nodeAboveThreshold);
              samplingPeriod = NORMAL_samplingPeriod;
              nodeAboveThreshold = defaultIP;
            }
            sendUDPMessage(4, packet.senderIP, 0);
            Serial.println("ACK SENT");
          }
          break;
        case 3:
          if (master) {
            addReading(packet.senderIP, packet.payload);
            if (timerTaStarted == false) {
              nodeAboveThreshold = packet.senderIP;
              samplingPeriod = ATTENTION_samplingPeriod;
              startTimerTa();
              attentionPhase(packet.senderIP);
              sendUDPMessage(6, broadcastIP, 0);
            } else if (timerTaStarted && millis() - timerTaStartTime > Ta) {
              timerTaStarted = false;
              faultPhase(nodeAboveThreshold);
              normalPhase(nodeAboveThreshold);
              sendUDPMessage(7, nodeAboveThreshold, 0);
              sendUDPMessage(8, broadcastIP, 0);
              samplingPeriod = NORMAL_samplingPeriod;
              nodeAboveThreshold = defaultIP;
            }
            sendUDPMessage(4, packet.senderIP, 0);
            Serial.println("ACK SENT");
          }
          break;
        case 4:
          Serial.println("Received ack");
          lastSentTime = 0;
          break;
        case 5:
          if (master) {
            Serial.println("Received Ultimatum from a slave");
            sendUDPMessage(4, packet.senderIP, 0);
            Serial.println("ULTIMATUM ACK SENT");
          }
          break;
        case 6:
          samplingPeriod = ATTENTION_samplingPeriod;
          Serial.println("ATTENTION PHASE started. Adjusted sampling period.");
          break;
        case 7:
          Serial.println("Received STOP message. This node will now be killed.");
          killed = true;
          break;
        case 8:
          samplingPeriod = NORMAL_samplingPeriod;
          Serial.println("Received NORMAL message. Returning to normal phase.");
          break;
        default:
          break;
      }
    }
  }

  // Check for master declaration after random time
  if (!masterSet) {
    static unsigned long startTime = millis();
    static unsigned long randomTime = random(5000, 20000);

    if (!master && millis() - startTime > randomTime) {
      Serial.println("I'm the master");
      sendUDPMessage(1, broadcastIP, THRESHOLD);
      master = true;
      masterSet = true;
      masterIP = WiFi.localIP();
      subscribeToThreshold();
      mqttClient.update();
    }
  }

  delay(10);
}

int computeDelayFromIP() {
  int lastOctet = WiFi.localIP()[3];  // Extract the last octet of the IP address
  return (lastOctet % 10) * 10 + 10;  // Compute the delay
}

void sendUDPMessage(uint8_t operation, IPAddress targetIP, int payload) {
  delay(computeDelayFromIP());  // Add the delay
  packet.operation = operation;
  packet.payload = payload;
  packet.timestamp = millis();
  packet.senderIP = WiFi.localIP();
  udp.beginPacket(targetIP, remotePort);
  udp.write((uint8_t *)&packet, sizeof(packet));
  udp.endPacket();
}

void addReading(IPAddress node, int temp) {
  TempReading *newReading = new TempReading;
  newReading->nodeIP = node;
  newReading->tempValue = temp;
  newReading->next = head;
  head = newReading;
}

void printReadings() {
  Serial.print("Temperature Readings (threshold = ");
  Serial.print(THRESHOLD);
  Serial.println("): ");
  TempReading *current = head;
  int maxTemp = -1000;
  int minTemp = +1000;
  int sumTemp = 0;  // to store the sum of all temperatures
  int count = 0;    // to store the count of readings

  while (current) {
    Serial.print("Node: ");
    Serial.print(current->nodeIP);
    Serial.print(" - Temperature: ");
    Serial.println(current->tempValue);
    if (current->tempValue > maxTemp) {
      maxTemp = current->tempValue;
    }
    if (current->tempValue < minTemp) {
      minTemp = current->tempValue;
    }

    sumTemp += current->tempValue;
    count++;

    current = current->next;
  }

  if (count > 0) {
    Serial.print("Max Temperature: ");
    Serial.println(maxTemp);
    Serial.print("Min Temperature: ");
    Serial.println(minTemp);
    float avgTemp = (float)sumTemp / count;
    Serial.print("Average Temperature: ");
    Serial.println(avgTemp);
    if (ensureMQTTConnection()) {
      mqttClient.publish("teamA/max", String(maxTemp).c_str());
      mqttClient.publish("teamA/min", String(minTemp).c_str());
      mqttClient.publish("teamA/avg", String(avgTemp, 2).c_str());  // 2 decimal places for avg
    } else {
      Serial.println("MQTT ERROR...");
    }
  }
}

void flushReadings() {
  while (head) {
    TempReading *toDelete = head;
    head = head->next;
    delete toDelete;
  }
}

void startTimerTa() {
  timerTaStarted = true;
  timerTaStartTime = millis();
}

bool ensureMQTTConnection() {
  int timeout = 5;
  // Check if the MQTT client is still connected
  if (!mqttClient.isConnected()) {
    Serial.println("MQTT disconnected. Attempting to reconnect...");
    // First, ensure the underlying WiFiClient is connected
    if (!wifiClient.connected()) {
      Serial.println("WiFiClient disconnected. Attempting to reconnect...");
      while (!wifiClient.connect(broker, mqttPort) && timeout > 0) {
        Serial.print(".");
        delay(1000);
        timeout--;
      }
      if (timeout <= 0) {
        Serial.println("Failed to connect to the broker after multiple attempts.");
        return false;
      } else {
        Serial.println("Broker reconnected!");
        timeout = 5;
      }
    }

    // Now, attempt to reconnect MQTT client
    while (!mqttClient.connect(WiFi.localIP().toString(), username, password) && timeout > 0) {
      Serial.print(".");
      delay(1000);
      timeout--;
    }
    if (timeout <= 0) {
      Serial.println("Failed to connect to MQTT after multiple attempts.");
      return false;
    } else {
      Serial.println("MQTT reconnected!");
    }
  }
  return mqttClient.isConnected();
}

void attentionPhase(IPAddress node) {
  if (ensureMQTTConnection()) {
    Serial.print(node);
    mqttClient.publish("teamA/status", "ATTENTION");
    Serial.println(" node has a value above the threshold.");
  } else {
    Serial.println("MQTT ERROR...");
  }
}

void normalPhase(IPAddress node) {
  if (ensureMQTTConnection()) {
    Serial.print(node);
    mqttClient.publish("teamA/status", "NORMAL");
    Serial.println(" node that was over threshold. Solved situation.");
  } else {
    Serial.println("MQTT ERROR...");
  }
}

void faultPhase(IPAddress node) {
  if (ensureMQTTConnection()) {
    Serial.print(node);
    mqttClient.publish("teamA/fault", node.toString());
    Serial.println(" node that was over threshold. Killed.");
  } else {
    Serial.println("MQTT ERROR...");
  }
}
void subscribeToThreshold() {
  if (ensureMQTTConnection()) {  // Use the function we defined earlier to ensure connection
    mqttClient.subscribe("teamA/threshold", [](const String &payload, const size_t size) {
      int newThreshold = payload.toInt();

      if (newThreshold != THRESHOLD) {
        int oldThreshold = THRESHOLD;
        THRESHOLD = newThreshold;

        sendUDPMessage(1, broadcastIP, THRESHOLD);
        Serial.print("OLD THRESHOLD -> ");
        Serial.print(oldThreshold);
        Serial.print(" .... NEW THRESHOLD -> ");
        Serial.println(THRESHOLD);
      } else {
        Serial.println("Received threshold value is the same as the old one.");
      }
    });

    Serial.println("Subscribed to teamA/threshold");
  }
}
