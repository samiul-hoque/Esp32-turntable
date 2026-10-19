#include <Arduino.h>
#include <Update.h>
#include "config.h"
#include "device.h"
#include "motion.h"
#include "ota.h"
#include "watchdog.h"

// The firmware image is sent as a raw request body (Content-Type: application/octet-stream).
// The core's raw-body reader times out on every read, so an abandoned upload aborts cleanly.
// Its multipart parser does not (Parsing.cpp _uploadReadByte waits forever for a peer that
// went away), which froze the whole network task during testing. Multipart is refused.

static const uint32_t LOCK_LEASE_MS = 15000;  // renewed by every chunk
static const uint32_t READ_TIMEOUT_S = 3;     // max wait for the next byte of the upload

static bool s_authorized = false;
static String s_error;

static const char OTA_PAGE[] = R"rawliteral(<!DOCTYPE html>
<html><head><title>Turntable firmware update</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;background:#000;color:#fff;display:flex;justify-content:center;padding:40px 20px;margin:0}
.box{background:#1a1a1a;border:1px solid #333;border-radius:10px;padding:30px;width:400px;max-width:90vw}
input,button{width:100%;padding:10px;margin:8px 0;box-sizing:border-box;border-radius:4px}
button{background:#fff;color:#000;border:none;font-weight:700;cursor:pointer}
progress{width:100%}
#msg{margin-top:10px}
</style></head><body><div class="box">
<h3>Firmware update</h3>
<p style="font-size:12px;color:#aaa">Current version: {{FW}}. Upload a <b>firmware.bin</b>. The turntable must be idle. It restarts automatically when the update succeeds; if anything fails, the current firmware keeps running.</p>
<input type="file" id="file" accept=".bin">
<button id="go">Upload</button>
<progress id="bar" value="0" max="100" hidden></progress>
<div id="msg"></div>
</div>
<script>
document.getElementById('go').onclick = async function () {
  const f = document.getElementById('file').files[0];
  const msg = document.getElementById('msg');
  if (!f) { msg.textContent = 'Choose a file first.'; return; }
  try {
    const s = await (await fetch('/api/status')).json();
    if (s.motion.state === 'moving' || s.motion.state === 'delay') {
      msg.textContent = 'The turntable is moving. Wait for it to finish (or press Stop) and try again.';
      return;
    }
  } catch (e) { /* let the device decide */ }
  const bar = document.getElementById('bar');
  bar.hidden = false;
  msg.textContent = 'Uploading...';
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/update');
  xhr.setRequestHeader('Content-Type', 'application/octet-stream');
  xhr.upload.onprogress = e => { if (e.lengthComputable) bar.value = 100 * e.loaded / e.total; };
  xhr.onload = () => { msg.textContent = xhr.responseText; };
  xhr.onerror = () => { msg.textContent = 'Upload failed (connection lost). The current firmware is still installed.'; };
  xhr.send(f);
};
</script></body></html>
)rawliteral";

class OtaHandler : public RequestHandler {
 public:
  bool canHandle(HTTPMethod method, String uri) override {
    return uri == "/update" && (method == HTTP_GET || method == HTTP_POST);
  }
  bool canRaw(String uri) override { return uri == "/update"; }
  // Claim multipart uploads only to refuse them before the hang-prone parser runs.
  bool canUpload(String uri) override { return uri == "/update"; }

  void upload(WebServer& server, String, HTTPUpload& up) override {
    if (up.status == UPLOAD_FILE_START) {
      s_authorized = server.authenticate(OTA_USER, device::otaPassword());
      s_error = "Send the firmware as a raw body (Content-Type: application/octet-stream), not as a form upload";
      Serial.println("[ota] refused multipart upload; send the file as a raw body");
      server.client().stop();  // ends the parser immediately
    }
  }

  void raw(WebServer& server, String, HTTPRaw& raw) override {
    switch (raw.status) {
      case RAW_START: {
        s_error = "";
        server.client().setTimeout(READ_TIMEOUT_S);
        s_authorized = server.authenticate(OTA_USER, device::otaPassword());
        if (!s_authorized) {
          s_error = "Unauthorized";
          return;
        }
        if (Update.isRunning()) Update.abort();
        motion::lockFor(LOCK_LEASE_MS);  // lock first, then check: nothing can start in between
        if (motion::isBusy()) {
          s_error = "The turntable is moving. Wait for it to finish (or press Stop) and try again";
          return;
        }
        int len = server.clientContentLength();
        Serial.printf("[ota] receiving %d bytes\n", len);
        if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN, U_FLASH)) s_error = Update.errorString();
        break;
      }
      case RAW_WRITE:
        watchdog::beat("web:ota");
        if (s_error.length()) return;  // keep draining the body, then report
        motion::lockFor(LOCK_LEASE_MS);
        if (Update.write(raw.buf, raw.currentSize) != raw.currentSize) s_error = Update.errorString();
        break;
      case RAW_END:
        if (s_error.length()) {
          if (Update.isRunning()) Update.abort();
          return;
        }
        // end(true) runs esp_ota_end(), which validates the whole image before the
        // boot partition is switched. A bad image is never booted.
        if (!Update.end(true)) s_error = Update.errorString();
        break;
      case RAW_ABORTED:
        if (Update.isRunning()) Update.abort();
        motion::lockFor(0);
        Serial.println("[ota] upload interrupted");
        break;
    }
  }

  bool handle(WebServer& server, HTTPMethod method, String) override {
    if (method == HTTP_GET) {
      if (!server.authenticate(OTA_USER, device::otaPassword())) {
        server.requestAuthentication();
        return true;
      }
      String page = OTA_PAGE;
      page.replace("{{FW}}", FW_VERSION);
      server.send(200, "text/html", page);
      return true;
    }

    if (!s_authorized) {
      server.requestAuthentication();
      return true;
    }
    if (s_error.length()) {
      if (Update.isRunning()) Update.abort();
      motion::lockFor(0);
      Serial.printf("[ota] failed: %s\n", s_error.c_str());
      server.send(500, "text/plain", "Update failed: " + s_error + ". The current firmware is still installed.");
      return true;
    }
    Serial.println("[ota] success, restarting");
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", "Update OK. The turntable is restarting; reconnect in about 10 seconds.");
    delay(1000);
    ESP.restart();
    return true;
  }
};

namespace ota {

void begin(WebServer& server) {
  server.addHandler(new OtaHandler());
}

}  // namespace ota
