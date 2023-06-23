#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define TAMANHO_BUFFER 401
#define TAMANHO_NOME 101

#define MODO_DEBUGER

/* Declaração de variável global para permitir associar o descritor de arquivo do socket
// aos tratamentos de sinais do processo e rotina de erro */
int server_socket = 0;

// Função que realiza fechamento seguro do comunicador na ocorrência de erros
void error(const char *msg)
{
    perror(msg);

    if (server_socket >= 0)
    {
        close(server_socket);
    }

    exit(1);
}

// Função que realiza fechamento seguro do comunicador na ocorrência de sinais do sistema operacional
void fecha_conexao()
{
#ifdef MODO_DEBUGER
    printf("\n Vou fechar as conexões\n");
#endif
    if (server_socket > 0)
    {
        close(server_socket);
    }
    exit(1);
}

// Função que envia uma mensagem pela rede
int envia_mensagem(int server_socket, char buffer[], int tamanho)
{
    int enviado; // número de bytes enviados em cada chamada

    // Enviar o tamanho da mensagem
#ifdef MODO_DEBUGER
    printf("\n Tamanho da mensagem: %d", tamanho);
    printf("\n Tamanho do tamanho: %d\n", sizeof(tamanho));
#endif

    enviado = send(server_socket, (char *)&tamanho, sizeof(tamanho), 0);

#ifdef MODO_DEBUGER
    printf("\n Tamanho enviado: %d\n", enviado);
#endif

    if (enviado <= 0)
    {
        return enviado; // erro ao enviar tamanho
    }

// Enviar a mensagem
#ifdef MODO_DEBUGER
    printf("\n Mensagem: %s", buffer);
    printf("\n Tamanho da mensagem: %d\n", tamanho);
#endif

    enviado = send(server_socket, buffer, tamanho, 0);

#ifdef MODO_DEBUGER
    printf("\n Enviado: %d\n", enviado);
#endif

    return enviado; // sucesso ao enviar mensagem ou erro se enviado <= 0
}

// Função que recebe uma mensagem pela rede
int recebe_mensagem(int server_socket, char buffer[], int tamanhoMax)
{
    int total = 0;                // total de bytes recebidos
    int bytes_left = sizeof(int); // bytes restantes para receber o tamanho
    int n;                        // número de bytes recebidos em cada chamada
    int size;                     // tamanho da mensagem na ordem de bytes do host

    // Receber o tamanho da mensagem
    while (total < sizeof(int))
    {
#ifdef MODO_DEBUGER
        printf("\n Receber tamanho da mensagem do servidor pela conexão %d\n", server_socket);
#endif

        n = recv(server_socket, (char *)&size + total, bytes_left, 0);

        if (n <= 0)
        {
            return n; // erro ao receber tamanho ou conexão fechada pelo outro lado
        }
        total += n;
        bytes_left -= n;

#ifdef MODO_DEBUGER
        printf("\n N: %d", n);
        printf("\n Tamanho Network: %d", size);
        printf("\n Total: %d", total);
        printf("\n bytes_left: %d\n", bytes_left);
#endif
    }

    // Comentei porque o tamanho já está vindo correto (problema biendian vs litle endian)
    // size = ntohl(size); // converter o tamanho da mensagem para a ordem de bytes do host

#ifdef MODO_DEBUGER
    printf("\n Tamanho Host: %d\n", size);
#endif

    // buffer = malloc(size); // alocar memória para a mensagem

    if (buffer == NULL)
    {
        return -3; // erro ao alocar memória
    }

    total = 0;         // reiniciar o total de bytes recebidos
    bytes_left = size; // bytes restantes para receber a mensagem

    // Receber a mensagem
    while (total < size)
    {
#ifdef MODO_DEBUGER
        printf("\n Receber mensagem do servidor pela conexão %d\n", server_socket);
#endif
        n = recv(server_socket, buffer + total, bytes_left, 0);

        if (n <= 0)
        {
            // free(buffer); // liberar a memória alocada
            return n; // erro ao receber ou conexão fechada pelo outro lado
        }
        total += n;
        bytes_left -= n;

#ifdef MODO_DEBUGER
        printf("\n N: %d", n);
        printf("\n Buffer: %s", buffer);
        printf("\n Total: %d", total);
        printf("\n bytes_left: %d\n", bytes_left);
#endif
    }

    // free(buffer); // liberar a memória alocada
    return n; // sucesso ao receber mensagem ou erro se n <= 0
}

