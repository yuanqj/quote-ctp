#include <csignal>
#include <iostream>
#include <uuid/uuid.h>
#include <cmdline/cmdline.h>
#include "quote_client.hh"

std::string *gen_uuid();
cmdline::parser* config_cli(int argc, char** argv);
std::vector<std::string> *parse_instruments(const std::string *str, char sep);
void term_sig_handler(int signum);

QuoteClient *client;


int main(int argc, char** argv) {
    cmdline::parser *params = config_cli(argc, argv);
    std::string *uuid = gen_uuid();
    std::cout << std::endl << "Quote-CTP Starts: " << *uuid << std::endl;
    signal(SIGINT, term_sig_handler);
    signal(SIGTERM, term_sig_handler);

    auto instruments = parse_instruments(&params->get<std::string>("instruments"), ';');
    client = new QuoteClient(
            uuid,
            &params->get<std::string>("broker"),
            &params->get<std::string>("investor"),
            &params->get<std::string>("password"),
            &params->get<std::string>("front-addr"),
            instruments,
            &params->get<std::string>("path-conn"),
            &params->get<std::string>("path-data")
    );
    client->run();

    std::cout << std::endl << "Quote-CTP Ends: " << *uuid << std::endl << std::endl;
    delete(client);
    delete(params);
    delete(instruments);
    delete(uuid);
    return 0;
}

cmdline::parser *config_cli(int argc, char** argv) {
    auto *params = new cmdline::parser();
    params->add<std::string>("broker", 0, "Broker ID", true, "");
    params->add<std::string>("investor", 0, "Investor ID", true, "");
    params->add<std::string>("password", 0, "Investor password", true, "");
    params->add<std::string>("front-addr", 0, "Front server address", true, "");
    params->add<std::string>("instruments", 0, "Instruments to subscribe which separated with ';'", true, "");
    params->add<std::string>("path-conn", 0, "File path for storing connection flow", true, "");
    params->add<std::string>("path-data", 0, "File path for storing tick data", true, "");
    params->parse_check(argc, argv);
    return params;
}

void term_sig_handler(int signum) {
    std::cout << std::endl << "Termination signal received ["<< signum <<"]." << std::endl;
    client->stop();
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
