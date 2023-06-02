#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>

#define SERVER_PORT 12345
#define MAX_CLIENTS 100
#define TAMANHO_BUFFER 401

#define MODO_DEBUGER

/* Declaração de variáveis globaia para permitir associar os descritores de arquivo dos sockets
// aos tratamentos de sinais do processo e rotina de erro */
int sockfd = 0;
int client_sockets[MAX_CLIENTS];

// Função que realiza fechamento seguro do comunicador na ocorrência de erros
void error(const char *msg)
{
    int i;

    if (sockfd > 0)
    {
        close(sockfd);
    }

    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_sockets[i] > 0)
        {
            close(client_sockets[i]);
        }
    }

    perror(msg);
    exit(1);
}

// Função que realiza fechamento seguro do comunicador na ocorrência de sinais do sistema operacional
void fecha_conexao()
{
    int i;

#ifdef MODO_DEBUGER
    printf("\n Vou fechar as conexões\n");
#endif
    if (sockfd > 0)
    {
        close(sockfd);
    }

    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_sockets[i] > 0)
        {
            close(client_sockets[i]);
        }
    }

    exit(1);
}

// Função que envia uma mensagem pela rede
int envia_mensagem(int dest_socket, char buffer[], int tamanho)
{
    int enviado; // número de bytes enviados em cada chamada

    // Enviar o tamanho da mensagem
#ifdef MODO_DEBUGER
    printf("\n Tamanho da mensagem: %d", tamanho);
    printf("\n Tamanho do tamanho: %d\n", sizeof(tamanho));
#endif

    enviado = send(dest_socket, (char *)&tamanho, sizeof(tamanho), 0);

#ifdef MODO_DEBUGER
    printf("\n Tamanho enviado: %d\n", enviado);
#endif

    if (enviado <= 0)
    {
        return enviado; // erro ao enviar tamanho
    }

    // Enviar a mensagem
#ifdef MODO_DEBUGER
    printf("\n Mensagem: %s\n", buffer);
#endif

    enviado = send(dest_socket, buffer, tamanho, 0);

#ifdef MODO_DEBUGER
    printf("\n Tamanho enviado: %d\n", enviado);
#endif

    return enviado; // sucesso ao enviar mensagem ou erro se enviado <= 0
}

// Função que recebe uma mensagem pela rede
int recebe_mensagem(int client_socket, char buffer[], int tamanhoMax)
{
    int total = 0;               // total de bytes recebidos
    int bytesLeft = sizeof(int); // bytes restantes para receber o tamanho
    int n;                       // número de bytes recebidos em cada chamada
    int size;                    // tamanho da mensagem na ordem de bytes do host

    // Receber o tamanho da mensagem
    while (total < sizeof(int))
    {
#ifdef MODO_DEBUGER
        printf("\n Receber tamanho da mensagem do cliente %d\n", client_socket);
#endif

        n = recv(client_socket, (char *)&size + total, bytesLeft, 0);

        if (n <= 0)
        {
            return n; // erro ao receber tamanho ou conexão fechada pelo outro lado
        }

        total += n;
        bytesLeft -= n;

#ifdef MODO_DEBUGER
        printf("\n N: %d", n);
        printf("\n Tamanho: %d", size);
        printf("\n Total: %d", total);
        printf("\n BytesLeft: %d\n", bytesLeft);
#endif
    }

    // size = ntohl(size); // converter o tamanho da mensagem para a ordem de bytes do host

    // buffer = malloc(size); // alocar memória para a mensagem

    if (buffer == NULL)
    {
        return -3; // erro ao alocar memória
    }

    total = 0;        // reiniciar o total de bytes recebidos
    bytesLeft = size; // bytes restantes para receber a mensagem

    // Receber a mensagem
    while (total < size)
    {
#ifdef MODO_DEBUGER
        printf("\n Receber mensagem do cliente %d\n", client_socket);
#endif
        n = recv(client_socket, buffer + total, bytesLeft, 0);

        if (n <= 0)
        {
            // free(buffer); // liberar a memória alocada
            return n; // erro ao receber mensagem ou conexão fechada pelo outro lado
        }

        total += n;
        bytesLeft -= n;

#ifdef MODO_DEBUGER
        printf("\n N: %d", n);
        printf("\n Buffer: %s", buffer);
        printf("\n Total: %d", total);
        printf("\n BytesLeft: %d\n", bytesLeft);
#endif
    }

    // free(buffer); // liberar a memória alocada
    return n; // sucesso ao receber
}

// Função para enviar uma mensagem para todos os outros clientes conectados
void broadcast_message(int sd, char buffer[], int client_sockets[], int max_clients)
{
    int i, dest_socket;

#ifdef MODO_DEBUGER
    printf("\n Mensagem recebida broadcast: %s", buffer);
    printf("\n Tamanho da mensagem broadcast: %d\n", strlen(buffer));
#endif

    for (i = 0; i < max_clients; i++)
    {
        dest_socket = client_sockets[i];
        if (dest_socket == 0 || dest_socket == sd)
        {
            continue;
        }
        if (envia_mensagem(dest_socket, buffer, strlen(buffer)) <= 0)
        {
            error("\n Erro ao enviar a mensagem para outro cliente \n ");
        }
    }
}

