#ifndef TOXPK_H
#define TOXPK_H

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <QHash>

class ToxPk
{
public:
    ToxPk();
    ToxPk(const ToxPk& other);
    explicit ToxPk(const QByteArray& rawId);
    explicit ToxPk(const uint8_t* rawId);
    ToxPk& operator=(const ToxPk& other) = default;
    ToxPk& operator=(ToxPk&& other) = default;

    bool operator==(const ToxPk& other) const;
    bool operator!=(const ToxPk& other) const;
    bool operator<(const ToxPk& other) const;
    QString toString() const;
    QByteArray getKey() const;
    const uint8_t* getBytes() const;
    bool isEmpty() const;

    static int getPkSize();

private:
    QByteArray key;
};

inline uint qHash(const ToxPk& key)
{
    return qHash(key.getKey());
}

#endif // TOXPK_H
