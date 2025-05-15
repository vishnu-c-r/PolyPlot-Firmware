// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "../Machine/MachineConfig.h"
#include "../Config.h"
#include "../Serial.h"    // is_realtime_command()
#include "../Settings.h"  // settings_execute_line()

#ifdef ENABLE_WIFI

#    include "WifiServices.h"
#    include "WifiConfig.h"  // wifi_config

#    include "WebServer.h"

#    include <WebSocketsServer.h>
#    include <WiFi.h>
#    include <WebServer.h>
#    include <StreamString.h>
#    include <Update.h>
#    include <esp_wifi_types.h>
#    include <ESPmDNS.h>
#    include <ESP32SSDP.h>
#    include <DNSServer.h>
#    include "WebSettings.h"

#    include "WSChannel.h"

#    include "WebClient.h"

#    include "src/Protocol.h"
#    include "src/FluidPath.h"
#    include "src/WebUI/JSONEncoder.h"

#    include "src/HashFS.h"
#    include <list>

#    include "PenConfig.h"
#    include "ToolConfig.h"

extern volatile bool pen_change;

namespace WebUI {
    const byte DNS_PORT = 53;
    DNSServer  dnsServer;
}

#    include <esp_ota_ops.h>

//embedded response file if no files on LocalFS
#    include "NoFile.h"

namespace WebUI {
    // Error codes for upload
    const int ESP_ERROR_AUTHENTICATION   = 1;
    const int ESP_ERROR_FILE_CREATION    = 2;
    const int ESP_ERROR_FILE_WRITE       = 3;
    const int ESP_ERROR_UPLOAD           = 4;
    const int ESP_ERROR_NOT_ENOUGH_SPACE = 5;
    const int ESP_ERROR_UPLOAD_CANCELLED = 6;
    const int ESP_ERROR_FILE_CLOSE       = 7;

    static const char LOCATION_HEADER[] = "Location";

    Web_Server webServer __attribute__((init_priority(108)));
    bool       Web_Server::_setupdone = false;
    uint16_t   Web_Server::_port      = 0;

    UploadStatus      Web_Server::_upload_status   = UploadStatus::NONE;
    WebServer*        Web_Server::_webserver       = NULL;
    WebSocketsServer* Web_Server::_socket_server   = NULL;
    WebSocketsServer* Web_Server::_socket_serverv3 = NULL;
#    ifdef ENABLE_AUTHENTICATION
    AuthenticationIP* Web_Server::_head  = NULL;
    uint8_t           Web_Server::_nb_ip = 0;
    const int         MAX_AUTH_IP        = 10;
#    endif
    FileStream* Web_Server::_uploadFile = nullptr;

    EnumSetting *http_enable, *http_block_during_motion;
    IntSetting*  http_port;

    Web_Server::Web_Server() {
        http_port   = new IntSetting("HTTP Port", WEBSET, WA, "ESP121", "HTTP/Port", DEFAULT_HTTP_PORT, MIN_HTTP_PORT, MAX_HTTP_PORT);
        http_enable = new EnumSetting("HTTP Enable", WEBSET, WA, "ESP120", "HTTP/Enable", DEFAULT_HTTP_STATE, &onoffOptions);
        http_block_during_motion = new EnumSetting("Block serving HTTP content during motion",
                                                   WEBSET,
                                                   WA,
                                                   "",
                                                   "HTTP/BlockDuringMotion",
                                                   DEFAULT_HTTP_BLOCKED_DURING_MOTION,
                                                   &onoffOptions);
    }
    Web_Server::~Web_Server() {
        end();
    }

