-- Tabela para armazenar os portais de detecção
CREATE TABLE IF NOT EXISTS portais (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    nome TEXT UNIQUE NOT NULL,
    localizacao TEXT
);

-- Tabela para armazenar configurações globais
CREATE TABLE IF NOT EXISTS configuracoes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    chave TEXT UNIQUE NOT NULL,
    valor TEXT
);

-- Tabela para armazenar o log de detecções e dados do STLN
CREATE TABLE IF NOT EXISTS leituras (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    epc TEXT NOT NULL,
    timestamp TEXT NOT NULL,
    nome_estacao TEXT,
    permitido_movimentar INTEGER,
    alerta_gerado TEXT,
    patrimonio_stln TEXT,
    descricao_stln TEXT,
    responsaveis TEXT,
    descricao_bem TEXT,
    numero_registro TEXT
);