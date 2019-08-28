#include "quote_processor.hh"
#include <iostream>


/*** CThostFtdcDepthMarketDataField:
 * SHFE, INE, CFE
 *     TradingDay: 交易日
 *     ActionDay: 行情日
 * DCE
 *     TradingDay: 交易日
 *     ActionDay: 交易日
 * CZC
 *     TradingDay: 行情日
 *     ActionDay: 行情日
 */
static inline uint filter(TThostFtdcTimeType time_str, TThostFtdcPriceType price, TThostFtdcVolumeType volume) {
    // Invalid price
    if (price < 1e-7) return 0;

    time_t now = time(nullptr);
    struct tm tm = {0};
    localtime_r(&now, &tm);

    // Invalid tick in non-trading periods
    bool non_trading = (tm.tm_wday==6 && tm.tm_hour>5) ||
                       tm.tm_wday==0 ||
                       (tm.tm_wday==1 && tm.tm_hour<8) ||
                       (tm.tm_hour>5 && tm.tm_hour<8) ||
                       (tm.tm_hour>=17 && tm.tm_hour<20);
    if (non_trading && volume > 1e-3) return 0;

    char **time_ptr = &time_str;
    auto h=(int)strtol(*time_ptr, time_ptr+2, 10);
    if (h == tm.tm_hour) { // Valid time
    } else if (h==23 && tm.tm_hour==0) {
        now -= 3600;
    } else if (h==0 && tm.tm_hour==23) {
        now += 3600;
    } else if (volume < 1e-3) {
    } else { // Invalid time
        return 0;
    }
    localtime_r(&now, &tm);
    return uint((tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
}

QuoteProcessor::QuoteProcessor(const std::string *data_path) {
    this->data_path = boost::filesystem::path(*data_path);
    this->running = new std::atomic<bool>(true);
    this->date = new std::atomic<uint>(0);
    this->buff = new moodycamel::ConcurrentQueue<CThostFtdcDepthMarketDataField*>(1024);
    this->processor = new std::thread(&QuoteProcessor::process, this);
}

QuoteProcessor::~QuoteProcessor() {
    this->stop();
    delete(this->processor);
    CThostFtdcDepthMarketDataField* tick;
    while (this->buff->try_dequeue(tick)) delete(tick);
    delete(this->buff);
    delete(this->running);
}

void QuoteProcessor::set_date(uint date) {
    uint date0 = this->date->load(std::memory_order_acquire);
    if (date == date0) return;
    this->date->store(date, std::memory_order_acquire);
    if (date == 0) return;
    boost::filesystem::path data_dir(std::to_string(date));
    boost::filesystem::create_directory(this->data_path/data_dir);
}

void QuoteProcessor::join() {
    this->processor->join();
}

void QuoteProcessor::stop() {
    this->running->store(false, std::memory_order_acquire);
}

bool QuoteProcessor::on_tick(CThostFtdcDepthMarketDataField *tick) {
    if (!this->running->load(std::memory_order_acquire) || this->date->load(std::memory_order_acquire) <= 0) return false;
    this->buff->enqueue(tick);
    return true;
}

void QuoteProcessor::process() {
    CThostFtdcDepthMarketDataField* tick;
    while (this->running->load(std::memory_order_acquire)) {
        if (this->buff->try_dequeue(tick)) {
            uint date = filter(tick->UpdateTime, tick->LastPrice, tick->Volume);
            if (date <= 0) continue;
            printf("TICK: Code=%s, Date=%d, UpdateTime=%s, Volume=%d\n", tick->InstrumentID, date, tick->UpdateTime, tick->Volume);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}