    bool Web_Server::begin() {
        bool no_error = true;
        _setupdone    = false;

        if (!WebUI::http_enable->get()) {
            return false;
        }
        _port = WebUI::http_port->get();

        //create instance
        _webserver = new WebServer(_port);
#    ifdef ENABLE_AUTHENTICATION
        //here the list of headers to be recorded
        const char* headerkeys[]   = { "Cookie" };
        size_t      headerkeyssize = sizeof(headerkeys) / sizeof(char*);
        //ask server to track these headers
        _webserver->collectHeaders(headerkeys, headerkeyssize);
#    endif

        //here the list of headers to be recorded
        const char* headerkeys[]   = { "If-None-Match" };
        size_t      headerkeyssize = sizeof(headerkeys) / sizeof(char*);
        _webserver->collectHeaders(headerkeys, headerkeyssize);

        _socket_server = new WebSocketsServer(_port + 1);
        _socket_server->begin();
        _socket_server->onEvent(handle_Websocket_Event);

        _socket_serverv3 = new WebSocketsServer(_port + 2, "", "webui-v3");
        _socket_serverv3->begin();
        _socket_serverv3->onEvent(handle_Websocketv3_Event);

        //events functions
        //_web_events->onConnect(handle_onevent_connect);
        //events management
        // _webserver->addHandler(_web_events);
        _webserver->sendHeader("Access-Control-Allow-Origin", "*");
        _webserver->sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        _webserver->sendHeader("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
        //Web server handlers
        //trick to catch command line on "/" before file being processed
        _webserver->on("/", [this]() { handle_root("/"); });
        _webserver->on("/admin", [this]() { handle_root("/admin"); });
        _webserver->on("/wifi", [this]() { handle_root("/wifi"); });
        _webserver->on("/atc", [this]() { handle_root("/atc"); });

        _webserver->onNotFound(handle_not_found);

        //need to be there even no authentication to say to UI no authentication
        _webserver->on("/login", HTTP_ANY, handle_login);

        //web commands
        _webserver->on("/command", HTTP_ANY, handle_web_command);
        _webserver->on("/command_silent", HTTP_ANY, handle_web_command_silent);
        _webserver->on("/feedhold_reload", HTTP_ANY, handleFeedholdReload);

        //LocalFS
        _webserver->on("/files", HTTP_ANY, handleFileList, LocalFSFileupload);

        //web update
        _webserver->on("/updatefw", HTTP_ANY, handleUpdate, WebUpdateUpload);

        //Direct SD management
        _webserver->on("/upload", HTTP_ANY, handle_direct_SDFileList, SDFileUpload);
        //_webserver->on("/SD", HTTP_ANY, handle_SDCARD);

        if (WiFi.getMode() == WIFI_AP) {
            // if DNSServer is started with "*" for domain name, it will reply with
            // provided IP to all DNS request
            dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
            log_info("Captive Portal Started");

            // Redirect captive portal detection URLs directly to WiFi configuration page
            _webserver->on("/generate_204", HTTP_ANY, [this]() {
                _webserver->sendHeader(LOCATION_HEADER, "/wifi", true);
                _webserver->send(302, "text/plain", "Redirecting to WiFi configuration");
            });

            _webserver->on("/gconnectivitycheck.gstatic.com", HTTP_ANY, [this]() {
                _webserver->sendHeader(LOCATION_HEADER, "/wifi", true);
                _webserver->send(302, "text/plain", "Redirecting to WiFi configuration");
            });

            _webserver->on("/fwlink/", HTTP_ANY, [this]() {
                _webserver->sendHeader(LOCATION_HEADER, "/wifi", true);
                _webserver->send(302, "text/plain", "Redirecting to WiFi configuration");
            });
        }

        //SSDP service presentation
        if (WiFi.getMode() == WIFI_STA && WebUI::wifi_sta_ssdp->get()) {
            _webserver->on("/description.xml", HTTP_GET, handle_SSDP);
            //Add specific for SSDP
            SSDP.setSchemaURL("description.xml");
            SSDP.setHTTPPort(_port);
            SSDP.setName(wifi_config.Hostname().c_str());
            SSDP.setURL("/");
            SSDP.setDeviceType("upnp:rootdevice");
            /*Any customization could be here
            SSDP.setModelName (ESP32_MODEL_NAME);
            SSDP.setModelURL (ESP32_MODEL_URL);
            SSDP.setModelNumber (ESP_MODEL_NUMBER);
            SSDP.setManufacturer (ESP_MANUFACTURER_NAME);
            SSDP.setManufacturerURL (ESP_MANUFACTURER_URL);
            */

            //Start SSDP
            log_info("SSDP Started");
            SSDP.begin();
        }

        // Add pen configuration endpoints
        _webserver->on("/penconfig", HTTP_OPTIONS, [this]() {
            addCORSHeaders();
            _webserver->send(204);
        });
        _webserver->on("/penconfig", HTTP_GET, handleGetPenConfig);
        _webserver->on("/penconfig", HTTP_POST, handleSetPenConfig);
        _webserver->on("/penconfig", HTTP_DELETE, handleDeletePen);

        // Add tool configuration endpoints
        _webserver->on("/toolconfig", HTTP_OPTIONS, [this]() {
            addCORSHeaders();
            _webserver->send(204);
        });
        _webserver->on("/toolconfig", HTTP_GET, handleGetToolConfig);
        _webserver->on("/toolconfig", HTTP_POST, handleSetToolConfig);
        _webserver->on("/toolconfig/position", HTTP_POST, handleUpdateToolPosition);
        _webserver->on("/toolconfig/status", HTTP_GET, handleGetToolStatus);

        // Add explicit routes for JS module and CSS files with correct MIME types

        // Add generic handler for other assets in /ui/assets/
        _webserver->on("/penconfig", HTTP_DELETE, handleDeletePen);

        // Add explicit routes for JS module and CSS files with correct MIME types

        // Add generic handler for other assets in /ui/assets/
        _webserver->on("/ui/assets/", HTTP_GET, [this]() {
            String path = _webserver->uri();
            _webserver->sendHeader("Cache-Control", "public, max-age=31536000");

            if (path.endsWith(".js")) {
                _webserver->sendHeader("Content-Type", "application/javascript; charset=utf-8");
            } else if (path.endsWith(".css")) {
                _webserver->sendHeader("Content-Type", "text/css; charset=utf-8");
            } else if (path.endsWith(".woff2")) {
                _webserver->sendHeader("Content-Type", "font/woff2");
            } else if (path.endsWith(".ttf")) {
                _webserver->sendHeader("Content-Type", "font/ttf");
            }

            // Try to serve gzipped version first
            if (myStreamFile((path + ".gz").c_str())) {
                _webserver->sendHeader("Content-Encoding", "gzip");
            } else {
                myStreamFile(path.c_str());
            }
        });

        // Add generic handler for static assets with proper caching and compression
        _webserver->on("/index.html", HTTP_GET, [this]() {
            _webserver->sendHeader("Content-Type", "text/html; charset=utf-8");
            _webserver->sendHeader("Cache-Control", "public, max-age=31536000");  // Cache for 1 year
            myStreamFile("/index.html");
        });

        // Generic handler for assets folder
        _webserver->on("/assets/", HTTP_GET, [this]() {
            String path = _webserver->uri();
            _webserver->sendHeader("Cache-Control", "public, max-age=31536000");  // Cache for 1 year

            if (path.endsWith(".js")) {
                _webserver->sendHeader("Content-Type", "application/javascript; charset=utf-8");
            } else if (path.endsWith(".css")) {
                _webserver->sendHeader("Content-Type", "text/css; charset=utf-8");
            } else if (path.endsWith(".woff2")) {
                _webserver->sendHeader("Content-Type", "font/woff2");
            } else if (path.endsWith(".ttf")) {
                _webserver->sendHeader("Content-Type", "font/ttf");
            }

            // Try to serve gzipped version first
            if (myStreamFile((path + ".gz").c_str())) {
                _webserver->sendHeader("Content-Encoding", "gzip");
            } else {
                myStreamFile(path.c_str());
            }
        });

        // Static file handlers
        _webserver->on("/admin.html", HTTP_GET, [this]() {
            _webserver->sendHeader("Content-Type", "text/html; charset=utf-8");
            _webserver->sendHeader("Content-Encoding", "gzip");
            myStreamFile("/ui/admin.html.gz");
        });

        _webserver->on("/ui/index.html", HTTP_GET, [this]() {
            _webserver->sendHeader("Content-Type", "text/html; charset=utf-8");
            _webserver->sendHeader("Content-Encoding", "gzip");
            myStreamFile("/ui/index.html.gz");
        });

        // Handle all assets dynamically, ignoring hashes in filenames
        _webserver->on("/assets/", HTTP_GET, [this]() {
            String path = _webserver->uri();  // Define 'path' before using it
            // log_debug("Asset request: " << path.c_str());
            _webserver->sendHeader("Cache-Control", "public, max-age=31536000");  // Cache for 1 year
            _webserver->sendHeader("Access-Control-Allow-Origin", "*");

            if (path.indexOf("index-") >= 0 && path.endsWith(".js")) {
                _webserver->sendHeader("Content-Type", "application/javascript; charset=utf-8");
            } else if (path.indexOf("index-") >= 0 && path.endsWith(".css")) {
                _webserver->sendHeader("Content-Type", "text/css; charset=utf-8");
            } else if (path.endsWith(".woff2")) {
                _webserver->sendHeader("Content-Type", "font/woff2");
            } else if (path.endsWith(".ttf")) {
                _webserver->sendHeader("Content-Type", "font/ttf");
            }

            // Try to serve gzipped version first
            if (myStreamFile((path + ".gz").c_str())) {
                _webserver->sendHeader("Content-Encoding", "gzip");
            } else {
                myStreamFile(path.c_str());
            }
        });

        // Root handler with optimized file serving
        _webserver->on("/", [this]() {
            _webserver->sendHeader("Content-Type", "text/html; charset=utf-8");
            _webserver->sendHeader("Content-Encoding", "gzip");
            myStreamFile("/index.html.gz");
        });

        // Add toggle endpoint for pen change mode
        _webserver->on("/penchangemode", HTTP_GET, [this]() {
            addCORSHeaders();
            handlePenChangeMode();
        });

        _webserver->on("/penchangemode", HTTP_POST, [this]() {
            addCORSHeaders();
            handlePenChangeMode();
        });

        _webserver->on("/penchangemode", HTTP_OPTIONS, [this]() {
            addCORSHeaders();
            _webserver->send(204);
        });

        // Add restart endpoint
        _webserver->on("/restart", HTTP_ANY, [this]() {
            addCORSHeaders();
            AuthenticationLevel auth_level = is_authenticated();
            if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
                _webserver->send(401, "application/json", "{\"error\":\"Authentication failed\"}");
                return;
            }

            _webserver->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Restarting system\"}");
            delay_ms(500);  // Give time for response to be sent
            COMMANDS::restart_MCU();
        });

        log_info("HTTP started on port " << WebUI::http_port->get());
        //start webserver
        _webserver->begin();

        //add mDNS
        if (WiFi.getMode() == WIFI_STA && WebUI::wifi_sta_ssdp->get()) {
            MDNS.addService("http", "tcp", _port);
        }

        HashFS::hash_all();

