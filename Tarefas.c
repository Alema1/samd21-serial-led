#include <asf.h>
#include <ctype.h>
#include <string.h>

// Variaveis globais das tarefas
char buffer[160];                // Buffer compartilhado
int newBufferAvailable = 0;      // Signals new command is available when == 1
static xSemaphoreHandle mutex;   // Mutex for synchronization

// Prototipo do inicializador
void CriaTarefas(void);

// Prototipos das tarefas
void RecebeComando(void);
void SetaComando(void);
void Pisca(void);
void Brilha(void);

void CriaTarefas(void){

	// Inicializa mutex
	mutex = xSemaphoreCreateMutex();
	
	// Cria tarefas
	xTaskCreate(RecebeComando, (const char *) "RecebeComando", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
	xTaskCreate(SetaComando, (const char *) "SetaComando", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
	xTaskCreate(Brilha, (const char *) "Brilha", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3, NULL);
	xTaskCreate(Pisca, (const char *) "Pisca", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3, NULL);
	
}

// Receives a string through UART containing the command to be executed and its arguments
void RecebeComando(void){

	for(;;){
		
		// Get command from terminal through UART
		xSemaphoreTake(mutex, portMAX_DELAY);
		
		// Atomically receives command
		printf("%s", "\nComando>");
		scanf("%s", &buffer);
		newBufferAvailable = 1;
		
		// Gives up mutex
		xSemaphoreGive(mutex);
		
		for(;;){
			
			// Lock mutex
			xSemaphoreTake(mutex, portMAX_DELAY);
			
			// If command has been executed, waits for a new command
			if(newBufferAvailable == 0){
				xSemaphoreGive(mutex);
				break;
			}
			
			// Gives up mutex
			xSemaphoreGive(mutex);
		}
		
	}

}

// Executes the command received through UART by thread RecebeComando
void SetaComando(void){

	int i = 0;
	char* argPtr;
	char* args[3];

	args[0] = malloc(80);
	args[1] = malloc(80);
	args[2] = NULL;

	for(;;){

		// Waits for command from terminal
		for(;;){
			
			// Lock mutex
			xSemaphoreTake(mutex, portMAX_DELAY);
			
			if(newBufferAvailable == 1){
				xSemaphoreGive(mutex);
				break;
			}
			
			// Gives up mutex
			xSemaphoreGive(mutex);
		}
		
		// Begins executing given command
		xSemaphoreTake(mutex, portMAX_DELAY);

		// Convert all chars to lowercase
		for(i = 0 ; i < 160; i++){
			if(buffer[i] == '\n'){
				buffer[i] = ' '; // If given command has no arguments, inserts ' ' separator so strtok() can do its thing
			}
			buffer[i] = tolower(buffer[i]);
		}

		// Set arguments (finds ' ' between command and its arguments)
		args[0] = strtok(buffer, " ");
		args[1] = strtok(NULL, " ");

		// Compare args[0] with specific command literals to determine which command to execute
		if(strcmp(args[0], "blink") == 0 || strcmp(args[0], "pisca") == 0){
			frequencia = atoi(args[1]);
			brilhaFlag = 0;
			piscaFlag = 1;
		}

		else if(strcmp(args[0], "brightness") == 0 || strcmp(args[0], "brilho") == 0){
			brilho = atoi(args[1]);
			piscaFlag = 0;
			brilhaFlag = 1;
		}

		else if(strcmp(args[0], "exit") == 0 || strcmp(args[0], "sair") == 0){
			printf("Saindo do programa\n");
			exit(EXIT_SUCCESS);
		}

		else if(strcmp(args[0], "help") == 0 || strcmp(args[0], "ajuda") == 0){
			printf("Comandos validos:");
			printf("\n\tBlink/Pisca       : LED pisca com a frequencia desejada (pisca <frequencia>)");
			printf("\n\tBrightness/Brilha : LED brilha com a intensidade desejada (0%% a 100%%) (brilha <instensidade>)");
			printf("\n\tExir/Sair         : Fecha o programa");
			printf("\n\tHelp/Ajuda        : Exibe novamente esse menu");
		}

		else{
			printf("Insira um comando válido (Insira \"Help/Ajuda\" para saber mais)");
		}

		// Signals command has been executed
		newBufferAvailable = 0;
		xSemaphoreGive(mutex);
	}

	// Only reaches this point when exit command is issued
	//return EXIT_SUCCESS;
}

void Pisca(void){
	
	// "piscaFlag" é variavel global declarada em main.c
	
	while(1){
		
		if(piscaFlag == 1){
			// Alterna entre brilho maximo e brilho minimo na frequencia desejada
			tcc_set_compare_value(&tcc_instance, 0, Alema1map(brilho,1,100,1000,1));
			delay_ms((1/frequencia)*500);
			tcc_set_compare_value(&tcc_instance, 0, 1001);
			delay_ms((1/frequencia)*500);
		}
	}
}

void Brilha(void){
	
	// "Brilho" e "brilhaFlag" sao variaveis globais declaradas em main.c
	
	while(1){
		
		if(brilhaFlag == 1){
			// LED brilha a uma certa porcentagem de luminosidade
			if(brilho == 0){
				tcc_set_compare_value(&tcc_instance, 0, 1001);	//Brilho zero
				} else {
				tcc_set_compare_value(&tcc_instance, 0, Alema1map(brilho,1,100,1000,1));
			}
			
		}
	}
}