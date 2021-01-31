#ifndef EXCHANGE_KRAKEN_H
#define EXCHANGE_KRAKEN_H

#include "exchange.h"

class Exchange_Kraken : public Exchange
{
public:
    Exchange_Kraken(QByteArray pRestSign, QByteArray pRestKey);
    ~Exchange_Kraken();
private:
    bool isApiDown;
	bool isReplayPending(int);
    bool tickerOnly;
    
    JulyHttp *julyHttp;
    
    quint64 lastFetchTid;
    int lastFeeFetchDay;
    
    QList<DepthItem> *depthAsks;
	QList<DepthItem> *depthBids;
    QList<QByteArray> cancelingOrderIDs;
    
    QMap<double,double> lastDepthAsksMap;
	QMap<double,double> lastDepthBidsMap;
    
    QTime authRequestTime;
    
    quint32 privateNonce;
    
    qint64 lastPrivateCall;
    qint64 timeBetweenPrivateCalls;
    
    void clearVariables();
    void depthSubmitOrder(QString, QMap<double, double>* currentMap, double priceDouble, double amount, bool isAsk);
    void depthUpdateOrder(QString, double, double, bool);
    void sendToApi(int reqType, QByteArray method, bool auth=false, bool sendNow=true, QByteArray commands=0);
    
    void secondSlotPublic();
    void secondSlotPrivate();
private slots:
    void reloadDepth();
    void sslErrors(const QList<QSslError>&);
    void dataReceivedAuth(QByteArray, int);
    void secondSlot();
    void quitThread();
public slots:
    void clearValues();
    void getHistory(bool);
    void buy(QString, double, double);
    void sell(QString, double, double);
    void cancelOrder(QString, QByteArray);
};

#endif // EXCHANGE_KRAKEN_H