        _setupdone = true;
        return no_error;
    }

    void Web_Server::end() {
        _setupdone = false;

        SSDP.end();

        //remove mDNS
        mdns_service_remove("_http", "_tcp");

        if (_socket_server) {
            delete _socket_server;
            _socket_server = NULL;
        }

        if (_socket_serverv3) {
            delete _socket_serverv3;
            _socket_serverv3 = NULL;
        }

        if (_webserver) {
            delete _webserver;
            _webserver = NULL;
        }

#    ifdef ENABLE_AUTHENTICATION
        while (_head) {
            AuthenticationIP* current = _head;
            _head                     = _head->_next;
            delete current;
        }
        _nb_ip = 0;
#    endif
    }

    // Move the endsWithCI function declaration and implementation up here, before streamFileFromPath
    static bool endsWithCI(const char* suffix, const char* test) {
        size_t slen = strlen(suffix);
        size_t tlen = strlen(test);

        if (slen > tlen) {
            return false;
        }

        // Compare from the end of both strings
        const char* s = suffix + slen - 1;
        const char* t = test + tlen - 1;

        while (s >= suffix) {
            if (tolower(*s) != tolower(*t)) {
                return false;
            }
            s--;
            t--;
        }
        return true;
    }

    bool Web_Server::myStreamFile(const char* path, bool download) {
        std::error_code ec;
        // log_debug("Trying to serve file: " << path);

        // Try to open the file from the SD card first
        FluidPath sdPath { path, sdName, ec };
        if (!ec) {
            // log_debug("Found file on SD: " << sdPath.c_str());
            return streamFileFromPath(sdPath, download);
        }
        // log_debug("SD file error: " << ec.message());

        // If the file isn't on the SD card, fallback to SPIFFS
        FluidPath spiffsPath { path, localfsName, ec };
        if (!ec) {
            // log_debug("Found file on SPIFFS: " << spiffsPath.c_str());
            return streamFileFromPath(spiffsPath, download);
        }
        // log_debug("SPIFFS file error: " << ec.message());

        return false;
    }

    bool Web_Server::streamFileFromPath(const FluidPath& fpath, bool download) {
        FileStream*         file   = nullptr;
        bool                isGzip = false;
        std::string         actualPath;
        size_t              fileSize   = 0;
        static const size_t CHUNK_SIZE = 2048;  // Read 1KB at a time
        uint8_t             chunk[CHUNK_SIZE];

        // First try to get file size without keeping file open
        try {
            file       = new FileStream(fpath, "r", "");
            actualPath = fpath.c_str();
            fileSize   = file->size();
            delete file;
            file = nullptr;
        } catch (const Error err) {
            if (file) {
                delete file;
                file = nullptr;
            }

            try {
                std::filesystem::path gzpath(fpath);
                gzpath += ".gz";
                file       = new FileStream(gzpath, "r", "");
                isGzip     = true;
                actualPath = gzpath.string();
                fileSize   = file->size();
                delete file;
                file = nullptr;
            } catch (const Error err) {
                if (file) {
                    delete file;
                }
                // log_debug(fpath.c_str() << " not found");
                return false;
            }
        }

        // Try to get hash
        std::string hash = HashFS::hash(actualPath);
        // log_debug("File hash: " << (hash.length() ? hash : "none") << " for " << actualPath);

        // Check if client has valid cached version
        if (hash.length() && _webserver->hasHeader("If-None-Match")) {
            if (std::string(_webserver->header("If-None-Match").c_str()) == hash) {
                // log_debug(actualPath << " is cached by client");
                _webserver->send(304);
                return true;
            }
        }

        // Set response headers
        if (download) {
            _webserver->sendHeader("Content-Disposition", "attachment");
        }
        if (hash.length()) {
            _webserver->sendHeader("ETag", hash.c_str());
        }
        _webserver->setContentLength(fileSize);
        if (isGzip) {
            _webserver->sendHeader("Content-Encoding", "gzip");
        }

        // Set correct content type
        const char* contentType = getContentType(fpath.c_str());
        if (endsWithCI(".js", fpath.c_str())) {
            _webserver->sendHeader("Content-Type", "application/javascript; charset=utf-8");
        }
        _webserver->send(200, contentType, "");

        // Now open file again for streaming
        bool success = true;
        try {
            file = new FileStream(isGzip ? (fpath.c_str() + std::string(".gz")) : fpath.c_str(), "r", "");
            // Stream file in chunks
            size_t bytesRemaining = fileSize;
            while (bytesRemaining > 0) {
                size_t bytesToRead = min(CHUNK_SIZE, bytesRemaining);
                if (file->read(chunk, bytesToRead) != bytesToRead) {
                    success = false;
                    break;
                }
                if (_webserver->client().write(chunk, bytesToRead) != bytesToRead) {
                    success = false;
                    break;
                }
                bytesRemaining -= bytesToRead;
                delay(0);  // Prevent watchdog trigger
            }
        } catch (const Error err) { success = false; }

        if (file) {
            delete file;
        }

        // log_debug("Served " << actualPath << " with type " << contentType);
        return success;
    }

    void Web_Server::sendWithOurAddress(const char* content, int code) {
        auto        ip    = WiFi.getMode() == WIFI_STA ? WiFi.localIP() : WiFi.softAPIP();
        std::string ipstr = IP_string(ip);
        if (_port != 80) {
            ipstr += ":";
            ipstr += std::to_string(_port);
        }

        std::string scontent(content);
        replace_string_in_place(scontent, "$WEB_ADDRESS$", ipstr);
        replace_string_in_place(scontent, "$QUERY$", _webserver->uri().c_str());
        _webserver->send(code, "text/html", scontent.c_str());
    }

    // Captive Portal Page for use in AP mode
    const char PAGE_CAPTIVE[] =
        "<HTML>\n<HEAD>\n<title>Captive Portal</title> \n</HEAD>\n<BODY>\n<CENTER>Captive Portal page : $QUERY$- you will be "
        "redirected...\n<BR><BR>\nif not redirected, <a href='http://$WEB_ADDRESS$'>click here</a>\n<BR><BR>\n<PROGRESS name='prg' "
        "id='prg'></PROGRESS>\n\n<script>\nvar i = 0; \nvar x = document.getElementById(\"prg\"); \nx.max=5; \nvar "
        "interval=setInterval(function(){\ni=i+1; \nvar x = document.getElementById(\"prg\"); \nx.value=i; \nif (i>5) "
        "\n{\nclearInterval(interval);\nwindow.location.href='/';\n}\n},1000);\n</script>\n</CENTER>\n</BODY>\n</HTML>\n\n";

    void Web_Server::sendCaptivePortal() {
        sendWithOurAddress(PAGE_CAPTIVE, 200);
    }

    //Default 404 page that is sent when a request cannot be satisfied
    const char PAGE_404[] =
        "<HTML>\n<HEAD>\n<title>Redirecting...</title> \n</HEAD>\n<BODY>\n<CENTER>Unknown page : $QUERY$- you will be "
        "redirected...\n<BR><BR>\nif not redirected, <a href='http://$WEB_ADDRESS$'>click here</a>\n<BR><BR>\n<PROGRESS name='prg' "
        "id='prg'></PROGRESS>\n\n<script>\nvar i = 0; \nvar x = document.getElementById(\"prg\"); \nx.max=5; \nvar "
        "interval=setInterval(function(){\ni=i+1; \nvar x = document.getElementById(\"prg\"); \nx.value=i; \nif (i>5) "
        "\n{\nclearInterval(interval);\nwindow.location.href='/';\n}\n},1000);\n</script>\n</CENTER>\n</BODY>\n</HTML>\n\n";

    void Web_Server::send404Page() {
        sendWithOurAddress(PAGE_404, 404);
    }

    void Web_Server::handle_root(const String& path) {
        log_info("WebUI: Request from " << _webserver->client().remoteIP());

        // If in AP mode and requesting root page, redirect to WiFi config page
        if (path == "/" && WiFi.getMode() == WIFI_AP) {
            _webserver->sendHeader(LOCATION_HEADER, "/wifi", true);
            _webserver->send(302, "text/plain", "Redirecting to WiFi configuration");
            return;
        }

        if (path == "/admin") {
            if (myStreamFile("/ui/admin.html"))
                return;
        } else if (path == "/wifi") {
            if (myStreamFile("/ui/wifi.html"))
                return;
        } else if (path == "/atc") {
            if (myStreamFile("/ui/atc.html"))
                return;
        } else if (path == "/") {
            // Explicitly set content type for index.html
            _webserver->sendHeader("Content-Type", "text/html; charset=utf-8");
            if (myStreamFile("/ui/index.html")) {
                return;
            }
        }

        // If we did not send any HTML, send the default content
        _webserver->sendHeader("Content-Encoding", "gzip");
        _webserver->send_P(200, "text/html", PAGE_NOFILES, PAGE_NOFILES_SIZE);
    }

    void Web_Server::handle_options() {
        _webserver->sendHeader("Access-Control-Allow-Origin", "*");
        _webserver->sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        _webserver->sendHeader("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
        _webserver->send(204);  // No Content response for OPTIONS request
    }

    // Handle filenames and other things that are not explicitly registered
    void Web_Server::handle_not_found() {
        if (is_authenticated() == AuthenticationLevel::LEVEL_GUEST) {
            _webserver->sendHeader(LOCATION_HEADER, "/");
            _webserver->send(302);
            return;
        }

        std::string path(_webserver->urlDecode(_webserver->uri()).c_str());

        // If pen change mode is active, restrict access to non-ATC pages
        // This ensures safety by keeping the user in the ATC interface until the flag is unset
        if (pen_change && path.find("/atc") == std::string::npos && path.find("/penchangemode") == std::string::npos &&
            path != "/command" &&         // Allow commands to be sent
            path != "/command_silent") {  // Allow silent commands

            // Redirect to ATC page with a clear message about the restriction
            _webserver->send(403,
                             "text/html",
                             "<html><body><h2>Pen Change Mode Active</h2>"
                             "<p>The machine is in pen change mode. Other UI functions are temporarily restricted.</p>"
                             "<p><a href='/atc'>Go to ATC interface</a></p>"
                             "</body></html>");
            return;
        }

        // Continue with normal request processing
        if (path.rfind("/api/", 0) == 0) {
            _webserver->send(404);
            return;
        }

        // Download a file. The true forces a download instead of displaying the file
        if (myStreamFile(path.c_str(), true)) {
            return;
        }

        if (WiFi.getMode() == WIFI_AP) {
            sendCaptivePortal();
            return;
        }

        // This lets the user customize the not-found page by
        // putting a "404.htm" file on the local filesystem
        if (myStreamFile("404.htm")) {
            return;
        }

        send404Page();
    }

    //http SSDP xml presentation
    void Web_Server::handle_SSDP() {
        StreamString sschema;
        if (!sschema.reserve(1024)) {
            _webserver->send(500);
            return;
        }
        const char*       templ = "<?xml version=\"1.0\"?>"
                                  "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
                                  "<specVersion>"
                                  "<major>1</major>"
                                  "<minor>0</minor>"
                                  "</specVersion>"
                                  "<URLBase>http://%s:%u/</URLBase>"
                                  "<device>"
                                  "<deviceType>upnp:rootdevice</deviceType>"
                                  "<friendlyName>%s</friendlyName>"
                                  "<presentationURL>/</presentationURL>"
                                  "<serialNumber>%s</serialNumber>"
                                  "<modelName>ESP32</modelName>"
                                  "<modelNumber>Marlin</modelNumber>"
                                  "<modelURL>http://espressif.com/en/products/hardware/esp-wroom-32/overview</modelURL>"
                                  "<manufacturer>Espressif Systems</manufacturer>"
                                  "<manufacturerURL>http://espressif.com</manufacturerURL>"
                                  "<UDN>uuid:%s</UDN>"
                                  "</device>"
                                  "</root>\r\n"
                                  "\r\n";
        char              uuid[37];
        const std::string sip    = IP_string(WiFi.localIP());
        uint32_t          chipId = (uint16_t)(ESP.getEfuseMac() >> 32);
        sprintf(uuid,
                "38323636-4558-4dda-9188-cda0e6%02x%02x%02x",
                (uint16_t)((chipId >> 16) & 0xff),
                (uint16_t)((chipId >> 8) & 0xff),
                (uint16_t)chipId & 0xff);
        const std::string serialNumber = std::to_string(chipId);
        sschema.printf(templ, sip, _port, wifi_config.Hostname(), serialNumber, uuid);
        _webserver->send(200, "text/xml", sschema);
    }

    // WebUI sends a PAGEID arg to identify the websocket it is using
    int Web_Server::getPageid() {
        if (_webserver->hasArg("PAGEID")) {
            return _webserver->arg("PAGEID").toInt();
        }
        return -1;
    }
    void Web_Server::synchronousCommand(const char* cmd, bool silent, AuthenticationLevel auth_level) {
        char line[256];
        strncpy(line, cmd, 255);
        webClient.attachWS(_webserver, silent);
        Error err = settings_execute_line(line, webClient, auth_level);
        if (err != Error::Ok) {
            std::string answer = "Error: ";
            const char* msg    = errorString(err);
            if (msg) {
                answer += msg;
            } else {
                answer += std::to_string(static_cast<int>(err));
            }
            answer += "\n";
            webClient.sendError(500, answer);
        } else {
            // This will send a 200 if it hasn't already been sent
            webClient.write(nullptr, 0);
        }
        webClient.detachWS();
    }
    void Web_Server::websocketCommand(const char* cmd, int pageid, AuthenticationLevel auth_level) {
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            _webserver->send(401, "text/plain", "Authentication failed\n");
            return;
        }

        bool hasError = WSChannels::runGCode(pageid, cmd);
        _webserver->send(hasError ? 500 : 200, "text/plain", hasError ? "WebSocket dead" : "");
    }
    void Web_Server::_handle_web_command(bool silent) {
        _webserver->sendHeader("Access-Control-Allow-Origin", "*");
        AuthenticationLevel auth_level = is_authenticated();
        if (_webserver->hasArg("cmd")) {  // WebUI3

            auto cmd = _webserver->arg("cmd");
            // [ESPXXX] commands expect data in the HTTP response
            if (cmd.startsWith("[ESP") || cmd.startsWith("$/")) {
                synchronousCommand(cmd.c_str(), silent, auth_level);
            } else {
                websocketCommand(cmd.c_str(), -1, auth_level);  // WebUI3 does not support PAGEID
            }
            return;
        }
        if (_webserver->hasArg("plain")) {
            synchronousCommand(_webserver->arg("plain").c_str(), silent, auth_level);
            return;
        }
        if (_webserver->hasArg("commandText")) {
            auto cmd = _webserver->arg("commandText");
            if (cmd.startsWith("[ESP")) {
                // [ESPXXX] commands expect data in the HTTP response
                // Only the fallback web page uses commandText with [ESPxxx]
                synchronousCommand(cmd.c_str(), silent, auth_level);
            } else {
                websocketCommand(_webserver->arg("commandText").c_str(), getPageid(), auth_level);
            }
            return;
        }
        _webserver->send(500, "text/plain", "Invalid command");
    }

    //login status check
    void Web_Server::handle_login() {
#    ifdef ENABLE_AUTHENTICATION
        const char* smsg;
        std::string sUser, sPassword;
        const char* auths;
        int         code            = 200;
        bool        msg_alert_error = false;
        //disconnect can be done anytime no need to check credential
        if (_webserver->hasArg("DISCONNECT")) {
            std::string cookie(_webserver->header("Cookie").c_str());
            int         pos = cookie.find("ESPSESSIONID=");
            std::string sessionID;
            if (pos != std::string::npos) {
                int pos2  = cookie.find(";", pos);
                sessionID = cookie.substr(pos + strlen("ESPSESSIONID="), pos2);
            }
            ClearAuthIP(_webserver->client().remoteIP(), sessionID);
            _webserver->sendHeader("Set-Cookie", "ESPSESSIONID=0");
            _webserver->sendHeader("Cache-Control", "no-cache");
            sendAuth("Ok", "guest", "");
            //_webserver->client().stop();
            return;
        }

        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            auths = "guest";
        } else if (auth_level == AuthenticationLevel::LEVEL_USER) {
            auths = "user";
        } else if (auth_level == AuthenticationLevel::LEVEL_ADMIN) {
            auths = "admin";
        } else {
            auths = "???";
        }

        //check is it is a submission or a query
        if (_webserver->hasArg("SUBMIT")) {
            //is there a correct list of query?
            if (_webserver->hasArg("PASSWORD") && _webserver->hasArg("USER")) {
                //USER
                sUser = _webserver->arg("USER").c_str();
                if (!((sUser == DEFAULT_ADMIN_LOGIN) || (sUser == DEFAULT_USER_LOGIN))) {
                    msg_alert_error = true;
                    smsg            = "Error : Incorrect User";
                    code            = 401;
                }

                if (msg_alert_error == false) {
                    //Password
                    sPassword = _webserver->arg("PASSWORD").c_str();
                    std::string sadminPassword(admin_password->get());
                    std::string suserPassword(user_password->get());

                    if (!(sUser == DEFAULT_ADMIN_LOGIN && sPassword == sadminPassword) ||
                        (sUser == DEFAULT_USER_LOGIN && sPassword == suserPassword)) {
                        msg_alert_error = true;
                        smsg            = "Error: Incorrect password";
                        code            = 401;
                    }
                }
            } else {
                msg_alert_error = true;
                smsg            = "Error: Missing data";
                code            = 500;
            }
            //change password
            if (_webserver->hasArg("PASSWORD") && _webserver->hasArg("USER") && _webserver->hasArg("NEWPASSWORD") &&
                (msg_alert_error == false)) {
                std::string newpassword(_webserver->arg("NEWPASSWORD").c_str());

                char pwdbuf[MAX_LOCAL_PASSWORD_LENGTH + 1];
                newpassword.toCharArray(pwdbuf, MAX_LOCAL_PASSWORD_LENGTH + 1);

                Error err;

                if (sUser == DEFAULT_ADMIN_LOGIN) {
                    err = admin_password->setStringValue(pwdbuf);
                } else {
                    err = user_password->setStringValue(pwdbuf);
                }
                if (err != Error::Ok) {
                    msg_alert_error = true;
                    smsg            = "Error: Password cannot contain spaces";
                    code            = 500;
                }
            }
            if ((code == 200) || (code == 500)) {
                AuthenticationLevel current_auth_level;
                if (sUser == DEFAULT_ADMIN_LOGIN) {
                    current_auth_level = AuthenticationLevel::LEVEL_ADMIN;
                } else if (sUser == DEFAULT_USER_LOGIN) {
                    current_auth_level = AuthenticationLevel::LEVEL_USER;
                } else {
                    current_auth_level = AuthenticationLevel::LEVEL_GUEST;
                }
                //create Session
                if ((current_auth_level != auth_level) || (auth_level == AuthenticationLevel::LEVEL_GUEST)) {
                    AuthenticationIP* current_auth = new AuthenticationIP;
                    current_auth->level            = current_auth_level;
                    current_auth->ip               = _webserver->client().remoteIP();
                    strcpy(current_auth->sessionID, create_session_ID());
                    strcpy(current_auth->userID, sUser.c_str());
                    current_auth->last_time = millis();
                    if (AddAuthIP(current_auth)) {
                        std::string tmps = "ESPSESSIONID=";
                        tmps += current_auth->sessionID.c_str();
                        _webserver->sendHeader("Set-Cookie", tmps);
                        _webserver->sendHeader("Cache-Control", "no-cache");
                        switch (current_auth->level) {
                            case AuthenticationLevel::LEVEL_ADMIN:
                                auths = "admin";
                                break;
                            case AuthenticationLevel::LEVEL_USER:
                                auths = "user";
                                break;
                            default:
                                auths = "guest";
                                break;
                        }
                    } else {
                        delete current_auth;
                        msg_alert_error = true;
                        code            = 500;
                        smsg            = "Error: Too many connections";
                    }
                }
            }
            if (code == 200) {
                smsg = "Ok";
            }

            sendAuth("Ok", "guest", "");
        } else {
            if (auth_level != AuthenticationLevel::LEVEL_GUEST) {
                std::string cookie(_webserver->header("Cookie").c_str());
                int         pos = cookie.find("ESPSESSIONID=");
                std::string sessionID;
                if (pos != std::string::npos) {
                    int pos2                            = cookie.find(";", pos);
                    sessionID                           = cookie.substr(pos + strlen("ESPSESSIONID="), pos2);
                    AuthenticationIP* current_auth_info = GetAuth(_webserver->client().remoteIP(), sessionID.c_str());
                    if (current_auth_info != NULL) {
                        sUser = current_auth_info->userID;
                    }
                }
            }
            sendAuth(smsg, auths, "");
        }
