#pragma once
#include "Shared.h"
struct TraceOverride { DWORD preferred_va; void* target; };
struct TraceStats { LONG entered, left, texts, consumed, abandoned, failures; unsigned depth; };
bool InstallTraceTests(const std::vector<TraceOverride>& targets);
void RemoveTraceTests();
TraceStats ReadTraceStats();
bool EnableTraceTranslationTests(const std::wstring& root);
bool ReloadTraceTranslationTests();
LONG ReadTraceTranslationCount();
bool PrepareTracePresentationTests();
bool SetTracePresentationBuffer(bool stream,const std::string& bytes);
std::string ReadTracePresentationBuffer(bool stream,bool original=false);

std::string ReadTraceAssemblyBuffer(bool original=false);
void SetTracePrivateAssemblyBuffer(const std::string& bytes);

bool TestMenuSourceMonitor();

bool TestMenuSelectionComposition();

bool TestRecoveredOutputSites014();
