// Codigo desenvolvido pelos alunos João Henrique e Marcelle Andrade
// para a disciplina de Sistemas Operacionais 2024.2
// Professor: João Carlos 
// Arquivo: Trafego.c

// Bibliotecas
#include <stdio.h>    // Biblioteca para entrada e saída de dados
#include <string.h>   // Biblioteca para manipulação de strings
#include <stdlib.h>   // Biblioteca para funções de alocação de memória
#include <unistd.h>   // Biblioteca para funções de sistema
#include <sys/wait.h> // Biblioteca para funções de espera de processos
#include <time.h>     // Biblioteca para funções de tempo
#include <signal.h>   // Biblioteca para manipulação de sinais
#include <pthread.h>  // Biblioteca para manipulação de threads

// Defines com o número máximo
#define MAX_AVIOES_AEROPORTO 3 // Número máximo de aviões no aeroporto
#define TEMPO_POUSO 5          // Tempo que o avião fica parado no aeroporto
#define TEMPO_DECOLAGEM 3      // Tempo que o avião fica no ar

// Enum para definir os estados do avião
typedef enum
{
    DECOLANDO, // Estado do avião ao decolar
    POUSANDO,  // Estado do avião ao pousar
    PARADO,    // Estado do avião parado
    VOANDO,    // Estado do avião voando
} Estado;

// Estrutura para definir os dados do avião
typedef struct
{
    int id;
    float latitude;
    float longitude;
    int altitude;
    char permissao[10];
    Estado estado;

} Trafego;

typedef struct
{
    int contador_pousos;
    int contador_decolagens;
} ThreadArgs;

// Funções
void aviao(int readfd, int writefd);
void requisicao(int readfd, int writefd);
void handle_sigint(int sig);       // interrupção do usuário
void thread_function(void *args); // Função da thread
void iniciar_contador();

// Variáveis globais
FILE *arquivo = NULL;
pthread_t monitor_thread;
pthread_mutex_t contador_mutex = PTHREAD_MUTEX_INITIALIZER;
int total_pousos = 0;
int total_decolagens = 0;
volatile sig_atomic_t should_terminate = 0;

// Função principal
int main()
{
    int torre;                     // Quem vai coordenar as operações (PAI)
    int piperequisicao[2];         // Requisição dos aviões → torre
    int pipecontrole[2];           // Processamento de torre → avião
    srand(time(NULL));             // Gera números aleatórios com base no tempo
    signal(SIGINT, handle_sigint); // Gerenciamento de interrupções (CTRL + C)

    // Para criar o processo do avião
    if (pipe(piperequisicao) < 0 || pipe(pipecontrole) < 0)
    {
        printf("A pista encontra-se interditada (ERRO AO CRIAR PIPES)");
        exit(1);
    }

    // Se o processo der erro na criação
    if ((torre = fork()) < 0)
    {
        printf("Erro da chamada de fork (processo não criado)");
        exit(1);
    }

    if (torre == 0)
    { // Torre de Controle 0
        iniciar_contador();

        // Torre fecha as extremidades que não vai usar
        close(piperequisicao[1]);
        close(pipecontrole[0]);

        // Torre executa a requisição
        requisicao(piperequisicao[0], pipecontrole[1]);

        // Fecha os descritores após uso
        close(piperequisicao[0]);
        close(pipecontrole[1]);

        // Espera todos os processos acabarem
        while (wait(NULL) > 0);
    }

    else
    { // Processo pai: Criador dos aviões
        // Pai fecha as extremidades que não vai usar
        close(piperequisicao[0]);
        close(pipecontrole[1]);

        // Cria os processos dos aviões
        for (int i = 0; i < MAX_AVIOES_AEROPORTO * 2; i++)
        {
            if (fork() == 0)
            {                                              // Processo avião
                aviao(pipecontrole[0], piperequisicao[1]); // Executa a função aviao
                close(pipecontrole[0]);                    // Fecha o descritor
                close(piperequisicao[1]);                  // Fecha o descritor
                exit(0);                                   // Encerra o processo
            }
        }

        // Fecha os descritores após criar todos os aviões
        close(pipecontrole[0]);   // Fecha o descritor
        close(piperequisicao[1]); // Fecha o descritor

        // Aguarda todos os processos filhos terminarem
        while (wait(NULL) > 0);
    }

    return 0;
}

