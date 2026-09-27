#include <WiFi.h>
#include <PubSubClient.h>

WiFiClient net;
PubSubClient client(net);

void setup() {
  Serial.begin(115200);
  WiFi.begin("Wokwi-GUEST", "", 6);              // Wi-Fi virtuel de Wokwi
  while (WiFi.status() != WL_CONNECTED) { delay(250); Serial.print("."); }
  Serial.println("\nWi-Fi OK");

  client.setServer("host.wokwi.internal", 1883); // = ton PC, via la passerelle Wokwi
  if (client.connect("esp32-test")) {
    client.publish("test/bonjour", "Bonjour depuis Wokwi");
    Serial.println("MQTT OK");
  } else {
    Serial.printf("Echec MQTT, code %d\n", client.state());
  }
}

void loop() { client.loop(); }