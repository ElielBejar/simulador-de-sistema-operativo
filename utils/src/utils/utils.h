#ifndef UTILS_H_
#define UTILS_H_

#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<unistd.h>
#include<netdb.h>
#include<commons/collections/list.h>
#include<commons/log.h>
#include<commons/string.h>
#include<commons/config.h>
#include<string.h>
#include<assert.h>

/**
* @fn    decir_hola
* @brief Imprime un saludo al nombre que se pase por parámetro por consola.
*/

#define IP "127.0.0.1"

typedef enum
{
	MENSAJE,
	PAQUETE
}op_code;

typedef struct
{
	int size;
	void* stream;
} t_buffer;

typedef struct
{
	op_code codigo_operacion;
	t_buffer* buffer;
} t_paquete;

typedef struct{
	uint32_t PC;
	uint8_t AX;
	uint8_t BX;
	uint8_t CX;
	uint8_t DX;
	uint32_t EAX;
	uint32_t EBX;
	uint32_t ECX;
	uint32_t EDX;
	uint32_t SI;
	uint32_t DI;
}registros_cpu;
typedef struct{
	char* pid;
    int program_counter;
    float quantum;
    registros_cpu registros;
	int nro_desalojos;
}pcb;


void decir_hola(char* quien);

extern t_log* logger;

t_log* iniciar_logger(char *file, char *process_name);
t_config* iniciar_config(char *path, t_log* logger);
void terminar_programa(int conexion, t_log* logger, t_config* config);
int iniciar_servidor(char*);
int esperar_cliente(int);
void* recibir_buffer(int*, int);
t_list* recibir_paquete(int);
void recibir_mensaje(int);
void recibir_mensaje_prima(int, t_log*);
char* leer_mensaje(int);
int recibir_operacion(int);

void* serializar_paquete(t_paquete* paquete, int bytes);


int crear_conexion(char* ip, char* puerto);
void enviar_mensaje(char* mensaje, int socket_cliente);
t_paquete* crear_paquete(void);
void agregar_a_paquete(t_paquete* paquete, void* valor, int tamanio);
void enviar_paquete(t_paquete* paquete, int socket_cliente);
void liberar_conexion(int socket_cliente);
void eliminar_paquete(t_paquete* paquete);
int realizar_handshake(char *ip, char *puerto, char *quien_realiza_solicitud, char *con_quien_se_conecta);

void agregar_pcb_a_paquete(t_paquete* paquete, pcb* pcb_a_enviar);
void agregar_registros_a_paquete(registros_cpu registros, t_paquete* paquete);
void cargar_pcb(pcb* nuevo_pcb, t_list* lista);
void cargar_registros(pcb* pcb, t_list* lista);
void levantar_pcb(pcb* pcb);

void element_destroy(void* self);
void list_destroy_with_elements(void* list);

#endif