// Verificar se há novas mensagens em algum socket de cliente. Se houver, envia mensagem recebida para outros clientes
void recebe_envia_mensagens_clientes(char buffer[], int client_sockets[], int maxClients, fd_set *readfds, int bufferSize)
{
    int i, sd;
    char string_erro_cliente[100];
    char string_socket_cliente[10];

    for (i = 0; i < maxClients; i++)
    {
        sd = client_sockets[i];

        if (FD_ISSET(sd, readfds) == 0)
        {
            continue;
        }

#ifdef MODO_DEBUGER
        printf("\n Há atividade no cliente %d\n", sd);
#endif

        // Receber a mensagem do cliente
        memset(buffer, 0, bufferSize);

        if (recebe_mensagem(sd, buffer, bufferSize) <= 0)
        {
            snprintf(string_erro_cliente, 100, "\n Erro ao receber a mensagem, desconectando cliente %d", sd);

            perror(string_erro_cliente);

            // Desconectar o cliente
            close(sd);
            client_sockets[i] = 0;
            continue;
        }

#ifdef MODO_DEBUGER
        printf("\n Mensagem recebida: %s\n", buffer);
#endif

        // Enviar a mensagem para os outros clientes conectados
        broadcast_message(sd, buffer, client_sockets, maxClients);
    }
}

// Adiciona ao conjunto Sockets prontos para o select
void add_socket_ready(int client_sockets[], fd_set *readfds, int *max_sd)
{
    int i, sd;
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        sd = client_sockets[i];
        if (sd > 0)
        {
            FD_SET(sd, readfds);
        }
        if (sd > *max_sd)
        {
            *max_sd = sd;
        }
    }
}

// Função para adicionar um novo socket de cliente ao array
void add_client_socket(int new_sockfd, int client_sockets[], int max_clients)
{
    int i;

    for (i = 0; i < max_clients; i++)
    {
        if (client_sockets[i] != 0)
        {
            continue;
        }
        client_sockets[i] = new_sockfd;
        break;
    }
}

int main()
{
    int new_sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[TAMANHO_BUFFER];
    int i, max_sd;
    int optval = 1; // valor da opção SO_REUSEADDR

    // Criar o socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        error("\n Erro ao criar o socket\n ");
    }

#ifdef MODO_DEBUGER
    printf("\n Criei o socket %d do servidor\n", sockfd);
#endif

    // Configurar o endereço do servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    // Configurar a opção SO_REUSEADDR
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
    {
        error("\n Erro ao configurar a opção SO_REUSEADDR\n ");
    }

    // Vincular o socket ao endereço
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        error("\n Erro ao vincular o socket ao endereço\n ");
    }

#ifdef MODO_DEBUGER
    printf("\n Vinculei o socket ao endereço e vou esperar conexões\n");
#endif

    // Tratamento de sinais
    sigset(SIGINT, fecha_conexao);
    sigset(SIGILL, fecha_conexao);
    sigset(SIGTERM, fecha_conexao);
    sigset(SIGSEGV, fecha_conexao);

    // Esperar por conexões
    if (listen(sockfd, MAX_CLIENTS) < 0)
    {
        error("\n Erro ao aguardar por conexões\n ");
    }

#ifdef MODO_DEBUGER
    printf("\n Inicializarei array de sockets\n");
#endif

    // Inicializar os arrays de sockets
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        client_sockets[i] = 0;
    }

    while (1)
    {
        // Limpar os conjuntos de descritores
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        max_sd = sockfd;

#ifdef MODO_DEBUGER
        printf("\n Entrei em loop\n");
#endif

        // Adicionar ao conjunto os sockets dos clientes prontos para o select
        add_socket_ready(client_sockets, &readfds, &max_sd);

#ifdef MODO_DEBUGER
        printf("\n Adicionei sockets de clientes prontos para o select\n");
#endif

        // Aguardar por atividade em algum socket
        if (select(max_sd + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            error("\n Erro ao aguardar por atividade\n ");
        }

#ifdef MODO_DEBUGER
        printf("\n Realizei select\n");
#endif

        // Verificar se há uma nova conexão
        if (FD_ISSET(sockfd, &readfds))
        {
            new_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
            if (new_sockfd < 0)
            {
                error("\n Erro ao aceitar a conexão\n ");
            }

            // Armazena e envia a mensagem de boas vindas para o cliente recém conectado
            snprintf(buffer, TAMANHO_BUFFER, "Bem vindo ao comunicador, cliente %d!", new_sockfd);

            if (envia_mensagem(new_sockfd, buffer, strlen(buffer)) < 0)
            {
                error("\n Erro ao enviar a mensagem para o cliente conectado\n ");
            }

            // Adicionar o novo socket dos clientes ao array
            add_client_socket(new_sockfd, client_sockets, MAX_CLIENTS);

#ifdef MODO_DEBUGER
            printf("\n Adicionei novos sockets de clientes\n");
#endif
        }
#ifdef MODO_DEBUGER
        else
        {
            printf("\n Nenhum novo Cliente. Vou verificar recebimento e tratamento de mensagens.\n");
        }
#endif

        // Verificar se há novas mensagens em algum socket de cliente. Se houver, envia mensagem recebida para outros clientes
        recebe_envia_mensagens_clientes(buffer, client_sockets, MAX_CLIENTS, &readfds, TAMANHO_BUFFER);
    }

    // Fechar o socket do servidor
    close(sockfd);

    return 0;
}