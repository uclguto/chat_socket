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

#define SERVER_PORT 12345
#define MAX_CLIENTS 100
#define TAMANHO_BUFFER 401
#define TAMANHO_NOME 101

#define MODO_DEBUGER

/* Declaração de variáveis globais para permitir associar os descritores de arquivo dos sockets
// aos tratamentos de sinais do processo e rotina de erro */
int sockfd = 0;
int clientes_sockets[MAX_CLIENTS];

typedef struct cliente
{
    char nome[TAMANHO_NOME];
    int socket;
} Cliente;

// Realiza fechamento seguro do comunicador na ocorrência de erros
void error(const char *msg)
{
    int i;

    if (sockfd > 0)
    {
        close(sockfd);
    }

    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (clientes_sockets[i] > 0)
        {
            close(clientes_sockets[i]);
        }
    }

    perror(msg);
    exit(1);
}

// Realiza fechamento seguro do comunicador na ocorrência de sinais do sistema operacional
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
        if (clientes_sockets[i] > 0)
        {
            close(clientes_sockets[i]);
        }
    }

    exit(1);
}

// Desconecta o cliente que em algum momento apresentou falhas de comunicação
void deconecta_cliente(int indice_cliente, int clientes_sockets[], int clientes_pendentes[], Cliente clientes_aprovados[])
{
#ifdef MODO_DEBUGER
    printf("\n Vou desconectar o cliente %d\n", clientes_sockets[indice_cliente]);
#endif

    close(clientes_sockets[indice_cliente]);
    clientes_sockets[indice_cliente] = 0;
    clientes_pendentes[indice_cliente] = 0;
    clientes_aprovados[indice_cliente].socket = 0;
    clientes_aprovados[indice_cliente].nome[0] = '\0';
}

// Envia uma mensagem pela rede
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

// Recebe uma mensagem pela rede
int recebe_mensagem(int client_socket, char buffer[], int tamanho_buffer)
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

// Envia uma mensagem para todos os outros clientes conectados
void broadcast_message(int socket_cliente, char buffer[], int clientes_sockets[], int max_clients)
{
    int i, dest_socket;

#ifdef MODO_DEBUGER
    printf("\n Mensagem recebida broadcast: %s", buffer);
    printf("\n Tamanho da mensagem broadcast: %d\n", strlen(buffer));
#endif

    for (i = 0; i < max_clients; i++)
    {
        dest_socket = clientes_sockets[i];
        if (dest_socket == 0 || dest_socket == socket_cliente)
        {
            continue;
        }
        if (envia_mensagem(dest_socket, buffer, strlen(buffer)) <= 0)
        {
            error("\n Erro ao enviar a mensagem para outro cliente \n ");
        }
    }
}

// Verifica se há novas mensagens em algum socket de cliente aprovado. Se houver, verifica recebimento de nome de usuário válido recebido e envia mensagem recebida para outros clientes
void trata_clientes_aprovados(int clientes_sockets[], Cliente clientes_aprovados[], int maxClients, fd_set *readfds, char buffer[], int tamanho_buffer)
{
    int i, socket_cliente;
    char string_erro_cliente[100];

    for (i = 0; i < maxClients; i++)
    {
        if (clientes_aprovados[i].socket == 0)
        {
            continue;
        }

        socket_cliente = clientes_aprovados[i].socket;

        if (FD_ISSET(socket_cliente, readfds) == 0)
        {
            continue;
        }

#ifdef MODO_DEBUGER
        printf("\n Há atividade no cliente %d\n", socket_cliente);
#endif

        // Receber a mensagem do cliente
        memset(buffer, 0, tamanho_buffer);

        if (recebe_mensagem(socket_cliente, buffer, tamanho_buffer) <= 0)
        {
            snprintf(string_erro_cliente, TAMANHO_BUFFER, "\n Erro ao receber a mensagem, desconectando cliente %d", socket_cliente);

            perror(string_erro_cliente);

            // Desconectar o cliente
            close(socket_cliente);
            clientes_sockets[i] = 0;
            clientes_aprovados[i].socket = 0;
            continue;
        }

#ifdef MODO_DEBUGER
        printf("\n Mensagem recebida: %s\n", buffer);
#endif

        // Enviar a mensagem para os outros clientes conectados
        broadcast_message(socket_cliente, buffer, clientes_sockets, maxClients);
    }
}

