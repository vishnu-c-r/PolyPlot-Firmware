#include "GrblParserC.h"
#include <string.h>
#include <stdlib.h>  // Add this for atoi()
#include <Arduino.h>

static size_t _report_len = 0;
static char _report[REPORT_BUFFER_LEN];
bool _ackwait = false;
int _ack_time_limit = 0;
bool _alarm14 = false;  // Change from static bool _alarm14 to just bool _alarm14

// Core communication functions
void fnc_send_line(const char* line, int timeout_ms) {
    // Wait for previous command acknowledgment
    unsigned long start = millis();
    while (_ackwait) {
        if ((millis() - start) >= timeout_ms) {
            _ackwait = false;  // Force clear if timeout
            break;
        }
        fnc_poll();
    }

    // Send the command
    const char* p = line;
    while (*p) {
        fnc_putchar(*p++);
        delay(1);  // Add small delay between characters
    }
    fnc_putchar('\n');

    // Set acknowledgment timeout
    _ack_time_limit = millis() + timeout_ms;
    _ackwait = true;
}

// Message parsing
static void parse_state(const char* state) {
    show_state(state);
}

static void parse_report() {
    if (_report_len == 0) return;

    // Handle acknowledgment
    if (strcmp(_report, "ok") == 0) {
        _ackwait = false;
        show_ok();
        return;
    }

    // Handle status reports - Format is like <Idle|...>
    if (_report[0] == '<') {
        char* state_end = strchr(_report + 1, '|');
        if (state_end) {
            *state_end = '\0';  // Split at the first | character
            parse_state(_report + 1);  // Send state string to callback
        }
        return;
    }

    // Handle alarms
    if (strncmp(_report, "ALARM:", 6) == 0) {
        int alarmCode = atoi(_report + 6);
        if (alarmCode == 14) {
            _alarm14 = true;
        }
        show_alarm(alarmCode);
        return;
    }

    // Handle errors
    if (strncmp(_report, "error:", 6) == 0) {
        _ackwait = false;
        show_error(atoi(_report + 6));
        return;
    }
}

// Input handling
void collect(uint8_t data) {
    char c = data;
    
    if (c == '\r') return;
    
    if (c == '\n') {
        if (_report_len > 0) {
            _report[_report_len] = '\0';
            parse_report();  // Process report immediately
            _report_len = 0;
        }
        return;
    }
    
    if (_report_len < REPORT_BUFFER_LEN - 1) {
        _report[_report_len++] = c;
    } else {
        _report_len = 0;  // Buffer overflow protection
    }
}

void fnc_poll() {
    static unsigned long last_status_request = 0;
    unsigned long now = millis();
    
    // Request status every 50ms if not waiting for ack
    if (!_ackwait && (now - last_status_request >= 50)) {
        fnc_putchar((uint8_t)StatusReport);
        last_status_request = now;
    }
    
    // Always process incoming data immediately
    while (true) {
        int c = fnc_getchar();
        if (c < 0) break;  // No more data
        collect(c);
    }
}

void fnc_wait_ready() {
    bool machine_ready = false;
    unsigned long start_time = millis();
    
    while (!machine_ready && (millis() - start_time) < 5000) {
        fnc_putchar((uint8_t)StatusReport);
        
        for (int i = 0; i < 100; i++) {  // Poll for 100ms
            int c = fnc_getchar();
            if (c >= 0) {
                collect(c);
            }
            delay(1);
        }
        
        if (_alarm14) {
            machine_ready = true;
            break;
        }
        
        if (!machine_ready) {
            delay(100);
        }
    }
}

// Weak implementation of callbacks
void __attribute__((weak)) show_state(const char* state) {}
void __attribute__((weak)) show_error(int error) {}
void __attribute__((weak)) show_alarm(int alarm) {}
void __attribute__((weak)) show_ok() {}