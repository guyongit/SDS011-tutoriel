/*
Name:       SDS011_WebServer_V1.ino
Created:	22/05/2019 08:39:08
Author:     guy limpas
*/

#include <WiFi.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

#define OLED_RESET   27  //pin 27 HUZZAH32

//Configuration du port série de l'ESP32:
#ifdef ESP32  
HardwareSerial portSerial(2);
#endif

//Création instances de classe:
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);
Adafruit_BME280 bme;

//SSID + mot de passe du Routeur ou Box:
const char* ssid = "";
const char* password = "";


WiFiServer server(80); // port 80.

long LastSampleTime = 0;
const long period = 5000; //Intervalle reception des mesures ici 5 secondes.
String header; // Pour la réception des données du SDS011.
byte rxBuffer[10]; //pour la réception des données du port série.
byte index1 = 0; // index pour rxBuffer.
				 //bool pressed = false;
				 //bool sleep_work = false;
float pm25, pm10;		//calcul des valeurs PM2.5 et PM10.
int _pm25, _pm10; //pour la conversion en integer(float au départ).
int IQApm25, IQApm10;   //calcul du IQA pour PM2.5 et PM10.
String echelleIQA10 = "", echelleIQA25 = "", _echelleIQA10 = "", _echelleIQA25 = "";
float temperature;
int humidite;
byte txBufer[19]; //pour la transmission au SDS011.
				  //pour modifier le style de la page HTML:
String spanClasspm25, spanClasspm10, spanClassIQA;

void setup()
{
	pinMode(14, INPUT_PULLUP);//configuration pin 14 en entrée avec PULLUP.

							  //pinMode(32, INPUT_PULLUP);//configuration pin 32 en entrée avec PULLUP.
							  //attachInterrupt(32, isr, FALLING);

	portSerial.begin(9600); //Communication à 9600 entre ESP32 et SDS011.
	Serial.begin(115200);   //Pour le débug.

	if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D))
	{
		Serial.println("SSD1306 failed !");
		for (;;);
	}

	bme.begin();
	//paramétrage du SDS011 en mode passif:
	setWorkingMode(1);

	Affichage_oled_wait();

	//Connection au routeur WIFI:
	Serial.print("Connecte a: ");
	Serial.println(ssid);
	WiFi.begin(ssid, password);
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		Serial.print(".");
		//si bouton pressé on sort de While lorsque on ne peut se
		//connecter à une box ou à un routeur:
		if (!digitalRead(14)) break;
	}
	//On affiche l'adresse IP et on démarre le serveur:
	Serial.println("");
	Serial.println("Wifi connecte !");
	Serial.print("IP adresse: "); Serial.println(WiFi.localIP());

	Affichage_oled_IP();
	delay(500);

	server.begin();//On démarre le Serveur.
}

