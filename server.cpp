#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>

#include "Log.h"
#include "server.h"
#include "tank.h"
#include "pump.h"

static ESP8266WebServer server(80);

static String getContentType(String filename)
{ // convert the file extension to the MIME type
    if (filename.endsWith(".html"))
        return "text/html";
    else if (filename.endsWith(".css"))
        return "text/css";
    else if (filename.endsWith(".js"))
        return "application/javascript";
    else if (filename.endsWith(".ico"))
        return "image/x-icon";
    else if (filename.endsWith(".gz"))
        return "application/x-gzip";
    return "text/plain";
}

static bool sendHistoryJson(String path)
{
    if (path.endsWith(".json") && SD.exists(path))
    {
        File file = SD.open(path, FILE_READ);
        if (file)
        {
            String header =
                String("HTTP/1.1 200 OK\r\n") +
                "Content-Type: text/json\r\n" +
                "Connection: close\r\n" + // the connection will be closed after completion of the response
                "\r\n";
            WiFiClient client = server.client();
            client.print(header + "[");
            while (file.available())
            {
                client.write(file.read());
            }
            file.close();
            client.print("]");
            client.flush();
            client.stop();
            return true;
        }
    }
    return false;
}

static bool sendLast30daysJson(String path)
{
    bool ret = false;
    String filename;
    int data_offset;

    if (!path.endsWith("last30days.json"))
    {
        return false;
    }

    if (!tank_get_last_30days_file_and_offset(filename, data_offset))
    {
        Log.error("Failed to get 30 days offset");
        return false;
    }

    File file = SD.open(filename, FILE_READ);
    if (file)
    {
        if (file.seek(data_offset))
        {
            String header =
                String("HTTP/1.1 200 OK\r\n") +
                "Content-Type: text/json\r\n" +
                "Connection: close\r\n" + // the connection will be closed after completion of the response
                "\r\n";
            WiFiClient client = server.client();
            client.print(header + "[");
            while (file.available())
            {
                client.write(file.read());
            }
            client.print("]");
            client.flush();
            client.stop();
            ret = true;
        }
        else
        {
            Log.error("Failed to set 30 days offset");
        }

        file.close();
    }
    else
    {
        Log.error("Failed to open 30 days file");
    }

    return ret;
}

static bool sendFile(String path)
{ // send the right file to the client (if it exists)
    Log.info("handleFileRead: %s", path.c_str());
    if (path.endsWith("/"))
        path += "index.html";                  // If a folder is requested, send the index file
    String contentType = getContentType(path); // Get the MIME type
    String pathWithGz = path + ".gz";
    if (sendLast30daysJson(path))
    {
        return true;
    }

    if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path))
    {                                                       // If the file exists, either as a compressed archive, or normal
        if (SPIFFS.exists(pathWithGz))                      // If there's a compressed version available
            path += ".gz";                                  // Use the compressed verion
        File file = SPIFFS.open(path, "r");                 // Open the file
        size_t sent = server.streamFile(file, contentType); // Send it to the client
        file.close();                                       // Close the file again
        Log.info("Sent file: %s", path.c_str());
        return true;
    }
    if (sendHistoryJson(path))
    {
        return true;
    }
    Log.warn("File Not Found: %s", path.c_str()); // If the file doesn't exist, return false
    return false;
}

void server_init()
{
    SPIFFS.begin();

    server.onNotFound([]() {                                  // If the client requests any URI
        if (!sendFile(server.uri()))                          // send it if it exists
            server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
    });

    server.on("/all", HTTP_GET, []() {
        String json = "{";
        json += "\"heap\":" + String(ESP.getFreeHeap());
        json += ", \"analog\":" + String(analogRead(A0)); //String(tank_get_level());
        json += ", \"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
        json += "}";
        server.send(200, "text/json", json);
        json = String();
    });

    server.on("/time", HTTP_GET, []() {
        String json = "{";
        json += "\"epoch\":" + String(now());
        json += "}";
        server.send(200, "text/json", json);
        json = String();
    });

    server.on("/stats.json", HTTP_GET, []() {
        String json = "{\"TANK\":" + tank_get_stats_json();
        json += ",\"PUMP\":" + pump_get_stats_json() + "}";
        server.send(200, "text/json", json);
    });

    server.on("/24h_history.json", HTTP_GET, []() {
        server.send(200, "text/json", tank_get_last_24h_json());
    });

    server.on("/enable_pump", HTTP_POST, []() {
        server.send(200, "text/plain", "Post route");
        pump_enable();
    });

    server.on("/disable_pump", HTTP_POST, []() {
        server.send(200, "text/plain", "Post route");
        pump_disable();
    });

    // Start the server
    server.begin();
    Log.info("Server started");
}

void server_handle()
{
    server.handleClient();
}
