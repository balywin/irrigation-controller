// webserver_embedded.h
// Declaration for the embedded webserver setup function implemented in
// src/webserver_embedded.cpp
#pragma once

#include <ESPAsyncWebServer.h>

// Install routes to serve embedded files and provide config API endpoints.
// The server instance must remain alive for the lifetime of the handlers.
void setupEmbeddedWebServer(AsyncWebServer &server);
