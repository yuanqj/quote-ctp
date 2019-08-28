#include "quote_client.hh"
#include <iconv/iconv.h>
#include <boost/filesystem.hpp>

void gbk2utf8(char *src, char *dst, size_t len_out) {
    size_t len_in = strlen(src) + 1;
    iconv_t cd;
    cd = iconv_open("UTF-8", "GBK");
    if (cd != (iconv_t)-1) {
        size_t ret = iconv(cd, &src, &len_in, &dst, &len_out);
        if (ret != 0) std::cout << "iconv failed: " << strerror(errno) << std::endl;
        iconv_close(cd);
    }
}

std::string parse_err_msg(TThostFtdcErrorMsgType msg) {
    int size = sizeof(TThostFtdcErrorMsgType) * 5;
    char utf8_str[size];
    gbk2utf8(msg, utf8_str, (size_t)size);
    return std::string(utf8_str);
}

bool show_error(CThostFtdcRspInfoField *pRspInfo) {
    bool is_err = pRspInfo != nullptr && pRspInfo->ErrorID != 0;
    if (is_err) std::cout << "ERROR: [" << pRspInfo->ErrorID << "]" << parse_err_msg(pRspInfo->ErrorMsg) << std::endl;
    return is_err;
}


QuoteClient::QuoteClient(
        const std::string *uuid,
        const std::string *broker,
        const std::string *investor,
        const std::string *password,
        const std::string *front_addr,
        std::vector<std::string> *instruments,
        const std::string *path_conn,
        const std::string *path_data
) {
    std::cout << std::endl << "CTPAPI Version: \"" << CThostFtdcMdApi::GetApiVersion() << "\"" << std::endl << std::endl;
    this->uuid = uuid;
    this->broker = broker;
    this->investor = investor;
    this->password = password;
    this->front_addr = front_addr;
    this->instruments = instruments;
    this->conn_path = path_conn;
    this->processor = new QuoteProcessor(path_data);

    boost::filesystem::path conn_path(*this->conn_path), conn_prefix(*uuid + "_");
    this->ctp_api = CThostFtdcMdApi::CreateFtdcMdApi((conn_path/conn_prefix).c_str());
}

QuoteClient::~QuoteClient() {
    delete(this->processor);
    boost::filesystem::path conn_path(*this->conn_path);
    boost::filesystem::remove(conn_path/boost::filesystem::path(*this->uuid + "_DialogRsp.con"));
    boost::filesystem::remove(conn_path/boost::filesystem::path(*this->uuid + "_QueryRsp.con"));
    boost::filesystem::remove(conn_path/boost::filesystem::path(*this->uuid + "_TradingDay.con"));
}

void QuoteClient::run() {
    if (this->ctp_api == nullptr) return;
    this->ctp_api->RegisterSpi((CThostFtdcMdSpi*)this);
    char front_addr[this->front_addr->length() + 1];
    strcpy(front_addr, this->front_addr->c_str());
    this->ctp_api->RegisterFront(front_addr);
    this->ctp_api->Init();
    this->processor->join();
}

void QuoteClient::stop() {
    this->logout();
    this->ctp_api->Release();
    this->processor->stop();
}

void QuoteClient::OnFrontConnected(){
    std::cout << "OnFrontConnected" << std::endl;
    login();
}

void QuoteClient::OnFrontDisconnected(int nReason) {
    std::string reason = "Unknown";
    if (nReason == 0x1001) reason = "网络读失败";
    else if (nReason == 0x1002) reason = "网络写失败";
    else if (nReason == 0x2001) reason = "接收心跳超时";
    else if (nReason == 0x2002) reason = "发送心跳失败";
    else if (nReason == 0x2003) reason = "收到错误报文";
    std::cout << "OnFrontDisconnected: [0x" << nReason << "] " << reason <<std::endl;
}

void QuoteClient::OnHeartBeatWarning(int nTimeLapse){
    std::cout << "OnHeartBeatWarning: TimeLapse=" << nTimeLapse <<std::endl;
}

void QuoteClient::OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    show_error(pRspInfo);
}

void QuoteClient::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    if (bIsLast && !show_error(pRspInfo)) {
        auto date = (uint)strtol(pRspUserLogin->TradingDay, nullptr, 10);
        std::cout << "OnRspUserLogin" << std::endl;
        std::cout <<std::endl << "************* TradingDate: " << date <<std::endl <<std::endl;
        this->processor->set_date(date);
        this->subscribe();
    }
}

void QuoteClient::OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    std::cout << "OnRspUserLogout" << std::endl;
    this->processor->set_date(0);
}

void QuoteClient::OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    std::cout << ">>>>>>>>>>>>> Subscribe: " << pSpecificInstrument->InstrumentID << std::endl;
    show_error(pRspInfo);
    if (bIsLast) std::cout << std::endl;
}

void QuoteClient::OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    std::cout << ">>>>>>>>>>>>> Unsubscribe: " << pSpecificInstrument->InstrumentID << std::endl;
    show_error(pRspInfo);
}

void QuoteClient::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData) {
    auto tick = (CThostFtdcDepthMarketDataField*)memcpy(
            new u_char[sizeof(CThostFtdcDepthMarketDataField)],
            pDepthMarketData,
            sizeof(CThostFtdcDepthMarketDataField)
    );
    this->processor->on_tick(tick);
}

void QuoteClient::OnRspSubForQuoteRsp(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    std::cout << "FUNC: " << __FUNCTION__ << std::endl;
}

void QuoteClient::OnRspUnSubForQuoteRsp(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    std::cout << "FUNC: " << __FUNCTION__ << std::endl;
}

void QuoteClient::OnRtnForQuoteRsp(CThostFtdcForQuoteRspField *pForQuoteRsp) {
    std::cout << "FUNC: " << __FUNCTION__ << std::endl;
}

void QuoteClient::login() {
    CThostFtdcReqUserLoginField req{0};
    strcpy(req.BrokerID, this->broker->c_str());
    strcpy(req.UserID, this->investor->c_str());
    strcpy(req.Password, this->password->c_str());
    ctp_api->ReqUserLogin(&req, ++this->req_id);
}

void QuoteClient::logout() {
    CThostFtdcUserLogoutField req{0};
    strcpy(req.BrokerID, this->broker->c_str());
    strcpy(req.UserID, this->investor->c_str());
    ctp_api->ReqUserLogout(&req, ++this->req_id);
}

void QuoteClient::subscribe() {
    auto count = (int)this->instruments->size();
    auto instruments = new char*[count];
    for(size_t i=0; i<count; i++){
        instruments[i]=new char[this->instruments->at(i).length()+1];
        strcpy(instruments[i], this->instruments->at(i).c_str());
    }
    this->ctp_api->SubscribeMarketData(instruments, count);
}
