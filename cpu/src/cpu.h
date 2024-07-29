#ifndef CPU_H_
#define CPU_H_

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<commons/log.h>
#include<commons/config.h>
#include<commons/collections/queue.h>
#include"utils/utils.h"
#include<pthread.h>
#include<unistd.h>
#include<semaphore.h>

t_log* logger_interrupt;
t_log* logger_dispatch;
t_log* logger;
t_config* config;

void levantar_config(void);
void iterator(char* value);
void* manejarPuertoDispatch(void *arg);
void* manejarPuertoInterrupt(void *arg);

void iniciar_hilos_cpu(void);
void recibir_info(t_list* lista);

pthread_t puertos_cpu[4];
typedef struct{
	char* ip_memoria;
    char* puerto_memoria;
    char* puerto_escucha_dispatch;
    char* puerto_escucha_interrupt;
    char* cantidad_entradas_tlb;
    char* algoritmo_tlb;
}cpu_configuracion;

cpu_configuracion cpu_config;

uint32_t identificador = 1;

int conexion_memoria;

sem_t sem_interrupt;
sem_t sem_instrucciones;
sem_t sem_respuesta_kernel;
sem_t sem_fin_interrupcion;
sem_t sem_esperar_interrupcion;
sem_t sem_iniciar_nuevo_proceso;
sem_t sem_llego_paquete_interrupt;
t_list* lista_interrupt;

bool es_interrupcion_valida;
bool es_interrupcion_por_quantum;
int conexion_kernel;
bool continua_ejecutando;
bool es_primera_interrupcion_recibida = true;
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
}registros;

typedef struct {
	char* pid;
	int pagina;
	int marco;
}base_tlb;

t_list* tablas_tlb;

pcb* pcb_proceso;
bool proceso_vacio = false;

void recibir_info(t_list* lista);
void exit_prima();
void pedir_instruccion_a_memoria();
char* recibir_instruccion_memoria();
void hacer_instruccion (char* instruccion);

void manejar_proceso();

void sum(char* registro_destino, char* registro_origen);
void sub(char* registro_destino, char* registro_origen);
void set(char* registro, char* valor);
void jnz(char* registro, char* instruccion);
void set(char* registro, char* valor);
void io_gen_sleep(char* instruccion, char* interfaz, char* unidades_de_trabajo);
void io_stdout_write_o_stdin_read(char* instruccion, char* interfaz, char* registro_direccion, char* registro_tam);
void io_fs_create_o_delete(char* instruccion, char* interfaz, char* nombre_archivo);
void io_fs_truncate(char* instruccion, char* interfaz, char* nombre_archivo, char* registro_tam);
void io_fs_write_o_read(char* instruccion, char* interfaz, char* nombre_archivo, char* registro_direccion, char* registro_tam, char* registro_puntero_archivo);
void wait(char* instruccion, char* recurso);
void signal(char* instruccion, char* recurso);
void copy_string(char* tam);

int obtener_valor(char* un_registro);
void setear_valor(char* registro, int valor);
int obtener_tamanio_registro(char* un_registro);

void manejar_pedido_interrupcion();
void enviar_contexto_de_ejecucion();

void empezar_a_ejecutar_proceso();
void esperar_su_desalojo();

char* leer_valor_memoria(char* registro_direccion, char*registro_datos);
char* leer_valor_memoria_prima(char* puntero_direccion, char* tam_lectura);

int obtener_marco_memoria(int nro_pagina);
int obtener_pagina(int direccion_logica);
int obtener_direccion_fisica(int direccion_logica, char* pid);
void reemplazar_en_tlb(char* pid, int marco, int nro_pagina);
int buscar_en_tlb(char* pid, int pagina, bool imprimo);

void escribir_en_memoria(void* dato, int tamanio, int direccion_fisica_aux, bool es_string);

void resize(char* nuevo_tam);

int recibir_marco_memoria();
char* recibir_leido_memoria();

void inicializar_tlb();

void mov_in(char* instruccion, char* registro_datos, char* registro_direccion);
void mov_out(char* instruccion, char* registro_direccion, char* registro_datos);



enum tipo_instruccion{
    ESCRIBIR,
    LEER,
    INSTRUCCION,
    MARCO, 
	RESIZE
};

int tam_pagina;

#endif /* CLIENT_H_ */