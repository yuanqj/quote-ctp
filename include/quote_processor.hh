#ifndef __QUOTE_PROCESSOR_HH__
#define __QUOTE_PROCESSOR_HH__

#include <string>
#include <thread>
#include <boost/filesystem.hpp>
#include <concurrentqueue/concurrentqueue.h>
#include <ctpapi/ThostFtdcMdApi.h>

class QuoteProcessor {
public:
    QuoteProcessor(const std::string *data_path);
    ~QuoteProcessor();
    void set_date(int date);
    bool on_tick(CThostFtdcDepthMarketDataField *tick);
    void wait();
    void stop();

private:
    int date;
    boost::filesystem::path data_path;
    std::thread *processor;
    std::atomic<bool> *running;
    moodycamel::ConcurrentQueue<CThostFtdcDepthMarketDataField*> *buff;

    void process();
};

#endif
