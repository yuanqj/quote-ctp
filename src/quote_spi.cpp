#include "quote_spi.hpp"

extern cmdline::parser params;
extern CThostFtdcMdApi* quoteApi;
extern char** codes;
extern int codeCount;
extern int tradingDate;
extern moodycamel::ConcurrentQueue<CThostFtdcDepthMarketDataField*> dataQueue;

void gbk2Utf8(char *src, char *dst, size_t outLen) {
    size_t inLen = strlen(src) + 1;
    iconv_t cd;
    cd = iconv_open("UTF-8", "GBK");
    if (cd != (iconv_t)-1) {
        size_t ret = iconv(cd, &src, &inLen, &dst, &outLen);
        if (ret != 0) printf("iconv failed: %s\n", strerror(errno));
        iconv_close(cd);
    }
}

std::string parseErrorMsg(TThostFtdcErrorMsgType msg) {
    int size = sizeof(TThostFtdcErrorMsgType) * 2;
    char utf8Str[size];
    gbk2Utf8(msg, utf8Str, (size_t) size);
    return std::string(utf8Str);
}

void QuoteSpi::OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    parseErrorRspInfo(pRspInfo);
}

void QuoteSpi::OnFrontConnected(){
    login();
}

void QuoteSpi::OnFrontDisconnected(int nReason) {
    std::string reason = "Unknown";
    if (nReason == 0x1001) reason = "网络读失败";
    else if (nReason == 0x1002) reason = "网络写失败";
    else if (nReason == 0x2001) reason = "接收心跳超时";
    else if (nReason == 0x2002) reason = "发送心跳失败";
    else if (nReason == 0x2003) reason = "收到错误报文";
    printf("Front disconnected: [0x%x] %s\n", nReason, reason.c_str());
}

void QuoteSpi::OnHeartBeatWarning(int nTimeLapse){
    printf("Heart beat warning: TimeLapse=%d\n", nTimeLapse);
}

void QuoteSpi::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    if (bIsLast && !parseErrorRspInfo(pRspInfo)) {
        tradingDate = atoi(pRspUserLogin->TradingDay);
        std::cout << "Login! TradingDate: " << tradingDate <<std::endl;
        this->subscribe();
    }
}

void QuoteSpi::OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    printf("Already logout!\n");
    tradingDate = 0;
}

void QuoteSpi::OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    std::cout << "Subscribe: " << pSpecificInstrument->InstrumentID << " (" << parseErrorMsg(pRspInfo->ErrorMsg) << ")" << std::endl;
}

void QuoteSpi::OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    std::cout << "Func: " << __FUNCTION__ << std::endl;
}

void QuoteSpi::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData) {
    CThostFtdcDepthMarketDataField* data = (CThostFtdcDepthMarketDataField*)memcpy(
            new u_char[sizeof(CThostFtdcDepthMarketDataField)],
            pDepthMarketData,
            sizeof(CThostFtdcDepthMarketDataField)
    );
    dataQueue.enqueue(data);
}

bool QuoteSpi::parseErrorRspInfo(CThostFtdcRspInfoField *pRspInfo)
{
    bool isErr = pRspInfo && pRspInfo->ErrorID != 0;
    if (isErr) printf("Response error: ErrorID=%d, ErrorMsg=%s\n", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    return isErr;
}


void QuoteSpi::OnRspSubForQuoteRsp(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    std::cout << "Subscribe: " << pSpecificInstrument->InstrumentID << " " << parseErrorMsg(pRspInfo->ErrorMsg) << std::endl;
}

void QuoteSpi::OnRspUnSubForQuoteRsp(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    std::cout << "Func: " << __FUNCTION__ << std::endl;
}

void QuoteSpi::OnRtnForQuoteRsp(CThostFtdcForQuoteRspField *pForQuoteRsp)
{
    std::cout << "Func: " << __FUNCTION__ << std::endl;
}

void QuoteSpi::login()
{
    CThostFtdcReqUserLoginField req;
    memset(&req, 0, sizeof(req));
    strcpy(req.BrokerID, params.get<std::string>("broker").c_str());
    strcpy(req.UserID, params.get<std::string>("investor").c_str());
    strcpy(req.Password, params.get<std::string>("password").c_str());
    int res = quoteApi->ReqUserLogin(&req, ++this->reqID);
    printf("Login %s!\n", (res==0) ? "successfully" : "failed");
}

void QuoteSpi::subscribe()
{
    int res = quoteApi->SubscribeMarketData(codes, codeCount);
    printf("Subscribe %s!\n", (res==0) ? "successfully" : "failed");
}
