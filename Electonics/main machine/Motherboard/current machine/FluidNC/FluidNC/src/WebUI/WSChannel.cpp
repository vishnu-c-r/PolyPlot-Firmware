// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WSChannel.h"

#ifdef ENABLE_WIFI
#    include "WebServer.h"
#    include <WebSocketsServer.h>
#    include <WiFi.h>

#    include "../Serial.h"  // is_realtime_command
#    include "../Limits.h"  // pen_change variable
#    include "../Job.h"     // Job::active() function

namespace WebUI {
    class WSChannels;

    WSChannel::WSChannel(WebSocketsServer* server, uint8_t clientNum) : Channel("websocket"), _server(server), _clientNum(clientNum) {}

    int WSChannel::read() {
        if (!_active) {
            return -1;
        }
        if (_rtchar == -1) {
            return -1;
        } else {
            auto ret = _rtchar;
            _rtchar  = -1;
            return ret;
        }
    }

    WSChannel::operator bool() const {
        return true;
    }

    size_t WSChannel::write(uint8_t c) {
        return write(&c, 1);
    }

    size_t WSChannel::write(const uint8_t* buffer, size_t size) {
        if (buffer == NULL || !_active || !size) {
            return 0;
        }

        bool complete_line = buffer[size - 1] == '\n';

        const uint8_t* out;
        size_t         outlen;
        if (_output_line.length() == 0 && complete_line) {
            // Avoid the overhead of std::string if the
            // input is a complete line and nothing is pending.
            out    = buffer;
            outlen = size;
        } else {
            // Otherwise collect input until we have line.
            _output_line.append((char*)buffer, size);
            if (!complete_line) {
                return size;
            }

            out    = (uint8_t*)_output_line.c_str();
            outlen = _output_line.length();
        }
        int stat = _server->canSend(_clientNum);
        if (stat < 0) {
            _active = false;
            log_debug("WebSocket is dead; closing");
            return 0;
        }
        if (!_server->sendBIN(_clientNum, out, outlen)) {
            _active = false;
            log_debug("WebSocket is unresponsive; closing");
        }
        if (_output_line.length()) {
            _output_line = "";
        }

        return size;
    }

    bool WSChannel::sendTXT(std::string& s) {
        if (!_active) {
            return false;
        }
        if (!_server->sendTXT(_clientNum, s.c_str())) {
            _active = false;
            log_debug("WebSocket is unresponsive; closing");
            return false;
        }
        return true;
    }

    void WSChannel::autoReport() {
        if (!_active) {
            return;
        }
        int stat = _server->canSend(_clientNum);
        if (stat < 0) {
            _active = false;
            log_debug("WebSocket is dead; closing");
            return;
        }
        if (stat == 0) {
            return;
        }

        Channel::autoReport();
    }

    WSChannel::~WSChannel() {}

    std::map<uint8_t, WSChannel*> WSChannels::_wsChannels;
    std::list<WSChannel*>         WSChannels::_webWsChannels;

    WSChannel* WSChannels::_lastWSChannel = nullptr;

    WSChannel* WSChannels::getWSChannel(int pageid) {
        WSChannel* wsChannel = nullptr;
        if (pageid != -1) {
            try {
                wsChannel = _wsChannels.at(pageid);
            } catch (std::out_of_range& oor) {}
        } else {
            // If there is no PAGEID URL argument, it is an old version of WebUI
            // that does not supply PAGEID in all cases.  In that case, we use
            // the most recently used websocket if it is still in the list.
            for (auto it = _wsChannels.begin(); it != _wsChannels.end(); ++it) {
                if (it->second == _lastWSChannel) {
                    wsChannel = _lastWSChannel;
                    break;
                }
            }
        }
        _lastWSChannel = wsChannel;
        return wsChannel;
    }

    void WSChannels::removeChannel(uint8_t num) {
        try {
            WSChannel* wsChannel = _wsChannels.at(num);
            _webWsChannels.remove(wsChannel);
            allChannels.kill(wsChannel);
            _wsChannels.erase(num);
        } catch (std::out_of_range& oor) {}
    }

