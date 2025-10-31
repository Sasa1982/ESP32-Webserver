// function defaults
//String listFiles(bool ishtml = false);
// list all of the files, if ishtml=true, return html rather than simple text
#include "Arduino.h"
//#include "device.h"
//#include "device_users.h"
#include "FS.h"
#include "FFat.h"
#include "SD.h"
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"
#include "webserver.h"
//#include "log.h"
  #define FILESYSTEM SD

AsyncWebServer server(80);
bool shouldReboot = false;    
const String default_httpuser = "Admin";
const String default_httppassword = "1111";
// Make size of files human readable
// source: https://github.com/CelliesProjects/minimalUploadAuthESP32

// --- Globale Definition ---
#define USE_SD_CARD true   // <--- ändere auf false, wenn du FFat verwenden willst
#define FILE_PATH_ROOT "/"  // Root-Verzeichnis für deine HTML-Dateien

// --- Funktion: humanReadableSize ---
String humanReadableSize(const size_t bytes)
{
  if (bytes < 1024) return String(bytes) + " B";
  else if (bytes < (1024 * 1024)) return String(bytes / 1024.0) + " KB";
  else if (bytes < (1024 * 1024 * 1024)) return String(bytes / 1024.0 / 1024.0) + " MB";
  else return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
}

// --- Funktion: Dateisystem initialisieren ---
bool initFileSystem()
{
  bool success = false;
  if (USE_SD_CARD)
  {
    Serial.println(" Initialisiere SD-Karte...");
    if (SD.begin())
    {
      Serial.println(" SD-Karte erfolgreich initialisiert");
      success = true;
    } 
    else 
    {
      Serial.println(" Fehler: SD-Karte konnte nicht gestartet werden");
    }
  } 
  else 
  {
    Serial.println(" Initialisiere FFat...");
    if (FFat.begin()) 
    {
      Serial.println(" FFat erfolgreich initialisiert");
      success = true;
    } 
    else 
    {
      Serial.println(" Fehler: FFat konnte nicht gestartet werden");
    }
  }
  return success;
}

String processor(const String& var) {
  if (var == "SWVERSION") {
    return "V1.1.1";
  }

  if (var == "CHIPID") {
   // return  Device.GetChipID();
     return  "1234567";
  }
  if (var == "FREEFFAT") {
    return humanReadableSize((FILESYSTEM.totalBytes() - FILESYSTEM.usedBytes()));
  }

  if (var == "USEDFFAT") {
    return humanReadableSize(FILESYSTEM.usedBytes());
  }

  if (var == "TOTALFFAT") {
    return humanReadableSize(FILESYSTEM.totalBytes());
  }
}

// String listFiles(bool ishtml,const char *path)
// {
//   String returnText = "";
//   Serial.print("Listing files stored on FFat");
//   File root = FILESYSTEM.open(path);
//   File foundfile = root.openNextFile();
//   if (ishtml)
//   {
//     returnText += "<table><tr><th align='left'>Name</th><th align='left'>Size</th><th></th><th></th></tr>";
//   }
//   while (foundfile)
//   {
//     if (ishtml)
//     {
//       returnText += "<tr align='left'><td>" + String(foundfile.name()) + "</td><td>" + humanReadableSize(foundfile.size()) + "</td>";
//       returnText += "<td><button onclick=\"downloadDeleteButton(\'" + String(path)+ "/"+ String(foundfile.name()) + "\', \'download\')\">Download</button>";
//       returnText += "<td><button onclick=\"downloadDeleteButton(\'" + String(path)+ "/"+ String(foundfile.name()) + "\', \'delete\')\">Delete</button></tr>";
//     }
//     else
//     {
//       returnText += "File: " + String(foundfile.name()) + " Size: " + humanReadableSize(foundfile.size()) + "\n";
//     }
//     foundfile = root.openNextFile();
//   }
//   if (ishtml)
//   {
//     returnText += "</table>";
//   }
//   root.close();
//   foundfile.close();
//   return returnText;
// }