// Função que verifica se recebeu mensagem pelo socket
int verifica_mensagem_socket(fd_set *readfds, int server_socket, char buffer[], int tamanho_max)
{
    int retorno_recebimento = 1;

    if (FD_ISSET(server_socket, readfds))
    {
        memset(buffer, 0, TAMANHO_BUFFER);

        // receber a mensagem do servidor
        retorno_recebimento = recebe_mensagem(server_socket, buffer, tamanho_max);

        if (retorno_recebimento > 0)
        {
            printf("\n %s\n", buffer);
        }
    }

    return retorno_recebimento;
}

// Função que verifica se recebeu mensagem por entrada de usuário através do shell
int verifica_mensagem_shell(fd_set *readfds, int server_socket, char buffer[], int tamanho_max)
{
    int retorno_envio = 1;

    if (FD_ISSET(STDIN_FILENO, readfds))
    {
#ifdef MODO_DEBUGER
        printf("\n Usuario digitou mensagem\n");
#endif

        fgets(buffer, tamanho_max, stdin);

        // a função fgets recebe a string com \n no final, que precisa ser lida com \0 nas funções de string
        buffer[strcspn(buffer, "\n")] = '\0';

        if (!(strcmp(buffer, "S") && strcmp(buffer, "s") && strcmp(buffer, "[S/s]")))
        {
            return -10;
        }

#ifdef MODO_DEBUGER
        printf("\n Mensagem a enviar: %s\n", buffer);
#endif

        retorno_envio = envia_mensagem(server_socket, buffer, strlen(buffer));

#ifdef MODO_DEBUGER
        if (retorno_envio > 0)
        {
            printf("\n Mensagem enviada: %s\n", buffer);
        }
#endif
    }

    return retorno_envio;
}

// Função que trata o processo de comunicação já aprovada entre o cliente e o servidor
int trata_comunicador(fd_set *readfds, int server_socket, char buffer[], int tamanho_max)
{
    int retorno_verificacao;

    retorno_verificacao = verifica_mensagem_shell(readfds, server_socket, buffer, TAMANHO_BUFFER);

    if (retorno_verificacao <= 0)
    {
        return retorno_verificacao;
    }

    retorno_verificacao = verifica_mensagem_socket(readfds, server_socket, buffer, TAMANHO_BUFFER);

    return retorno_verificacao;
}

// Função que verifica se a mensagem de retorno do servidor é a aprovação da comunicação
int verifica_retorno_aprovacao(char buffer[], char buffer_nome[], int tamanho_buffer)
{
    char mensagem_aprovacao[tamanho_buffer];

    // Armazena e envia a mensagem de boas vindas para o cliente recém conectado
    snprintf(mensagem_aprovacao, tamanho_buffer, "Usuário %s aprovado!", buffer_nome);

#ifdef MODO_DEBUGER
    printf("\n Usuário aprovado?\n");
    printf(" Buffer: %s\n", buffer);
    printf(" Mensagem_aprovacao: %s\n", mensagem_aprovacao);
#endif

    if (!strcmp(buffer, mensagem_aprovacao))
    {
#ifdef MODO_DEBUGER
        printf("\n Sim!\n");
#endif

        return -7; // Trata-se da aprovação da conferência do nome pelo servidor. É negativo para ser tratado pelo switch do main
    }

    return 1;
}

