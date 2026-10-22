#include "n8n.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include "web.h"

namespace n8n {

static String mcpUrl, tok, allowJson = "[]";
static SemaphoreHandle_t mtx;

void begin() {
  mtx = xSemaphoreCreateMutex();
  Preferences p;
  p.begin("n8n", true);
  mcpUrl = p.getString("url", "");
  tok = p.getString("tok", "");
  allowJson = p.getString("allow", "[]");
  p.end();
}

bool configured() { return mcpUrl.length() && tok.length(); }
String url() { return mcpUrl; }
String token() { return tok; }

void set(const char* u, const char* t) {
  Preferences p;
  p.begin("n8n", false);
  if (u) { mcpUrl = u; mcpUrl.trim(); p.putString("url", mcpUrl); }
  if (t) { tok = t; tok.trim(); p.putString("tok", tok); }
  p.end();
}

std::vector<Entry> allowed() {
  std::vector<Entry> out;
  xSemaphoreTake(mtx, portMAX_DELAY);
  JsonDocument d;
  if (!deserializeJson(d, allowJson))
    for (JsonObject o : d.as<JsonArray>()) out.push_back({o["id"] | "", o["name"] | "", o["kind"] | "workflow", o["desc"] | ""});
  xSemaphoreGive(mtx);
  return out;
}

bool isAllowed(const String& id, const char* kind) {
  for (auto& e : allowed())
    if (e.id == id && e.kind == kind) return true;
  return false;
}

void setAllowed(const String& json) {
  JsonDocument d;
  if (deserializeJson(d, json) || !d.is<JsonArray>()) return;
  String clean;
  serializeJson(d, clean);
  xSemaphoreTake(mtx, portMAX_DELAY);
  allowJson = clean;
  xSemaphoreGive(mtx);
  Preferences p;
  p.begin("n8n", false);
  p.putString("allow", allowJson);
  p.end();
}

String allowedJson() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  String s = allowJson;
  xSemaphoreGive(mtx);
  return s;
}

// Ein Werkzeug des MCP-Servers aufrufen; Ergebnis = strukturierter Inhalt (bzw. Text) als JSON-Text
static bool call(const char* tool, JsonDocument& args, String& out, String& err) {
  if (!configured()) { err = "n8n ist nicht eingerichtet"; return false; }
  JsonDocument req;
  req["jsonrpc"] = "2.0";
  req["id"] = (uint32_t)millis();
  req["method"] = "tools/call";
  req["params"]["name"] = tool;
  req["params"]["arguments"] = args;
  String body, resp;
  serializeJson(req, body);
  String headers = "Authorization: Bearer " + tok + "\nAccept: application/json, text/event-stream";
  int code = web::post(mcpUrl, body, "application/json", resp, headers.c_str());
  if (code != 200) { err = "n8n antwortet nicht (" + String(code) + ")"; return false; }
  // Antwort kommt als Server-Sent-Event („data: {…}“) oder direkt als JSON
  int d = resp.indexOf("data: ");
  if (d >= 0) {
    int e = resp.indexOf('\n', d);
    resp = resp.substring(d + 6, e < 0 ? resp.length() : e);
  }
  JsonDocument r;
  if (DeserializationError e = deserializeJson(r, resp)) {
    Serial.printf("n8n: Antwort unlesbar (%s, %u Zeichen): %.160s\n", e.c_str(), resp.length(), resp.c_str());
    err = "n8n-Antwort unlesbar";
    return false;
  }
  if (r["error"]) { err = String("n8n: ") + (const char*)(r["error"]["message"] | "Fehler"); return false; }
  JsonVariant res = r["result"];
  if (res["isError"] | false) {
    err = String("n8n: ") + (const char*)(res["content"][0]["text"] | "Fehler");
    return false;
  }
  if (res["structuredContent"]) serializeJson(res["structuredContent"], out);
  else out = res["content"][0]["text"] | "";
  return true;
}

String listAll(String& err) {
  JsonDocument out;
  JsonArray a = out.to<JsonArray>();
  JsonDocument args;
  args["limit"] = 100;
  String s;
  if (!call("search_workflows", args, s, err)) return "";
  JsonDocument w;
  if (!deserializeJson(w, s))
    for (JsonObject o : w["data"].as<JsonArray>()) {
      if (!(o["availableInMCP"] | true)) continue;
      JsonObject e = a.add<JsonObject>();
      e["id"] = o["id"];
      e["name"] = o["name"];
      e["kind"] = "workflow";
      e["desc"] = o["description"] | "";
      e["active"] = o["active"];
    }
  JsonDocument args2;
  args2["limit"] = 50;
  String err2;
  if (call("search_agents", args2, s, err2)) {
    JsonDocument g;
    if (!deserializeJson(g, s))
      for (JsonObject o : g["data"].as<JsonArray>()) {
        if (!(o["availableInMCP"] | true)) continue;
        JsonObject e = a.add<JsonObject>();
        e["id"] = o["id"];
        e["name"] = o["name"];
        e["kind"] = "agent";
        e["desc"] = "";
        e["active"] = o["published"];
      }
  }
  String r;
  serializeJson(out, r);
  return r;
}

