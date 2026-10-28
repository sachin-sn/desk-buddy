#pragma once
// Bluetooth keyboard + media keys. Core 0 only.
void hidBegin();
bool hidConnected();
void hidEnd();            // stop Bluetooth completely (before sleeping)
int  hidBondCount();   // Macs this buddy has paired with (-1 = Bluetooth not running); for the serial log
// Runs a shortcut string (see keycombo.h). Returns false if not paired or the string is invalid.
// May block for up to ~2 s (waits and typing inside the sequence).
bool hidRun(const char* sequence);
