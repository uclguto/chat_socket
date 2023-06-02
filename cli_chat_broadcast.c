#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define BUFFER_SIZE 401

#define MODO_DEBUGER

void error(const char *msg)
{
    perror(msg);
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
        memset(buffer, 0, BUFFER_SIZE);

        // receber a mensagem do servidor
        retorno_recebimento = recebe_mensagem(server_socket, buffer, tamanho_max);

        if (retorno_recebimento > 0)
        {
            printf("\n Mensagem recebida: %s\n", buffer);
        }
    }

    return retorno_recebimento;
}

// Função que verifica se recebeu mensagem por entrada de usuário através do shell
int verifica_mensagem_shell(fd_set *readfds, int server_socket, char buffer[], int tamanho_max)
{
    int retorno_envio = 1;
    char buffer_padrao_string[tamanho_max];

    if (FD_ISSET(STDIN_FILENO, readfds))
    {
#ifdef MODO_DEBUGER
        printf("\n Usuario digitou mensagem\n");
#endif

        fgets(buffer, BUFFER_SIZE, stdin);

        strcpy(buffer_padrao_string, buffer);

        // a função fgets recebe a string com \n no final, que precisa ser lida com \0 nas funções de string
        buffer_padrao_string[strcspn(buffer_padrao_string, "\n")] = '\0';

        if (!(strcmp(buffer_padrao_string, "S") && strcmp(buffer_padrao_string, "s") && strcmp(buffer_padrao_string, "[S/s]")))
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

int main()
{
    int fecha_comunicador = 0;
    int server_socket, max_descritor_arquivo, retorno_verificacao;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
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
        close(server_socket);

        printf("\n Falha [%s] conexao servidor. \n", strerror(errno));
        return -6;
    }

#ifdef MODO_DEBUGER
    printf("Cliente conectado ao servidor %s na porta %d\n", SERVER_IP, SERVER_PORT);
#endif

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

        // ler a mensagem do usuário do shell
        printf("\n Digite uma mensagem para outros clientes ou [S/s] para sair:\n");

        // esperar por dados disponíveis no socket ou no prompt usando select
        if (select(max_descritor_arquivo + 1, &readfds, NULL, NULL, NULL) == -1)
        {
            perror("\n Erro ao aguardar por atividade\n");
            exit(1);
        }

#ifdef MODO_DEBUGER
        printf("\n Realizei select\n");
#endif

        retorno_verificacao = verifica_mensagem_socket(&readfds, server_socket, buffer, BUFFER_SIZE);
        if (retorno_verificacao < 0)
        {
            perror("\n Erro ao receber a mensagem\n");
            continue;
        }
        else if (retorno_verificacao == 0)
        {
            error("\n Servidor desconectado! Finalizando programa\n ");
            break;
        }

        retorno_verificacao = verifica_mensagem_shell(&readfds, server_socket, buffer, BUFFER_SIZE);
        if (retorno_verificacao == -10)
        {
            printf("\n Você optou por encerrar este Comunicador. Até a próxima! =D\n");
            fecha_comunicador = 1;
        }
        else if (retorno_verificacao <= 0)
        {
            error("\n Erro ao enviar a mensagem para outro cliente\n ");
            continue;
        }
    }

    // Fechar a conexão
    close(server_socket);
    return 0;
}