void loop()
{

	long now = millis();
	if (now > LastSampleTime + period)
	{
		LastSampleTime = now;
		temperature = bme.readTemperature();
		humidite = bme.readHumidity();
		getQueryData();//on demande au SDS011 de nous envoyer les mesures.
	}

	while (portSerial.available() > 0)//On enregistre les mesures dans rxBuffer[].
	{
		rxBuffer[index1] = portSerial.read();
		//Serial.println(rxBuffer[index1]);
		index1++;
	}

	if ((rxBuffer[0] == 170) && (rxBuffer[9] == 0xAB))//Si début fin de chaine.
	{
		bool testcrc = calculCheck(rxBuffer, rxBuffer[8]);//Vérification du chekcsum.
														  //Calcul des valeurs:
		pm25 = ((rxBuffer[3] * 256) + rxBuffer[2]) / 10.0;
		pm10 = ((rxBuffer[5] * 256) + rxBuffer[4]) / 10.0;

		//Calcul pm25 et pm10 avec correction:(en float et en integer)
		pm25 = pm25 / (1.0 + 0.48756*pow((humidite / 100.0), 8.60068));
		pm10 = pm10 / (1.0 + 0.81559*pow((humidite / 100.0), 5.83411));
		_pm25 = round(pm25);
		_pm10 = round(pm10);
		//Calcul niveau de pollution(de 0 à 300 et +):
		IQApm25 = pm25 * 100 / 25;
		IQApm10 = pm10 * 100 / 50;
		//Pour modifier la couleurs de fond du texte suivant 
		//la qualité de l'air dans page HTML (style span class):
		spanClasspm25 = classHTML(IQApm25);
		spanClasspm10 = classHTML(IQApm10);

		//echelle IQA (texte) ici pour web page en HTML:
		echelleIQA10 = qualiteIQA(IQApm10);
		echelleIQA25 = qualiteIQA(IQApm25);
		//echelle IQA (texte) ici pour affichage OLED:
		_echelleIQA10 = _qualiteIQA(IQApm10);
		_echelleIQA25 = _qualiteIQA(IQApm25);

		Affichage_oled();//Affichage des données sur écran OLED SSD1306.
		memset(rxBuffer, 0x00, sizeof(rxBuffer));//RAZ du tableau rxBuffer[].
		index1 = 0;
	}

	WiFiClient client = server.available();   //On écoute les clients entrants sur port 80.
	if (client) {                             //si un nouveau client se connecte,
		Serial.println("New Client.");          // print a message out in the serial port
		String currentLine = "";                // make a String to hold incoming data from the client
		while (client.connected()) {            // loop while the client's connected
			if (client.available()) {             // if there's bytes to read from the client,
				char c = client.read();             // read a byte, then
				Serial.write(c);                    // print it out the serial monitor
				header += c;
				if (c == '\n') {                    // if the byte is a newline character
													// if the current line is blank, you got two newline characters in a row.
													// that's the end of the client HTTP request, so send a response:
					if (currentLine.length() == 0) {
						// HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
						// and a content-type so the client knows what's coming, then a blank line:
						client.println("HTTP/1.1 200 OK");
						client.println("Content-type:text/html");
						client.println("Connection: close");
						client.println();

						// Affichage de la page web HTML:
						client.println("<!DOCTYPE html><html>");
						client.print("<head>");
						client.println("<title>Qualit&eacute; de l'air LABORATOIRE</title>");
						client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
						client.println("<link rel=\"icon\" href=\"data:,\">");
						//client.println("<meta http-equiv = \"refresh\" content=\"3\">");
						// Style de la page HTMl en CSS
						client.println("<style>body { text-align: center; font-family: \"Trebuchet MS\", Arial;}");
						client.println("table { border-style: outset; border-collapse: collapse; width:35%; margin-left:auto; margin-right:auto; }");
						client.println("th { padding: 12px; background-color: #0174DF; color: white; }");
						client.println("tr { border: 1px solid #ddd; padding: 12px; }");
						client.println("tr:hover { background-color: #bcbcbc; }");
						client.println("td { border: medium inset; padding: 12px; }");
						client.println(".sensor { color:white; font-weight: bold; background-color: #bcbcbc; padding: 1px; }");
						client.println(".bon { color:white; font-weight: bold; background-color: #008000; padding: 1px; }");
						client.println(".modere { color:white; font-weight: bold; background-color: #FFD700; padding: 1px; }");
						client.println(".mediocre { color:white; font-weight: bold; background-color: #FFA500; padding: 1px; }");
						client.println(".mauvais { color:white; font-weight: bold; background-color: #DC143C; padding: 1px; }");
						client.println(".tresmauvais { color:white; font-weight: bold; background-color: #4B0082; padding: 1px; }");
						client.println(".danger { color:white; font-weight: bold; background-color: #8B0000; padding: 1px; }");
						//client.println("</style></head><body><h1>Qualit&eacute; de l'air</h1>");
						client.println("</style>");
						//Script pour rafraichissement automatique de la page Web:
						client.println("<script>\n");
						client.println("setInterval(loadDoc,3000);\n");
						client.println("function loadDoc() {\n");
						client.println("var xhttp = new XMLHttpRequest();\n");
						client.println("xhttp.onreadystatechange = function() {\n");
						client.println("if (this.readyState == 4 && this.status == 200) {\n");
						client.println("document.body.innerHTML =this.responseText}\n");
						client.println("};\n");
						client.println("xhttp.open(\"GET\", \"/\", true);\n");
						client.println("xhttp.send();\n");
						client.println("}\n");
						client.println("</script>\n");
						//Fin du script raffraichissemnt (ici 3000 donc 3 secondes).
						client.println("</head><body><h1>Qualit&eacute; de l'air</h1>");
						client.println("<table><tr><th>MESURES</th><th>VALEURS</th></tr>");

						client.println("<tr><td>Temp&eacute;rature</td><td><span class=\"sensor\">");
						client.println(temperature);
						client.println(" &deg;C</span></td></tr>");

						client.println("<tr><td>Humidit&eacute;</td><td><span class=\"sensor\">");
						client.println(humidite);
						client.println(" %</span></td></tr>");

						client.println("<tr><td>PM2.5</td><td><span class=\"" + spanClasspm25 + "\">");
						client.println(pm25);
						client.println(" ug/m3</span></td></tr>");

						client.println("<tr><td>PM10</td><td><span class=\"" + spanClasspm10 + "\">");
						client.println(pm10);
						client.println(" ug/m3</span></td></tr>");

						client.println("<tr><td>IQA2.5</td><td><span class=\"" + spanClasspm25 + "\">");
						client.println(IQApm25);
						client.println(" </span></td></tr>");

						client.println("<tr><td>IQA10</td><td><span class=\"" + spanClasspm10 + "\">");
						client.println(IQApm10);
						client.println(" </span></td></tr>");

						client.println("<tr><td>Qualit&eacute; air(PM2.5)</td><td><span class=\"" + spanClasspm25 + "\">");
						client.println(echelleIQA25);
						client.println(" </span></td></tr>");

						client.println("</body></html>");

						// The HTTP response ends with another blank line
						client.println();
						// Break out of the while loop
						break;
					}
					else { // if you got a newline, then clear currentLine
						currentLine = "";
					}
				}
				else if (c != '\r') {  // if you got anything else but a carriage return character,
					currentLine += c;      // add it to the end of the currentLine
				}
			}
		}
		// Clear the header variable
		header = "";
		// Close the connection
		client.stop();
		Serial.println("Client disconnected.");
		Serial.println("");
	}
}



