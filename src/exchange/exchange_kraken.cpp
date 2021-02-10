#include "exchange_kraken.h"

#include "julyaes256.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

Exchange_Kraken::Exchange_Kraken(QByteArray pRestSign, QByteArray pRestKey)
    : Exchange()
{
    calculatingFeeMode=1;
    clearHistoryOnCurrencyChanged = true;
	baseValues.exchangeName="Kraken";
	baseValues.currentPair.name="BCH/BTC";
	baseValues.currentPair.setSymbol("BCH/BTC");
	baseValues.currentPair.currRequestPair="BCHXBT";
	baseValues.currentPair.priceDecimals=8;
    minimumRequestIntervalAllowed=500;
	baseValues.currentPair.priceMin=qPow(0.1,baseValues.currentPair.priceDecimals);
	baseValues.currentPair.tradeVolumeMin=0.01;
	baseValues.currentPair.tradePriceMin=0.0001;
    depthAsks=0;
	depthBids=0;
	forceDepthLoad=false;
	julyHttp=0;
	isApiDown=false;
	tickerOnly=false;
    
    setApiKeySecret(pRestKey, pRestSign);
    
    currencyMapFile="Kraken";
	defaultCurrencyParams.currADecimals=8;
	defaultCurrencyParams.currBDecimals=8;
	defaultCurrencyParams.currABalanceDecimals=8;
	defaultCurrencyParams.currBBalanceDecimals=8;
	defaultCurrencyParams.priceDecimals=8;
	defaultCurrencyParams.priceMin=qPow(0.1,baseValues.currentPair.priceDecimals);

	supportsLoginIndicator=false;
    supportsAccountVolume=false;

	authRequestTime.restart();
    privateNonce=(QDateTime::currentDateTime().toTime_t()-1371854884)*10;
    lastFeeFetchDay = -1;
    lastPrivateCall = 0;
    timeBetweenPrivateCalls = 5 * 1000; // 3 secs

    connect(this, &Exchange::threadFinished, this, &Exchange_Kraken::quitThread, Qt::DirectConnection);
}

Exchange_Kraken::~Exchange_Kraken()
{
}

void Exchange_Kraken::quitThread()
{
    clearValues();

    if (depthAsks)
        delete depthAsks;

    if (depthBids)
        delete depthBids;

    if (julyHttp)
        delete julyHttp;
}

void Exchange_Kraken::clearVariables()
{
    cancelingOrderIDs.clear();
    Exchange::clearVariables();
    lastHistory.clear();
    lastOrders.clear();
    reloadDepth();
    lastFetchTid = 0;
    lastFeeFetchDay = -1;
    lastPrivateCall = 0;
}

void Exchange_Kraken::clearValues()
{
    clearVariables();

    if (julyHttp)
        julyHttp->clearPendingData();
}

void Exchange_Kraken::reloadDepth()
{
    lastDepthBidsMap.clear();
    lastDepthAsksMap.clear();
    lastDepthData.clear();
    Exchange::reloadDepth();
}

inline double toDouble(const QJsonValue & val)
{
    double d = val.isDouble() ? val.toDouble() :
               val.isString() ? val.toString().toDouble() : 0;
    return d;
}

