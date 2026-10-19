#include <Arduino.h>
#include <Update.h>
#include "config.h"
#include "device.h"
#include "motion.h"
#include "ota.h"

static WebServer* s_server = nullptr;
static bool s_authorized = false;
static String s_error;

// Renewed by every chunk, so a dropped upload can never leave motion locked.
static const uint32_t LOCK_LEASE_MS = 15000;

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
document.getElementById('go').onclick = function () {
  const f = document.getElementById('file').files[0];
  const msg = document.getElementById('msg');
  if (!f) { msg.textContent = 'Choose a file first.'; return; }
  const bar = document.getElementById('bar');
  bar.hidden = false;
  const data = new FormData();
  data.append('firmware', f, f.name);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/update');
  xhr.upload.onprogress = e => { if (e.lengthComputable) bar.value = 100 * e.loaded / e.total; };
  xhr.onload = () => { msg.textContent = xhr.responseText; };
  xhr.onerror = () => { msg.textContent = 'Upload failed (connection lost).'; };
  xhr.send(data);
};
</script></body></html>
)rawliteral";

static bool authenticated() {
  return s_server->authenticate(OTA_USER, device::otaPassword());
}

static void handlePage() {
  if (!authenticated()) return s_server->requestAuthentication();
  String page = OTA_PAGE;
  page.replace("{{FW}}", FW_VERSION);
  s_server->send(200, "text/html", page);
}

static void handleUploadChunk() {
  HTTPUpload& up = s_server->upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      s_error = "";
      s_authorized = authenticated();
      if (!s_authorized) {
        s_error = "Unauthorized";
        return;
      }
      if (Update.isRunning()) Update.abort();  // leftover from a dropped upload
      // Lock first, then check: no rotation can start between the two.
      motion::lockFor(LOCK_LEASE_MS);
      if (motion::isBusy()) {
        s_error = "The turntable is moving. Wait for it to finish (or press Stop) and try again.";
        return;
      }
      Serial.printf("[ota] receiving %s\n", up.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) s_error = Update.errorString();
      break;
    case UPLOAD_FILE_WRITE:
      if (s_error.length()) return;
      motion::lockFor(LOCK_LEASE_MS);
      if (Update.write(up.buf, up.currentSize) != up.currentSize) s_error = Update.errorString();
      break;
    case UPLOAD_FILE_END:
      if (s_error.length()) {
        Update.abort();
        return;
      }
      // end(true) runs esp_ota_end(), which validates the whole image before
      // the boot partition is switched. A bad image is never booted.
      if (!Update.end(true)) s_error = Update.errorString();
      break;
    case UPLOAD_FILE_ABORTED:
      Update.abort();
      motion::lockFor(0);
      s_error = "Upload aborted";
      break;
  }
}

static void handleUploadDone() {
  if (!s_authorized) return s_server->requestAuthentication();
  if (s_error.length()) {
    if (Update.isRunning()) Update.abort();
    motion::lockFor(0);
    Serial.printf("[ota] failed: %s\n", s_error.c_str());
    s_server->send(500, "text/plain", "Update failed: " + s_error + ". The current firmware is still installed.");
    return;
  }
  Serial.println("[ota] success, restarting");
  s_server->sendHeader("Connection", "close");
  s_server->send(200, "text/plain", "Update OK. The turntable is restarting; reconnect in about 10 seconds.");
  delay(1000);
  ESP.restart();
}

namespace ota {

void begin(WebServer& server) {
  s_server = &server;
  server.on("/update", HTTP_GET, handlePage);
  server.on("/update", HTTP_POST, handleUploadDone, handleUploadChunk);
}

}  // namespace ota
