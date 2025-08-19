#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include "mqtt/async_client.h"

// ===================== USINGS ======================
using namespace std;
using namespace std::chrono_literals;
using json = nlohmann::json;

// ===================== CONFIGURAÇÕES GLOBAIS ======================
const string DFLT_SERVER_URI("mqtts://172.22.48.50:8883");
const string TOPIC("#");
const int QOS = 1;
const int N_RETRY_ATTEMPTS = 5;

// Caminhos dos certificados TLS
const std::string PATH_TO_CA_CERT = "/etc/mosquitto/meus_certificados/ca_chain.crt";
const std::string PATH_TO_CLIENT_CERT = "/etc/mosquitto/meus_certificados/nodes/tarnode1.2.lan.crt.pem";
const std::string PATH_TO_CLIENT_KEY = "/etc/mosquitto/meus_certificados/nodes/tarnode1.2.lan.key";

// Chave da API de Patrimônio
const std::string API_KEY = "b2109VWV.rxo3dB2x9rRRl2sbDGEHw7zdNI8RmFG9";
const std::string API_URL_BASE = "http://200.18.74.24/api/patrimonio/";

// ===================== ESTRUTURA PARA O TRABALHO DA WORKER THREAD ======================
struct ReadingJob {
    std::string epc;
    long timestamp;
    std::string mqttId;
};

// ===================== FUNÇÕES AUXILIARES HTTP ======================
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string getApiData(const std::string& url, const std::string& apiKey) {
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        struct curl_slist *headers = NULL;
        std::string authHeader = "Authorization: Api-Key " + apiKey;
        headers = curl_slist_append(headers, authHeader.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            std::cerr << "curl_easy_perform() falhou: " << curl_easy_strerror(res) << std::endl;
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    curl_global_cleanup();
    return readBuffer;
}

// Coloque esta função perto da getApiData
std::string postApiData(const std::string& url, const std::string& apiKey, const std::string& postData) {
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl) {
        // Headers para autenticação e tipo de conteúdo
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string authHeader = "Authorization: Api-Key " + apiKey;
        headers = curl_slist_append(headers, authHeader.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Configurações para o POST
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)postData.length());

        // Callback para a resposta
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            std::cerr << "curl_easy_perform() [POST] falhou: " << curl_easy_strerror(res) << std::endl;
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    curl_global_cleanup();
    return readBuffer;
}

// ===================== FILA SEGURA PARA TRABALHOS (THREAD-SAFE QUEUE) ======================
class JobQueue {
public:
    void push(const ReadingJob& job) {
        std::unique_lock<std::mutex> lock(mtx_);
        jobs_.push(job);
        lock.unlock();
        cond_.notify_one();
    }

    bool pop(ReadingJob& job) {
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock, [this] { return !jobs_.empty() || stop_; });

        if (stop_ && jobs_.empty()) {
            return false;
        }

        job = jobs_.front();
        jobs_.pop();
        return true;
    }

    void stop() {
        std::unique_lock<std::mutex> lock(mtx_);
        stop_ = true;
        lock.unlock();
        cond_.notify_all();
    }

private:
    std::queue<ReadingJob> jobs_;
    std::mutex mtx_;
    std::condition_variable cond_;
    std::atomic<bool> stop_{false};
};

// ===================== BANCO DE DADOS ======================
const char* DB_FILE = "Farrapo.db";
sqlite3* db;

static int sqlite_callback(void* data, int argc, char** argv, char** azColName){
    return 0;
}

class DatabaseHandler {
public:
    bool openConnection(){
        int rc = sqlite3_open(DB_FILE, &db);
        if(rc){
            std::cerr << "Erro ao abrir banco de dados: " << sqlite3_errmsg(db) << std::endl;
            return false;
        } else {
            std::cout << "Banco de dados aberto com sucesso!" << std::endl;
            return true;
        }
    }

    // DENTRO DA CLASSE DatabaseHandler
    bool createTable(){
        std::string sql = "CREATE TABLE IF NOT EXISTS leituras("
                        "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
                        "EPC TEXT NOT NULL,"
                        "TIMESTAMP TEXT NOT NULL,"
                        "MQTTID TEXT NOT NULL,"
                        "DESCRICAO_BEM TEXT,"
                        "NUMERO_REGISTRO TEXT,"
                        // --- NOVAS COLUNAS ---
                        "LOCAL_DETECCAO TEXT,"
                        "ALERTA_ENVIADO INTEGER,"  // Usamos INTEGER (0=false, 1=true)
                        "AUTORIZACAO_ATIVA INTEGER);"; // Usamos INTEGER (0=false, 1=true)
        
        // ... o resto da função continua igual ...
        char* errMsg = nullptr;
        int rc = sqlite3_exec(db, sql.c_str(), sqlite_callback, 0, &errMsg);
        if(rc != SQLITE_OK){
            std::cerr << "Erro ao criar tabela: " << errMsg << std::endl;
            sqlite3_free(errMsg);
            return false;
        } else {
            std::cout << "Tabela criada com sucesso!" << std::endl;
            return true;
        }
    }

