#pragma once

#ifndef cloud_hpp
#define cloud_hpp

#include <cstdint>

int cloudConnect(struct mqtt_client *client);
int cloudDisconnect(struct mqtt_client *client);
int cloudStart();
void cloudStop();

// *************************************************************
// Global Variables
// *************************************************************
extern bool val_networkConnected;
extern uint8_t val_cloudConnStatus;

#endif
