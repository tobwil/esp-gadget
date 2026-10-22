// n8n über den MCP-Server der eigenen Instanz (JSON-RPC über HTTPS). Der Assistent darf nur Workflows und
// Agenten benutzen, die in der Weboberfläche freigegeben wurden – Anlegen, Ändern, Löschen gibt es hier nicht.
#pragma once
#include <Arduino.h>
#include <vector>

namespace n8n {

struct Entry { String id, name, kind, desc; };  // kind: "workflow" oder "agent"

void begin();
bool configured();
String url();
void set(const char* url, const char* token);  // nullptr = unverändert; Token wird nie zurückgegeben

std::vector<Entry> allowed();
bool isAllowed(const String& id, const char* kind);
void setAllowed(const String& json);           // [{id,name,kind,desc}, …]
String allowedJson();

// Alles, was die Instanz anbietet (für die Auswahl in der Weboberfläche), als JSON-Liste
String listAll(String& err);
// Freigegebenen Workflow starten und auf das Ergebnis warten (bis ~90 s); Ergebnis als kurzer Text
String runWorkflow(const String& id, const String& input, String& err);
// Freigegebenem Agenten eine Nachricht schicken; session wird fortgeführt
String askAgent(const String& id, const String& message, String& session, String& err);

}  // namespace n8n
