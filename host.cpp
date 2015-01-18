#include "host.h"
#include <QtDebug>

Host::Host(const Host *host, QObject *parent) :
    QObject(parent), name(host->name), ip(host->ip), desc(host->desc), url(host->url)
{
    tableItem=NULL;
}

Host::Host(QObject *parent) :
    QObject(parent)
{
}
