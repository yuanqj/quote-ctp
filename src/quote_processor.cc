#include "quote_processor.hh"
#include <iostream>
#include <spdlog/spdlog.h>

const double MAX = 1e19;
const char *tick_temp = "{}T{}.{:03d}"  // TickTime
                        ", {}"     // LastPrice
                        ", {}"     // PreSettlementPrice
                        ", {}"     // PreClosePrice
                        ", {:.0f}" // PreOpenInterest
                        ", {}"     // OpenPrice
                        ", {}"     // HighestPrice
                        ", {}"     // LowestPrice
                        ", {}"     // Volume
                        ", {:.1f}" // Turnover
                        ", {:.0f}" // OpenInterest
                        ", {}"     // ClosePrice
                        ", {}"     // SettlementPrice
                        ", {}"     // UpperLimitPrice
                        ", {}"     // LowerLimitPrice
                        ", {}"     // PreDelta
                        ", {}"     // CurrDelta
                        ", {}"     // BidPrice1
                        ", {}"     // BidVolume1
                        ", {}"     // AskPrice1
                        ", {}"     // AskVolume1
                        ", {}"     // BidPrice2
                        ", {}"     // BidVolume2
                        ", {}"     // AskPrice2
                        ", {}"     // AskVolume2
                        ", {}"     // BidPrice3
                        ", {}"     // BidVolume3
                        ", {}"     // AskPrice3
                        ", {}"     // AskVolume3
                        ", {}"     // BidPrice4
                        ", {}"     // BidVolume4
                        ", {}"     // AskPrice4
                        ", {}"     // AskVolume4
                        ", {}"     // BidPrice5
                        ", {}"     // BidVolume5
                        ", {}"     // AskPrice5
                        ", {}"     // AskVolume5
                        ", {:.3f}";     // AveragePrice


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
static inline uint filter(TThostFtdcTimeType time_str, TThostFtdcPriceType price) {
    // Invalid price
    if (price < 1e-7 || price>MAX) return 0;

    time_t now = time(nullptr);
    struct tm tm = {0};
    localtime_r(&now, &tm);

    // Invalid tick in non-trading periods
    bool non_trading = (tm.tm_wday==6 && tm.tm_hour>4) ||
                       tm.tm_wday==0 ||
                       (tm.tm_wday==1 && tm.tm_hour<8) ||
                       (tm.tm_hour>4 && tm.tm_hour<8) ||
                       (tm.tm_hour>16 && tm.tm_hour<20);
    if (non_trading) return 0;

    char **time_ptr = &time_str;
    auto h=(int)strtol(*time_ptr, time_ptr+2, 10);
    if (h == tm.tm_hour) { // Valid time
    } else if (h==23 && tm.tm_hour==0) {
        now -= 3600;
    } else if (h==0 && tm.tm_hour==23) {
        now += 3600;
    } else { // Invalid time
        return 0;
    }
    localtime_r(&now, &tm);
    return uint((tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
}

QuoteProcessor::QuoteProcessor(const std::string *data_path, std::vector<std::string> *instruments) {
    this->data_path = boost::filesystem::path(*data_path);
    this->instruments = instruments;
    this->running = new std::atomic<bool>(true);
    this->date = new std::atomic<uint>(0);
    this->buff = new moodycamel::ConcurrentQueue<CThostFtdcDepthMarketDataField*>(1024);
    this->processor = new std::thread(&QuoteProcessor::process, this);

    spdlog::set_async_mode(8192, spdlog::async_overflow_policy::block_retry, nullptr, std::chrono::seconds(3));
    spdlog::set_pattern("%v");
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

    boost::filesystem::path data_dir=this->data_path/boost::filesystem::path(std::to_string(date/100))/boost::filesystem::path(std::to_string(date));
    boost::filesystem::create_directories(data_dir);
    for (const auto &instrument : *this->instruments) {
        auto logger = spdlog::get(instrument);
        if (logger) continue;
        boost::filesystem::path data_file = data_dir/boost::filesystem::path(instrument+".csv");
        spdlog::basic_logger_st(instrument, data_file.string());
    }
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
            uint date = filter(tick->UpdateTime, tick->LastPrice);
            if (date <= 0) continue;
//            printf("TICK: Code=%s, Date=%d, UpdateTime=%s, Volume=%d\n", tick->InstrumentID, date, tick->UpdateTime, tick->Volume);
            auto logger = spdlog::get(tick->InstrumentID);
            if (logger == nullptr) {
                std::cout << "Unknown Instrument: " << tick->InstrumentID << std::endl;
                continue;
            }

            if (tick->LastPrice>MAX) tick->LastPrice=0;
            if (tick->PreSettlementPrice>MAX) tick->PreSettlementPrice=0;
            if (tick->PreClosePrice>MAX) tick->PreClosePrice=0;
            if (tick->PreOpenInterest>MAX) tick->PreOpenInterest=0;
            if (tick->OpenPrice>MAX) tick->OpenPrice=0;
            if (tick->HighestPrice>MAX) tick->HighestPrice=0;
            if (tick->LowestPrice>MAX) tick->LowestPrice=0;
            if (tick->Turnover>MAX) tick->Turnover=0;
            if (tick->OpenInterest>MAX) tick->OpenInterest=0;
            if (tick->ClosePrice>MAX) tick->ClosePrice=0;
            if (tick->SettlementPrice>MAX) tick->SettlementPrice=0;
            if (tick->UpperLimitPrice>MAX) tick->UpperLimitPrice=0;
            if (tick->LowerLimitPrice>MAX) tick->LowerLimitPrice=0;
            if (tick->PreDelta>MAX) tick->PreDelta=0;
            if (tick->CurrDelta>MAX) tick->CurrDelta=0;
            if (tick->AveragePrice>MAX) tick->AveragePrice=0;
            if (tick->AskVolume1<=0) tick->AskPrice1=0;
            if (tick->AskVolume2<=0) tick->AskPrice2=0;
            if (tick->AskVolume3<=0) tick->AskPrice3=0;
            if (tick->AskVolume4<=0) tick->AskPrice4=0;
            if (tick->AskVolume5<=0) tick->AskPrice5=0;
            if (tick->BidVolume1<=0) tick->BidPrice1=0;
            if (tick->BidVolume2<=0) tick->BidPrice2=0;
            if (tick->BidVolume3<=0) tick->BidPrice3=0;
            if (tick->BidVolume4<=0) tick->BidPrice4=0;
            if (tick->BidVolume5<=0) tick->BidPrice5=0;

            logger->info(
                    tick_temp,
                    date,
                    tick->UpdateTime,
                    tick->UpdateMillisec,

                    tick->LastPrice,
                    tick->PreSettlementPrice,
                    tick->PreClosePrice,
                    tick->PreOpenInterest,
                    tick->OpenPrice,
                    tick->HighestPrice,
                    tick->LowestPrice,
                    tick->Volume,
                    tick->Turnover,
                    tick->OpenInterest,
                    tick->ClosePrice,
                    tick->SettlementPrice,
                    tick->UpperLimitPrice,
                    tick->LowerLimitPrice,
                    tick->PreDelta,
                    tick->CurrDelta,
                    tick->BidPrice1,
                    tick->BidVolume1,
                    tick->AskPrice1,
                    tick->AskVolume1,
                    tick->BidPrice2,
                    tick->BidVolume2,
                    tick->AskPrice2,
                    tick->AskVolume2,
                    tick->BidPrice3,
                    tick->BidVolume3,
                    tick->AskPrice3,
                    tick->AskVolume3,
                    tick->BidPrice4,
                    tick->BidVolume4,
                    tick->AskPrice4,
                    tick->AskVolume4,
                    tick->BidPrice5,
                    tick->BidVolume5,
                    tick->AskPrice5,
                    tick->AskVolume5,
                    tick->AveragePrice
            );
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
