#include <float.h>
#include <stdio.h>
#include <string.h>
#include <uuid/uuid.h>
#include <ctime>
#include <queue>
#include <random>
#include <fstream>
#include <iomanip>
#include <csignal>
#include <thread>
#include <iostream>
#include "ctpapi/ThostFtdcMdApi.h"
#include "cmdline/cmdline.h"
#include "concurrentqueue/concurrentqueue.h"
#include "restclient-cpp/restclient.h"
#include "Logging.hpp"
#include "QuoteSpi.hpp"

#define PROCESSOR_COUNT 6

void configCliParser(int argc, char** argv);
void parseCodes(std::string str, char sep);
void terminationSignalHandler(int signum);
void setupCTP();
void processData(int n);
void parseData(CThostFtdcDepthMarketDataField* data, int processorId);
long parseDatetime(TThostFtdcDateType dateStr, TThostFtdcTimeType timeStr, TThostFtdcMillisecType millisec);
long getNowTime(void);
std::string genUUID();

std::string uuid;
cmdline::parser params;
CThostFtdcMdApi* quoteApi;
char** codes;
unsigned long codeCount;
std::atomic<bool> running(true);
moodycamel::ConcurrentQueue<CThostFtdcDepthMarketDataField*> dataQueue(1024);
std::string dbWriteUrl;
int tradingDate = 0;


int main(int argc, char** argv) {
    printf("Quote-CTP starts......\n");
    uuid = genUUID();
    std::cout << "UUID: " << uuid << std::endl;

    signal(SIGINT, terminationSignalHandler);
    signal(SIGTERM, terminationSignalHandler);
    configCliParser(argc, argv);
    initSpdLog(uuid, params.get<std::string>("path-logs") + "Quote-CTP_" + uuid + ".log");
    dbWriteUrl = params.get<std::string>("db-url") + "/write?precision=ms&db=" + params.get<std::string>("db-name");

    std::thread dataProcessors[PROCESSOR_COUNT];
    setupCTP();
    for (int i=0; i<PROCESSOR_COUNT; i++) dataProcessors[i] = std::thread(processData, i);
    for (auto& dataProcessor: dataProcessors) dataProcessor.join();

    // release resource
    quoteApi->Release();
    for(int i=0;i<codeCount;i++) delete(codes[i]);
    delete(codes);
    printf("Quote-CTP ends......\n");
    return 0;
}

void configCliParser(int argc, char** argv) {
    params.add<std::string>("front", 0, "Front server address", true, "");
    params.add<std::string>("broker", 0, "Broker ID", true, "");
    params.add<std::string>("investor", 0, "Investor ID", true, "");
    params.add<std::string>("password", 0, "Investor password", true, "");
    params.add<std::string>("codes", 0, "Instrument codes to subscribe which separated with \";\"", true, "");
    params.add<std::string>("db-url", 0, "InfluxDB server URL", true, "");
    params.add<std::string>("db-name", 0, "InfluxDB database name", true, "");
    params.add<std::string>("path-conn", 0, "Temp path for storing connection flow", true, "");
    params.add<std::string>("path-logs", 0, "Full path for storing logs file", true, "");
    params.parse_check(argc, argv);
}

void terminationSignalHandler(int signum) {
    printf("Termination signal received\n");
    running.store(false, std::memory_order_release);
}

void setupCTP(){
    // parse front address
    std::string frontAddrStr = params.get<std::string>("front");
    char frontAddr[frontAddrStr.length() + 1];
    strcpy(frontAddr, frontAddrStr.c_str());
    parseCodes(params.get<std::string>("codes"), ';');

    std::string prefix=params.get<std::string>("path-conn")+uuid+"_";
    quoteApi = CThostFtdcMdApi::CreateFtdcMdApi(prefix.c_str());
    QuoteSpi* quoteSpi = new QuoteSpi();
    quoteApi->RegisterSpi((CThostFtdcMdSpi*)quoteSpi);

    quoteApi->RegisterFront(frontAddr);
    quoteApi->Init();
}

