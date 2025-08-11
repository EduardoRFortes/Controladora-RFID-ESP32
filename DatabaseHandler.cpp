#include "DatabaseHandler.h"
#include <fstream>
#include <sstream>

DatabaseHandler::DatabaseHandler() : db(nullptr) {}

DatabaseHandler::~DatabaseHandler() {
    if (db) {
        sqlite3_close(db);
        std::cout << "Conexão com o banco de dados fechada." << std::endl;
    }
}

bool DatabaseHandler::openConnection() {
    int rc = sqlite3_open("Farrapo.db", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Erro ao abrir banco de dados: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    std::cout << "Banco de dados 'Farrapo.db' aberto com sucesso!" << std::endl;
    return true;
}

static int DatabaseHandler::callback(void* data, int argc, char** argv, char** azColName) {
    // Este callback não será usado para INSERTS, mas é bom tê-lo.
    return 0;
}

bool DatabaseHandler::createTablesFromSchema(const std::string& schemaPath) {
    std::ifstream schemaFile(schemaPath);
    if (!schemaFile.is_open()) {
        std::cerr << "Erro: Nao foi possivel abrir o arquivo de schema: " << schemaPath << std::endl;
        return false;
    }

    std::stringstream schemaBuffer;
    schemaBuffer << schemaFile.rdbuf();
    std::string sql = schemaBuffer.str();

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Erro ao criar tabelas: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    std::cout << "Tabelas criadas com sucesso a partir do schema!" << std::endl;
    return true;
}

bool DatabaseHandler::insertLeitura(
    const std::string& epc,
    const std::string& timestamp,
    const std::string& nomeEstacao,
    int permitidoMovimentar,
    const std::string& alertaGerado,
    const std::string& patrimonioStln,
    const std::string& descricaoStln,
    const std::string& responsaveis,
    const std::string& descricaoBem,
    const std::string& numeroRegistro
) {
    const char* sql = "INSERT INTO leituras (epc, timestamp, nome_estacao, permitido_movimentar, alerta_gerado, patrimonio_stln, descricao_stln, responsaveis, descricao_bem, numero_registro) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Erro ao preparar statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    // Binde os valores aos placeholders
    sqlite3_bind_text(stmt, 1, epc.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, timestamp.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, nomeEstacao.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, permitidoMovimentar);
    sqlite3_bind_text(stmt, 5, alertaGerado.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, patrimonioStln.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, descricaoStln.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, responsaveis.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, descricaoBem.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, numeroRegistro.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Erro ao executar insert: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    std::cout << "Registro na tabela 'leituras' inserido com sucesso!" << std::endl;
    return true;
}