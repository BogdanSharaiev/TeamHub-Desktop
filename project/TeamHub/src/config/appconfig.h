#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QString>

namespace AppConfig {
QString djangoBaseUrl();
QString rgaServerUrl();
QString voiceServerHost();
quint16 voiceServerPort();
}

#endif // APPCONFIG_H