void Exchange_Kraken::dataReceivedAuth(QByteArray data, int reqType)
{
    if (debugLevel)
        logThread->writeLog("RCV: " + data);
    if (data.size() && data.at(0) == QLatin1Char('<'))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj;
    QJsonArray arr;
    
    bool success = !doc.isEmpty() && doc.isObject() &&
            doc.object().contains("error") &&
            doc.object().value("error").isArray() &&
            doc.object().value("error").toArray().isEmpty();
	QString errorString;
    if (!success) {
        if (doc.isEmpty() || !doc.isObject() || !doc.object().contains("error")) {
            errorString = "Wrong API data";
        } else {
            errorString = doc.object().value("error").toArray().at(0).toString();
        }
    }
    
	switch(reqType)
	{
	case 103: //ticker; public : Ticker
		{
            if (!success) {
                break;
            }
            
            obj = doc.object().value("result").toObject().value(baseValues.currentPair.currRequestPair).toObject();
            if (obj.isEmpty()) {
                break;
            }
            
            if (obj.contains("h")) {
                double newTickerHigh=obj.value("h").toArray().at(1).toString().toDouble();
                if (newTickerHigh != lastTickerHigh) {
                    IndicatorEngine::setValue(baseValues.exchangeName,baseValues.currentPair.symbol,"High",newTickerHigh);
                    lastTickerHigh=newTickerHigh;
                }
            }

            if (obj.contains("l")) {
                double newTickerLow=obj.value("l").toArray().at(1).toString().toDouble();
                if (newTickerLow != lastTickerLow) {
                    IndicatorEngine::setValue(baseValues.exchangeName,baseValues.currentPair.symbol,"Low",newTickerLow);
                    lastTickerLow=newTickerLow;
                }
            }

			if(obj.contains("b"))
            {
                double newTickerSell=obj.value("b").toArray().at(0).toString().toDouble();
                if(newTickerSell!=lastTickerSell)
                    IndicatorEngine::setValue(baseValues.exchangeName,baseValues.currentPair.symbol,"Sell",newTickerSell);
				lastTickerSell=newTickerSell;
			}

			if(obj.contains("a"))
            {
                double newTickerBuy=obj.value("a").toArray().at(0).toString().toDouble();
                if(newTickerBuy!=lastTickerBuy)
                    IndicatorEngine::setValue(baseValues.exchangeName,baseValues.currentPair.symbol,"Buy",newTickerBuy);
				lastTickerBuy=newTickerBuy;
			}

			if(obj.contains("v"))
			{
                double newTickerVolume=obj.value("v").toArray().at(1).toString().toDouble();
                if(newTickerVolume!=lastTickerVolume)
                    IndicatorEngine::setValue(baseValues.exchangeName,baseValues.currentPair.symbol,"Volume",newTickerVolume);
				lastTickerVolume=newTickerVolume;
			}

			if(obj.contains("c"))
			{
                double tickerLastDouble=obj.value("c").toArray().at(0).toString().toDouble();
                if(tickerLastDouble>0.0)
                    IndicatorEngine::setValue(baseValues.exchangeName,baseValues.currentPair.symbol,"Last",tickerLastDouble);
                lastTickerLast = tickerLastDouble;
			}
		}
		break;//ticker; public : Ticker
	case 109: //trades; public : public/Trades
		{
            arr = doc.object().value("result").toObject().value(baseValues.currentPair.currRequestPair).toArray();
			QList<TradesItem> *newTradesItems=new QList<TradesItem>;
            
            quint64 newTid = 0;
            for (int i = 0; i < arr.count(); ++i) {
                QJsonArray t = arr[i].toArray();
                TradesItem newItem;
                
                newItem.date = (quint32) t.at(2).toDouble();
                newItem.price = t.at(0).toString().toDouble();
                quint64 currentTid = QString::number(t.at(2).toDouble(), 'f', 4).replace(".","").toULongLong();
                if (currentTid <= lastFetchTid) {
                    continue;
                } else {
                    if (newTid < currentTid) {
                        newTid = currentTid;
                    }
                }
                newItem.amount = t.at(1).toString().toDouble();
				newItem.symbol=baseValues.currentPair.symbol;
				newItem.orderType=t.at(3).toString() == "s" ? 1:-1;
                
                if(newItem.isValid())(*newTradesItems)<<newItem;
				else if(debugLevel)logThread->writeLog("Invalid trades fetch data line:"+data,2);
            }
            if (newTid != 0) {
                lastFetchTid = newTid;
            }
            if(newTradesItems->count())emit addLastTrades(baseValues.currentPair.symbol,newTradesItems);
			else delete newTradesItems;
		}
		break;//trades; public : public/Trades
	case 110: //Fee; private:command=returnFeeInfo
		break;// Fee; private:command=returnFeeInfo
	case 111: //depth; public : public/Depth
		if(success)
		{
			emit depthRequestReceived();

			if(lastDepthData!=data)
			{
				lastDepthData=data;
				depthAsks=new QList<DepthItem>;
				depthBids=new QList<DepthItem>;

                QMap<double,double> currentAsksMap;
                QJsonArray asksList = doc.object().value("result").toObject().value(baseValues.currentPair.currRequestPair).toObject().value("asks").toArray();
                double groupedPrice=0.0;
                double groupedVolume=0.0;
				int rowCounter=0;

				for(int n=0;n<asksList.count();n++)
				{
					if (baseValues.depthCountLimit && rowCounter>=baseValues.depthCountLimit)
                        break;
                    QJsonArray curArr = asksList.at(n).toArray();
                    double priceDouble=curArr.at(0).toString().toDouble();
                    double amount=curArr.at(1).toString().toDouble();

					if(baseValues.groupPriceValue>0.0)
					{
						if(n==0)
						{
                            emit depthFirstOrder(baseValues.currentPair.symbol,priceDouble,amount,true);
							groupedPrice=baseValues.groupPriceValue*(int)(priceDouble/baseValues.groupPriceValue);
							groupedVolume=amount;
						}
						else
						{
							bool matchCurrentGroup=priceDouble<groupedPrice+baseValues.groupPriceValue;
							if(matchCurrentGroup)groupedVolume+=amount;
							if(!matchCurrentGroup||n==asksList.count()-1)
							{
                                depthSubmitOrder(baseValues.currentPair.symbol,
                                                 &currentAsksMap,groupedPrice+baseValues.groupPriceValue,groupedVolume,true);
								rowCounter++;
								groupedVolume=amount;
								groupedPrice+=baseValues.groupPriceValue;
							}
						}
					}
					else
					{
                        depthSubmitOrder(baseValues.currentPair.symbol,
                                         &currentAsksMap,priceDouble,amount,true);
						rowCounter++;
					}
				}
                QList<double> currentAsksList=lastDepthAsksMap.keys();
				for(int n=0;n<currentAsksList.count();n++)
                    if(currentAsksMap.value(currentAsksList.at(n),0)==0)depthUpdateOrder(baseValues.currentPair.symbol,
                                                                                         currentAsksList.at(n),0.0,true);
				lastDepthAsksMap=currentAsksMap;

                QMap<double,double> currentBidsMap;
                QJsonArray bidsList = doc.object().value("result").toObject().value(baseValues.currentPair.currRequestPair).toObject().value("bids").toArray();
				groupedPrice=0.0;
				groupedVolume=0.0;
				rowCounter=0;

				for(int n=0;n<bidsList.count();n++)
				{
					if (baseValues.depthCountLimit && rowCounter>=baseValues.depthCountLimit)
                        break;
                    QJsonArray curArr = asksList.at(n).toArray();
                    double priceDouble=curArr.at(0).toString().toDouble();
                    double amount=curArr.at(1).toString().toDouble();
					if(baseValues.groupPriceValue>0.0)
					{
						if(n==0)
						{
                            emit depthFirstOrder(baseValues.currentPair.symbol,priceDouble,amount,false);
							groupedPrice=baseValues.groupPriceValue*(int)(priceDouble/baseValues.groupPriceValue);
							groupedVolume=amount;
						}
						else
						{
							bool matchCurrentGroup=priceDouble>groupedPrice-baseValues.groupPriceValue;
							if(matchCurrentGroup)groupedVolume+=amount;
							if(!matchCurrentGroup||n==asksList.count()-1)
							{
                                depthSubmitOrder(baseValues.currentPair.symbol,
                                                 &currentBidsMap,groupedPrice-baseValues.groupPriceValue,groupedVolume,false);
								rowCounter++;
								groupedVolume=amount;
								groupedPrice-=baseValues.groupPriceValue;
							}
						}
					}
					else
					{
                        depthSubmitOrder(baseValues.currentPair.symbol,
                                         &currentBidsMap,priceDouble,amount,false);
						rowCounter++;
					}
				}
                QList<double> currentBidsList=lastDepthBidsMap.keys();
				for(int n=0;n<currentBidsList.count();n++)
                    if(currentBidsMap.value(currentBidsList.at(n),0)==0)depthUpdateOrder(baseValues.currentPair.symbol,
                                                                                         currentBidsList.at(n),0.0,false);
				lastDepthBidsMap=currentBidsMap;

                emit depthSubmitOrders(baseValues.currentPair.symbol,depthAsks, depthBids);
				depthAsks=0;
				depthBids=0;
			}
		}
		else if(debugLevel)logThread->writeLog("Invalid depth data:"+data,2);
		break; //depth; public : public/Depth
	case 202: //info; private : account/getbalances
		{
			if (!success)
            {
                break;
            }
            
            QJsonObject balances = doc.object().value("result").toObject();
            QStringList keys = balances.keys();

            for (const QString & key : qAsConst(keys))
            {
                const QJsonValue & val = balances[key];
                double balance = toDouble(val);

                if (key == baseValues.currentPair.currAStr || key == baseValues.currentPair.currAltAStr)
                {
//                    if (lastBtcBalance != balance)
//                    {
//                        emit accBtcBalanceChanged(baseValues.currentPair.symbol, balance);
//                    }
                    lastBtcBalance = balance;
                }
                if (key == baseValues.currentPair.currBStr || key == baseValues.currentPair.currAltBStr)
                {
//                    if (balance != lastUsdBalance)
//                    {
//                        emit accUsdBalanceChanged(baseValues.currentPair.symbol, balance);
//                    }
                    lastUsdBalance = balance;
                }
            }
		}
		break;//info; private : account/getbalances
	case 204://orders; private : market/getopenorders
		{
        if (!success)
        {
            break;
        }

        double lastABalance = lastBtcBalance;
        double lastBBalance = lastUsdBalance;

        QJsonObject open         = doc.object().value("result").toObject();
        QJsonObject transactions = open.value("open").toObject();
        QStringList keys = transactions.keys();
        if (keys.isEmpty())
        {
            emit ordersIsEmpty();
        }
        else
        {
            QList<OrderItem> * orders = new QList<OrderItem>;

            for (const QString & key : qAsConst(keys))
            {
                const QJsonObject & val = transactions[key].toObject();
                if (val.isEmpty())
                {
                    continue;
                }

                const QJsonObject & descr = val.value("descr").toObject();
                if (descr.value("pair").toString() != baseValues.currentPair.currAStr + baseValues.currentPair.currBStr)
                {
                    continue;
                }

                OrderItem currentOrder;

                currentOrder.oid    = key.toLatin1();
                currentOrder.date   = toDouble(val.value("opentm"));
                currentOrder.status = 1;
                currentOrder.amount = toDouble(val.value("vol"));
                // currentOrder.price  = toDouble(val.value("price"));
                currentOrder.symbol = baseValues.currentPair.symbol;
                currentOrder.type   = descr.value("type").toString() == "sell";
                currentOrder.price  = toDouble(descr.value("price"));

                if (currentOrder.isValid())
                {
                    (*orders) << currentOrder;
                }

                if (currentOrder.type)
                {
                    // if sell
                    lastABalance -= currentOrder.amount;
                }
                else
                {
                    // if buy
                    lastBBalance -= currentOrder.amount * currentOrder.price;
                }
            }

            if (lastOrders != data)
            {
                emit orderBookChanged(baseValues.currentPair.symbol, orders);

                lastOrders = data;
            }
        }

        emit accBtcBalanceChanged(baseValues.currentPair.symbol, lastABalance);
        emit accUsdBalanceChanged(baseValues.currentPair.symbol, lastBBalance);

        break;//orders; private : market/getopenorders
		}
	case 305: //market/cancel
		if(success && !cancelingOrderIDs.isEmpty())
		{
            emit orderCanceled(baseValues.currentPair.symbol, cancelingOrderIDs.takeFirst());
		}
		break;//market/cancel
	case 306:
        if(debugLevel)logThread->writeLog("Buy OK: "+data,2);
        break;//order/buy
	case 307:
        if(debugLevel)logThread->writeLog("Sell OK: "+data,2);
        break;//order/sell
    case 208: //history; private : account/getorderhistory
        {
        if (!success)
        {
            break;
        }

        arr = doc.object().value("result").toArray();
        if (lastHistory != data)
		{
            QJsonObject open         = doc.object().value("result").toObject();
            QJsonObject transactions = open.value("closed").toObject();
            QStringList keys = transactions.keys();
            if (keys.isEmpty())
            {
                emit ordersIsEmpty();
                break;
            }

            QList<HistoryItem> * historyItems = new QList<HistoryItem>;
            
            for (const QString & key : qAsConst(keys))
            {
                const QJsonObject & val = transactions[key].toObject();
                if (val.isEmpty())
                {
                    continue;
                }

                if (val["status"] == "canceled")
                {
                    continue;
                }

                const QJsonObject & descr = val.value("descr").toObject();
                if (descr.value("pair").toString() != baseValues.currentPair.currAStr + baseValues.currentPair.currBStr)
                {
                    continue;
                }

                HistoryItem currentHistoryItem;

                currentHistoryItem.dateTimeInt = toDouble(val.value("opentm"));
                currentHistoryItem.volume      = toDouble(val.value("vol"));
                currentHistoryItem.symbol      = baseValues.currentPair.symbol;
                currentHistoryItem.type        = descr.value("type").toString() == "sell" ? 1 : 2;
                currentHistoryItem.price       = toDouble(descr.value("price"));

                if(currentHistoryItem.isValid())
                {
                    (*historyItems) << currentHistoryItem;
                }
            }

            std::sort(historyItems->begin(), historyItems->end(),
                      [](const HistoryItem & l, const HistoryItem & r)
                      {
                          return l.dateTimeInt > r.dateTimeInt;
                      });

			emit historyChanged(historyItems);

            lastHistory = data;
		}
		break;//history; private : account/getorderhistory
		}
	default: break;
	}

    static int authErrorCount=0;
    if(reqType>=200 && reqType<300)
    {
        if(!success)
        {
            authErrorCount++;
            if(authErrorCount > 2)
            {
                QString authErrorString = getMidData("message\":\"","\"",&data);
                if (debugLevel)
                    logThread->writeLog("API error: "+authErrorString.toLatin1()+" ReqType: "+QByteArray::number(reqType),2);

                if (authErrorString=="invalid api key")authErrorString=julyTr("TRUNAUTHORIZED","Invalid API key.");
                else if(authErrorString.startsWith("invalid nonce parameter"))authErrorString=julyTr("THIS_PROFILE_ALREADY_USED","Invalid nonce parameter.");
                if(!authErrorString.isEmpty())emit showErrorMessage(authErrorString);
            }
        }
        else authErrorCount=0;
    }

    static int errorCount=0;
    if (!success)
    {
        errorCount++;
        if (errorCount<3)
            return;
        if (debugLevel)
            logThread->writeLog("API error: " + errorString.toLatin1() + " ReqType: " + QByteArray::number(reqType), 2);
        if (errorString.isEmpty())
            return;
        if (errorString == QLatin1String("no orders"))
            return;
        if (reqType < 300)
            emit showErrorMessage("I:>" + errorString);
    }
    else errorCount=0;
}

