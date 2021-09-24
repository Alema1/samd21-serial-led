/**
 * \par Controle Serial do Led da placa SAMD21J18A
 * @author Carlos Gewehr e Vinicius Schreiner
 * @version 1.0
 */

/**
 * \mainpage Trabalho final da Disciplina de Projeto de sistemas embarcados - UFSM
 * \par Objetivo:
 *
 * Este projeto tem como intuito fazer o controle do LED embutido na placa de forma serial, utilizando threads.
 * 
 *
 *
 * \par Funcionalidades:
 *
 * -# Controle de brilho e frequencia do LED.
 * -# Operacao via porta serial.
 * -# Inclui threads do sistema FreeRTOS.
 *
 *\par Outras Informacoes:
 *
 *Vinicius Schreiner <a href="https://github.com/Alema1">GitHuB</a>
 *
 *Carlos Gewehr <a href="https://github.com/cggewehr">GitHuB</a>
 */

#include <asf.h>
#include <ctype.h>
#include <string.h>

// Prototipo do inicializador
void CriaTarefas(void);

// Prototipos das tarefas
void RecebeComando(void);
void SetaComando(void);
void Pisca(void);
void Brilha(void);

// Prototipos das funções de setup
void configure_tcc(void);
void configure_usart(void);
void configure_eeprom(void);
void configure_bod(void);

// Prototipos das funçoes auxiliares das tarefas
long Alema1map(long x, long in_min, long in_max, long out_min, long out_max);
int CalculaPeriodo(int freq);

// Configuração do PWM
#define CONF_PWM_MODULE   TCC0
#define CONF_PWM_OUTPUT   4
#define CONF_PWM_OUT_PIN  PIN_PB30E_TCC0_WO0
#define CONF_PWM_OUT_MUX  MUX_PB30E_TCC0_WO0
#define PWM_PERIOD 1000
#define DUTY_CYCLE 1 // Porcentagem de aumento/decremento
#define PWM_STEP (PWM_PERIOD *DUTY_CYCLE) / 100

struct usart_module usart_instance;
struct usart_config usart_conf;
struct tcc_module tcc_instance;			// Create a module software instance structure for the TC module to store the TC driver state while it is in use
float PWM_RATE = PWM_PERIOD;

// Setup TCC
void configure_tcc() {
	struct tcc_config config_tcc;
	tcc_get_config_defaults(&config_tcc, CONF_PWM_MODULE);
	config_tcc.counter.period = PWM_PERIOD;   // Period register
	config_tcc.compare.wave_generation = TCC_WAVE_GENERATION_SINGLE_SLOPE_PWM;
	config_tcc.compare.match[0] = PWM_PERIOD;
	//config_tcc.wave.wave_polarity[0] = TCC_WAVE_POLARITY_1;
	config_tcc.pins.enable_wave_out_pin[CONF_PWM_OUTPUT] = true;
	config_tcc.pins.wave_out_pin[CONF_PWM_OUTPUT]        = CONF_PWM_OUT_PIN;
	config_tcc.pins.wave_out_pin_mux[CONF_PWM_OUTPUT]    = CONF_PWM_OUT_MUX;
	
	tcc_init(&tcc_instance, CONF_PWM_MODULE, &config_tcc);
	tcc_enable(&tcc_instance);
	
}

// Setup USART
void configure_usart(){
	
	usart_get_config_defaults(&usart_conf);
	usart_conf.baudrate    = 9600;
	usart_conf.mux_setting = EDBG_CDC_SERCOM_MUX_SETTING;
	usart_conf.pinmux_pad0 = EDBG_CDC_SERCOM_PINMUX_PAD0;
	usart_conf.pinmux_pad1 = EDBG_CDC_SERCOM_PINMUX_PAD1;
	usart_conf.pinmux_pad2 = EDBG_CDC_SERCOM_PINMUX_PAD2;
	usart_conf.pinmux_pad3 = EDBG_CDC_SERCOM_PINMUX_PAD3;
	stdio_serial_init(&usart_instance, EDBG_CDC_MODULE, &usart_conf);
		
	usart_enable(&usart_instance);
}

