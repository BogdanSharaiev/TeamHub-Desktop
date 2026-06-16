#ifndef AVATAR_H
#define AVATAR_H

#include <QColor>
#include <QObject>
#include <QPixmap>
#include <QString>

#include <functional>

namespace Avatar {

QString initialFor(const QString &name);
QColor colorForId(const QString &id);

QString resolveUrl(const QString &baseUrl, const QString &url);

QPixmap letterPixmap(const QString &text, const QColor &color, int size);

QPixmap circularPixmap(const QPixmap &source, int size);

void load(QObject *context,
          const QString &avatarUrl,
          const QPixmap &fallback,
          int size,
          std::function<void(QPixmap)> apply);

}

#endif // AVATAR_H