void Exchange_Kraken::depthUpdateOrder(QString symbol, double price, double amount, bool isAsk)
{
    if (symbol != baseValues.currentPair.symbol)
        return;

    if (isAsk)
    {
        if (depthAsks == 0)
            return;

        DepthItem newItem;
        newItem.price = price;
        newItem.volume = amount;

        if (newItem.isValid())
            (*depthAsks) << newItem;
    }
    else
    {
        if (depthBids == 0)
            return;

        DepthItem newItem;
        newItem.price = price;
        newItem.volume = amount;

        if (newItem.isValid())
            (*depthBids) << newItem;
    }
}

void Exchange_Kraken::depthSubmitOrder(QString symbol, QMap<double, double>* currentMap, double priceDouble,
                                      double amount, bool isAsk)
{
    if (symbol != baseValues.currentPair.symbol)
        return;

    if (priceDouble == 0.0 || amount == 0.0)
        return;

    if (isAsk)
    {
        (*currentMap)[priceDouble] = amount;

        if (lastDepthAsksMap.value(priceDouble, 0.0) != amount)
            depthUpdateOrder(symbol, priceDouble, amount, true);
    }
    else
    {
        (*currentMap)[priceDouble] = amount;

        if (lastDepthBidsMap.value(priceDouble, 0.0) != amount)
            depthUpdateOrder(symbol, priceDouble, amount, false);
    }
}

