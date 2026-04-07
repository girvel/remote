#include <IRremote.hpp>

void setup() {
  Serial.begin(9600);
  IrReceiver.begin(11);
  pinMode(8, OUTPUT);  // WTF why??
  Serial.println("Started.");
}

void loop() {
  if (IrReceiver.decode()) {
    Serial.println(IrReceiver.decodedIRData.command, HEX);
    delay(200);
    IrReceiver.resume();
  }
}