// Verifica se há novas mensagens em algum socket de cliente. Se houver, envia mensagem recebida para outros clientes
void recebe_envia_mensagens_clientes(int clientes_sockets[], Cliente clientes_aprovados[], int maxClients, fd_set *readfds, char buffer[], int tamanho_buffer)
{
    int i, socket_cliente;
    char string_erro_cliente[100];

    for (i = 0; i < maxClients; i++)
    {
        socket_cliente = clientes_aprovados[i].socket;

        if (FD_ISSET(socket_cliente, readfds) == 0)
        {
            continue;
        }

#ifdef MODO_DEBUGER
        printf("\n Há atividade no cliente %d\n", socket_cliente);
#endif

        // Receber a mensagem do cliente
        memset(buffer, 0, tamanho_buffer);

        if (recebe_mensagem(socket_cliente, buffer, tamanho_buffer) <= 0)
        {
            snprintf(string_erro_cliente, TAMANHO_BUFFER, "\n Erro ao receber a mensagem, desconectando cliente %d", socket_cliente);

            perror(string_erro_cliente);

            // Desconectar o cliente
            close(socket_cliente);
            clientes_sockets[i] = 0;
            clientes_aprovados[i].socket = 0;
            continue;
        }

#ifdef MODO_DEBUGER
        printf("\n Mensagem recebida: %s\n", buffer);
#endif

        // Enviar a mensagem para os outros clientes conectados
        broadcast_message(socket_cliente, buffer, clientes_sockets, maxClients);
    }
}

// Confirma se a mensagem de boas vindas que o cliente recebeu estava correta e, estando, o aprova para comunicação
int confirma_mensagem_aprovacao(int indice_cliente, int clientes_pendentes[], char buffer[], Cliente clientes_aprovados[])
{
    int i;
    int retorno_cliente;
    char mensagem_aprovacao[TAMANHO_BUFFER];

    snprintf(mensagem_aprovacao, TAMANHO_BUFFER, "Usuário %s aprovado!", clientes_aprovados[indice_cliente].nome);

#ifdef MODO_DEBUGER
    printf("\n Usuário aceitou aprovação?\n");
    printf(" Buffer: %s\n", buffer);
    printf(" Mensagem_aprovacao: %s\n", mensagem_aprovacao);
#endif

    if (strcmp(buffer, mensagem_aprovacao))
    {
        return -2;
    }

#ifdef MODO_DEBUGER
    printf("\n Sim!\n");
#endif

    clientes_aprovados[indice_cliente].socket = clientes_pendentes[indice_cliente];

    snprintf(mensagem_aprovacao, TAMANHO_BUFFER, "Seja bem vindo ao comunicador! Segue abaixo a lista de usuarios ativos no sistema para envio de mensagens:", buffer);

    retorno_cliente = envia_mensagem(clientes_aprovados[indice_cliente].socket, mensagem_aprovacao, strlen(mensagem_aprovacao));

    if (retorno_cliente <= 0)
    {
        return retorno_cliente;
    }

    strncpy(mensagem_aprovacao, "0 - Envio a todos os usuários", TAMANHO_BUFFER);

    retorno_cliente = envia_mensagem(clientes_aprovados[indice_cliente].socket, mensagem_aprovacao, strlen(mensagem_aprovacao));

    if (retorno_cliente <= 0)
    {
        return retorno_cliente;
    }

    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if ((clientes_aprovados[i].socket == 0) || (i == indice_cliente))
        {
            continue;
        }

        snprintf(mensagem_aprovacao, TAMANHO_BUFFER, "%d - %s", clientes_aprovados[i].socket, clientes_aprovados[i].nome);

        retorno_cliente = envia_mensagem(clientes_aprovados[indice_cliente].socket, mensagem_aprovacao, strlen(mensagem_aprovacao));

        if (retorno_cliente <= 0)
        {
            return retorno_cliente;
        }
    }

    strncpy(mensagem_aprovacao, "Para enviar mensagens através deste comunicador, primeiro envie o número identificador do usuário e, logo após, a mensagem desejada.", TAMANHO_BUFFER);

    retorno_cliente = envia_mensagem(clientes_aprovados[indice_cliente].socket, mensagem_aprovacao, strlen(mensagem_aprovacao));

    if (retorno_cliente <= 0)
    {
        return retorno_cliente;
    }

    return -7; // Caso de aprovação aproveitando valor de retorno negativo livre
}