bool Exchange_Kraken::isReplayPending(int reqType)
{
    if (julyHttp == 0)
        return false;

    return julyHttp->isReqTypePending(reqType);
}

void Exchange_Kraken::secondSlot()
{
    secondSlotPublic();
    secondSlotPrivate();

    Exchange::secondSlot();
}

void Exchange_Kraken::secondSlotPublic()
{
    static int sendCounter = 0;
    
    switch (sendCounter)
    {
        case 0:
            if (!isReplayPending(103))
                sendToApi(103, "Ticker", false, true, "pair=" + baseValues.currentPair.currRequestPair);

            break;

        case 1:
            if (!isReplayPending(109))
                sendToApi(109, "Trades", false, true, "pair=" + baseValues.currentPair.currRequestPair);

            break;

        case 2:
            if (isDepthEnabled() && (forceDepthLoad || !isReplayPending(111)))
            {
                emit depthRequested();
                sendToApi(111, "Depth", false, true, "pair=" + baseValues.currentPair.currRequestPair +
                          "&count=" + QString::number(baseValues.depthCountLimit).toLatin1());
                forceDepthLoad = false;
            }

            break;

        default:
            break;
    }

    if (sendCounter++ >= 2)
        sendCounter = 0;
}

void Exchange_Kraken::secondSlotPrivate()
{
    static int sendCounter = 0;
    
    qint64 time = QDateTime::currentMSecsSinceEpoch();
    if ((time - lastPrivateCall) < timeBetweenPrivateCalls) {
        return;
    } else {
        lastPrivateCall = time;
    }
    
    switch (sendCounter)
    {
        case 0:
            if (!isReplayPending(202))
                sendToApi(202, "Balance", true, true);
            break;

        case 1:
            if (!tickerOnly && !isReplayPending(204))
                sendToApi(204, "OpenOrders", true, true);
            break;
            
        case 2:
            if (!tickerOnly && !isReplayPending(110))
                sendToApi(110, "TradeVolume", true, true, "pair=" + baseValues.currentPair.currRequestPair);
            break;

        case 3:
            if (lastHistory.isEmpty())
                getHistory(false);

            break;

        default:
            break;
    }

    if (sendCounter++ >= 3)
        sendCounter = 0;
}

