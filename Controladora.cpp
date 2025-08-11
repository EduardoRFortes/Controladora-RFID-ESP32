#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "mqtt/async_client.h"

using namespace std;
using namespace std::chrono_literals;

// ===================== CONFIGURAÇÕES GLOBAIS ======================

const string DFLT_SERVER_URI("mqtts://mosquittoserver.lan:8883");
const string CLIENT_ID("paho_cpp_async_subscribe");

const string TOPIC("#");

const int QOS = 1;
const int N_RETRY_ATTEMPTS = 5;

// Caminhos dos certificados TLS
const std::string PATH_TO_CA_CERT = "/etc/mosquitto/certificate/ca_chain.crt";
const std::string PATH_TO_CLIENT_CERT = "/etc/mosquitto/certificate/nodes/tarnode1.lan.crt.pem";
const std::string PATH_TO_CLIENT_KEY = "/etc/mosquitto/certificate/nodes/tarnode1.lan.key";

/////////////////////////////////////////////////////////////////////////////

// Callbacks para o sucesso ou falha das ações solicitadas.
class action_listener : public virtual mqtt::iaction_listener
{
    std::string name_;

    void on_failure(const mqtt::token& tok) override
    {
        std::cout << name_ << " falha";
        if (tok.get_message_id() != 0)
            std::cout << " para token: [" << tok.get_message_id() << "]" << std::endl;
        std::cout << std::endl;
    }

    void on_success(const mqtt::token& tok) override
    {
        std::cout << name_ << " sucesso";
        if (tok.get_message_id() != 0)
            std::cout << " para token: [" << tok.get_message_id() << "]" << std::endl;
        auto top = tok.get_topics();
        if (top && !top->empty())
            std::cout << "\ttoken topico: '" << (*top)[0] << "', ..." << std::endl;
        std::cout << std::endl;
    }

public:
    action_listener(const std::string& name) : name_(name) {}
};

/////////////////////////////////////////////////////////////////////////////

/**
 * Classe de callback e listener local para uso com a conexão do cliente.
 */
class callback : public virtual mqtt::callback, public virtual mqtt::iaction_listener
{
    int nretry_;
    mqtt::async_client& cli_;
    mqtt::connect_options& connOpts_;
    action_listener subListener_;

    void reconnect()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        try {
            cli_.connect(connOpts_, nullptr, *this);
        }
        catch (const mqtt::exception& exc) {
            std::cerr << "Erro na reconexão: " << exc.what() << std::endl;
            exit(1);
        }
    }

    void on_failure(const mqtt::token& tok) override
    {
        std::cout << "Tentativa de conexão falhou" << std::endl;
        if (++nretry_ > N_RETRY_ATTEMPTS)
            exit(1);
        reconnect();
    }

    void on_success(const mqtt::token& tok) override {}

    void connected(const std::string& cause) override
    {
        std::cout << "\nConexão bem-sucedida" << std::endl;
        std::cout << "\nAssinando o tópico '" << TOPIC << "'\n"
                  << "\tpara o cliente " << CLIENT_ID << " usando QoS" << QOS << "\n"
                  << "\nPressione Q<Enter> para sair\n"
                  << std::endl;

        cli_.subscribe(TOPIC, QOS, nullptr, subListener_);
    }

    void connection_lost(const std::string& cause) override
    {
        std::cout << "\nConexão perdida" << std::endl;
        if (!cause.empty())
            std::cout << "\tcausa: " << cause << std::endl;

        std::cout << "Reconectando..." << std::endl;
        nretry_ = 0;
        reconnect();
    }

    void message_arrived(mqtt::const_message_ptr msg) override
    {
        std::cout << "Mensagem recebida" << std::endl;
        std::cout << "\ttopico: '" << msg->get_topic() << "'" << std::endl;
        std::cout << "\tpayload: '" << msg->to_string() << "'\n" << std::endl;
    }

    void delivery_complete(mqtt::delivery_token_ptr token) override {}

public:
    callback(mqtt::async_client& cli, mqtt::connect_options& connOpts)
        : nretry_(0), cli_(cli), connOpts_(connOpts), subListener_("Subscription")
    {
    }
};

/////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    auto serverURI = (argc > 1) ? std::string{argv[1]} : DFLT_SERVER_URI;

    mqtt::async_client cli(serverURI, CLIENT_ID);

    // Crie as opções de SSL com os métodos corretos
    mqtt::ssl_options ssl_options;
    ssl_options.set_trust_store(PATH_TO_CA_CERT);
    ssl_options.set_key_store(PATH_TO_CLIENT_CERT);
    ssl_options.set_private_key(PATH_TO_CLIENT_KEY);
    
    // Desabilita a verificação de hostname e define o protocolo TLS
    ssl_options.set_enable_server_cert_auth(false);
    ssl_options.set_ssl_version(3); // <-- CORREÇÃO: Valor 3 para TLSv1.2

    // Crie as opções de conexão e anexe as opções de SSL
    mqtt::connect_options connOpts;
    connOpts.set_clean_session(false);
    connOpts.set_ssl(ssl_options);

    callback cb(cli, connOpts);
    cli.set_callback(cb);

    try {
        std::cout << "Conectando ao servidor MQTT '" << serverURI << "'..." << std::flush;
        cli.connect(connOpts, nullptr, cb);
    }
    catch (const mqtt::exception& exc) {
        std::cerr << "\nERRO: Não foi possível conectar ao servidor MQTT: '" << serverURI << "'" << exc
                  << std::endl;
        return 1;
    }

    while (std::tolower(std::cin.get()) != 'q');

    try {
        std::cout << "\nDesconectando do servidor MQTT..." << std::flush;
        cli.disconnect()->wait();
        std::cout << "OK" << std::endl;
    }
    catch (const mqtt::exception& exc) {
        std::cerr << exc << std::endl;
        return 1;
    }

    return 0;
}