    // DENTRO DA CLASSE DatabaseHandler
    bool insertReading(const std::string& epc, const std::string& timestamp, const std::string& mqttId,
                    const std::string& descricao, const std::string& numeroRegistro,
                    // --- NOVOS PARÂMETROS ---
                    const std::string& localDeteccao, bool alertaEnviado, bool autorizacaoAtiva) {
        
        const char* sql = "INSERT INTO leituras (EPC, TIMESTAMP, MQTTID, DESCRICAO_BEM, NUMERO_REGISTRO, "
                        "LOCAL_DETECCAO, ALERTA_ENVIADO, AUTORIZACAO_ATIVA) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;

        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "Erro ao preparar insert: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        
        sqlite3_bind_text(stmt, 1, epc.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, timestamp.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, mqttId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, descricao.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, numeroRegistro.c_str(), -1, SQLITE_STATIC);
        // --- BIND DOS NOVOS VALORES ---
        sqlite3_bind_text(stmt, 6, localDeteccao.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 7, alertaEnviado ? 1 : 0);
        sqlite3_bind_int(stmt, 8, autorizacaoAtiva ? 1 : 0);


        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::cerr << "Erro ao executar insert: " << sqlite3_errmsg(db) << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }

        sqlite3_finalize(stmt);
        std::cout << "Registro de detecção inserido com sucesso para EPC: " << epc << std::endl;
        return true;
    }

    void closeConnection(){
        sqlite3_close(db);
        std::cout << "Conexão com o banco de dados encerrada." << std::endl;
    }
};

// ===================== FUNÇÃO DA WORKER THREAD ======================
// Substitua completamente sua worker_function antiga por esta
void worker_function(JobQueue& queue, DatabaseHandler& dbHandler) {
    std::cout << "Thread trabalhadora iniciada." << std::endl;
    const std::string url_deteccao = API_URL_BASE + "detectarbem/";

    while (true) {
        ReadingJob job;
        if (!queue.pop(job)) {
            break;
        }

        try {
            std::cout << "Worker processando detecção para EPC: " << job.epc << std::endl;
            
            // 1. Montar o corpo JSON para o POST
            json post_body;
            post_body["tag_rfid"] = job.epc;
            post_body["local_deteccao"] = job.mqttId; // Usando mqttId como o local

            // 2. Chamar a API via POST
            std::string apiResponse = postApiData(url_deteccao, API_KEY, post_body.dump());

            // 3. Processar a resposta da API
            if (!apiResponse.empty() && apiResponse.front() == '{') {
                json deteccaoData = json::parse(apiResponse);
                
                // Verifica se a resposta contém os dados esperados
                if (deteccaoData.count("bem_patrimonial") && deteccaoData.count("alerta_enviado")) {
                    
                    // Extrai os dados do bem
                    std::string descricao_bem = deteccaoData["bem_patrimonial"].value("descricao", "N/A");
                    std::string numero_registro = deteccaoData["bem_patrimonial"].value("numero_registro", "N/A");

                    // Extrai os dados da detecção
                    bool alerta_enviado = deteccaoData.value("alerta_enviado", false);
                    bool autorizacao_ativa = deteccaoData.value("autorizacao_ativa", false);
                    
                    // 4. Inserir o registro completo no banco de dados
                    dbHandler.insertReading(job.epc, std::to_string(job.timestamp), job.mqttId,
                                            descricao_bem, numero_registro,
                                            job.mqttId, alerta_enviado, autorizacao_ativa);

                } else {
                    std::string detail = deteccaoData.value("detail", "");
                    std::cerr << "Worker: Resposta da API nao contem os campos esperados. Detalhe: " << detail 
                              << " | Para EPC: " << job.epc << std::endl;
                }
            } else {
                 std::cerr << "Worker: Resposta da API (POST) nao e um JSON valido para EPC: " << job.epc << std::endl;
            }
        } catch(const std::exception& e) {
            std::cerr << "Worker: Exceção ao processar trabalho: " << e.what() << std::endl;
        }
    }
    std::cout << "Thread trabalhadora encerrada." << std::endl;
}

// ===================== CLASSES PAHO-MQTT ======================
class action_listener : public virtual mqtt::iaction_listener {
    std::string name_;
    void on_failure(const mqtt::token& tok) override { /* Implementação omitida por brevidade */ }
    void on_success(const mqtt::token& tok) override { /* Implementação omitida por brevidade */ }
public:
    action_listener(const std::string& name) : name_(name) {}
};

class callback : public virtual mqtt::callback, public virtual mqtt::iaction_listener {
    int nretry_;
    mqtt::async_client& cli_;
    mqtt::connect_options& connOpts_;
    action_listener subListener_;
    JobQueue& jobQueue_;