// --- Stabile Dateiübertragung (verhindert Content-Length-Mismatch) ---
void sendFileManually(AsyncWebServerRequest *request, const String &path, const String &contentType)
{
  File file = FILESYSTEM.open(path, "r");
  if (!file)
  {
    Serial.printf("Datei nicht gefunden: %s\n", path.c_str());
    request->send(404, "text/plain", "File not found: " + path);
    return;
  }

  size_t fileSize = file.size();
  Serial.printf("Sende Datei: %s (%u Bytes)\n", path.c_str(), (unsigned int)fileSize);

  // Datei direkt streamen, feste Content-Length
  AsyncWebServerResponse *response = request->beginResponse(file, contentType);
  response->addHeader("Content-Length", String(fileSize));  // exakte Größe setzen
  response->addHeader("Connection", "close");               // Verbindung beenden
  request->send(response);
}


String pathUpload ="/report/";

// --- simples File-Listing: kein Bootstrap, keine externen JS-Funktionen ---
String listFiles(bool ishtml, const char *path)
{
  String out;
  File root = FILESYSTEM.open(path);
  if (!root) return "Fehler: Verzeichnis nicht gefunden";

  File f = root.openNextFile();
  if (ishtml) out += "<table border='1' cellpadding='6' cellspacing='0'><tr><th>Name</th><th>Größe</th><th></th><th></th></tr>";
  while (f) {
    String name = String(f.name());
    String size = humanReadableSize(f.size());
    if (ishtml) {
      // einfache Links statt JS-Buttons
      String full = String(path) + "/" + name;
      out += "<tr><td>" + name + "</td><td>" + size + "</td>";
      out += "<td><a href=\"/file?name=" + full + "&action=download\">Download</a></td>";
      out += "<td><a href=\"/file?name=" + full + "&action=delete\" onclick=\"return confirm('Datei löschen?');\">Löschen</a></td></tr>";
    } else {
      out += "File: " + name + " Size: " + size + "\n";
    }
    f = root.openNextFile();
  }
  if (ishtml) out += "</table>";
  root.close();
  return out;
}


// handles uploads to the filserver
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  // make sure authenticated before allowing upload
//   if (checkUserWebAuth(request))
//   {
    String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
    Serial.print(logmessage);

    if (!index)
    {
      logmessage = "Upload Start: " + String(filename);
      // open the file on first call and store the file handle in the request object
      request->_tempFile = FILESYSTEM.open(pathUpload  + filename, "w");
      Serial.print(logmessage);
    }

    if (len)
    {
      // stream the incoming chunk to the opened file
      request->_tempFile.write(data, len);
      logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
      Serial.print(logmessage);
    }

    if (final)
    {
      logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
      // close the file handle as the upload is now done
      request->_tempFile.close();
      Serial.print(logmessage);
      request->redirect("/");
    }
//   }
//   else
//   {
//     Serial.print("Auth: Failed");
//     return request->requestAuthentication();
//   }
}

bool checkUserWebAuth(AsyncWebServerRequest *request)
{
  bool isAuthenticated = false;

  if (request->authenticate(default_httpuser.c_str(),  default_httppassword.c_str())) //Pincode des Benutzers
  {
    Serial.print("is authenticated via username and password");
    isAuthenticated = true;
  }
  return isAuthenticated;
}

// --- 404: nur Text, kein HTML im text/plain ---
void notFound(AsyncWebServerRequest *request)
{
  String msg = "404: " + request->url();
  Serial.print(msg);
  request->send(404, "text/plain", "Nicht gefunden: " + request->url());
}

