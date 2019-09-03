#include <csignal>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <cmdline/cmdline.h>
#include "quote_client.hh"

std::string *gen_uuid();
cmdline::parser* config_cli(int argc, char** argv);
std::string get_password();
std::vector<std::string> *parse_instruments(const std::string *str, char sep);
void term_sig_handler(int signum);

QuoteClient *client;


int main(int argc, char** argv) {
    cmdline::parser *params = config_cli(argc, argv);
    std::string *uuid = gen_uuid();
    std::cout << "Quote-CTP Starts: " << *uuid << std::endl;

    auto instruments = parse_instruments(&params->get<std::string>("instruments"), ';');
    auto pwd = get_password();
    client = new QuoteClient(
            uuid,
            &params->get<std::string>("broker"),
            &params->get<std::string>("investor"),
            &pwd,
            &params->get<std::string>("front-addr"),
            instruments,
            &params->get<std::string>("path-conn"),
            &params->get<std::string>("path-data")
    );
    signal(SIGINT, term_sig_handler);
    signal(SIGTERM, term_sig_handler);
    client->run();

    std::cout << "Quote-CTP Ends: " << *uuid << std::endl;
    delete(client);
    delete(instruments);
    delete(uuid);
    delete(params);
    return 0;
}

cmdline::parser *config_cli(int argc, char** argv) {
    auto *params = new cmdline::parser();
    params->add<std::string>("broker", 0, "Broker ID", true, "");
    params->add<std::string>("investor", 0, "Investor ID", true, "");
    params->add<std::string>("front-addr", 0, "Front server address", true, "");
    params->add<std::string>("instruments", 0, "Instruments to subscribe which separated with ';'", true, "");
    params->add<std::string>("path-conn", 0, "File path for storing connection flow", true, "");
    params->add<std::string>("path-data", 0, "File path for storing tick data", true, "");
    params->parse_check(argc, argv);
    return params;
}

void term_sig_handler(int signum) {
    std::cout << "Termination signal received ["<< signum <<"]." << std::endl;
    client->stop();
}

std::string get_password() {
    std::cout << "Input password for CTP account: ";
    termios t0 = {0};
    tcgetattr(STDIN_FILENO, &t0);
    termios t1 = t0;
    t1.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &t1);

    std::string pwd;
    getline(std::cin, pwd);
    std::cout << std::endl;
    return pwd;
}

std::vector<std::string> *parse_instruments(const std::string *str, char sep) {
    std::stringstream ss;
    ss.str(*str);

    std::string item;
    auto *instruments = new std::vector<std::string>();
    while (std::getline(ss, item, sep)) {
        *(std::back_inserter(*instruments)++) = item;
    }
    return instruments;
}

std::string *gen_uuid() {
    uuid_t uuid = {0};
    uuid_generate_time_safe(uuid);
    auto *uuid_str=new char[37];
    uuid_unparse_lower(uuid, uuid_str);
    return new std::string(uuid_str);
}
