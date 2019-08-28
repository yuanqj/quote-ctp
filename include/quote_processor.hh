#ifndef __QUOTE_PROCESSOR_HH__
#define __QUOTE_PROCESSOR_HH__

#include <string>
#include <thread>
#include <boost/filesystem.hpp>
#include <concurrentqueue/concurrentqueue.h>
#include <ctpapi/ThostFtdcMdApi.h>

class QuoteProcessor {
public:
    QuoteProcessor(const std::string *data_path, std::vector<std::string> *instruments);
    ~QuoteProcessor();
    void set_date(uint date);
    bool on_tick(CThostFtdcDepthMarketDataField *tick);
    void join();
    void stop();

private:
    std::atomic<uint> *date;
    std::vector<std::string> *instruments;
    boost::filesystem::path data_path;
    std::thread *processor;
    std::atomic<bool> *running;
    moodycamel::ConcurrentQueue<CThostFtdcDepthMarketDataField*> *buff = nullptr;

    void process();
};

#endif