    void WSChannels::removeChannel(WSChannel* channel) {
        _lastWSChannel = nullptr;
        _webWsChannels.remove(channel);
        allChannels.kill(channel);
        for (auto it = _wsChannels.cbegin(); it != _wsChannels.cend();) {
            if (it->second == channel) {
                it = _wsChannels.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool WSChannels::runGCode(int pageid, std::string_view cmd) {
        WSChannel* wsChannel = getWSChannel(pageid);
        if (wsChannel) {
            if (cmd.length()) {
                if (is_realtime_command(cmd[0])) {
                    for (auto const& c : cmd) {
                        wsChannel->handleRealtimeCharacter((uint8_t)c);
                    }
                } else {
                    wsChannel->push(cmd);
                    if (cmd.back() != '\n') {
                        wsChannel->push('\n');
                    }
                }
            }
            return false;
        }
        return true;  // Error - no websocket
    }

    bool WSChannels::sendError(int pageid, std::string err) {
        WSChannel* wsChannel = getWSChannel(pageid);
        if (wsChannel) {
            return !wsChannel->sendTXT(err);
        }
        return true;
    }

    void WSChannels::sendPing() {
        for (auto it = _wsChannels.begin(); it != _wsChannels.end();) {
            WSChannel* wsChannel = it->second;
            std::string s("PING:");
            s += std::to_string(wsChannel->id());
            wsChannel->sendTXT(s);

            ++it;
        }
    }

    void WSChannels::handleEvent(WebSocketsServer* server, uint8_t num, uint8_t type, uint8_t* payload, size_t length) {
        switch (type) {
            case WStype_DISCONNECTED:
                log_debug("WebSocket disconnect " << num);
                WSChannels::removeChannel(num);
                break;            case WStype_CONNECTED: {
                // Check if a job is currently active and block new connections
                if (Job::active()) {
                    log_debug("WebSocket connection " << num << " blocked - job in progress");
                    
                    // Send job status message and immediately disconnect
                    std::string jobBlockMsg = "{\"error\":\"job_in_progress\",\"message\":\"A job is currently running. Please wait for completion.\"}";
                    server->sendTXT(num, jobBlockMsg.c_str());
                    server->disconnect(num);
                    return;
                }

                WSChannel* wsChannel = new WSChannel(server, num);
                if (!wsChannel) {
                    log_error("Creating WebSocket channel failed");
                } else {
                    std::string uri((char*)payload, length);

                    IPAddress ip = server->remoteIP(num);
                    log_debug("WebSocket " << num << " from " << ip << " uri " << uri);

                    _lastWSChannel = wsChannel;
                    allChannels.registration(wsChannel);
                    _wsChannels[num] = wsChannel;

                    if (uri == "/") {
                        std::string s("CURRENT_ID:");
                        s += std::to_string(num);
                        // send message to client
                        _webWsChannels.push_front(wsChannel);
                        wsChannel->sendTXT(s);
                        s = "ACTIVE_ID:";
                        s += std::to_string(wsChannel->id());
                        wsChannel->sendTXT(s);

                        // Fix ambiguous push() call by using std::string
                        std::string report_cmd = "$Report/Interval=50\n";
                        wsChannel->push(report_cmd);
                    }

                    // Allow up to 5 clients to be connected simultaneously
                    // Count current connections and disconnect if we have too many
                    int connectedClients = 0;
                    for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
                        if (server->clientIsConnected(i)) {
                            connectedClients++;
                        }
                    }
                    
                    // If we have more than 5 clients, disconnect the oldest ones
                    if (connectedClients > 5) {
                        int toDisconnect = connectedClients - 5;
                        for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX && toDisconnect > 0; i++) {
                            if (i != num && server->clientIsConnected(i)) {
                                server->disconnect(i);
                                toDisconnect--;
                            }
                        }
                    }
                }
            } break;
            case WStype_TEXT: {
                try {
                    std::string msg = (const char*)payload;
                    if (msg.rfind("PONG:", 0) == 0) {
                        // Handle PONG response
                        if (auto* channel = _wsChannels.at(num)) {
                            // Commented out due to function being disabled
                            // channel->updateLastPong();
                        }
                    } else {
                        _wsChannels.at(num)->push(payload, length);
                    }
                } catch (std::out_of_range& oor) {}
                break;
            }
            case WStype_PONG:
                // Handle WebSocket protocol level pong
                try {
                    if (auto* channel = _wsChannels.at(num)) {
                        // Commented out due to function being disabled
                        // channel->updateLastPong();
                    }
                } catch (std::out_of_range& oor) {}
                break;
            default:
                break;
        }
    }

    void WSChannels::handlev3Event(WebSocketsServer* server, uint8_t num, uint8_t type, uint8_t* payload, size_t length) {
        switch (type) {
            case WStype_DISCONNECTED:
                log_debug("WebSocket disconnect " << num);
                WSChannels::removeChannel(num);
                break;            case WStype_CONNECTED: {
                log_debug("WStype_Connected");
                
                // Check if a job is currently active and block new connections
                if (Job::active()) {
                    log_debug("WebSocket v3 connection " << num << " blocked - job in progress");
                    
                    // Send job status message and immediately disconnect
                    std::string jobBlockMsg = "{\"error\":\"job_in_progress\",\"message\":\"A job is currently running. Please wait for completion.\"}";
                    server->sendTXT(num, jobBlockMsg.c_str());
                    server->disconnect(num);
                    return;
                }

                WSChannel* wsChannel = new WSChannel(server, num);
                if (!wsChannel) {
                    log_error("Creating WebSocket channel failed");
                } else {
                    std::string uri((char*)payload, length);

                    IPAddress ip = server->remoteIP(num);
                    log_debug("WebSocket " << num << " from " << ip << " uri " << uri);

                    _lastWSChannel = wsChannel;
                    allChannels.registration(wsChannel);
                    _wsChannels[num] = wsChannel;

                    if (uri == "/") {
                        std::string s("currentID:");
                        s += std::to_string(num);
                        // send message to client
                        _webWsChannels.push_front(wsChannel);
                        wsChannel->sendTXT(s);
                        s = "activeID:";
                        s += std::to_string(wsChannel->id());
                        server->broadcastTXT(s.c_str());

                        // Send initial connection success message
                        std::string connected = "{\"status\":\"connected\"}";
                        wsChannel->sendTXT(connected);

                        // Fix ambiguous push() call by using std::string
                        std::string report_cmd = "$Report/Interval=50\n";
                        wsChannel->push(report_cmd);

                        // Log that we've set the report interval
                        log_debug("Set report interval to 50ms for WebSocket " << num);
                    }

                    // Allow up to 5 clients to be connected simultaneously
                    // Count current connections and disconnect if we have too many
                    int connectedClients = 0;
                    for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
                        if (server->clientIsConnected(i)) {
                            connectedClients++;
                        }
                    }
                    
                    // If we have more than 5 clients, disconnect the oldest ones
                    if (connectedClients > 5) {
                        int toDisconnect = connectedClients - 5;
                        for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX && toDisconnect > 0; i++) {
                            if (i != num && server->clientIsConnected(i)) {
                                server->disconnect(i);
                                toDisconnect--;
                            }
                        }
                    }
                }
            } break;
            case WStype_TEXT: {
                try {
                    std::string msg = (const char*)payload;
                    if (msg.rfind("PONG:", 0) == 0) {
                        // Handle PONG response
                        if (auto* channel = _wsChannels.at(num)) {
                            // Commented out due to function being disabled
                            // channel->updateLastPong();
                        }
                    } else if (msg.rfind("PING:", 0) == 0) {
                        std::string response("PING:60000:60000");
                        _wsChannels.at(num)->sendTXT(response);
                    } else {
                        _wsChannels.at(num)->push(payload, length);
                    }
                } catch (std::out_of_range& oor) {}
                break;
            }
            case WStype_PONG:
                try {
                    if (auto* channel = _wsChannels.at(num)) {
                        // Commented out due to function being disabled
                        // channel->updateLastPong();
                    }
                } catch (std::out_of_range& oor) {}
                break;
            case WStype_BIN:
                try {
                    _wsChannels.at(num)->push(payload, length);
                } catch (std::out_of_range& oor) {}
                break;
            default:
                break;
        }
    }
}
#endif