// Verifica recebimento de nomes válidos dos novos funcionários. Se o nome for aprovado, chama função para enviar mensagem de boas vindas e a lista de usuários aprovados.
void trata_aprovacao_clientes(int clientes_sockets[], int clientes_pendentes[], Cliente clientes_aprovados[], int maxClients, fd_set *readfds, char buffer[], int tamanho_nome)
{
    int i, retorno_cliente;
    char mensagem_aprovacao[TAMANHO_BUFFER];
    char string_erro_cliente[100];

    for (i = 0; i < maxClients; i++)
    {
        if (clientes_pendentes[i] == 0)
        {
            continue;
        }

        if (FD_ISSET(clientes_pendentes[i], readfds) == 0)
        {
            continue;
        }

#ifdef MODO_DEBUGER
        printf("\n Há atividade no cliente %d\n", clientes_pendentes[i]);
#endif

        memset(buffer, 0, TAMANHO_BUFFER);

        if (recebe_mensagem(clientes_pendentes[i], buffer, tamanho_nome) <= 0)
        {
            snprintf(string_erro_cliente, TAMANHO_BUFFER, "\n Erro ao receber a mensagem, desconectando cliente %d", clientes_pendentes[i]);

            perror(string_erro_cliente);

            deconecta_cliente(i, clientes_sockets, clientes_pendentes, clientes_aprovados);
            continue;
        }

#ifdef MODO_DEBUGER
        printf("\n Nome recebido do cliente: %s\n", buffer);
#endif

        if (clientes_aprovados[i].nome[0] == '\0')
        {
            strncpy(clientes_aprovados[i].nome, buffer, tamanho_nome);
            snprintf(mensagem_aprovacao, TAMANHO_BUFFER, "Usuário %s aprovado!", buffer);

            retorno_cliente = envia_mensagem(clientes_pendentes[i], mensagem_aprovacao, strlen(mensagem_aprovacao));
        }
        else
        {
            retorno_cliente = confirma_mensagem_aprovacao(i, clientes_pendentes, buffer, clientes_aprovados);
        }

        switch (retorno_cliente)
        {
        case 0:
            snprintf(string_erro_cliente, TAMANHO_BUFFER, "\n Conexão encerrada, desconectando cliente %d", clientes_pendentes[i]);
            perror(string_erro_cliente);

            deconecta_cliente(i, clientes_sockets, clientes_pendentes, clientes_aprovados);
            break;

        case -1:
            snprintf(string_erro_cliente, TAMANHO_BUFFER, "\n Erro ao enviar a mensagem, desconectando cliente %d", clientes_pendentes[i]);
            perror(string_erro_cliente);

            deconecta_cliente(i, clientes_sockets, clientes_pendentes, clientes_aprovados);
            break;

        case -2:
            snprintf(string_erro_cliente, TAMANHO_BUFFER, "\n Mensagem de confirmação diferente do que o esperado, desconectando cliente %d", clientes_pendentes[i]);
            perror(string_erro_cliente);

            deconecta_cliente(i, clientes_sockets, clientes_pendentes, clientes_aprovados);
            break;

        case -7:
            // Caso de aprovação aproveitando valor de retorno negativo livre
#ifdef MODO_DEBUGER
            printf("\n Usuário aprovado\n");
#endif
            break;

        default:
            break;
        }
    }
}

