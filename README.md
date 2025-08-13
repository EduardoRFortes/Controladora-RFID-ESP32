Documentação da Aplicação: Controlador de Detecção RFID

1. Visão Geral

Esta aplicação é um serviço de retaguarda (backend) desenvolvido em C++ que atua como um controlador inteligente para leitores de RFID. Sua principal função é escutar mensagens de detecção de tags RFID enviadas via protocolo MQTT, processar cada detecção em tempo real, validar a informação com uma API de patrimônio externa e registrar as detecções válidas em um banco de dados local para histórico e auditoria.

O projeto foi desenhado para ser robusto e eficiente, utilizando uma arquitetura multi-thread para garantir que nenhuma leitura de tag seja perdida, mesmo sob alto volume de detecções ou em caso de lentidão da rede ou da API externa.

2. Arquitetura da Aplicação

A aplicação utiliza um design multi-thread para separar as responsabilidades e garantir a responsividade. Existem três fluxos de execução principais:

    Thread Principal (main): É o ponto de entrada da aplicação. Suas responsabilidades são:

        Iniciar e configurar os componentes principais (Banco de Dados, Cliente MQTT).

        Lançar a Thread de Trabalho (Worker Thread).

        Conectar-se ao broker MQTT.

        Aguardar um sinal de encerramento do usuário (pressionar 'q').

        Coordenar o encerramento limpo de todos os componentes.

    Thread de Callback MQTT (Gerenciada pela biblioteca Paho-MQTT): Esta thread é criada e gerenciada pela biblioteca Paho. Sua única e exclusiva responsabilidade é escutar as mensagens do broker MQTT.

        Quando uma mensagem chega (função message_arrived), ela realiza o mínimo de processamento possível: decodifica o JSON, formata o EPC (adicionando o prefixo "E2") e coloca os dados em uma fila segura (JobQueue).

        É crucial que esta thread seja muito rápida e não bloqueante, para que possa processar centenas ou milhares de leituras por segundo sem perder nenhuma.

    Thread de Trabalho (worker_function): Esta é uma thread que nós criamos explicitamente. Sua responsabilidade é fazer todo o "trabalho pesado" e lento.

        Ela fica em um loop contínuo, monitorando a fila de trabalhos (JobQueue).

        Quando um novo trabalho aparece na fila, ela o retira e executa as operações lentas e bloqueantes:

            A requisição de rede para a API externa via POST.

            A escrita no arquivo de banco de dados SQLite.

        Essa separação garante que a lentidão da API ou do disco não afete a capacidade da aplicação de receber novas leituras de tags.

3. Componentes Principais e Suas Funções

O código é dividido em vários componentes lógicos:

main():	Orquestra a inicialização e o encerramento da aplicação. Cria e gerencia o ciclo de vida dos outros componentes.

callback (classe):	Implementa os handlers de eventos do cliente MQTT. A função message_arrived é a porta de entrada para todas as leituras de RFID no sistema.

JobQueue (classe):	É o "coração" da arquitetura concorrente. Uma fila segura que atua como um buffer entre a thread rápida do MQTT e a thread lenta de trabalho, permitindo que elas se comuniquem sem conflitos.

worker_function:	A função que executa em uma thread separada para processar os trabalhos da fila. É aqui que a lógica de negócio (chamar a API, salvar no banco) é executada.

DatabaseHandler (classe):	Abstrai toda a interação com o banco de dados SQLite. É responsável por abrir a conexão, criar a tabela (se não existir) e inserir os registros.

postApiData (função):	Uma função auxiliar que lida com a complexidade de fazer uma requisição HTTP POST com um corpo JSON, usando a biblioteca libcurl.

Constantes Globais:	No topo do arquivo, são definidas todas as configurações estáticas da aplicação, como endereços de servidores, chave de API, tópicos MQTT e caminhos de certificados.

4. Fluxo de Dados de uma Leitura

Quando uma tag RFID é lida pelo leitor, o seguinte fluxo acontece:

    O leitor RFID publica uma mensagem JSON no tópico MQTT configurado.

    A Thread de Callback MQTT da nossa aplicação recebe a mensagem.

    A função message_arrived decodifica o JSON, pega o EPC de 22 caracteres e o formata, adicionando o prefixo "E2".

    Os dados formatados (EPC, timestamp, mqttId) são empacotados em um ReadingJob e colocados na JobQueue.

    A Thread de Trabalho, que estava aguardando, detecta o novo trabalho e o retira da fila.

    A worker_function usa os dados do trabalho para montar um corpo JSON para a requisição POST. O mqttId é usado como local_deteccao.

    A função postApiData é chamada, enviando a requisição para a API de Patrimônio no endpoint /api/patrimonio/detectarbem/.

    A worker_function aguarda a resposta.

    Se a resposta da API for um sucesso (código 200 OK):

        O JSON da resposta é decodificado.

        As informações do bem (descricao, numero_registro) e da detecção (alerta_enviado, etc.) são extraídas.

        A função dbHandler.insertReading é chamada com todos os dados combinados (do MQTT e da API).

        Um novo registro completo é salvo na tabela leituras do banco de dados Farrapo.db.

    Se a resposta da API for um erro (ex: 404 Not Found):

        A resposta de erro é registrada no console.

        Nenhum dado é inserido no banco de dados. O ciclo para esta leitura termina aqui.

5. Dependências

Para compilar e executar esta aplicação, as seguintes bibliotecas são necessárias:

    Paho MQTT C++ Client (lib-mqttpp3): Para comunicação com o broker MQTT.

    libcurl (libcurl4-openssl-dev): Para fazer requisições HTTP/HTTPS para a API externa.

    nlohmann/json: Uma biblioteca (geralmente adicionada como um único arquivo de cabeçalho) para facilitar a manipulação de JSON em C++.

    SQLite3 (libsqlite3-dev): Para a criação e manipulação do banco de dados local.

6. Como Compilar e Executar

A compilação é feita através do compilador g++, linkando todas as dependências necessárias.

g++ -o main main.cpp -lsqlite3 -lcurl -lpaho-mqttpp3 -lpaho-mqtt3as -pthread -std=c++17

    -pthread: Essencial para habilitar o suporte a std::thread.

    -std=c++17: Especifica o padrão do C++ a ser usado.

Para executar a aplicação, basta rodar o arquivo compilado:

./main

7. Manutenção e Possíveis Melhorias

    Tratamento de Erros: A aplicação poderia ser melhorada para registrar erros de forma mais persistente (em um arquivo de log, por exemplo) em vez de apenas no console.

    Arquivo de Configuração: As constantes globais (URLs, chave de API) poderiam ser movidas para um arquivo de configuração externo (ex: config.json) para que a aplicação não precise ser recompilada a cada mudança de ambiente.

    Resiliência: Implementar uma lógica para o que fazer se a API ou o banco de dados ficarem indisponíveis por um longo período (ex: salvar os trabalhos em um arquivo para processamento posterior).
