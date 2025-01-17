#include <LIDARLite.h>
#include <Servo.h>
#include <Wire.h>

Servo hor_servo;  // Horizontal axis
Servo vert_servo; // Vertical axis

// connect Blue (SDA) to A4
// connect Green (SCL) to A5
// Yellow and Orange are not connected
LIDARLite lidar;
                

int HORIZONTAL_AXIS_PIN = 9;
int VERTICAL_AXIS_PIN = 10;

void setup() {
  Serial.begin(9600);
  hor_servo.attach(HORIZONTAL_AXIS_PIN); 
  vert_servo.attach(VERTICAL_AXIS_PIN);

  hor_servo.write(70);
  delay(500);
  vert_servo.write(70);
  delay(500);

  lidar.begin(0, true);
  lidar.configure(0);
}

void printval(int i, int j) {
      Serial.print("V");
      Serial.print(i);
      Serial.print(" H");
      Serial.print(j);
      Serial.print(" = ");
      Serial.println(lidar.distance());
}



void loop() {
    // Let client know we are waiting for a command
    Serial.println("Ready");
  
    // Wait until "sweep" is sent over the serial line
    while (Serial.available() == 0);

    String inputStr = Serial.readString();
    if (inputStr == "sweep") {
      sweep();
    }
} // loop()

void sweep() {
    int i, j;

    int hor_min = 0;
    int hor_max = 180;
    int vert_min = 60; // 40;
    int vert_max = 170; // 120; // actually: 180

    int wait = 40;

    bool left_to_right = true;

    for (i = vert_min ; i <= vert_max ; ++i) {
      vert_servo.write(i);
      if (left_to_right) {
        for (j = hor_min ; j <= hor_max ; ++j) {
          hor_servo.write(j);
          printval(i,j);
          delay(wait);
        } // for j
      } else {
        for (j = hor_max ; j >= hor_min ; --j) {
          hor_servo.write(j);
          printval(i,j);
          delay(wait);
        } // for j
      } // if
      left_to_right = !left_to_right;
    } // for i
} // sweep()
