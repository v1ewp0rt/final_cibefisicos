#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <OneWire.h>
#include <SPI.h>
#include <SD.h>
#include <DHT.h>
#include <DallasTemperature.h>
#include <RTClib.h>

#define PULSADOR 2
#define HALL 6

// Empaqueto los valores a registrar
struct Medicion { 
    int temperatura, humedadIn, humedadEx, dia, hora, minuto;
    bool disponible=true; 
}; struct Promedios { int temperatura, humedadIn, humedadEx; };

bool iniciado;
bool pulsadorEstado;
bool pulsadorEstadoAnterior;
Medicion mediciones[6];
RTC_DS1307 rtc;
DHT dhtIn(4, DHT22);
DHT dhtEx(5, DHT22);
LiquidCrystal_I2C lcd(0x27, 20, 4);
OneWire ow(3);
DallasTemperature tmp(&ow);
unsigned long tiempoInicial;
unsigned long tiempoActual;
unsigned long ultimaVentilacion;
unsigned long ultimaMedicion;
int ultimaTemperatura=0, ultimaHumedad=0;
DateTime momentoActual;
bool maximaAlcanzada;

void estado_lcd(int estado) {
    if (estado==0) {
        lcd.setCursor(7, 1); lcd.print(momentoActual.hour());
        lcd.setCursor(9, 1); lcd.print(":");
        lcd.setCursor(10, 1); lcd.print(momentoActual.minute());
        lcd.setCursor(6, 3); lcd.print("Iniciar");
    } else if (estado==1) {
        lcd.setCursor(1, 1); lcd.print(momentoActual.hour());
        lcd.setCursor(3, 1); lcd.print(":");
        lcd.setCursor(4, 1); lcd.print(momentoActual.minute());
        lcd.setCursor(9, 1); lcd.print(ultimaTemperatura);
        lcd.setCursor(11, 1); lcd.print("C");
        lcd.setCursor(16, 1); lcd.print(ultimaHumedad);
        lcd.setCursor(18, 1); lcd.print("%");
        lcd.setCursor(6, 3); lcd.print("Detener");
    } else if (estado==2) { lcd.setCursor(3, 2); lcd.print("FALTA VENTILAR"); } 
      else if (estado==3) { lcd.setCursor(3, 2); lcd.print("TEMPERATURA OK"); }
} Medicion nueva_medicion() {
    Medicion nueva;
    tmp.requestTemperatures();
    nueva.disponible = false;
    // Fecha de medición
    nueva.dia = momentoActual.day();
    nueva.hora = momentoActual.hour();
    nueva.minuto = momentoActual.minute();
    // Senso las magnitudes
    nueva.temperatura = tmp.getTempCByIndex(0);
    nueva.humedadIn = dhtIn.readHumidity();
    nueva.humedadEx = dhtEx.readHumidity();
    return nueva;
} void guardar_registro(String ruta, Promedios data) {
    File registro = SD.open(ruta, FILE_WRITE);
    if (registro) {
      registro.print(mediciones[0].dia);
      registro.print(":"); registro.print(mediciones[0].hora);
      registro.print(":"); registro.print(mediciones[0].minuto);
      registro.print(" - ");
      registro.print(mediciones[5].dia);
      registro.print(":"); registro.print(mediciones[5].hora);
      registro.print(":"); registro.print(mediciones[5].minuto);
      registro.print("    Temperatura promedio: "); 
      registro.print(data.temperatura); registro.println("°C");
      registro.print("    Humedad promedio interna: ");
      registro.print(data.humedadIn); registro.println("%");
      registro.print("    Humedad promedio externa: ");
      registro.print(data.humedadEx); registro.println("%");
      registro.close();
      Serial.println("Registro actualizado");
    } else { Serial.println("Error al actualizar registro"); }
} void gestionar_mediciones() {
    // Si pasaron 10 segundos desde la ultima medición o nunca se midió
    if (tiempoActual-ultimaMedicion > 10000) {
      bool hayEspacio=false;
      // Nueva medición en el ultimo elemento vacío del arreglo
      for (Medicion &m : mediciones) {
          // Utilizé default dia=0 para indicar que ese espacio en el arreglo está disponible
          if (m.disponible) { 
              hayEspacio = true;
              m = nueva_medicion();
              Serial.println("Medición guardada");
              break;
          }
      } if (!hayEspacio) {
          // Calcular promedios
          Promedios promediosActuales;
          promediosActuales.temperatura = 0;
          promediosActuales.humedadIn = 0;
          promediosActuales.humedadEx = 0;
          for (Medicion &m : mediciones) { 
              promediosActuales.temperatura += m.temperatura; 
              promediosActuales.humedadIn += m.humedadIn;
              promediosActuales.humedadEx += m.humedadEx;
          } promediosActuales.temperatura /= 6; 
          promediosActuales.humedadIn /= 6; 
          promediosActuales.humedadEx /= 6;
          // Escribir registro
          guardar_registro("registro.txt", promediosActuales);
          // Limpiar arreglo de mediciones
          for (Medicion &m : mediciones) { m.disponible = true; }
          mediciones[0] = nueva_medicion();
      } // Valores a mostrar en la pantalla
      ultimaTemperatura = tmp.getTempCByIndex(0);
      ultimaHumedad = dhtIn.readHumidity()-dhtEx.readHumidity();
      ultimaMedicion = millis();
    }
} void gestionar_eventos() {
    // Si la temperatura supera los 60 grados se advierte
    if (ultimaTemperatura>=60) { maximaAlcanzada=true; }
    if (maximaAlcanzada) { estado_lcd(3); }
    // Si pasaron 3 minutos sin ventilar (abrir la compostera) se advierte
    if (tiempoActual-ultimaVentilacion>=180000) { estado_lcd(2); }
    if (digitalRead(HALL)) { ultimaVentilacion=tiempoActual; }
} void setup() {
    maximaAlcanzada = 0;
    ultimaVentilacion = 0;
    ultimaMedicion = 0;
    iniciado = false;
    pulsadorEstadoAnterior = 0;
    pinMode(PULSADOR, INPUT_PULLUP);
    pinMode(HALL, INPUT_PULLUP);
    Wire.begin();
    Serial.begin(9600);
    while (!Serial) {;}
    rtc.begin();
    dhtIn.begin(); dhtEx.begin();
    lcd.begin(20, 4); lcd.clear();
    if (!SD.begin(10)) { Serial.println("Error al iniciar SD"); }
} void loop() {
    lcd.clear();
    tiempoActual = millis();
    momentoActual = rtc.now();
    pulsadorEstado = !digitalRead(PULSADOR);
    if (!iniciado) {
        estado_lcd(0);
        // Si se presiona el botón se inicia el proceso
        if (pulsadorEstadoAnterior == 1 && pulsadorEstado == 0) { 
            // Reseteo de los parámetros
            tiempoInicial = millis();
            ultimaVentilacion = tiempoActual;
            ultimaMedicion = 0; 
            iniciado=true; 
        }
    } else {
        gestionar_mediciones();
        estado_lcd(1);
        gestionar_eventos();
        // Si se presiona el botón se detiene el proceso
        if (pulsadorEstadoAnterior == 1 && pulsadorEstado == 0) { 
            maximaAlcanzada = 0;
            for (Medicion &m : mediciones) { m.disponible = true; }
            iniciado=false; 
        }
    }
    delay(1000); // Tasa de actualización de 1 segundo como mínimo
    pulsadorEstadoAnterior = pulsadorEstado;
}