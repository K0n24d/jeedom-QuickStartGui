#include "searchpage.h"
#include "udpsearchworker.h"
#include "pingsearchworker.h"

#ifdef WITH_ZEROCONF
#include "bonjoursearchworker.h"
#endif

#include "dnslookupsearchworker.h"
#include <QVariant>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QTableWidget>
#include <QLabel>
#include <QHeaderView>
#include <QtDebug>
#include <QMutexLocker>
#include <QCoreApplication>


SearchPage::SearchPage(QWidget *parent) :
    QWizardPage(parent), progressBar(this), hostsTable(this)
{
    qDebug() << Q_FUNC_INFO << "Start";
//    qRegisterMetaType<Host>("Host");

    progressBar.setTextVisible(false);

    hostsTable.setColumnCount(3);
    hostsTable.setRowCount(0);
    hostsTable.setSelectionBehavior(QAbstractItemView::SelectRows);

    QStringList labels;
    labels << tr("Name") << tr("URL") << tr("IP Address") << tr("Description");
    hostsTable.setHorizontalHeaderLabels(labels);
    hostsTable.setDisabled(true);
    connect(&hostsTable, SIGNAL(itemSelectionChanged()), this, SIGNAL(completeChanged()));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(&hostsTable);
    layout->addStretch();
    layout->addWidget(&progressBar);
    setLayout(layout);

    qDebug() << Q_FUNC_INFO << "End";
}

void SearchPage::initializePage()
{
    qDebug() << Q_FUNC_INFO;

    isCleaningUp=false;

    QString title;

    if(!field("advancedSearch").toBool())
    {
        title=tr("Automatique search...");
    }
    else
    {
        title=tr("Advanced search...");
    }

    setTitle(title);
    setSubTitle(tr("Jeedom boxes will appear as they're found during the search."));

    hostsTable.clearContents();
    hostsTable.setRowCount(0);
    foreach(Host *host, hosts.values())
    {
        host->deleteLater();
    }
    hosts.clear();

    {
        QMutexLocker locker(&searchWorkersMutex);
        searchWorkers.empty();
    }

    if(field("dns").toBool())
        addWorker(new DNSLookupSearchWorker);

#ifdef WITH_ZEROCONF
    if(field("zeroconf").toBool())
    {
        addWorker(new BonjourSearchWorker(QString("_https._tcp")));
        addWorker(new BonjourSearchWorker(QString("_http._tcp")));
    }
#endif

    if(field("udp").toBool())
        addWorker(new UdpSearchWorker(wizard()));

    if(field("ping").toBool())
        addWorker(new PingSearchWorker);

    if(!searchWorkers.isEmpty())
    {
        progressBar.setRange(0,0);
        progressBar.setVisible(true);
    }
    else
    {
        progressBar.setRange(0,100);
        progressBar.setVisible(false);
    }

}

void SearchPage::addWorker(SearchWorker *worker)
{
    QMutexLocker locker(&searchWorkersMutex);

    qDebug() << Q_FUNC_INFO;

    QThread *searchThread = new QThread(this);
    worker->moveToThread(searchThread);
    connect(worker, SIGNAL(finished()), this, SLOT(searchFinished()));
    connect(worker, SIGNAL(error(QString,QString)), this, SLOT(gotError(QString,QString)));

    connect(worker, SIGNAL(host(Host*)), this, SLOT(gotHost(Host*)));
    connect(searchThread, SIGNAL(finished()), worker, SLOT(deleteLater()));
    connect(searchThread, SIGNAL(started()), worker, SLOT(discover()));
    connect(this, SIGNAL(cleaningUp()), worker, SLOT(stop()));

    searchThreads << searchThread;
    searchWorkers << worker;

    searchThread->start();

    QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
}

void SearchPage::resizeEvent(QResizeEvent *)
{
    qDebug() << Q_FUNC_INFO;

    hostsTable.setColumnWidth(0, hostsTable.width()/4);
    hostsTable.setColumnWidth(2, hostsTable.width()/4);

#if QT_VERSION >= 0x050000
    hostsTable.horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
#else
    hostsTable.horizontalHeader()->setResizeMode(1, QHeaderView::Stretch);
#endif
}