//Fonction qualite echelle de niveau de la pollution pour page HTML :
//l'affichage dans OLED et 
String qualiteIQA(int16_t valueIQA)
{
	if (valueIQA >= 0 && valueIQA <= 50)  return "Bon";
	if (valueIQA >= 51 && valueIQA <= 100)  return "Mod&eacute;r&eacute;";
	if (valueIQA >= 101 && valueIQA <= 150)  return "M&eacute;diocre";
	if (valueIQA >= 151 && valueIQA <= 200)  return "Mauvais";
	if (valueIQA >= 201 && valueIQA <= 300)  return "Tr&eacute;s mauvais";
	if (valueIQA >= 301)  return "Dangereux";
}

//Fonction qualite echelle de niveau de la pollution pour l'affichage dans OLED:
String _qualiteIQA(int16_t valueIQA)
{
	if (valueIQA >= 0 && valueIQA <= 50)  return "Bon";
	if (valueIQA >= 51 && valueIQA <= 100)  return "Modere";
	if (valueIQA >= 101 && valueIQA <= 150)  return "Mediocre";
	if (valueIQA >= 151 && valueIQA <= 200)  return "Mauvais";
	if (valueIQA >= 201 && valueIQA <= 300)  return "Tres mauvais";
	if (valueIQA >= 301)  return "Dangereux";
}

//Pour modifier le style css préformaté:
String classHTML(int16_t valueIQA)
{
	if (valueIQA >= 0 && valueIQA <= 50)  return "bon";
	if (valueIQA >= 51 && valueIQA <= 100)  return "modere";
	if (valueIQA >= 101 && valueIQA <= 150)  return "mediocre";
	if (valueIQA >= 151 && valueIQA <= 200)  return "mauvais";
	if (valueIQA >= 201 && valueIQA <= 300)  return "tresmauvais";
	if (valueIQA >= 301)  return "danger";
}