#    else
        sendAuth("Ok", "admin", "");
#    endif
    }

    // This page is used when you try to reload WebUI during motion,
    // to avoid interrupting that motion.  It lets you wait until
    // motion is finished or issue a feedhold.
    void Web_Server::handleReloadBlocked() {
        _webserver->send(503,
                         "text/html",
                         "<!DOCTYPE html><html><body>"
                         "<h3>Cannot load WebUI while moving</h3>"
                         "<button onclick='window.location.reload()'>Retry</button>"
                         "&nbsp;Retry (you must first wait for motion to finish)<br><br>"
                         "<button onclick='window.location.replace(\"/feedhold_reload\")'>Feedhold</button>"
                         "&nbsp;Stop the motion with feedhold and then retry<br>"
                         "</body></html>");
    }
    // This page issues a feedhold to pause the motion then retries the WebUI reload
    void Web_Server::handleFeedholdReload() {
        protocol_send_event(&feedHoldEvent);
        // Go to the main page
        _webserver->sendHeader(LOCATION_HEADER, "/");
        _webserver->send(302);
    }

    //push error code and message to websocket.  Used by upload code
    void Web_Server::pushError(int code, const char* st, bool web_error, uint16_t timeout) {
        if (_socket_server && st) {
            std::string s("ERROR:");
            s += std::to_string(code) + ":";
            s += st;

            WSChannels::sendError(getPageid(), st);

            if (web_error != 0 && _webserver && _webserver->client().available() > 0) {
                _webserver->send(web_error, "text/xml", st);
            }

            uint32_t start_time = millis();
            while ((millis() - start_time) < timeout) {
                _socket_server->loop();
                delay_ms(10);
            }

            if (_socket_serverv3) {
                start_time = millis();
                while ((millis() - start_time) < timeout) {
                    _socket_serverv3->loop();
                    delay_ms(10);
                }
            }
        }
    }

    //abort reception of packages
    void Web_Server::cancelUpload() {
        if (_webserver && _webserver->client().available() > 0) {
            HTTPUpload& upload = _webserver->upload();
            upload.status      = UPLOAD_FILE_ABORTED;
            errno              = ECONNABORTED;
            _webserver->client().stop();
            delay(100);
        }
    }

    //LocalFS files uploader handle
    void Web_Server::fileUpload(const char* fs) {
        HTTPUpload& upload = _webserver->upload();
        //this is only for admin and user
        if (is_authenticated() == AuthenticationLevel::LEVEL_GUEST) {
            _upload_status = UploadStatus::FAILED;
            log_info("Upload rejected");
            sendJSON(401, "{\"status\":\"Authentication failed!\"}");
            pushError(ESP_ERROR_AUTHENTICATION, "Upload rejected", 401);
        } else {
            if ((_upload_status != UploadStatus::FAILED) || (upload.status == UPLOAD_FILE_START)) {
                if (upload.status == UPLOAD_FILE_START) {
                    _webserver->sendHeader("Access-Control-Allow-Origin", "*");
                    std::string sizeargname(upload.filename.c_str());
                    sizeargname += "S";
                    size_t filesize = _webserver->hasArg(sizeargname.c_str()) ? _webserver->arg(sizeargname.c_str()).toInt() : 0;
                    uploadStart(upload.filename.c_str(), filesize, fs);
                } else if (upload.status == UPLOAD_FILE_WRITE) {
                    uploadWrite(upload.buf, upload.currentSize);
                } else if (upload.status == UPLOAD_FILE_END) {
                    std::string sizeargname(upload.filename.c_str());
                    sizeargname += "S";
                    size_t filesize = _webserver->hasArg(sizeargname.c_str()) ? _webserver->arg(sizeargname.c_str()).toInt() : 0;
                    uploadEnd(filesize);
                } else {  //Upload cancelled
                    uploadStop();
                    return;
                }
            }
        }
        uploadCheck();
    }

    void Web_Server::sendJSON(int code, const char* s) {
        _webserver->sendHeader("Cache-Control", "no-cache");
        _webserver->send(200, "application/json", s);
    }

    void Web_Server::sendAuth(const char* status, const char* level, const char* user) {
        std::string s;
        JSONencoder j(&s);
        j.begin();
        j.member("status", status);
        if (*level != '\0') {
            j.member("authentication_lvl", level);
        }
        if (*user != '\0') {
            j.member("user", user);
        }
        j.end();
        sendJSON(200, s);
    }

    void Web_Server::sendStatus(int code, const char* status) {
        std::string s;
        JSONencoder j(&s);
        j.begin();
        j.member("status", status);
        j.end();
        sendJSON(code, s);
    }

    void Web_Server::sendAuthFailed() {
        sendStatus(401, "Authentication failed");
    }

    void Web_Server::LocalFSFileupload() {
        fileUpload(localfsName);
    }
    void Web_Server::SDFileUpload() {
        fileUpload(sdName);
    }

    //Web Update handler
    void Web_Server::handleUpdate() {
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level != AuthenticationLevel::LEVEL_ADMIN) {
            _upload_status = UploadStatus::NONE;
            _webserver->send(403, "text/plain", "Not allowed, log in first!\n");
            return;
        }

        sendStatus(200, std::to_string(int(_upload_status)).c_str());

        // Automatic restart on successful update
        if (_upload_status == UploadStatus::SUCCESSFUL) {
            delay_ms(1000);
            COMMANDS::restart_MCU();
        } else {
            _upload_status = UploadStatus::NONE;
        }
    }

    //File upload for Web update
    void Web_Server::WebUpdateUpload() {
        static size_t   last_upload_update;
        static uint32_t maxSketchSpace = 0;

        //only admin can update FW
        if (is_authenticated() != AuthenticationLevel::LEVEL_ADMIN) {
            _upload_status = UploadStatus::FAILED;
            log_info("Upload rejected");
            sendAuthFailed();
            pushError(ESP_ERROR_AUTHENTICATION, "Upload rejected", 401);
        } else {
            //get current file ID
            HTTPUpload& upload = _webserver->upload();
            if ((_upload_status != UploadStatus::FAILED) || (upload.status == UPLOAD_FILE_START)) {
                //Upload start
                //**************
                if (upload.status == UPLOAD_FILE_START) {
                    log_info("Update Firmware");
                    _upload_status = UploadStatus::ONGOING;
                    std::string sizeargname(upload.filename.c_str());
                    sizeargname += "S";
                    if (_webserver->hasArg(sizeargname.c_str())) {
                        maxSketchSpace = _webserver->arg(sizeargname.c_str()).toInt();
                    }
                    //check space
                    size_t flashsize = 0;
                    if (esp_ota_get_running_partition()) {
                        const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
                        if (partition) {
                            flashsize = partition->size;
                        }
                    }
                    if (flashsize < maxSketchSpace) {
                        pushError(ESP_ERROR_NOT_ENOUGH_SPACE, "Upload rejected, not enough space");
                        _upload_status = UploadStatus::FAILED;
                        log_info("Update cancelled");
                    }
                    if (_upload_status != UploadStatus::FAILED) {
                        last_upload_update = 0;
                        if (!Update.begin()) {  //start with max available size
                            _upload_status = UploadStatus::FAILED;
                            log_info("Update cancelled");
                            pushError(ESP_ERROR_NOT_ENOUGH_SPACE, "Upload rejected, not enough space");
                        } else {
                            log_info("Update 0%");
                        }
                    }
                    //Upload write
                    //**************
                } else if (upload.status == UPLOAD_FILE_WRITE) {
                    delay_ms(1);
                    //check if no error
                    if (_upload_status == UploadStatus::ONGOING) {
                        if (((100 * upload.totalSize) / maxSketchSpace) != last_upload_update) {
                            if (maxSketchSpace > 0) {
                                last_upload_update = (100 * upload.totalSize) / maxSketchSpace;
                            } else {
                                last_upload_update = upload.totalSize;
                            }

                            log_info("Update " << last_upload_update << "%");
                        }
                        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                            _upload_status = UploadStatus::FAILED;
                            log_info("Update write failed");
                            pushError(ESP_ERROR_FILE_WRITE, "File write failed");
                        }
                    }
                    //Upload end
                    //**************
                } else if (upload.status == UPLOAD_FILE_END) {
                    if (Update.end(true)) {  //true to set the size to the current progress
                        //Now Reboot
                        log_info("Update 100%");
                        _upload_status = UploadStatus::SUCCESSFUL;
                    } else {
                        _upload_status = UploadStatus::FAILED;
                        log_info("Update failed");
                        pushError(ESP_ERROR_UPLOAD, "Update upload failed");
                    }
                } else if (upload.status == UPLOAD_FILE_ABORTED) {
                    log_info("Update failed");
                    _upload_status = UploadStatus::FAILED;
                    return;
                }
            }
        }

        if (_upload_status == UploadStatus::FAILED) {
            cancelUpload();
            Update.end();
        }
    }

    void Web_Server::handleFileOps(const char* fs) {
        //this is only for admin and user
        if (is_authenticated() == AuthenticationLevel::LEVEL_GUEST) {
            _upload_status = UploadStatus::NONE;
            sendAuthFailed();
            return;
        }

        std::error_code ec;

        std::string path("");
        std::string sstatus("Ok");
        if ((_upload_status == UploadStatus::FAILED) || (_upload_status == UploadStatus::FAILED)) {
            sstatus = "Upload failed";
        }
        _upload_status      = UploadStatus::NONE;
        bool     list_files = true;
        uint64_t totalspace = 0;
        uint64_t usedspace  = 0;

        //get current path
        if (_webserver->hasArg("path")) {
            path += _webserver->arg("path").c_str();
            // path.trim();
            replace_string_in_place(path, "//", "/");
            if (path[path.length() - 1] == '/') {
                path = path.substr(0, path.length() - 1);
            }
            if (path.length() & path[0] == '/') {
                path = path.substr(1);
            }
        }

        FluidPath fpath { path, fs, ec };
        if (ec) {
            sendJSON(200, "{\"status\":\"No SD card\"}");
            return;
        }

        // Handle deletions and directory creation
        if (_webserver->hasArg("action") && _webserver->hasArg("filename")) {
            std::string action(_webserver->arg("action").c_str());
            std::string filename = std::string(_webserver->arg("filename").c_str());
            if (action == "delete") {
                if (stdfs::remove(fpath / filename, ec)) {
                    sstatus = filename + " deleted";
                    HashFS::delete_file(fpath / filename);
                } else {
                    sstatus = "Cannot delete ";
                    sstatus += filename + " " + ec.message();
                }
            } else if (action == "deletedir") {
                stdfs::path dirpath { fpath / filename };
                // log_debug("Deleting directory " << dirpath);
                int count = stdfs::remove_all(dirpath, ec);
                if (count > 0) {
                    sstatus = filename + " deleted";
                    HashFS::report_change();
                } else {
                    // log_debug("remove_all returned " << count);
                    sstatus = "Cannot delete ";
                    sstatus += filename + " " + ec.message();
                }
            } else if (action == "createdir") {
                if (stdfs::create_directory(fpath / filename, ec)) {
                    sstatus = filename + " created";
                    HashFS::report_change();
                } else {
                    sstatus = "Cannot create ";
                    sstatus += filename + " " + ec.message();
                }
            } else if (action == "rename") {
                if (!_webserver->hasArg("newname")) {
                    sstatus = "Missing new filename";
                } else {
                    std::string newname = std::string(_webserver->arg("newname").c_str());
                    std::filesystem::rename(fpath / filename, fpath / newname, ec);
                    if (ec) {
                        sstatus = "Cannot rename ";
                        sstatus += filename + " " + ec.message();
                    } else {
                        sstatus = filename + " renamed to " + newname;
                        HashFS::rename_file(fpath / filename, fpath / newname);
                    }
                }
            }
        }

        //check if no need build file list
        if (_webserver->hasArg("dontlist") && _webserver->arg("dontlist") == "yes") {
            list_files = false;
        }

        std::string        s;
        WebUI::JSONencoder j(&s);
        j.begin();

        if (list_files) {
            auto iter = stdfs::directory_iterator { fpath, ec };
            if (!ec) {
                j.begin_array("files");
                for (auto const& dir_entry : iter) {
                    j.begin_object();
                    j.member("name", dir_entry.path().filename());
                    j.member("shortname", dir_entry.path().filename());
                    j.member("size", dir_entry.is_directory() ? -1 : dir_entry.file_size());
                    j.member("datetime", "");
                    j.end_object();
                }
                j.end_array();
            }
        }

        auto space = stdfs::space(fpath, ec);
        totalspace = space.capacity;
        usedspace  = totalspace - space.available;

        j.member("path", path.c_str());
        j.member("total", formatBytes(totalspace));
        j.member("used", formatBytes(usedspace + 1));

        uint32_t percent = totalspace ? (usedspace * 100) / totalspace : 100;

        j.member("occupation", percent);
        j.member("status", sstatus);
        j.end();
        sendJSON(200, s);
    }

    void Web_Server::handle_direct_SDFileList() {
        handleFileOps(sdName);
    }
    void Web_Server::handleFileList() {
        handleFileOps(localfsName);
    }

    // File upload
    void Web_Server::uploadStart(const char* filename, size_t filesize, const char* fs) {
        std::error_code ec;

        FluidPath fpath { filename, fs, ec };
        if (ec) {
            _upload_status = UploadStatus::FAILED;
            log_info("Upload filesystem inaccessible");
            pushError(ESP_ERROR_FILE_CREATION, "Upload rejected, filesystem inaccessible");
            return;
        }

        auto space = stdfs::space(fpath);
        if (filesize && filesize > space.available) {
            // If the file already exists, maybe there will be enough space
            // when we replace it.
            auto existing_size = stdfs::file_size(fpath, ec);
            if (ec || (filesize > (space.available + existing_size))) {
                _upload_status = UploadStatus::FAILED;
                log_info("Upload not enough space");
                pushError(ESP_ERROR_NOT_ENOUGH_SPACE, "Upload rejected, not enough space");
                return;
            }
        }

        if (_upload_status != UploadStatus::FAILED) {
            //Create file for writing
            try {
                _uploadFile    = new FileStream(fpath, "w");
                _upload_status = UploadStatus::ONGOING;
            } catch (const Error err) {
                _uploadFile    = nullptr;
                _upload_status = UploadStatus::FAILED;
                log_info("Upload failed - cannot create file");
                pushError(ESP_ERROR_FILE_CREATION, "File creation failed");
            }
        }
    }

    void Web_Server::uploadWrite(uint8_t* buffer, size_t length) {
        delay_ms(1);
        if (_uploadFile && _upload_status == UploadStatus::ONGOING) {
            //no error write post data
            if (length != _uploadFile->write(buffer, length)) {
                _upload_status = UploadStatus::FAILED;
                log_info("Upload failed - file write failed");
                pushError(ESP_ERROR_FILE_WRITE, "File write failed");
            }
        } else {  //if error set flag UploadStatus::FAILED
            _upload_status = UploadStatus::FAILED;
            log_info("Upload failed - file not open");
            pushError(ESP_ERROR_FILE_WRITE, "File not open");
        }
    }

    void Web_Server::uploadEnd(size_t filesize) {
        //if file is open close it
        if (_uploadFile) {
            //            delete _uploadFile;
            // _uploadFile = nullptr;

            std::string pathname = _uploadFile->fpath();
            delete _uploadFile;
            _uploadFile = nullptr;
            // log_debug("pathname " << pathname);

            FluidPath filepath { pathname, "" };

            HashFS::rehash_file(filepath);

            // Check size
            if (filesize) {
                uint32_t actual_size;
                try {
                    actual_size = stdfs::file_size(filepath);
                } catch (const Error err) { actual_size = 0; }

                if (filesize != actual_size) {
                    _upload_status = UploadStatus::FAILED;
                    pushError(ESP_ERROR_UPLOAD, "File upload mismatch");
                    log_info("Upload failed - size mismatch - exp " << filesize << " got " << actual_size);
                }
            }
        } else {
            _upload_status = UploadStatus::FAILED;
            log_info("Upload failed - file not open");
            pushError(ESP_ERROR_FILE_CLOSE, "File close failed");
        }
        if (_upload_status == UploadStatus::ONGOING) {
            _upload_status = UploadStatus::SUCCESSFUL;
        } else {
            _upload_status = UploadStatus::FAILED;
            pushError(ESP_ERROR_UPLOAD, "Upload error 8");
        }
    }
    void Web_Server::uploadStop() {
        _upload_status = UploadStatus::FAILED;
        log_info("Upload cancelled");
        if (_uploadFile) {
            std::filesystem::path filepath = _uploadFile->fpath();
            delete _uploadFile;
            _uploadFile = nullptr;
            HashFS::rehash_file(filepath);
        }
    }
    void Web_Server::uploadCheck() {
        std::error_code error_code;
        if (_upload_status == UploadStatus::FAILED) {
            cancelUpload();
            if (_uploadFile) {
                std::filesystem::path filepath = _uploadFile->fpath();
                delete _uploadFile;
                _uploadFile = nullptr;
                stdfs::remove(filepath, error_code);
                HashFS::rehash_file(filepath);
            }
        }
    }

    void Web_Server::handle() {
        static uint32_t start_time = millis();
        if (WiFi.getMode() == WIFI_AP) {
            dnsServer.processNextRequest();
        }
        if (_webserver) {
            _webserver->handleClient();
        }
        if (_socket_server && _setupdone) {
            _socket_server->loop();
        }
        if (_socket_serverv3 && _setupdone) {
            _socket_serverv3->loop();
        }
        if ((millis() - start_time) > 3000 && _socket_server) {
            WSChannels::sendPing();
            start_time = millis();
        }
    }

    void Web_Server::handle_Websocket_Event(uint8_t num, uint8_t type, uint8_t* payload, size_t length) {
        WSChannels::handleEvent(_socket_server, num, type, payload, length);
    }

    void Web_Server::handle_Websocketv3_Event(uint8_t num, uint8_t type, uint8_t* payload, size_t length) {
        WSChannels::handlev3Event(_socket_serverv3, num, type, payload, length);
    }

    //Convert file extension to content type
    struct mime_type {
        const char* suffix;
        const char* mime_type;
    } mime_types[] = { { ".html", "text/html" },
                       { ".htm", "text/html" },
                       { ".css", "text/css" },
                       { ".js", "application/javascript" },
                       { ".png", "image/png" },
                       { ".gif", "image/gif" },
                       { ".jpeg", "image/jpeg" },
                       { ".jpg", "image/jpeg" },
                       { ".ico", "image/x-icon" },
                       { ".xml", "text/xml" },
                       { ".pdf", "application/x-pdf" },
                       { ".zip", "application/x-zip" },
                       { ".gz", "application/x-gzip" },
                       { ".txt", "text/plain" },
                       { "", "application/octet-stream" } };

    const char* Web_Server::getContentType(const char* filename) {
        mime_type* m;
        for (m = mime_types; *(m->suffix) != '\0'; ++m) {
            if (endsWithCI(m->suffix, filename)) {
                return m->mime_type;
            }
        }
        return m->mime_type;
    }

    //check authentification
    AuthenticationLevel Web_Server::is_authenticated() {
#    ifdef ENABLE_AUTHENTICATION
        if (_webserver->hasHeader("Cookie")) {
            std::string cookie(_webserver->header("Cookie").c_str());
            size_t      pos = cookie.find("ESPSESSIONID=");
            if (pos != std::string::npos) {
                size_t      pos2      = cookie.find(";", pos);
                std::string sessionID = cookie.substr(pos + strlen("ESPSESSIONID="), pos2);
                IPAddress   ip        = _webserver->client().remoteIP();
                //check if cookie can be reset and clean table in same time
                return ResetAuthIP(ip, sessionID.c_str());
            }
        }
        return AuthenticationLevel::LEVEL_GUEST;
#    else
        return AuthenticationLevel::LEVEL_ADMIN;
#    endif
    }

