#include "quote_processor.hh"

QuoteProcessor::QuoteProcessor(const std::string *data_path) {
    this->data_path = boost::filesystem::path(*data_path);
    this->running = new std::atomic<bool>(true);
    this->processor = new std::thread(&QuoteProcessor::process, this);
    this->buff = new moodycamel::ConcurrentQueue<CThostFtdcDepthMarketDataField*>(1024);
}

QuoteProcessor::~QuoteProcessor() {
    this->stop();
    delete(this->processor);
    CThostFtdcDepthMarketDataField* tick;
    while (this->buff->try_dequeue(tick)) delete(tick);
    delete(this->buff);
    delete(this->running);
}

void QuoteProcessor::set_date(int date) {
    if (date == this->date) return;
    this->date = date;
    if (this->date == 0) return;
    boost::filesystem::path data_dir(std::to_string(this->date));
    boost::filesystem::create_directory(this->data_path/data_dir);
}

void QuoteProcessor::wait() {
    this->processor->join();
}

void QuoteProcessor::stop() {
    this->running->store(false, std::memory_order_acquire);
}

bool QuoteProcessor::on_tick(CThostFtdcDepthMarketDataField *tick) {
    if (!this->running->load(std::memory_order_acquire) || this->date <= 0) return false;
    this->buff->enqueue(tick);
    return true;
}

void QuoteProcessor::process() {
    CThostFtdcDepthMarketDataField* tick;
    while (this->running->load(std::memory_order_acquire)) {
        if (this->buff->try_dequeue(tick)) {
            printf("TICK: Code=%s, UpdateTime=%s\n", tick->InstrumentID, tick->UpdateTime);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}