void SearchPage::cleanupPage()
{
    isCleaningUp=true;
    emit(cleaningUp());

    foreach(Host *host, hosts.values())
        delete host;

    hosts.clear();
    hostsTable.clearContents();

    while(!searchThreads.isEmpty())
    {
        foreach(QThread * searchThread, searchThreads)
            if(searchThread->isFinished())
                searchThreads.removeAll(searchThread);

        foreach(SearchWorker * searchWorker, searchWorkers)
            qDebug() << searchWorker->metaObject()->className();

        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    }

    searchThreads.clear();
}

bool SearchPage::isComplete() const
{
    return (searchWorkers.isEmpty() && (hostsTable.rowCount()>0));
}

void SearchPage::gotHost(Host *newHost)
{
    qDebug() << Q_FUNC_INFO << newHost->name << newHost->url;
    Host *host = new Host(newHost, this);
    delete newHost;

    QMutexLocker tableMutexLocker(&tableMutex);

    if(hosts.contains(host->url))
    {
        Host *oldHost=hosts[host->url];
        oldHost->desc.append("\n");
        oldHost->desc.append(host->desc);
        if(oldHost->tableItem)
        {
            int row = oldHost->tableItem->row();
            hostsTable.item(row, 0)->setToolTip(oldHost->desc);
            hostsTable.item(row, 1)->setToolTip(oldHost->desc);
            hostsTable.cellWidget(row, 1)->setToolTip(oldHost->desc);
            hostsTable.item(row, 2)->setToolTip(oldHost->desc);
        }

        host->deleteLater();
        return;
    }

    hosts.insert(host->url, host);

    hostsTable.setEnabled(true);

    int row=hostsTable.rowCount();
    hostsTable.setRowCount(row+1);

    QTableWidgetItem * item = new QTableWidgetItem(host->name);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    item->setToolTip(host->desc);
    hostsTable.setItem(row, 0, item);
    host->tableItem = item;

    QLabel *label = new QLabel(QString("<html><body><a href=\"%1\">%2</a></body></html>").arg(host->url, host->url));
    label->setOpenExternalLinks(true);
    label->setTextInteractionFlags(Qt::LinksAccessibleByKeyboard|Qt::LinksAccessibleByMouse);
    label->setToolTip(host->desc);
    hostsTable.setCellWidget(row, 1, label);

    item = new QTableWidgetItem();
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    item->setToolTip(host->desc);
    hostsTable.setItem(row, 1, item);

    item = new QTableWidgetItem(host->ip);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    item->setToolTip(host->desc);
    hostsTable.setItem(row, 2, item);
}

void SearchPage::gotError(const QString &title, const QString &message)
{
    qDebug() << Q_FUNC_INFO << title << message;

    QMessageBox::warning(NULL, title, message, QMessageBox::Close);
}

void SearchPage::searchFinished()
{
    QMutexLocker locker(&searchWorkersMutex);
    SearchWorker* sendingWorker = qobject_cast<SearchWorker*>(sender());
    bool workerConsideredRunning = searchWorkers.contains(sendingWorker);
    qDebug() << Q_FUNC_INFO << "Nb recherches en cours:" << searchWorkers.count()
             << "Fin de la recherche:" << sendingWorker->metaObject()->className()
             << "/" << workerConsideredRunning;

    if(!workerConsideredRunning)
        return;

    searchWorkers.removeOne(sendingWorker);
    sendingWorker->deleteLater();

    if(isCleaningUp)
        return;

    if(searchWorkers.isEmpty())
    {
        progressBar.setRange(0,100);
        progressBar.setVisible(false);
        setTitle(tr("Search finished."));
        if(hostsTable.rowCount()<=0)
        {
            setSubTitle(tr("No Jeedom box could be found. Please make sure they're powered on "
                           "and connected to the same network than your computer."
                           ));
            QMessageBox::warning(this, tr("Jeedom Finder"), tr("No Jeedom box could be found."), QMessageBox::Close, QMessageBox::Close);
        }
        else
            setSubTitle(tr("It is recommended to save the access URL to your Jeedom box "
                           "on your computer for futur use."
                           ));
            emit(completeChanged());
    }
    else
    {
        foreach(SearchWorker * searchWorker, searchWorkers)
            qDebug() << Q_FUNC_INFO << searchWorker->metaObject()->className() << "en cours";
    }
}