void configureWebServer()
{
  // --- 404-Handler ---
  server.onNotFound(notFound);

  // --- Upload-Handler ---
  server.onFileUpload(handleUpload);

  // --- Startseite => login.html (ohne Auth) ---
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.print("GET /");
    request->send(FILESYSTEM, "/login.html", "text/html");
  });

  // --- menu.html (mit Auth) ---
  server.on("/menu.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (checkUserWebAuth(request))
      request->send(FILESYSTEM, "/menu.html", "text/html", false, processor);
    else
      return request->requestAuthentication();
  });

  // --- dashboard.html (mit Auth) ---
  server.on("/dashboard.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (checkUserWebAuth(request))
      request->send(FILESYSTEM, "/dashboard.html", "text/html", false, processor);
    else
      return request->requestAuthentication();
  });

  // --- profil.html (mit Auth) ---
  server.on("/profil.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (checkUserWebAuth(request))
      request->send(FILESYSTEM, "/profil.html", "text/html", false, processor);
    else
      return request->requestAuthentication();
  });

  // --- files.html (mit Auth) ---
  server.on("/files.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (checkUserWebAuth(request))
      request->send(FILESYSTEM, "/files.html", "text/html", false, processor);
    else
      return request->requestAuthentication();
  });
  // weitere eigene HTMLs nach gleichem Muster:
  server.on("/system.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (checkUserWebAuth(request))
      request->send(FILESYSTEM, "/system.html", "text/html", false, processor);
    else
      return request->requestAuthentication();
  });
  server.on("/produkte.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (checkUserWebAuth(request))
      request->send(FILESYSTEM, "/produkte.html", "text/html", false, processor);
    else
      return request->requestAuthentication();
  });


  // --- reboot.html (mit Auth) ---

  server.on("/dashboard.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (checkUserWebAuth(request))
      request->send(FILESYSTEM, "/dashboard.html", "text/html", false, processor);
    else
      return request->requestAuthentication();
  });

    server.on("/profil.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (checkUserWebAuth(request))
      request->send(FILESYSTEM, "/profil.html", "text/html", false, processor);
    else
      return request->requestAuthentication();
  });

  server.on("/reboot.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (checkUserWebAuth(request))
      request->send(FILESYSTEM, "/reboot.html", "text/html", false, processor);
    else
      return request->requestAuthentication();
  });

  // --- Dateien auflisten ---
  server.on("/listfilesRezepte", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (checkUserWebAuth(request))
      request->send(200, "text/html", listFiles(true, "/rezepte"));
    else
      return request->requestAuthentication();
  });

  server.on("/listfilesProducts", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (checkUserWebAuth(request))
      request->send(200, "text/html", listFiles(true, "/products"));
    else
      return request->requestAuthentication();
  });

  server.on("/listfilesReport", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (checkUserWebAuth(request))
      request->send(200, "text/html", listFiles(true, "/report"));
    else
      return request->requestAuthentication();
  });

  // --- User-Datenbank ---
  server.on("/user/users.json", HTTP_GET, [](AsyncWebServerRequest *request) {
  request->send(FILESYSTEM, "/user/users.json", "application/json");
});

  // --- Download & Delete ---
  server.on("/file", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkUserWebAuth(request)) return request->requestAuthentication();

    if (request->hasParam("name") && request->hasParam("action")) {
      String fileName = request->getParam("name")->value();
      String action   = request->getParam("action")->value();
      String filepath = fileName.startsWith("/") ? fileName : ("/" + fileName);

      if (!FILESYSTEM.exists(filepath)) {
        request->send(400, "text/plain", "ERROR: file does not exist");
        return;
      }

      if (action == "download") {
        request->send(FILESYSTEM, filepath, "application/octet-stream");
      } else if (action == "delete") {
        FILESYSTEM.remove(filepath);
        request->send(200, "text/plain", "deleted: " + filepath);
      } else {
        request->send(400, "text/plain", "ERROR: invalid action");
      }
    } else {
      request->send(400, "text/plain", "ERROR: name and action params required");
    }
  });

  // --- Freigabe für statische Inhalte (Bilder, CSS, JS) ---
  // Damit z. B. /web/pictures/login_logo.png ohne Auth geladen werden kann
  server.serveStatic("/web/", FILESYSTEM, "/web/");

  // --- Optional: alle übrigen Dateien automatisch bereitstellen ---
  // (nur aktivieren, wenn du möchtest, dass alle Dateien ohne Auth verfügbar sind)
  // server.serveStatic("/", FILESYSTEM, "/").setDefaultFile("login.html");

  // --- Stabile Bildauslieferung (ohne chunked encoding) ---
  server.on("/web/pictures/login_logo.png", HTTP_GET, [](AsyncWebServerRequest *request) {
  sendFileManually(request, "/web/pictures/login_logo.png", "image/png");
  });

  server.on("/web/pictures/Profile/profil_Mike_Aussendorf.jpeg", HTTP_GET, [](AsyncWebServerRequest *request) {
  sendFileManually(request, "/web/pictures/Profile/profil_Mike_Aussendorf.jpeg", "image/jpeg");
  });

  server.on("/web/pictures/login_background.jpg", HTTP_GET, [](AsyncWebServerRequest *request) {
  sendFileManually(request, "/web/pictures/login_background.jpg", "image/jpeg");
  });

  // --- Start Webserver ---
  server.begin();  
  Serial.println("Webserver gestartet!");
}