// erster vorhandener Text unter mehreren möglichen Stellen der Antwort
static String first(std::initializer_list<JsonVariantConst> vs) {
  for (JsonVariantConst v : vs) {
    if (v.is<const char*>()) return v.as<const char*>();
    if (v.is<long>()) return String(v.as<long>());
  }
  return "";
}

// „ - Node name: When chat message received“ hinter der Überschrift der Auslöser-Art
static String triggerName(const String& info, const char* kind) {
  int k = info.indexOf(kind);
  if (k < 0) return "";
  int n = info.indexOf("Node name: ", k);
  if (n < 0) return "";
  n += 11;
  int e = info.indexOf('\n', n);
  String r = info.substring(n, e < 0 ? info.length() : e);
  r.trim();
  return r;
}

String runWorkflow(const String& id, const String& input, String& err) {
  if (!isAllowed(id, "workflow")) { err = "Dieser Workflow ist nicht freigegeben"; return ""; }
  // Auslöser herausfinden: Chat, Webhook, Formular oder ohne Eingabe
  JsonDocument a;
  a["workflowId"] = id;
  a["detailLevel"] = "execution";
  String s;
  if (!call("get_workflow_details", a, s, err)) return "";
  JsonDocument det;
  deserializeJson(det, s);
  String info = det["triggerInfo"] | "";
  bool active = det["workflow"]["active"] | false;
  JsonDocument x;
  x["workflowId"] = id;
  x["executionMode"] = active ? "production" : "manual";
  String t;
  if ((t = triggerName(info, "Chat trigger")).length()) {
    x["triggerNodeName"] = t;
    x["inputs"]["chatInput"] = input.length() ? input : String("Los");
  } else if ((t = triggerName(info, "Webhook trigger")).length()) {
    x["triggerNodeName"] = t;
    x["inputs"]["webhookData"]["method"] = "POST";
    x["inputs"]["webhookData"]["body"]["text"] = input;
  } else if ((t = triggerName(info, "Form trigger")).length()) {
    x["triggerNodeName"] = t;
    x["inputs"]["formData"]["text"] = input;
  }
  if (!call("execute_workflow", x, s, err)) return "";
  JsonDocument ex;
  deserializeJson(ex, s);
  String execId = first({ex["executionId"], ex["data"]["executionId"], ex["id"]});
  if (!execId.length()) return "Gestartet.";
  // auf das Ende warten
  JsonDocument g;
  g["workflowId"] = id;
  g["executionId"] = execId;
  String status;
  for (int i = 0; i < 45; i++) {
    delay(2000);
    if (!call("get_workflow_execution", g, s, err)) return "";
    JsonDocument st;
    deserializeJson(st, s);
    status = first({st["status"], st["data"]["status"], st["execution"]["status"]});
    if (status.length() && status != "running" && status != "new" && status != "waiting") break;
  }
  if (status == "running" || status == "new") return "Läuft noch – das Ergebnis kommt später in n8n an.";
  // Ergebnis: Daten der Knoten, gekürzt
  g["includeData"] = true;
  g["truncateData"] = 1;
  if (!call("get_workflow_execution", g, s, err)) return "";
  if (s.length() > 6000) s = s.substring(0, 6000) + " …";
  return "Status " + status + ". Daten: " + s;
}

String askAgent(const String& id, const String& message, String& session, String& err) {
  if (!isAllowed(id, "agent")) { err = "Dieser Agent ist nicht freigegeben"; return ""; }
  JsonDocument a;
  a["agentId"] = id;
  a["request"]["type"] = "message";
  a["request"]["message"] = message;
  if (session.length()) a["request"]["sessionId"] = session;
  String s;
  if (!call("call_agent", a, s, err)) return "";
  JsonDocument r;
  if (!deserializeJson(r, s)) {
    String sid = first({r["sessionId"], r["data"]["sessionId"]});
    if (sid.length()) session = sid;
  }
  if (s.length() > 6000) s = s.substring(0, 6000) + " …";
  return s;
}

}  // namespace n8n
