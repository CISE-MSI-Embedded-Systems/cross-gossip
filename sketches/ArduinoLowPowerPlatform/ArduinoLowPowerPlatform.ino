const int source_pin = A0;

const float reference_voltage = 5.0f;
const int adc_full_scale = 1023; // 10-bit ADC

void setup() {
  Serial.begin(115200);
}

void loop() {
  int source_sample_code = analogRead(source_pin);

  // Instantaneous voltage coming from the source (function generator)
  float source_voltage =
      (source_sample_code * reference_voltage) / adc_full_scale;

  Serial.print("code: ");
  Serial.print(source_sample_code);
  Serial.print("  source_voltage: ");
  Serial.println(source_voltage);

  delay(50);
}
