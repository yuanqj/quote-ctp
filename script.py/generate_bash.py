import os
import time
import json
import os.path as op
import getpass
import argparse
from pyctp import ApiStruct, TraderApi


class TradeClient(TraderApi):
    def __init__(self, broker, investor, password, app_id, auth_code, front):
        self._broker = bytes(broker, encoding='gb2312')
        self._investor = bytes(investor, encoding='gb2312')
        self._password = bytes(password, encoding='gb2312')
        self._app_id = bytes(app_id, encoding='gb2312')
        self._auth_code = bytes(auth_code, encoding='gb2312')
        self._front_id, self._session_id = None, None
        self._req_id, self._order_ref, self._ready = 0, 0, False
        self._instruments_fut_financial, self._instruments_fut_goods, self._instruments_opt_goods = [], [], []
        self.Create(bytes('./tmp/', encoding='gb2312'))
        self.SubscribePublicTopic(2)
        self.SubscribePrivateTopic(2)
        self.RegisterFront(bytes(front, encoding='gb2312'))
        self.Init()
        print('CTP API Version: "{}"'.format(self.GetApiVersion().decode(encoding='gb2312')))
        for i in range(3):
            time.sleep(1)
            if self.ready:
                break
            print('Waiting for CTP client connect...')
        else:
            print('*************** CTP client connect failed...')
            return
        self._instruments = {
            'FutFinancial': self._instruments_fut_financial,
            'FutGoods': self._instruments_fut_goods,
            'OptGoods': self._instruments_opt_goods,
        }

    def export(self):
        print()
        print('FutFinancial: ', len(self._instruments_fut_financial))
        print('FutGoods: ', len(self._instruments_fut_goods))
        print('OptGoods: ', len(self._instruments_opt_goods))
        with open(op.join(op.dirname(op.abspath(__file__)), 'instruments.json'), 'w') as f:
            f.write(json.dumps(self._instruments, indent=2, ensure_ascii=False))

    @property
    def req_id(self):
        self._req_id += 1
        return self._req_id

    @property
    def order_ref(self):
        self._order_ref += 1
        return '{:012d}'.format(self._order_ref)

    @property
    def ready(self):
        return self._ready

    def OnFrontConnected(self):
        print('*************** OnFrontConnected')
        self.ReqAuthenticate(
            ApiStruct.ReqAuthenticate(BrokerID=self._broker, UserID=self._investor, AppID=self._app_id, AuthCode=self._auth_code),
            self.req_id,
        )

    def OnFrontDisconnected(self, nReason):
        reasons = {
            0x1001: '网络读失败',
            0x1002: '网络写失败',
            0x2001: '接收心跳超时',
            0x2002: '发送心跳失败',
            0x2003: '收到错误报文',
        }
        print('*************** OnFrontDisconnected[{:0x}] {}'.format(nReason, reasons[nReason]))

    def OnRspAuthenticate(self, pRspAuthenticate, pRspInfo, nRequestID, bIsLast):
        if pRspInfo is not None and pRspInfo.ErrorID > 0:
            print('*************** OnRspAuthenticate ERROR: {}'.format(pRspInfo.ErrorMsg.decode(encoding='gb2312')))
            return
        print('*************** OnRspAuthenticate')
        self.ReqUserLogin(
            ApiStruct.ReqUserLogin(BrokerID=self._broker, UserID=self._investor, Password=self._password),
            self.req_id,
        )

    def OnRspUserLogin(self, pRspUserLogin, pRspInfo, nRequestID, bIsLast):
        if pRspInfo.ErrorID > 0:
            print('*************** OnRspUserLogin ERROR: {}'.format(pRspInfo.ErrorMsg.decode(encoding='gb2312')))
        else:
            print('*************** OnRspUserLogin')
            self._front_id, self._session_id = pRspUserLogin.FrontID, pRspUserLogin.SessionID
            if pRspUserLogin.MaxOrderRef:
                self._order_ref = int(pRspUserLogin.MaxOrderRef.decode(encoding='gb2312'))
            self.ReqSettlementInfoConfirm(
                ApiStruct.SettlementInfoConfirm(BrokerID=self._broker, InvestorID=self._investor),
                self.req_id,
            )

    def OnRspUserLogout(self, pUserLogout, pRspInfo, nRequestID, bIsLast):
        print('*************** OnRspUserLogout')

    def OnRspSettlementInfoConfirm(self, pSettlementInfoConfirm, pRspInfo, nRequestID, bIsLast):
        if pRspInfo.ErrorID > 0:
            print('*************** OnRspSettlementInfoConfirm ERROR: {}'.format(pRspInfo.ErrorMsg.decode(encoding='gb2312')))
        else:
            print('*************** OnRspSettlementInfoConfirm')
            self.ReqQryInstrument(
                ApiStruct.QryInstrument(),
                self.req_id,
            )
    
    def OnRspQryInstrument(self, pInstrument, pRspInfo, nRequestID, bIsLast):
        if pRspInfo and pRspInfo.ErrorID > 0:
            print('*************** OnRspQryInstrument ERROR: {}'.format(pRspInfo.ErrorMsg.decode(encoding='gb2312')))
            return
        cls, ex = int(pInstrument.ProductClass), pInstrument.ExchangeID.decode(encoding='gb2312')
        inst = pInstrument.InstrumentID.decode(encoding='gb2312')
        if cls == 1:
            if ex == 'CFFEX':
                self._instruments_fut_financial.append(inst)
            else:
                self._instruments_fut_goods.append(inst)
        elif cls == 2:
            self._instruments_opt_goods.append(inst)
        self._ready = bIsLast


