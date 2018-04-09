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

void quote_spi::OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    parseErrorRspInfo(pRspInfo);
}

void quote_spi::OnFrontConnected(){
    login();
}

void quote_spi::OnFrontDisconnected(int nReason) {
    std::string reason = "Unknown";
    if (nReason == 0x1001) reason = "网络读失败";
    else if (nReason == 0x1002) reason = "网络写失败";
    else if (nReason == 0x2001) reason = "接收心跳超时";
    else if (nReason == 0x2002) reason = "发送心跳失败";
    else if (nReason == 0x2003) reason = "收到错误报文";
    printf("Front disconnected: [0x%x] %s\n", nReason, reason.c_str());
}

void quote_spi::OnHeartBeatWarning(int nTimeLapse){
    printf("Heart beat warning: TimeLapse=%d\n", nTimeLapse);
}

void quote_spi::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    if (bIsLast && !parseErrorRspInfo(pRspInfo)) {
        tradingDate = atoi(pRspUserLogin->TradingDay);
        std::cout << "Login! TradingDate: " << tradingDate <<std::endl;
        this->subscribe();
    }
}

void quote_spi::OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    printf("Already logout!\n");
    tradingDate = 0;
}

void quote_spi::OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    std::cout << "Subscribe: " << pSpecificInstrument->InstrumentID << " (" << parseErrorMsg(pRspInfo->ErrorMsg) << ")" << std::endl;
}

void quote_spi::OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    std::cout << "Func: " << __FUNCTION__ << std::endl;
}

void quote_spi::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData) {
    CThostFtdcDepthMarketDataField* data = (CThostFtdcDepthMarketDataField*)memcpy(
            new u_char[sizeof(CThostFtdcDepthMarketDataField)],
            pDepthMarketData,
            sizeof(CThostFtdcDepthMarketDataField)
    );
    dataQueue.enqueue(data);
}

bool quote_spi::parseErrorRspInfo(CThostFtdcRspInfoField *pRspInfo)
{
    bool isErr = pRspInfo && pRspInfo->ErrorID != 0;
    if (isErr) printf("Response error: ErrorID=%d, ErrorMsg=%s\n", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    return isErr;
}


void quote_spi::OnRspSubForQuoteRsp(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    std::cout << "Subscribe: " << pSpecificInstrument->InstrumentID << " " << parseErrorMsg(pRspInfo->ErrorMsg) << std::endl;
}

void quote_spi::OnRspUnSubForQuoteRsp(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    std::cout << "Func: " << __FUNCTION__ << std::endl;
}

void quote_spi::OnRtnForQuoteRsp(CThostFtdcForQuoteRspField *pForQuoteRsp)
{
    std::cout << "Func: " << __FUNCTION__ << std::endl;
}

void quote_spi::login()
{
    CThostFtdcReqUserLoginField req;
    memset(&req, 0, sizeof(req));
    strcpy(req.BrokerID, params.get<std::string>("broker").c_str());
    strcpy(req.UserID, params.get<std::string>("investor").c_str());
    strcpy(req.Password, params.get<std::string>("password").c_str());
    int res = quoteApi->ReqUserLogin(&req, ++this->reqID);
    printf("Login %s!\n", (res==0) ? "successfully" : "failed");
}

void quote_spi::subscribe()
{
    int res = quoteApi->SubscribeMarketData(codes, codeCount);
    printf("Subscribe %s!\n", (res==0) ? "successfully" : "failed");
}