// Função que simula as requisições do avião na hora do pouso
void aviao(int readfd, int writefd)
{
    srand(getpid()); // Gera números aleatórios com base no PID

    // Função para manipular o tráfego do avião
    Trafego aviao;

    // Preenche os dados de forma aleatória
    aviao.id = getpid();                                           // PID como ID único
    aviao.latitude = -90.0 + ((float)rand() / RAND_MAX) * 180.0;   // Gera latitude aleatória entre -90.0 e 90.0
    aviao.longitude = -180.0 + ((float)rand() / RAND_MAX) * 360.0; // Gera longitude aleatória entre -180.0 e 180.0
    aviao.altitude = 1000 + rand() % 39001;                        // Gera altitude aleatória entre 1000 e 40000
    snprintf(aviao.permissao, sizeof(aviao.permissao), "Negado");  // Seta permissão (negado como default)
    aviao.estado = VOANDO;                                         // Estado do avião (VOANDO como default)

    while (1)
    {
        // Se ele solicitou permissão para pouso, ele envia os dados para a torre
        if (write(writefd, &aviao, sizeof(Trafego)) < 0)
        {
            perror("Erro ao enviar dados para a torre");
            exit(1);
        }

        // Mensagem de solicitação do avião (pouso ou decolagem)
        printf("Avião %d solicitando %s\n", aviao.id,
               aviao.estado == VOANDO ? "pouso" : "decolagem");

        // Se ele vai pousar/decolar, primeiro tem que aguardar resposta da torre
        Trafego resposta;
        if (read(readfd, &resposta, sizeof(Trafego)) < 0)
        {
            perror("Erro ao receber resposta da torre");
            exit(1);
        }

        // Mensagem de resposta (Aprovado ou negado)
        printf("Avião %d recebeu resposta: %s\n", aviao.id, resposta.permissao);

        // Procedimento caso aprovado
        if (strcmp(resposta.permissao, "Aprovado") == 0)
        {
            // Procedimento caso aprovado e estiver parado
            if (aviao.estado == PARADO)
            {
                printf("Avião %d iniciando decolagem...\n\n", aviao.id);
                sleep(TEMPO_DECOLAGEM);
                aviao.estado = VOANDO;

                int tempo_voando = rand() % 10 + 10;
                printf("Avião %d ficará voando por %d segundos\n\n", aviao.id, tempo_voando);
                sleep(tempo_voando);
                // Procedimento caso aprovado e estiver voando
            }
            else if (aviao.estado == VOANDO)
            {
                printf("Avião %d iniciando pouso...\n\n", aviao.id);
                sleep(TEMPO_POUSO);
                aviao.estado = PARADO;

                int tempo_parado = rand() % 10 + 10;
                printf("Avião %d ficará parado por %d segundos\n\n", aviao.id, tempo_parado);
                sleep(tempo_parado);
            }
        }
        // Procedimento caso negado
        else
        {
            // Procedimento caso negado e estiver voando
            if (aviao.estado == VOANDO)
            {
                printf("Avião %d aguardando novo pedido para pouso...\n\n", aviao.id);
            }
            // Procedimento caso negado e estiver parado
            else if (aviao.estado == PARADO)
            {
                printf("Avião %d aguardando novo pedido para decolagem...\n\n", aviao.id);
            }
            sleep(5);
        }
    }
}