void Exchange_Kraken::getHistory(bool force)
{
	if (tickerOnly)
        return;

    if (force)
        lastHistory.clear();

    if(!isReplayPending(208))
        sendToApi(208, "ClosedOrders", true, true, "type=closed");
    
    // [TODO]: retrive fee from Kraken
    if (lastFee != 0.16) {
        lastFee = 0.16;
        emit accFeeChanged(baseValues.currentPair.symbol, lastFee);
    }
}

void Exchange_Kraken::buy(QString symbol, double apiBtcToBuy, double apiPriceToBuy)
{
    if (tickerOnly)
        return;

    CurrencyPairItem pairItem;
    pairItem = baseValues.currencyPairMap.value(symbol, pairItem);

    if (pairItem.symbol.isEmpty())
        return;

    QByteArray data = "pair=" + pairItem.currRequestPair +
            "&type=buy&ordertype=limit&price=" + 
            JulyMath::byteArrayFromDouble(apiPriceToBuy, pairItem.priceDecimals, 0) + "&volume=" +
            JulyMath::byteArrayFromDouble(apiBtcToBuy, pairItem.currADecimals, 0);

    if (debugLevel)
        logThread->writeLog("Buy: " + data, 2);

    sendToApi(306, "AddOrder", true, true, data);
}