    void reconnect() {
        std::this_thread::sleep_for(chrono::milliseconds(2500));
        try {
            cli_.connect(connOpts_, nullptr, *this);
        } catch (const mqtt::exception& exc) {
            std::cerr << "Erro na reconexão: " << exc.what() << std::endl;
            exit(1);
        }
    }

    void on_failure(const mqtt::token& tok) override {
        std::cout << "Tentativa de conexão falhou" << std::endl;
        if (++nretry_ > N_RETRY_ATTEMPTS) exit(1);
        reconnect();
    }

    void on_success(const mqtt::token& tok) override {}

    void connected(const std::string& cause) override {
        std::cout << "\nConexão bem-sucedida" << std::endl;
        std::cout << "\nAssinando o tópico '" << TOPIC << "' para o cliente " 
                  << cli_.get_client_id() << std::endl;
        cli_.subscribe(TOPIC, QOS, nullptr, subListener_);
    }

    void connection_lost(const std::string& cause) override {
        std::cout << "\nConexão perdida" << std::endl;
        if (!cause.empty()) std::cout << "\tcause: " << cause << std::endl;
        std::cout << "Reconectando..." << std::endl;
        nretry_ = 0;
        reconnect();
    }

    // ##### A ALTERAÇÃO ESTÁ NESTA FUNÇÃO #####
    void message_arrived(mqtt::const_message_ptr msg) override {
        try {
            std::string payload_str = msg->to_string();
            json payload = json::parse(payload_str);
            
            ReadingJob job;
            
            // 1. Pega o valor original do EPC
            std::string raw_epc = payload.value("epc", "");

            // 2. Continua apenas se o EPC não for vazio
            if (!raw_epc.empty()) {
                // 3. Adiciona o prefixo "E2" ao valor antes de atribuir ao trabalho
                job.epc = "E2" + raw_epc; 

                job.timestamp = payload.value("timestamp", 0);
                job.mqttId = payload.value("mqttId", "");

                std::cout << "Novo trabalho adicionado à fila para o EPC (formatado): " << job.epc << std::endl;
                jobQueue_.push(job);
            }
        } catch (const std::exception& e) {
            std::cerr << "Erro na thread MQTT ao processar mensagem: " << e.what() << std::endl;
        }
    }

    void delivery_complete(mqtt::delivery_token_ptr token) override {}

public:
    callback(mqtt::async_client& cli, mqtt::connect_options& connOpts, JobQueue& queue)
        : nretry_(0), cli_(cli), connOpts_(connOpts), subListener_("Subscription"), jobQueue_(queue) {}
};

// ===================== MAIN ======================
int main(int argc, char* argv[]) {
    auto timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::string UNIQUE_CLIENT_ID = "paho_subscriber_" + std::to_string(timestamp);

    JobQueue jobQueue;
    DatabaseHandler dbHandler;
    
    if (!dbHandler.openConnection()) {
        return 1;
    }
    dbHandler.createTable();

    std::thread worker(worker_function, std::ref(jobQueue), std::ref(dbHandler));

    auto serverURI = (argc > 1) ? std::string{argv[1]} : DFLT_SERVER_URI;
    mqtt::async_client cli(serverURI, UNIQUE_CLIENT_ID);

    mqtt::ssl_options ssl_options;
    ssl_options.set_trust_store(PATH_TO_CA_CERT);
    ssl_options.set_key_store(PATH_TO_CLIENT_CERT);
    ssl_options.set_private_key(PATH_TO_CLIENT_KEY);
    ssl_options.set_enable_server_cert_auth(true);

    mqtt::connect_options connOpts;
    connOpts.set_clean_session(false);
    connOpts.set_ssl(ssl_options);
    connOpts.set_keep_alive_interval(std::chrono::seconds(60));

    callback cb(cli, connOpts, jobQueue);
    cli.set_callback(cb);

    try {
        std::cout << "Conectando ao servidor MQTT '" << serverURI << "'..." << std::flush;
        cli.connect(connOpts, nullptr, cb);
    }
    catch (const mqtt::exception& exc) {
        std::cerr << "\nERRO: Não foi possível conectar: " << exc << std::endl;
        jobQueue.stop();
        worker.join();
        return 1;
    }

    std::cout << "\nPressione Q<Enter> para sair\n" << std::endl;
    while (std::tolower(std::cin.get()) != 'q');

    try {
        std::cout << "\nDesconectando do servidor MQTT..." << std::flush;
        cli.disconnect()->wait();
        std::cout << "OK" << std::endl;
    }
    catch (const mqtt::exception& exc) {
        std::cerr << exc << std::endl;
    }
    
    std::cout << "Sinalizando para a thread trabalhadora encerrar..." << std::flush;
    jobQueue.stop();
    worker.join();
    std::cout << "OK" << std::endl;

    dbHandler.closeConnection();

    return 0;
}