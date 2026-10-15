#pragma once
#include <QObject>
#include <QVariant>

// Keep the workspace alive across synchronous operations that pump the event loop.
class SessionTaskGuard
{
public:
    explicit SessionTaskGuard(QObject *owner) : m_owner(owner),
        m_previous(owner->property("sessionBusySynchronous").toBool())
    { m_owner->setProperty("sessionBusySynchronous", true); }
    ~SessionTaskGuard() { m_owner->setProperty("sessionBusySynchronous", m_previous); }
    SessionTaskGuard(const SessionTaskGuard &) = delete;
    SessionTaskGuard &operator=(const SessionTaskGuard &) = delete;
private:
    QObject *m_owner;
    bool m_previous;
};
