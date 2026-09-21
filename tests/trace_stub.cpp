#include "trace.h"

bool lmcTrace::traceMode = false;
QString lmcTrace::fileName;

lmcTrace::lmcTrace(void) {}
lmcTrace::~lmcTrace(void) {}
void lmcTrace::init(XmlMessage*) {}
void lmcTrace::write(const QString&, bool) {}
