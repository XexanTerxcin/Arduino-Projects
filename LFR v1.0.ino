// Pin Definitions
const int sensorPins[5] = {A0, A1, A2, A3, A4};  // IR Sensor pins
const int ENA = 2;
const int ENB = 7;
const int IN1 = 3;
const int IN2 = 4;
const int IN3 = 5;
const int IN4 = 6;

// PID constants
float Kp = 25;
float Ki = 0;
float Kd = 15;

int baseSpeed = 150;       // Base motor speed
int maxSpeed = 175;

// PID variables
int lastError = 0;
float integral = 0;

// Weights for position calculation
int weights[5] = {-2, -1, 0, 1, 2};

void setup() {
  // Motor pins
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // Sensor pins
  for (int i = 0; i < 5; i++) {
    pinMode(sensorPins[i], INPUT);
  }

  // Enable motor drivers
  digitalWrite(ENA, HIGH);
  digitalWrite(ENB, HIGH);

  // Start Serial Monitor
  Serial.begin(9600);
  Serial.println("Line follower PID starting...");
}

void loop() {
  int rawValues[5];
  int binValues[5];
  int position = 0;
  int total = 0;

  Serial.print("Raw: ");
  for (int i = 0; i < 5; i++) {
    rawValues[i] = analogRead(sensorPins[i]);
    binValues[i] = rawValues[i] < 677 ? 1 : 0;  // You can adjust 500 later based on raw values
    Serial.print(rawValues[i]);
    Serial.print(" ");
  }

  Serial.print(" | Digital: ");
  for (int i = 0; i < 5; i++) {
    Serial.print(binValues[i]);
    Serial.print(" ");
    position += binValues[i] * weights[i];
    total += binValues[i];
  }

  Serial.print(" | ");

  // If no sensor detects the line, stop
  if (total == 0) {
    stopMotors();
    Serial.println("Line lost. Stopping.");
    delay(10);
    return;
  }

  int error = position;
  integral += error;
  float derivative = error - lastError;
  float correction = Kp * error + Ki * integral + Kd * derivative;
  lastError = error;

  int leftSpeed = baseSpeed - correction;
  int rightSpeed = baseSpeed + correction;

  leftSpeed = constrain(leftSpeed, 0, maxSpeed);
  rightSpeed = constrain(rightSpeed, 0, maxSpeed);

  moveMotors(leftSpeed, rightSpeed);

  Serial.print("Error: ");
  Serial.print(error);
  Serial.print(" | Correction: ");
  Serial.print(correction);
  Serial.print(" | LSpeed: ");
  Serial.print(leftSpeed);
  Serial.print(" | RSpeed: ");
  Serial.println(rightSpeed);
  
  moveMotors(leftSpeed, rightSpeed);
  delay(20);   // Let it run for a bit
  stopMotors();
  delay(300);   // Pause, then resume

  //delay(400);  
  // Adjust delay if needed for smoother Serial Monitor scrolling
}

// Motor control
void moveMotors(int leftSpeed, int rightSpeed) {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, leftSpeed);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  analogWrite(ENB, rightSpeed);
}

void stopMotors() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
} 