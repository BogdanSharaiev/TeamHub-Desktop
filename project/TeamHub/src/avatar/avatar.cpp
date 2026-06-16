#include "avatar.h"

#include <QApplication>
#include <QFont>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

namespace Avatar {

QString initialFor(const QString &name)
{
    for (const QChar &c : name) {
        if (c.isLetterOrNumber())
            return QString(c.toUpper());
    }
    return "?";
}

QColor colorForId(const QString &id)
{
    static const QList<QColor> palette = {QColor("#007acc"),
                                          QColor("#4ec9b0"),
                                          QColor("#c586c0"),
                                          QColor("#ce9178"),
                                          QColor("#569cd6"),
                                          QColor("#d7ba7d"),
                                          QColor("#b5cea8"),
                                          QColor("#f44747")};
    const quint32 h = qHash(id);
    return palette[h % palette.size()];
}

QString resolveUrl(const QString &baseUrl, const QString &url)
{
    if (url.isEmpty() || !QUrl(url).isRelative())
        return url;

    QString base = baseUrl;
    while (base.endsWith('/'))
        base.chop(1);

    return url.startsWith('/') ? base + url : base + '/' + url;
}

QPixmap letterPixmap(const QString &text, const QColor &color, int size)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, size, size);

    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(qRound(size * (size <= 24 ? 0.46 : 0.42)));
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(pix.rect(), Qt::AlignCenter, text);

    return pix;
}

QPixmap circularPixmap(const QPixmap &source, int size)
{
    QPixmap scaled = source.scaled(size,
                                   size,
                                   Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);
    const QRect cropRect((scaled.width() - size) / 2, (scaled.height() - size) / 2, size, size);
    scaled = scaled.copy(cropRect);

    QPixmap result(size, size);
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addEllipse(0, 0, size, size);
    p.setClipPath(path);
    p.drawPixmap(0, 0, scaled);

    return result;
}

void load(QObject *context,
          const QString &avatarUrl,
          const QPixmap &fallback,
          int size,
          std::function<void(QPixmap)> apply)
{
    apply(fallback);

    if (avatarUrl.isEmpty())
        return;

    static QNetworkAccessManager *nam = new QNetworkAccessManager(qApp);
    QNetworkReply *reply = nam->get(QNetworkRequest(QUrl(avatarUrl)));
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, size, apply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;

        QPixmap photo;
        if (photo.loadFromData(reply->readAll()) && !photo.isNull())
            apply(circularPixmap(photo, size));
    });
}

}