// Função que simula a torre recebendo e processando a solicitação do avião
void requisicao(int readfd, int writefd)
{
    arquivo = fopen("avioes_pipe.csv", "w");
    if (arquivo == NULL)
    {
        perror("Erro ao criar o arquivo");
        exit(1);
    }

    // Cabeçalho do arquivo
    fprintf(arquivo, "ID,Latitude,Longitude,Altitude,Permissao\n");

    Trafego aviao;

    // Quantidade de bytes lido no read abaixo (para determinar erros)
    size_t bytesRead;

    // Contador de aviões no aeroporto
    int avioes_no_aeroporto = 0;

    while ((bytesRead = read(readfd, &aviao, sizeof(Trafego))) > 0)
    {
        printf("Torre recebeu solicitação de %s do avião %d\n", aviao.estado == VOANDO ? "pouso" : "decolagem", aviao.id);

        // Procedimento caso a torre receber uma mensagem de um avião voando
        if (aviao.estado == VOANDO)
        {
            // Procedimento caso o aeroporto não estiver lotado
            if (avioes_no_aeroporto < MAX_AVIOES_AEROPORTO)
            {
                snprintf(aviao.permissao, sizeof(aviao.permissao), "Aprovado");
                avioes_no_aeroporto++;
                printf("Torre: Pouso autorizado para avião %d. Aviões no aeroporto: %d\n",
                       aviao.id, avioes_no_aeroporto);
            }
            // Procedimento caso o aeroporto estiver lotado
            else
            {
                snprintf(aviao.permissao, sizeof(aviao.permissao), "Negado");
                printf("Torre: Pouso negado para avião %d. Aeroporto lotado. Aviões no aeroporto: %d\n",
                       aviao.id, avioes_no_aeroporto);
            }
        }
        // Procedimento caso a torre receber uma mensagem de um avião parado
        else if (aviao.estado == PARADO)
        {
            snprintf(aviao.permissao, sizeof(aviao.permissao), "Aprovado");
            avioes_no_aeroporto--;
            printf("Torre: Decolagem autorizada para avião %d. Aviões no aeroporto: %d\n",
                   aviao.id, avioes_no_aeroporto);
        }

        // Escreve os dados no arquivo
        fprintf(arquivo, "%d,%.6f,%.6f,%d,%s\n",
                aviao.id, aviao.latitude, aviao.longitude, aviao.altitude, aviao.permissao);

        // Envia resposta para o avião
        if (write(writefd, &aviao, sizeof(Trafego)) < 0)
        {
            perror("Erro ao enviar resposta para o avião");
            fclose(arquivo);
            exit(1);
        }

        printf("Torre enviou resposta ao avião %d: %s\n", aviao.id, aviao.permissao);

        // Libera a pista
        if (strcmp(aviao.permissao, "Aprovado") == 0)
        {
            printf("Torre: Pista liberada\n\n");
        }
        
        if (aviao.estado == VOANDO && strcmp(aviao.permissao, "Aprovado") == 0)
        {
            pthread_mutex_lock(&contador_mutex);
            total_pousos++;
            pthread_mutex_unlock(&contador_mutex);
        }
        else if (aviao.estado == PARADO && strcmp(aviao.permissao, "Aprovado") == 0)
        {
            pthread_mutex_lock(&contador_mutex);
            total_decolagens++;
            pthread_mutex_unlock(&contador_mutex);
        }

        // Aguarda 3 segundos antes de processar a próxima solicitação
        sleep(4);
    }

    // Verificação de erro
    if (bytesRead < 0)
    {
        perror("Erro ao ler os dados do pipe");
    }
}

// Função que monitora as estatísticas do aeroporto
void thread_function(void *args)
{
    ThreadArgs *threadArgs = (ThreadArgs *)args; // Argumentos da thread

    while (!should_terminate)
    {
        pthread_mutex_lock(&contador_mutex); // Bloqueia o acesso ao contador
        
        // Imprime estatísticas do aeroporto
        threadArgs->contador_pousos = total_pousos;
        threadArgs->contador_decolagens = total_decolagens;
        
        printf("\n=== Estatísticas do Aeroporto ===\n");
        printf("Total de pousos: %d\n", threadArgs->contador_pousos);
        printf("Total de decolagens: %d\n", threadArgs->contador_decolagens);
        printf("================================\n\n");
        
        pthread_mutex_unlock(&contador_mutex); // Desbloqueia o acesso ao contador
        sleep(10); // Aguarda 10 segundos antes de imprimir as estatísticas novamente
    }
    free(args);
    //return NULL; // Retorna NULL
}

// Handle no caso de interrupção (CTRL + C)
void handle_sigint(int sig)
{
    should_terminate = 1;
    
    if (arquivo != NULL)
    {
        // Adiciona uma linha em branco para separar os dados das estatísticas
        fprintf(arquivo, "\n=== Estatísticas Finais ===\n");
        fprintf(arquivo, "Total de pousos realizados,%d\n", total_pousos);
        fprintf(arquivo, "Total de decolagens realizadas,%d\n", total_decolagens);

        fclose(arquivo);
        printf("\nEstatísticas gravadas e arquivo 'avioes_pipe.csv' fechado com sucesso.\n");
    }

    pthread_join(monitor_thread, NULL);
    pthread_mutex_destroy(&contador_mutex);
    exit(0);
}

// Adicione esta função após a main() para iniciar a thread
void iniciar_contador()
{
    ThreadArgs *args = malloc(sizeof(ThreadArgs)); // Aloca memória para os argumentos da thread
    args->contador_pousos = 0;                     // Parâmetro 1
    args->contador_decolagens = 0;                 // Parâmetro 2

    // Cria a thread
    if (pthread_create(&monitor_thread, NULL, (void *) thread_function, args) != 0)
    {
        perror("Erro ao criar thread de monitoramento"); // Erro ao criar a thread
        free(args);                                      // Libera a memória alocada
        exit(1);                                         // Encerra o processo
    }
}