#    ifdef ENABLE_AUTHENTICATION

    //add the information in the linked list if possible
    bool Web_Server::AddAuthIP(AuthenticationIP* item) {
        if (_nb_ip > MAX_AUTH_IP) {
            return false;
        }
        item->_next = _head;
        _head       = item;
        _nb_ip++;
        return true;
    }

    //Session ID based on IP and time using 16 char
    char* Web_Server::create_session_ID() {
        static char sessionID[17];
        //reset SESSIONID
        for (int i = 0; i < 17; i++) {
            sessionID[i] = '\0';
        }
        //get time
        uint32_t now = millis();
        //get remote IP
        IPAddress remoteIP = _webserver->client().remoteIP();
        //generate SESSIONID
        if (0 > sprintf(sessionID,
                        "%02X%02X%02X%02X%02X%02X%02X%02X",
                        remoteIP[0],
                        remoteIP[1],
                        remoteIP[2],
                        remoteIP[3],
                        (uint8_t)((now >> 0) & 0xff),
                        (uint8_t)((now >> 8) & 0xff),
                        (uint8_t)((now >> 16) & 0xff),
                        (uint8_t)((now >> 24) & 0xff))) {
            strcpy(sessionID, "NONE");
        }
        return sessionID;
    }

    bool Web_Server::ClearAuthIP(IPAddress ip, const char* sessionID) {
        AuthenticationIP* current  = _head;
        AuthenticationIP* previous = NULL;
        bool              done     = false;
        while (current) {
            if ((ip == current->ip) && (strcmp(sessionID, current->sessionID) == 0)) {
                //remove
                done = true;
                if (current == _head) {
                    _head = current->_next;
                    _nb_ip--;
                    delete current;
                    current = _head;
                } else {
                    previous->_next = current->_next;
                    _nb_ip--;
                    delete current;
                    current = previous->_next;
                }
            } else {
                previous = current;
                current  = current->_next;
            }
        }
        return done;
    }

    //Get info
    AuthenticationIP* Web_Server::GetAuth(IPAddress ip, const char* sessionID) {
        AuthenticationIP* current = _head;
        //AuthenticationIP * previous = NULL;
        while (current) {
            if (ip == current->ip) {
                if (strcmp(sessionID, current->sessionID) == 0) {
                    //found
                    return current;
                }
            }
            //previous = current;
            current = current->_next;
        }
        return NULL;
    }

    //Review all IP to reset timers
    AuthenticationLevel Web_Server::ResetAuthIP(IPAddress ip, const char* sessionID) {
        AuthenticationIP* current  = _head;
        AuthenticationIP* previous = NULL;
        while (current) {
            if ((millis() - current->last_time) > 360000) {
                //remove
                if (current == _head) {
                    _head = current->_next;
                    _nb_ip--;
                    delete current;
                    current = _head;
                } else {
                    previous->_next = current->_next;
                    _nb_ip--;
                    delete current;
                    current = previous->_next;
                }
            } else {
                if (ip == current->ip && strcmp(sessionID, current->sessionID) == 0) {
                    //reset time
                    current->last_time = millis();
                    return (AuthenticationLevel)current->level;
                }
                previous = current;
                current  = current->_next;
            }
        }
        return AuthenticationLevel::LEVEL_GUEST;
    }