// Função que trata o processo de envio e aprovação do nome do cliente pelo servidor
int trata_aprovacao_nome(fd_set *readfds, int server_socket, char buffer[], char nome_cliente[], int tamanho_nome, int tamanho_buffer)
{
    int retorno_verificacao;

    retorno_verificacao = verifica_mensagem_shell(readfds, server_socket, nome_cliente, tamanho_nome);

    if (retorno_verificacao <= 0)
    {
        return retorno_verificacao;
    }

    retorno_verificacao = verifica_mensagem_socket(readfds, server_socket, buffer, tamanho_buffer);

    if (retorno_verificacao <= 0)
    {
        return retorno_verificacao;
    }

    retorno_verificacao = verifica_retorno_aprovacao(buffer, nome_cliente, tamanho_buffer);

    return retorno_verificacao;
}

int main()
{
    int fecha_comunicador = 0;
    int apto_comunicacao = 0;
    int max_descritor_arquivo, retorno_comunicador;
    struct sockaddr_in server_addr;
    char buffer[TAMANHO_BUFFER];
    char nome_cliente[TAMANHO_NOME];
    fd_set readfds; // conjunto de descritores para o select

    // Criar o socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        error("\n Erro ao criar o socket\n");
    }

    // Configurar o endereço do servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &(server_addr.sin_addr)) <= 0)
    {
        error("\n Erro ao converter o endereço IP\n");
    }

    // Conectar ao servidor
    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        error("\n Falha de conexao no servidor \n");
    }

#ifdef MODO_DEBUGER
    printf("Cliente conectado ao servidor %s na porta %d\n", SERVER_IP, SERVER_PORT);
#endif

    // Tratamento de sinais
    sigset(SIGINT, fecha_conexao);
    sigset(SIGILL, fecha_conexao);
    sigset(SIGTERM, fecha_conexao);
    sigset(SIGSEGV, fecha_conexao);

    printf("\n A qualquer momento, digite [S/s] para sair:\n");

    // Enviar e receber mensagens
    while (!fecha_comunicador)
    {
        // limpar o conjunto de descritores
        FD_ZERO(&readfds);

        // adicionar o socket e o prompt ao conjunto de descritores
        FD_SET(server_socket, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        // definir o valor máximo de descritor
        if (server_socket > STDIN_FILENO)
        {                                          // se a condição for verdadeira
            max_descritor_arquivo = server_socket; // atribui sock a max_descritor_arquivo
        }
        else
        {                                         // caso contrário
            max_descritor_arquivo = STDIN_FILENO; // atribui STDIN_FILENO a max_descritor_arquivo
        }

        // esperar por dados disponíveis no socket ou no prompt usando select
        if (select(max_descritor_arquivo + 1, &readfds, NULL, NULL, NULL) == -1)
        {
            error("\n Erro ao aguardar por atividade\n");
        }

#ifdef MODO_DEBUGER
        printf("\n Realizei select\n");
#endif

        if (!apto_comunicacao)
        {
            retorno_comunicador = trata_aprovacao_nome(&readfds, server_socket, buffer, nome_cliente, TAMANHO_NOME, TAMANHO_BUFFER);
        }

        if (apto_comunicacao)
        {
            retorno_comunicador = trata_comunicador(&readfds, server_socket, buffer, TAMANHO_BUFFER);
        }

        switch (retorno_comunicador)
        {
        case 0:
            error("\n Servidor desconectado! Finalizando programa\n");

        case -1:
            error("\n Erro ao receber ou enviar mensagem\n");

        case -3:
            error("\n Erro ao alocar memória para o buffer\n");

        case -7:
           //Caso de aprovação aproveitando valor de retorno negativo livre
            apto_comunicacao = 1;

#ifdef MODO_DEBUGER
            printf("\n Usuário aprovado\n");
#endif

            break;

        case -10:
            printf("\n Você optou por encerrar este Comunicador. Até a próxima! =D\n");
            fecha_comunicador = 1;
            break;

        default:
            break;
        }
    }

    // Fechar a conexão
    close(server_socket);
    return 0;
}