_cmd_temp = '''
{{ echo $password | ./quote-ctp \\
    --broker={broker} \\
    --investor={investor} \\
    --front-addr='{front}' \\
    --path-conn='{conn}' \\
    --path-data='{data}' \\
    --instruments='{instruments}'
}} &
'''
_pwd_temp = '''
echo -n *************PASSWORD for CTP account:
read -s password
echo ''
'''


def gen_bash(broker, investor, front):
    import random
    with open('instruments.json', encoding='utf-8') as f:
        instruments = json.load(f)
    cmds = []
    path_conn = op.join(op.dirname(op.abspath(__file__)), 'tmp')
    classes = ['FutFinancial', 'FutGoods', 'OptGoods']
    batches = [30, 30, 300]
    for i, cls in enumerate(classes):
        _instruments = instruments[cls]
        path_data = op.join(op.dirname(op.abspath(__file__)), 'quote/{}'.format(cls))
        random.shuffle(_instruments)
        groups = [_instruments[j:j + batches[i]] for j in range(0, len(_instruments), batches[i])]
        for group in groups:
            cmd = _cmd_temp.format(
                broker=broker,
                investor=investor,
                front=front,
                conn=path_conn,
                data=path_data,
                cls=cls,
                instruments=';'.join(group),
            )
            cmds.append(cmd.strip())
    print('\nQuoteProcess#: {}\n'.format(len(cmds)))
    cmds = '\n\n'.join(cmds)

    comment = '### This file is auto generated.'
    cmds = '\n\n'.join([comment, _pwd_temp, cmds, '\nwait'])
    with open('quote-ctp.sh', 'w') as f:
        f.write(cmds)


def parse_cli():
    parser = argparse.ArgumentParser(description='Instrument List')
    parser.add_argument('--broker', type=str, required=True, help='BrokerID')
    parser.add_argument('--investor', type=str, required=True, help='InvestorID')
    parser.add_argument('--app-id', type=str, required=True, help='APP ID')
    parser.add_argument('--auth-code', type=str, required=True, help='Authentication Code')
    parser.add_argument('--front-addr-trade', type=str, required=True, help='Trade Front Address')
    parser.add_argument('--front-addr-quote', type=str, required=True, help='Quote Front Address')
    return parser.parse_args()


def main():
    os.makedirs('./tmp', exist_ok=True)
    os.makedirs('./quote', exist_ok=True)
    args = parse_cli()
    password = getpass.getpass()
    client = TradeClient(
        args.broker,
        args.investor,
        password,
        args.app_id,
        args.auth_code,
        args.front_addr_trade,
    )
    if not client.ready:
        return
    client.export()
    gen_bash(
        args.broker,
        args.investor,
        args.front_addr_quote,
    )


if __name__ == '__main__':
    main()