#    endif

    // Enhanced CORS header support: use Origin header if provided
    void Web_Server::addCORSHeaders() {
        const char* origin = _webserver->header("Origin").c_str();
        if (origin && strlen(origin) > 0) {
            _webserver->sendHeader("Access-Control-Allow-Origin", origin);
        } else {
            _webserver->sendHeader("Access-Control-Allow-Origin", "*");
        }
        _webserver->sendHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        _webserver->sendHeader("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept, Authorization");
        _webserver->sendHeader("Access-Control-Allow-Credentials", "true");
        _webserver->sendHeader("Access-Control-Max-Age", "3600");
    }

    void Web_Server::handleGetPenConfig() {
        addCORSHeaders();
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            _webserver->send(401, "text/plain", "Authentication failed");
            return;
        }

        PenConfig& config = PenConfig::getInstance();
        config.loadConfig();
        _webserver->send(200, "application/json", config.toJSON().c_str());
    }

    void Web_Server::handleSetPenConfig() {
        addCORSHeaders();
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            _webserver->send(401, "application/json", "{\"error\":\"Authentication failed\"}");
            return;
        }

        if (!_webserver->hasArg("plain")) {
            _webserver->send(400, "application/json", "{\"error\":\"Missing pen configuration data\"}");
            return;
        }

        std::string jsonData = _webserver->arg("plain").c_str();
        PenConfig&  config   = PenConfig::getInstance();

        if (config.fromJSON(jsonData)) {
            if (config.saveConfig()) {
                _webserver->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                _webserver->send(500, "application/json", "{\"error\":\"Failed to save configuration\"}");
            }
        } else {
            _webserver->send(400, "application/json", "{\"error\":\"Invalid pen configuration data\"}");
        }
    }

    void Web_Server::handleDeletePen() {
        addCORSHeaders();
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            _webserver->send(401, "application/json", "{\"error\":\"Authentication failed\"}");
            return;
        }

        if (!_webserver->hasArg("name")) {
            _webserver->send(400, "application/json", "{\"error\":\"Missing pen name\"}");
            return;
        }

        std::string penName = _webserver->arg("name").c_str();
        PenConfig&  config  = PenConfig::getInstance();

        if (config.deletePen(penName)) {
            config.saveConfig();
            _webserver->send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            _webserver->send(404, "application/json", "{\"error\":\"Pen not found\"}");
        }
    }

    // GET /toolconfig
    // Retrieves the complete tool configuration
    // Returns JSON containing all tool positions and their states
    void Web_Server::handleGetToolConfig() {
        addCORSHeaders();
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            _webserver->send(401, "text/plain", "Authentication failed");
            return;
        }

        ToolConfig& config = ToolConfig::getInstance();
        config.loadConfig();  // Load latest config from file
        _webserver->send(200, "application/json", config.toJSON().c_str());
    }

    // POST /toolconfig
    // Updates the entire tool configuration
    // Expects JSON in the request body containing complete tool configuration
    void Web_Server::handleSetToolConfig() {
        addCORSHeaders();
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            _webserver->send(401, "application/json", "{\"error\":\"Authentication failed\"}");
            return;
        }

        if (!_webserver->hasArg("plain")) {
            _webserver->send(400, "application/json", "{\"error\":\"Missing tool configuration data\"}");
            return;
        }

        std::string jsonData = _webserver->arg("plain").c_str();
        ToolConfig& config   = ToolConfig::getInstance();

        if (config.fromJSON(jsonData)) {
            if (config.saveConfig()) {
                _webserver->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                _webserver->send(500, "application/json", "{\"error\":\"Failed to save configuration\"}");
            }
        } else {
            _webserver->send(400, "application/json", "{\"error\":\"Invalid tool configuration data\"}");
        }
    }

    // POST /toolconfig/position
    // Updates the position of a specific tool
    // Expects JSON in format: {"number": X, "x": X, "y": Y, "z": Z, "occupied": true/false}
    void Web_Server::handleUpdateToolPosition() {
        addCORSHeaders();
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            _webserver->send(401, "application/json", "{\"error\":\"Authentication failed\"}");
            return;
        }

        if (!_webserver->hasArg("plain")) {
            _webserver->send(400, "application/json", "{\"error\":\"Missing position data\"}");
            return;
        }

        std::string jsonData = _webserver->arg("plain").c_str();
        ToolConfig& config   = ToolConfig::getInstance();

        Tool position;  // Changed from ToolPosition to Tool
        try {
            // Parse each field from the JSON
            int   number;
            float x, y, z;
            bool  occupied;

            // Use helper functions to parse the JSON fields
            if (!config.parseJsonNumber(jsonData, "number", number) || !config.parseJsonFloat(jsonData, "x", x) ||
                !config.parseJsonFloat(jsonData, "y", y) || !config.parseJsonFloat(jsonData, "z", z)) {
                _webserver->send(400, "application/json", "{\"error\":\"Invalid position format\"}");
                return;
            }

            // Build the position object
            position.number   = number;
            position.x        = x;
            position.y        = y;
            position.z        = z;
            position.occupied = (jsonData.find("\"occupied\":true") != std::string::npos);

            // Validate position against known ranges
            if (!config.validatePosition(position)) {
                _webserver->send(400, "application/json", "{\"error\":\"Position values out of valid range\"}");
                return;
            }

            // Safety check for potential collisions
            if (config.checkCollisionRisk(0, position.number)) {
                _webserver->send(409, "application/json", "{\"error\":\"Movement would risk collision\"}");
                return;
            }

            // Update the position if all checks pass
            if (config.updateTool(position)) {  // Changed from updatePosition to updateTool
                _webserver->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                _webserver->send(500, "application/json", "{\"error\":\"Failed to update position\"}");
            }
        } catch (...) { _webserver->send(400, "application/json", "{\"error\":\"Failed to parse position data\"}"); }
    }

    // GET /toolconfig/status
    // Returns current status of all tools including positions and states
    // Used for real-time monitoring of tool positions and states
    void Web_Server::handleGetToolStatus() {
        addCORSHeaders();
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            _webserver->send(401, "text/plain", "Authentication failed");
            return;
        }

        ToolConfig& config = ToolConfig::getInstance();
        ToolStatus  status = config.getStatus();

        std::string output;
        JSONencoder j(&output);
        j.begin();
        j.member("currentPen", std::to_string(status.currentPen).c_str());
        j.member("totalPens", "6");  // From toolconfig.json
        j.member("inMotion", status.inMotion ? "true" : "false");
        j.member("error", status.error ? "true" : "false");
        if (status.error) {
            j.member("lastError", status.lastError.c_str());
        }

        // Include full position information for all tools
        j.begin_array("positions");
        for (int i = 1; i <= 6; i++) {      // Iterate through all tools
            Tool* pos = config.getTool(i);  // Changed from getPosition to getTool
            if (pos) {
                j.begin_object();
                j.member("number", std::to_string(i).c_str());
                j.member("x", std::to_string(pos->x).c_str());
                j.member("y", std::to_string(pos->y).c_str());
                j.member("z", std::to_string(pos->z).c_str());
                j.member("occupied", pos->occupied ? "true" : "false");
                j.end_object();
            }
        }
        j.end_array();

        j.end();
        _webserver->send(200, "application/json", output.c_str());
    }

    void Web_Server::handlePenChangeMode() {
        log_info("PenChangeMode endpoint called with method: " << _webserver->method());

        addCORSHeaders();
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            _webserver->send(401, "application/json", "{\"error\":\"Authentication failed\"}");
            return;
        }

        // GET request - return current pen_change flag state
        if (_webserver->method() == HTTP_GET) {
            std::string output;
            JSONencoder j(&output);
            j.begin();
            j.member("pen_change_mode", pen_change ? "true" : "false");
            j.end();
            _webserver->send(200, "application/json", output.c_str());
            return;
        }

        // POST request - set pen_change flag state
        if (_webserver->method() == HTTP_POST) {
            bool enable_mode = false;

            if (_webserver->hasArg("plain")) {
                std::string jsonData = _webserver->arg("plain").c_str();
                log_info("Received JSON data: " << jsonData.c_str());
                // Parse enable flag from JSON
                if (jsonData.find("\"enable\":true") != std::string::npos) {
                    enable_mode = true;
                } else if (jsonData.find("\"enable\":false") != std::string::npos) {
                    enable_mode = false;
                } else {
                    _webserver->send(400, "application/json", "{\"error\":\"Invalid data format\"}");
                    return;
                }
            } else {
                _webserver->send(400, "application/json", "{\"error\":\"Missing data\"}");
                return;
            }

            // Update the pen_change flag state
            pen_change = enable_mode;

            // Log the state change
            if (enable_mode) {
                log_info("Pen change mode enabled via API");
            } else {
                log_info("Pen change mode disabled via API");
            }

            // Send JSON response with updated state
            std::string output;
            JSONencoder j(&output);
            j.begin();
            j.member("status", "ok");
            j.member("pen_change_mode", pen_change ? "true" : "false");
            j.end();
            _webserver->send(200, "application/json", output.c_str());
            return;
        }

        // Reject other HTTP methods
        _webserver->send(405, "text/plain", "Method Not Allowed");
    }
#endif
}  // namespace web_server