void processData(int n) {
    CThostFtdcDepthMarketDataField* data;
    printf("Processing thread starts: %02d\n", n);
    while (running.load(std::memory_order_acquire)) {
        if (dataQueue.try_dequeue(data)) {
            try {
                parseData(data, n);
                delete data;
            } catch (const std::exception& exc) {
                printf("Error while parsing data: %s\n", exc.what());
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    printf("Processing thread ends: %02d\n", n);
}

void parseData(CThostFtdcDepthMarketDataField* tick, int processorId) {
    long receivedTime = getNowTime();
    std::ostringstream sTick;
    // Measurement
    sTick << tick->InstrumentID
          // Tags
          << ",source=CTP,date=" << tradingDate
          // Fields
          << " "
          << "P=" << tick->LastPrice
          << ",AccV=" << tick->Volume
          << ",AccT=" << tick->Turnover
          << ",AvgP=" << tick->AveragePrice
          << ",OI=" << tick->OpenInterest;

    if (tick->BidVolume1>0) sTick << ",BP1=" << tick->BidPrice1 << ",BV1=" << tick->BidVolume1;
    if (tick->AskVolume1>0) sTick << ",AP1=" << tick->AskPrice1 << ",AV1=" << tick->AskVolume1;
    if (tick->BidVolume2>0) sTick << ",BP2=" << tick->BidPrice2 << ",BV2=" << tick->BidVolume2;
    if (tick->AskVolume2>0) sTick << ",AP2=" << tick->AskPrice2 << ",AV2=" << tick->AskVolume2;
    if (tick->BidVolume3>0) sTick << ",BP3=" << tick->BidPrice3 << ",BV3=" << tick->BidVolume3;
    if (tick->AskVolume3>0) sTick << ",AP3=" << tick->AskPrice3 << ",AV3=" << tick->AskVolume3;
    if (tick->BidVolume4>0) sTick << ",BP4=" << tick->BidPrice4 << ",BV4=" << tick->BidVolume4;
    if (tick->AskVolume4>0) sTick << ",AP4=" << tick->AskPrice4 << ",AV4=" << tick->AskVolume4;
    if (tick->BidVolume5>0) sTick << ",BP5=" << tick->BidPrice5 << ",BV5=" << tick->BidVolume5;
    if (tick->AskVolume5>0) sTick << ",AP5=" << tick->AskPrice5 << ",AV5=" << tick->AskVolume5;

    if (tick->Volume == 0) { // Fields only in the pre-opening tick for last trading date
        sTick << ",ULP=" << tick->UpperLimitPrice
              << ",LLP=" << tick->LowerLimitPrice
              << ",SP=" << tick->PreSettlementPrice
              << ",D=" << tick->PreDelta;
    } else { // Fields not in the pre-opening tick for current trading date
        sTick << ",HP=" << tick->HighestPrice << ",LP=" << tick->LowestPrice;
        // Fields only in closing ticks
        if (tick->SettlementPrice != DBL_MAX) sTick << ",SP=" << tick->SettlementPrice;
        if (tick->CurrDelta != DBL_MAX) sTick << ",D=" << tick->CurrDelta;
    }

    // Timestamp
    long ts = parseDatetime(tick->ActionDay, tick->UpdateTime, tick->UpdateMillisec);
    sTick << " " << ts;

    if (ts<0) { // Ignore invalid tick
        printf("Tick ignored: Thread=%02d, Code=%s, UpdateTime=%s\n", processorId, tick->InstrumentID, tick->UpdateTime);
        logger->info("Tick Ignored. Code={}, TradingDay={}, ActionDay={}, UpdateTime={}, [InfluxDB] {}", tick->InstrumentID, tick->TradingDay, tick->ActionDay, tick->UpdateTime, sTick.str());
    } else { // Save to InfluxDB
        long latency = receivedTime-ts;
        RestClient::Response resp = RestClient::post(dbWriteUrl, "application/octet-stream", sTick.str());
        if (resp.code >= 200 && resp.code < 300) {
            long savedTime = getNowTime();
            printf("Tick saved: Thread=%02d, Code=%s, LatencyRecv=%ld, LatencySave=%ld\n", processorId, tick->InstrumentID, latency, savedTime-receivedTime);
        } else {
            printf("Failed to save tick into InfluxDB: [%d] %s\n", resp.code, resp.body.c_str());
        };
        logger->info("Tick Saved. Code={}, TradingDay={}, ActionDay={}, UpdateTime={}, [InfluxDB] {}", tick->InstrumentID, tick->TradingDay, tick->ActionDay, tick->UpdateTime, sTick.str());
    }
}

/*** ActionDay:
 * SHFE
 *     TradingDay: 交易日
 *     ActionDay: 行情日
 * DCE
 *     TradingDay: 交易日
 *     ActionDay: 交易日
 * CZC
 *     TradingDay: 行情日
 *     ActionDay: 行情日
 */
long parseDatetime(TThostFtdcDateType dateStr, TThostFtdcTimeType timeStr, TThostFtdcMillisecType millisec) {
    int h, m, s;
    sscanf(timeStr, "%2d:%2d:%2d", &h, &m, &s);

    // Convert ActionDay
    time_t now = time(0);
    struct tm *tmNow = localtime(&now);
    if (h==23 && tmNow->tm_hour==0) {
        now -= 3600;
        tmNow = localtime(&now);
    } else if (h==0 && tmNow->tm_hour==23) {
        now += 3600;
        tmNow = localtime(&now);
    } else if (h>18 && tmNow->tm_hour>5 && tmNow->tm_hour<9) { // Invalid
        return -1;
    }
    // Parse DateTime
    sscanf(timeStr, "%2d:%2d:%2d", &tmNow->tm_hour, &tmNow->tm_min, &tmNow->tm_sec);
    long ts = mktime(tmNow);
    return ts * 1000 + millisec;
}

long getNowTime(void) {
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    long ms = (long)(spec.tv_nsec / 1e6);
    return spec.tv_sec * 1000 + ms;
}

void parseCodes(std::string str, char sep) {
    std::stringstream ss;
    ss.str(str);

    std::string item;
    std::vector<std::string> elems;
    while (std::getline(ss, item, sep)) {
        *(std::back_inserter(elems)++) = item;
    }

    codeCount = elems.size();
    codes = new char*[codeCount];
    for(int i=0;i<codeCount;i++){
        codes[i]=new char[elems[i].length()+1];
        strcpy(codes[i], elems[i].c_str());
    }
}

std::string genUUID() {
    uuid_t uuid = {0};
    uuid_generate_time_safe(uuid);
    char uuid_str[37];
    uuid_unparse_lower(uuid, uuid_str);
    return std::string(uuid_str);
}
