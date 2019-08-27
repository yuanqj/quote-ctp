#include <float.h>
#include <stdio.h>
#include <string.h>
#include <ctime>
#include <queue>
#include <random>
#include <fstream>
#include <iomanip>
#include <csignal>
#include <thread>
#include <iostream>
#include <uuid/uuid.h>
#include <ctpapi/ThostFtdcMdApi.h>
#include <cmdline/cmdline.h>
#include <concurrentqueue/concurrentqueue.h>
#include <restclient-cpp/restclient.h>
#include "quote_client.hh"

std::string gen_uuid();
void config_cli(int argc, char** argv);
std::vector<std::string> parse_instruments(const std::string *str, char sep);
void term_sig_handler(int signum);

std::string uuid;
cmdline::parser *params;
QuoteClient *client;


int main(int argc, char** argv) {
    printf("Quote-CTP starts......\n");
    uuid = gen_uuid();
    std::cout << "UUID: " << uuid << std::endl;

    signal(SIGINT, term_sig_handler);
    signal(SIGTERM, term_sig_handler);
    params = new cmdline::parser();
    config_cli(argc, argv);

    auto instruments = parse_instruments(&params->get<std::string>("instruments"), ';');
    client = new QuoteClient(
            &params->get<std::string>("broker"),
            &params->get<std::string>("investor"),
            &params->get<std::string>("password"),
            &params->get<std::string>("front-addr"),
            &instruments,
            &params->get<std::string>("path-conn"),
            &params->get<std::string>("path-conn")
    );
    client->init();
    client->wait();

    delete(client);
    delete(params);
    printf("Quote-CTP ends......\n");
    return 0;
}

void config_cli(int argc, char** argv) {
    params->add<std::string>("broker", 0, "Broker ID", true, "");
    params->add<std::string>("investor", 0, "Investor ID", true, "");
    params->add<std::string>("password", 0, "Investor password", true, "");
    params->add<std::string>("front-addr", 0, "Front server address", true, "");
    params->add<std::string>("instruments", 0, "Instruments to subscribe which separated with ';'", true, "");
    params->add<std::string>("path-conn", 0, "File path for storing connection flow", true, "");
    params->add<std::string>("path-data", 0, "File path for storing tick data", true, "");
    params->parse_check(argc, argv);
}

void term_sig_handler(int signum) {
    printf("\nTermination signal received\n\n");
    if (client) client->stop();
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
long parseDatetime(TThostFtdcDateType dateStr, TThostFtdcTimeType timeStr, TThostFtdcMillisecType millisec, TThostFtdcVolumeType volume) {
    // Convert ActionDay
    time_t now = time(0);
    struct tm tmNow = {0};
    localtime_r(&now, &tmNow);

    // Invalid tick in non-trading periods
    bool non_trading = (tmNow.tm_wday==6 && tmNow.tm_hour>8) || tmNow.tm_wday==0 || (tmNow.tm_wday==1 && tmNow.tm_hour<8) || (tmNow.tm_hour>=5 && tmNow.tm_hour<8) || (tmNow.tm_hour>=17 && tmNow.tm_hour<20);
    if (non_trading && volume > 0) return -1;

    int h, m, s;
    sscanf(timeStr, "%2d:%2d:%2d", &h, &m, &s);
    if (h==23 && tmNow.tm_hour==0 && tmNow.tm_min<10) {
        now -= 3600;
        localtime_r(&now, &tmNow);
    } else if (h==0 && tmNow.tm_hour==23) {
        now += 3600;
        localtime_r(&now, &tmNow);
    } else if (h>16 && tmNow.tm_hour<9) { // Invalid
        return -1;
    }
    // Parse DateTime
    tmNow.tm_hour=h; tmNow.tm_min=m; tmNow.tm_sec=s;
    long ts = mktime(&tmNow);
    return ts * 1000 + millisec;
}

long getNowTime() {
    struct timespec spec{0};
    clock_gettime(CLOCK_REALTIME, &spec);
    long ms = (long)(spec.tv_nsec / 1e6);
    return spec.tv_sec * 1000 + ms;
}

std::vector<std::string> parse_instruments(const std::string *str, char sep) {
    std::stringstream ss;
    ss.str(*str);

    std::string item;
    std::vector<std::string> instruments;
    while (std::getline(ss, item, sep)) {
        *(std::back_inserter(instruments)++) = item;
    }
    return instruments;
}

std::string gen_uuid() {
    uuid_t uuid = {0};
    uuid_generate_time_safe(uuid);
    char uuid_str[37];
    uuid_unparse_lower(uuid, uuid_str);
    return std::string(uuid_str);
}
