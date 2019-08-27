#include "quote_client.hh"
#include <iconv/iconv.h>

void gbk2utf8(char *src, char *dst, size_t outLen) {
    size_t inLen = strlen(src) + 1;
    iconv_t cd;
    cd = iconv_open("UTF-8", "GBK");
    if (cd != (iconv_t)-1) {
        size_t ret = iconv(cd, &src, &inLen, &dst, &outLen);
        if (ret != 0) printf("iconv failed: %s\n", strerror(errno));
        iconv_close(cd);
    }
}

std::string parse_err_msg(TThostFtdcErrorMsgType msg) {
    int size = sizeof(TThostFtdcErrorMsgType) * 3;
    char utf8_str[size];
    gbk2utf8(msg, utf8_str, (size_t) size);
    return std::string(utf8_str);
}

QuoteClient::QuoteClient(
        const std::string *broker,
        const std::string *investor,
        const std::string *password,
        const std::string *front_addr,
        std::vector<std::string> *instruments,
        const std::string *path_conn,
        const std::string *path_data
) {
    this->broker = broker;
    this->investor = investor;
    this->password = password;
    this->front_addr = front_addr;
    this->instruments = instruments;
    this->processor = new QuoteProcessor(path_data);
    this->ctp_api = CThostFtdcMdApi::CreateFtdcMdApi(path_conn->c_str());
}

QuoteClient::~QuoteClient() {
    delete(this->processor);
}

void QuoteClient::init() {
    this->ctp_api->RegisterSpi((CThostFtdcMdSpi*)this);
    char front_addr[this->front_addr->length() + 1];
    strcpy(front_addr, this->front_addr->c_str());
    this->ctp_api->RegisterFront(front_addr);
    this->ctp_api->Init();
}

void QuoteClient::wait() {
    this->processor->wait();
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
    printf("Front disconnected: [0x%x] %s\n", nReason, reason.c_str());
}

void QuoteClient::OnHeartBeatWarning(int nTimeLapse){
    printf("Heart beat warning: TimeLapse=%d\n", nTimeLapse);
}

void QuoteClient::OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    show_error(pRspInfo);
}

void QuoteClient::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    if (bIsLast && !show_error(pRspInfo)) {
        auto date = (int)strtol(pRspUserLogin->TradingDay, nullptr, 10);
        std::cout << "OnRspUserLogin: " << date <<std::endl;
        this->processor->set_date(date);
        this->subscribe();
    }
}

void QuoteClient::OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    printf("Already logout!\n");
    this->processor->set_date(0);
}

void QuoteClient::OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    std::cout << "Subscribe: " << pSpecificInstrument->InstrumentID << " (" << parse_err_msg(pRspInfo->ErrorMsg) << ")" << std::endl;
}

void QuoteClient::OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    std::cout << "OnRspUnSubMarketData: " << pSpecificInstrument->InstrumentID << " (" << parse_err_msg(pRspInfo->ErrorMsg) << ")" << std::endl;
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
    std::cout << "Subscribe: " << pSpecificInstrument->InstrumentID << " " << parse_err_msg(pRspInfo->ErrorMsg) << std::endl;
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

bool QuoteClient::show_error(CThostFtdcRspInfoField *pRspInfo) {
    bool isErr = pRspInfo && pRspInfo->ErrorID != 0;
    if (isErr) printf("Response error: ErrorID=%d, ErrorMsg=%s\n", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    return isErr;
}