// Prepara descritor de arquivos com os sockets do servidor e dos clientes para o select
void prepara_descritor_arquivos(int clientes_sockets[], fd_set *readfds, int *max_socket_cliente)
{
    int i, socket_cliente;

    // Limpar os conjuntos de descritores
    FD_ZERO(readfds);
    FD_SET(sockfd, readfds);
    *max_socket_cliente = sockfd;

    for (i = 0; i < MAX_CLIENTS; i++)
    {
        socket_cliente = clientes_sockets[i];
        if (socket_cliente > 0)
        {
            FD_SET(socket_cliente, readfds);
        }
        if (socket_cliente > *max_socket_cliente)
        {
            *max_socket_cliente = socket_cliente;
        }
    }
}

// Adiciona um novo socket de cliente aos arrays para aguardar aprovação
void adiciona_novo_cliente(int new_sockfd, int clientes_sockets[], int clientes_pendentes[], int max_clients)
{
    int i;

    for (i = 0; i < max_clients; i++)
    {
        if (clientes_sockets[i] != 0)
        {
            continue;
        }

        clientes_sockets[i] = new_sockfd;
        clientes_pendentes[i] = new_sockfd;

        break;
    }
}

// Verifica se há uma nova conexão
void verifica_novas_conexoes(int sockfd, int clientes_sockets[], int clientes_pendentes[], fd_set *readfds, char buffer[], int tamanho_buffer)
{
    int new_sockfd;
    struct sockaddr_in client_addr;
    socklen_t client_len;

    if (FD_ISSET(sockfd, readfds))
    {
        new_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (new_sockfd < 0)
        {
            error("\n Erro ao aceitar a conexão\n ");
        }

        // Armazena e envia a mensagem de boas vindas para o cliente recém conectado
        snprintf(buffer, tamanho_buffer, "Bem vindo, cliente %d! Digite seu nome de usuário com até 100 caracteres para ser aprovado no comunicador.", new_sockfd);

        if (envia_mensagem(new_sockfd, buffer, strlen(buffer)) < 0)
        {
            error("\n Erro ao enviar a mensagem para o cliente conectado\n ");
        }

        // Adicionar o novo socket dos clientes ao array
        adiciona_novo_cliente(new_sockfd, clientes_sockets, clientes_pendentes, MAX_CLIENTS);

#ifdef MODO_DEBUGER
        printf("\n Adicionei novo socket de clientes\n");
#endif
    }
#ifdef MODO_DEBUGER
    else
    {
        printf("\n Nenhum novo cliente. Vou verificar recebimento e tratamento de mensagens.\n");
    }
#endif
}

int main()
{
    int i, max_socket_cliente;
    int optval = 1; // valor da opção SO_REUSEADDR
    int clientes_pendentes[MAX_CLIENTS];

    char buffer[TAMANHO_BUFFER];
    struct sockaddr_in server_addr;
    fd_set readfds; // conjunto de descritores para o select

    Cliente clientes_aprovados[MAX_CLIENTS];

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
        clientes_sockets[i] = 0;
        clientes_pendentes[i] = 0;
        clientes_aprovados[i].socket = 0;
        clientes_aprovados[i].nome[0] = '\0';
    }

    while (1)
    {
        prepara_descritor_arquivos(clientes_sockets, &readfds, &max_socket_cliente);

#ifdef MODO_DEBUGER
        printf("\n Adicionei sockets de clientes prontos para o select\n");
#endif

        if (select(max_socket_cliente + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            error("\n Erro ao aguardar por atividade\n ");
        }

#ifdef MODO_DEBUGER
        printf("\n Realizei select\n");
#endif

        verifica_novas_conexoes(sockfd, clientes_sockets, clientes_pendentes, &readfds, buffer, TAMANHO_BUFFER);

        trata_aprovacao_clientes(clientes_sockets, clientes_pendentes, clientes_aprovados, MAX_CLIENTS, &readfds, buffer, TAMANHO_NOME);

        trata_clientes_aprovados(clientes_sockets, clientes_aprovados, MAX_CLIENTS, &readfds, buffer, TAMANHO_BUFFER);
    }

    // Fechar o socket do servidor
    close(sockfd);

    return 0;
}