// Setup MVN
void configure_eeprom(void){
	
	// Setup EEPROM emulator service
	enum status_code error_code = eeprom_emulator_init();

//! [check_init_ok]
	if (error_code == STATUS_ERR_NO_MEMORY) {
		while (true) {
			/* No EEPROM section has been set in the device's fuses */
		}
	}
//! [check_init_ok]
//! [check_re-init]
	else if (error_code != STATUS_OK) {
		/* Erase the emulated EEPROM memory (assume it is unformatted or
		 * irrecoverably corrupt) */
		eeprom_emulator_erase_memory();
		eeprom_emulator_init();
	}
//! [check_re-init]
}

// Setup Brown Out Detector
void configure_bod(void){

	struct bod_config config_bod33;
	
	bod_get_config_defaults(&config_bod33);
	config_bod33.action = BOD_ACTION_INTERRUPT;
	/* BOD33 threshold level is about 3.2V */
	config_bod33.level = 48;
	bod_set_config(BOD_BOD33, &config_bod33);
	bod_enable(BOD_BOD33);

	SYSCTRL->INTENSET.reg = SYSCTRL_INTENCLR_BOD33DET;
	system_interrupt_enable(SYSTEM_INTERRUPT_MODULE_SYSCTRL);

}

long Alema1map(long x, long in_min, long in_max, long out_min, long out_max) {
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

int CalculaPeriodo(int freq){
	return (1/freq)*500;
}

// Variaveis globais a serem usadas pelas threads
int brilho;                          // Valor de intensidade do brilho do LED (0 a 100)%
int frequencia;                      // Frequencia a qual o LED deve piscar
int brilhaFlag;                      // Sinaliza que o LED deve brilhar a uma determinada intensidade
int piscaFlag;                       // Sinaliza que o LED deve piscar a uma determinada frequencia
int piscaQtd;                        // Quantidade de vezes por qual o LED deve piscar
int newBufferAvailable = 0;          // Sinaliza novo comando a ser interpretado
char buffer[55];                     // Buffer compartilhado por onde os comandos são passados
char buffer2[55];
static xSemaphoreHandle mutex;       // Mutex para sincronizacao
uint8_t page_data[EEPROM_PAGE_SIZE]; // Buffer para leitura de EEPROM emulada

// Handles das tarefas
xTaskHandle SetaComandoHandle;
xTaskHandle RecebeComandoHandle;
xTaskHandle PiscaHandle;
xTaskHandle BrilhaHandle;

int main(){
	
	// Setup da placa
	system_init();
	configure_tcc();
	configure_usart();
	configure_eeprom();
	configure_bod();
	
	// Inicializa variaveis globais
	brilho = 0;
	frequencia = 0;
	brilhaFlag = 0;
	piscaFlag = 0;

	printf("Criando tarefas\n");

	// Cria tarefas e inicializa suas variaveis
	CriaTarefas();

	printf("Inicializando escalonador do freeRTOS\n");

	// Inicia escalonador do freeRTOS
	vTaskStartScheduler();

	printf("Tarefas executando\n");

	do {
		// Executa tarefas concorrentemente
	} while (true);
}

void CriaTarefas(){

	int rc;

	// Inicializa mutex
	mutex = xSemaphoreCreateMutex();
	
	// Cria tarefas
	rc = xTaskCreate(RecebeComando, (const char *) "RecebeComando", configMINIMAL_STACK_SIZE + 1000, NULL, tskIDLE_PRIORITY + 1, &RecebeComandoHandle);
	if(rc == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY){
		printf("Nao foi possivel inicializar a tarefa RecebeComando\n");
	}
	
	rc = xTaskCreate(SetaComando, (const char *) "SetaComando", configMINIMAL_STACK_SIZE + 500, NULL, tskIDLE_PRIORITY + 1, &SetaComandoHandle);
	if(rc == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY){
		printf("Nao foi possivel inicializar a tarefa SetaComando\n");
	}
	
	rc = xTaskCreate(Brilha, (const char *) "Brilha", configMINIMAL_STACK_SIZE + 100, NULL, tskIDLE_PRIORITY + 1, &BrilhaHandle);
	if(rc == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY){
		printf("Nao foi possivel inicializar a tarefa Brilha\n");
	}
	
	rc = xTaskCreate(Pisca, (const char *) "Pisca", configMINIMAL_STACK_SIZE + 100, NULL, tskIDLE_PRIORITY + 2, &PiscaHandle);
	if(rc == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY){
		printf("Nao foi possivel inicializar a tarefa Pisca\n");
	}
	
	printf("Tarefas criadas\nSuspendendo tarefas\n");
	
	// Suspende tarefas até serem acordas
	vTaskSuspend(SetaComandoHandle);
	vTaskSuspend(BrilhaHandle);
	vTaskSuspend(PiscaHandle);
	
	printf("Programa pronto para ser inicializado\n");
	
}

// Receives a string through UART containing the command to be executed and its arguments
void RecebeComando(){

	int i = 0;
	char currentChar;

	xSemaphoreTake(mutex, portMAX_DELAY);
	//printf("RecebeComando inicializada\n");
	xSemaphoreGive(mutex);

	while(1){
		
		// Get command from terminal through UART
		xSemaphoreTake(mutex, portMAX_DELAY);
		
		// Atomically receives command
		printf("%s", "\nComando>");
		while(1){
			currentChar = getchar();
			printf("%c", currentChar);
			
			if (currentChar == '\r'){ // Ignores \r
				continue; 
			} else if (currentChar == '\n'){ // Terminates buffer string
				buffer[i] = '\0';
				break;
			} else { 
				buffer[i] = currentChar;
				i++;
			}
		}
		
		i = 0;
		newBufferAvailable = 1;
		
		// Gives up mutex
		xSemaphoreGive(mutex);
		
		// Wakes up SetaComando and waits until command is processed
		vTaskResume(SetaComandoHandle);
		vTaskDelay(1);
		
		// Waits for command to be processed
		while(1){
			xSemaphoreTake(mutex, portMAX_DELAY);
			if (newBufferAvailable == 0){
				// Cleans buffer
				for(i = 0 ; i < 55 ; i++){
					buffer[i] = '\0';
				}
				i = 0;
				xSemaphoreGive(mutex);
				break;
			}
			xSemaphoreGive(mutex);
		}
		
		//xSemaphoreTake(mutex, portMAX_DELAY);
		//printf("\nEsperando SetaComando processar o comando\n");
		//xSemaphoreGive(mutex);
		//vTaskSuspend(RecebeComandoHandle);
		
		// Only reaches this point when task is woken up by SetaComando
		
	}

}

// Executes the command received through UART by thread RecebeComando
void SetaComando(){

	int i, j = 0;
	uint8_t page_line_counter;
	char* args[3];
	
	xSemaphoreTake(mutex, portMAX_DELAY);
	//printf("SetaComando inicializada\n");
	xSemaphoreGive(mutex);

	while(1){

		// Waits for wake-up from RecebeComando
		
		// Begins interpreting given command
		xSemaphoreTake(mutex, portMAX_DELAY);
		vTaskSuspend(RecebeComandoHandle);
		
			// Formata o buffer 
		
		// Convert all chars to lowercase
		for(i = 0 ; i < 55 ; i++){
			if(buffer[i] == '\n'){
				buffer[i] = ' '; // If given command has no arguments, inserts ' ' separator so strtok() can do its thing
			}
			buffer[i] = tolower(buffer[i]);
		}

		// Set arguments (finds ' ' between command and its arguments)
		args[0] = strtok(buffer, " ");
		args[1] = strtok(NULL, " ");
		args[2] = strtok(NULL, " ");
		
			// Interpreta comando

		// Compare args[0] with specific command literals to determine which command to execute
		if( strcmp(args[0], "blink") == 0 || strcmp(args[0], "pisca") == 0 ){
			
			if( atoi(args[1]) > 33){
				printf("AVISO: FREQUENCIA DESEJADA MAIOR DO QUE A FREQUENCIA SUPORTADA\n");
			}
			
			vTaskSuspend(BrilhaHandle);
			frequencia = atoi(args[1]);
			piscaQtd = atoi(args[2]);
			brilhaFlag = 0;
			piscaFlag = 1;
			vTaskResume(PiscaHandle);
		}

		else if( strcmp(args[0], "brightness") == 0 || strcmp(args[0], "brilho") == 0 || strcmp(args[0], "brilha") == 0){
			
			// If LED brightness value is between 0 and 100, sets it to that value, else, prints error msg
			if(atoi(args[1]) <= 100 && atoi(args[1]) > 0){
				vTaskSuspend(PiscaHandle);
				brilho = atoi(args[1]);
				piscaFlag = 0;
				brilhaFlag = 1;
				vTaskResume(BrilhaHandle);
			} else {
				printf("Insira um valor valido (entre 0 e 100) para o valor de brilho desejado\n");
			}
		}
		
		else if( strcmp(args[0], "print") == 0 || strcmp(args[0], "mostrar") == 0 ){
			
			if( strcmp(args[1], "brightness") == 0 || strcmp(args[1], "brilho") == 0 ){
				
				// Prints LED brightness
				if(brilhaFlag == 1){
					printf("Brilho atual do LED: %d%%\n", brilho);
				} else {
					printf("LED nao esta programado para brilhar a uma instensidade fixa\n");
				}
			}
			
			else if( strcmp(args[1], "freq") == 0 ){
				
				// Prints LED frequency
				if(piscaFlag == 1){
					printf("Ultima frequencia do LED: %d\n", frequencia);
				} else {
					printf("LED nao foi programado para piscar\n");
				}						
			}
				
			else if( strcmp(args[1], "log") == 0 ){
				
				// Prints log
				eeprom_emulator_read_page(0, page_data);
				page_line_counter = page_data[0];
				for(i = 1 ; i < page_line_counter ; i++){
					eeprom_emulator_read_page(i, page_data);
					
					for (j = 0 ; j < 56 ; j++){
						printf("%c", page_data);
					}
					
					printf("\n");
				}
			}
		}

		else if( strcmp(args[0], "exit") == 0 || strcmp(args[0], "sair" ) == 0){
			printf("Saindo do programa\n");
			tcc_set_compare_value(&tcc_instance, 0, 1001);
			exit(EXIT_SUCCESS);
		}

		else if( strcmp(args[0], "help") == 0 || strcmp(args[0], "ajuda" ) == 0){
			printf("Comandos validos:");
			printf("\n\tBlink/Pisca       : LED pisca com a frequencia desejada por uma quantidade de vezes (pisca <frequencia> <qtd>)");
			printf("\n\tBrightness/Brilha : LED brilha com a intensidade desejada (0%% a 100%%) (brilha <instensidade>)");
			printf("\n\tPrint             : Exibe valor desejado (print <freq, brilho, brightness, log>");
			printf("\n\tExir/Sair         : Fecha o programa");
			printf("\n\tHelp/Ajuda        : Exibe novamente esse menu\n");
		}
		
		else if( strcmp(args[0], "reset") == 0){
			
			if(strcmp(args[1], "brilho") == 0){
				
				// Resets brightness
				brilhaFlag = 0;
				brilho = 0;
				vTaskSuspend(BrilhaHandle);
				tcc_set_compare_value(&tcc_instance, 0, 1001);
				
			} else if(strcmp(args[1], "freq") == 0){
				
				// Resets blinking frequency
				piscaFlag = 0;
				frequencia = 0;
				vTaskSuspend(PiscaHandle);
				tcc_set_compare_value(&tcc_instance, 0, 1001);
				
			} else if(strcmp(args[1], "log") == 0){
				
				// Resets log line counter
				eeprom_emulator_read_page(0, page_data);
				page_data[0] = 0;
				eeprom_emulator_write_page(0, page_data);
				eeprom_emulator_commit_page_buffer();
			}
			
		}

		else{
			printf("Insira um comando válido (Insira \"Help/Ajuda\" para saber mais)\n");
		}
		
		//eeprom_emulator_read_page(0, page_data);
		//printf("Page_data[0]: %d\n", page_data[0]);
		//page_data[0]++;
		//eeprom_emulator_write_page(0, page_data);
		//eeprom_emulator_commit_page_buffer();
		//
		
			// Salva na memoria EEPROM o buffer
		
		// Le primeira pagina da memoria EEPROM emulada e armazena no buffer "page_data"
		eeprom_emulator_read_page(0, page_data);
		page_line_counter = page_data[0];
		
		// Se contador de paginas == 255 (valor maximo para inteiro de 8 bits), printa aviso
		//if(page_line_counter == 255){
			//printf("AVISO: ULTIMA LINHA DO LOG, PROXIMO COMANDO SOBREESCREVERA PRIMEIRA LINHA\n");
		//}
		
		// Primeira pagina aponta para a proxima pagina a ser escrita
		page_data[0]++;
		if(page_data[0] == 0){
			page_data[0] = 1; // Nao sobreescreve contador
		}
		page_line_counter = page_data;
		eeprom_emulator_write_page(0, page_data);

		// Limpa dados anteriores da pagina
		eeprom_emulator_read_page(page_line_counter, buffer);
		for(i = 0 ; i < 56 ; i++){
			page_data[i] = '\0';
		}
		
		// Copia buffer para memoria EEPROM, na posição determinada pelo primeiro elemento da pagina 0 (page_line_counter)
		strncpy(page_data, buffer, 55);
		eeprom_emulator_write_page(page_line_counter, page_data);
		
		// Escreve definitivamente na memoria EEPROM o contador de paginas e o comando atual
		eeprom_emulator_commit_page_buffer();

		// Signals command has been executed
		newBufferAvailable = 0;
		vTaskResume(RecebeComandoHandle);
		xSemaphoreGive(mutex);
		vTaskDelay(1);
	}

}

void Pisca(){
	
	// "piscaFlag" é variavel global declarada em main.c
	int i = 0;
	
	xSemaphoreTake(mutex, portMAX_DELAY);
	//printf("Pisca inicializada\n");
	xSemaphoreGive(mutex);
	
	while(1){
		
		if(piscaFlag == 0){
			vTaskSuspend(PiscaHandle);
		}
		
		xSemaphoreTake(mutex, portMAX_DELAY);
		//printf("LED piscando com frequencia de %d Hz", frequencia);
			
		// Alterna entre brilho maximo e brilho minimo na frequencia desejada até a quantidade de vezes a piscar ser atingida
		for(i = 0 ; i < piscaQtd ; i++){
			
			// Seta brilho para intensidade maxima
			tcc_set_compare_value(&tcc_instance, 0, 0);
			//delay_ms( (int) (1/frequencia)*500);
			
			// Suspende tarefa por metade do periodo relativo a frequencia desejada
			vTaskDelay( 1*500/(frequencia*portTICK_PERIOD_MS) );
			
			// Seta brilho para intensidade minima
			tcc_set_compare_value(&tcc_instance, 0, 1001);
			//delay_ms( (int) (1/frequencia)*500);
			
			// Suspende tarefa por metade do periodo relativo a frequencia desejada 
			vTaskDelay( 1*500/(frequencia*portTICK_PERIOD_MS) );
			
		}
		
		xSemaphoreGive(mutex);
		vTaskSuspend(PiscaHandle);
	}
	
}

void Brilha(){
	
	// "Brilho" e "brilhaFlag" sao variaveis globais declaradas em main.c
	xSemaphoreTake(mutex, portMAX_DELAY);
	//printf("Brilha inicializada\n");
	xSemaphoreGive(mutex);
	
	while(1){
		
		xSemaphoreTake(mutex, portMAX_DELAY);
		//printf("LED brilhando com instensidade de %d %%", brilho);

		// LED brilha a uma certa porcentagem de luminosidade
		if(brilho == 0){
			tcc_set_compare_value(&tcc_instance, 0, 1001);	//Brilho zero
		} else {
			tcc_set_compare_value(&tcc_instance, 0, Alema1map(brilho,1,100,1000,1));
		}
		
		xSemaphoreGive(mutex);
		vTaskDelay(1);
	}
}