void Affichage_oled()
{
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(WHITE);
	display.setCursor(0, 0);
	display.print("T:"); display.print(temperature); display.cp437(true); display.write(248); display.print("C  ");
	display.print("Hum:"); display.print(humidite); display.write(37);
	display.println();
	display.drawLine(20, 12, display.width() - 21, 12, WHITE);
	display.println();
	display.println("Concentration:(ug/m3)");
	display.print("PM2.5: "); display.print(_pm25); display.print("  IQA:"); display.println(IQApm25);
	display.println();
	display.print("PM10: "); display.print(_pm10); display.print("  IQA:"); display.println(IQApm10);
	display.println(); display.print(" IQA : "); display.print(_echelleIQA25);
	display.drawLine(20, 55, display.width() - 21, 55, WHITE);
	display.println();
	display.display();//Ne pas oublier cette ligne, autrement l'écran reste vide !.
}

void Affichage_oled_IP()
{
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(WHITE);
	display.setCursor(0, 0);
	display.println("    Connecte !");
	display.println("SSID: ");
	display.println(ssid);
	display.setTextSize(1);
	display.println(); display.print(WiFi.localIP());
	display.display();//Ne pas oublier cette ligne, autrement l'écran reste vide !.
}

void Affichage_oled_wait()
{
	display.clearDisplay();
	display.setTextSize(2);
	display.setTextColor(WHITE);
	display.setCursor(0, 0);
	display.println("Recherche");
	display.println("ROUTEUR...");
	display.display();//Ne pas oublier cette ligne, autrement l'écran reste vide !.
}

//Fonction pour le calcul du Checksum :
bool calculCheck(uint8_t buff[], uint8_t crc)
{
	uint8_t crc_calc = 0;
	for (uint8_t i = 2; i < 8; i++)
	{
		crc_calc += buff[i];
	}
	return crc == crc_calc;//Si checkSum OK, retour True.
}

//Fonction pour le calcul du checksum (CRC) pour l'envoie d'information
//au SDS011:
uint8_t getCRC(uint8_t buff[])
{
	uint8_t crc = 0;
	for (uint8_t i = 2; i < 17; i++)
	{
		crc += buff[i];
	}
	return crc;
}

//Fonction pour paramétrer le SDS011 en Report active ou Query mode
//Report active mode: envoie auto des données (par défaut 1 secondes)
//Report query mode: envoie sur demande des données (par défaut dans ce programme).
void setWorkingMode(bool mode) //auto=0, question=1.
{
	memset(txBufer, 0, sizeof(txBufer));
	txBufer[0] = 0xAA;
	txBufer[1] = 0xB4;
	txBufer[2] = 0x02;
	txBufer[3] = 0x01;
	txBufer[4] = mode;
	txBufer[15] = 0xFF;
	txBufer[16] = 0xFF;
	txBufer[17] = getCRC(txBufer);
	txBufer[18] = 0xAB;
	portSerial.write(txBufer, sizeof(txBufer));
}

//Fonction pour demander au SDS011 l'envoie des mesures:
void getQueryData()
{
	memset(txBufer, 0, sizeof(txBufer));
	txBufer[0] = 0xAA;
	txBufer[1] = 0xB4;
	txBufer[2] = 0x04;
	//txBufer[3] = 0x01;
	//txBufer[4] = 0x01;
	txBufer[15] = 0xFF;
	txBufer[16] = 0xFF;
	txBufer[17] = getCRC(txBufer);
	txBufer[18] = 0xAB;
	portSerial.write(txBufer, sizeof(txBufer));
}

//Fonction pour mettre le SDS en mode sommeil(pas utilisée):
void setSleepWork(bool data) // sleep=0 , work=1.
{
	memset(txBufer, 0, sizeof(txBufer));
	txBufer[0] = 0xAA;
	txBufer[1] = 0xB4;
	txBufer[2] = 0x06;
	txBufer[3] = 0x01;
	txBufer[4] = data;
	txBufer[15] = 0xFF;
	txBufer[16] = 0xFF;
	txBufer[17] = getCRC(txBufer);
	txBufer[18] = 0xAB;
	portSerial.write(txBufer, sizeof(txBufer));
}

void IRAM_ATTR isr()
{
	//pressed = true;
}