void Exchange_Kraken::sell(QString symbol, double apiBtcToSell, double apiPriceToSell)
{
    if (tickerOnly)
        return;

    CurrencyPairItem pairItem;
    pairItem = baseValues.currencyPairMap.value(symbol, pairItem);

    if (pairItem.symbol.isEmpty())
        return;

    QByteArray data = "pair=" + pairItem.currRequestPair +
            "&type=sell&ordertype=limit&price=" + 
            JulyMath::byteArrayFromDouble(apiPriceToSell, pairItem.priceDecimals, 0) + "&volume=" +
            JulyMath::byteArrayFromDouble(apiBtcToSell, pairItem.currADecimals, 0);

    if (debugLevel)
        logThread->writeLog("Sell: " + data, 2);

    sendToApi(307, "AddOrder", true, true, data);
}

void Exchange_Kraken::cancelOrder(QString, QByteArray order)
{
	if (tickerOnly)
        return;

    cancelingOrderIDs << order;

	order.prepend("txid=");

	if (debugLevel)
        logThread->writeLog("Cancel order: " + order, 2);

	sendToApi(305, "CancelOrder", true, true, order);
}

void Exchange_Kraken::sendToApi(int reqType, QByteArray method, bool auth, bool sendNow, QByteArray commands)
{
	if(julyHttp==0)
	{ 
        julyHttp = new JulyHttp("api.kraken.com","", this);

		connect(julyHttp, SIGNAL(anyDataReceived()), baseValues_->mainWindow_, SLOT(anyDataReceived()));
		connect(julyHttp, SIGNAL(apiDown(bool)), baseValues_->mainWindow_, SLOT(setApiDown(bool)));
		connect(julyHttp, SIGNAL(setDataPending(bool)), baseValues_->mainWindow_, SLOT(setDataPending(bool)));
		connect(julyHttp, SIGNAL(errorSignal(QString)), baseValues_->mainWindow_, SLOT(showErrorMessage(QString)));
		connect(julyHttp, SIGNAL(sslErrorSignal(const QList<QSslError> &)), this, SLOT(sslErrors(const QList<QSslError> &)));
		connect(julyHttp, SIGNAL(dataReceived(QByteArray, int)), this, SLOT(dataReceivedAuth(QByteArray, int)));
	}

	if(auth)
    {
        QByteArray nonce = QByteArray::number(++privateNonce);
        QByteArray postData = "nonce=" + nonce + "&" + commands;
        QByteArray forHash = "/0/private/" + method + QByteArray::fromHex(JulyAES256::sha256(nonce + postData));
		if(sendNow)
			julyHttp->sendData(reqType, "POST /0/private/" + method, postData, "API-Key: " + getApiKey() + "\r\n" +
                               "API-Sign: "+ hmacSha512(QByteArray::fromBase64(getApiSign()), forHash).toBase64()+"\r\n");
		else
			julyHttp->prepareData(reqType, "POST /0/private/" + method, postData, "API-Key: " + getApiKey() + "\r\n" +
                                  "API-Sign: "+ hmacSha512(QByteArray::fromBase64(getApiSign()), forHash).toBase64()+"\r\n");
	}
	else
	{
		if (commands.isEmpty())
		{
			if(sendNow)
				julyHttp->sendData(reqType, "GET /0/public/" + method);
			else 
				julyHttp->prepareData(reqType, "GET /0/public/" + method);
		} else {
            if(sendNow)
				julyHttp->sendData(reqType, "GET /0/public/" + method + "?" + commands);
			else 
				julyHttp->prepareData(reqType, "GET /0/public/" + method + "?" + commands);
        }
    }
}

void Exchange_Kraken::sslErrors(const QList<QSslError>& errors)
{
    QStringList errorList;

    for (int n = 0; n < errors.count(); n++)
        errorList << errors.at(n).errorString();

    if (debugLevel)
        logThread->writeLog(errorList.join(" ").toLatin1(), 2);

    emit showErrorMessage("SSL Error: " + errorList.join(" "));
}
