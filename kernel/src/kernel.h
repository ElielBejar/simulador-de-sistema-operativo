#ifndef KERNEL_H_
#define KERNEL_H_

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<commons/log.h>
#include<semaphore.h>
#include<commons/string.h>
#include<commons/config.h>
#include<commons/collections/queue.h>
#include<commons/temporal.h>
#include<readline/readline.h>
#include<readline/history.h>
#include<pthread.h>
#include<time.h>
#include<sys/time.h>
#include"utils/utils.h"
#include<commons/collections/dictionary.h>

int contador_pid = 1;
int server_fd;

sem_t sem_ejecutar;
sem_t sem_planificacion;

sem_t sem_procesos_en_ready;
sem_t sem_procesos_en_ready_m;

sem_t sem_esperar_instruccion;
sem_t sem_pausa_conteo_quantum;

sem_t sem_desalojo_quantum;
sem_t sem_verificar_desalojo_quantum;

sem_t sem_desalojo_planificacion;
sem_t termino_la_instruccion;
sem_t sem_replanificacion_quantum;

sem_t sem_multiprogramacion;
sem_t sem_hay_procesos_en_new;
sem_t sem_caso_exit_usuario;
sem_t sem_caso_exit_usuario_p;

sem_t mutex_lista_estados;

enum{
	EJECUTAR_SCRIPT,
	INICIAR_PROCESO,
	FINALIZAR_PROCESO,
	DETENER_PLANIFICACION,
	INICIAR_PLANIFICACION,
	PROCESO_ESTADO,
};

bool es_primer_proceso = true;
typedef struct{
	char* puerto_escucha;
	char* ip_memoria;
	char* puerto_memoria;
	char* ip_cpu;
	char* puerto_cpu_dispatch;
	char* puerto_cpu_interrupt;
	char* algoritmo_planificacion;
	char* quantum;
	char* recursos;
	char* instancias_recursos;
	char* grado_multiprogramacion;
}kernel_configuracion;

kernel_configuracion kernel_config;
t_log* logger;
t_config* config;
pthread_t hilos_kernel[5];
uint32_t identificador = 2;
int grado_multiprogramacion;
int conexion_memoria;
int conexion_cpu_dispatch;
int conexion_cpu_interrupt;
int conexion_io;
t_queue* cola_new;
t_queue* cola_ready;
t_queue* cola_ready_m;
t_queue* cola_exit;
t_queue* cola_exec;
t_list* colas_block_recursos;
t_list* colas_block_io;
t_dictionary* diccionario_conexiones_io;
t_dictionary* diccionario_instrucciones_permitidas_io;

t_list* lista_estados;
t_list* lista_estados_logueables;

t_list* lista_pids_ready;
t_list* lista_pids_ready_m;

t_list* lista_recursos_retenidos;

bool esperar_fin_instruccion = false;
bool desaloja_por_instruccion;
bool fin_por_quantum = false;
bool desaloja_por_usuario = false;
typedef struct {
	t_queue*cola;
	char*identificador;
	sem_t habilitacion_cola;
	sem_t sem_hay_proceso;
}cola_block_io;

typedef struct {
	t_queue*cola;
	char*identificador;
	int instancias_recurso;
	sem_t sem_mutex_recurso;
}cola_block_recurso;

typedef struct {
	char* pid;
	char* recurso;
}recursos_retenidos;

void recibir_info(char*, char*);

void inicializar(t_log*);

void iniciar_semaforos();

void iterator(char* value);

void levantar_config(void);

void* comenzar_servidor(void *arg);
void* comenzar_consola_interactiva(void* arg);
void* administrar_procesos_new(void* arg);
void* planificacion_de_procesos(void* arg);

char** separar_instruccion(char* cadena, char* separador);

void iniciar_hilos_kernel(void);
void iniciar_colas_procesos(void);

void ejecutar_script(char* path);

void mandar_pedido(int conexion, char* motivo, char* posible_argumento, char* pid);

pcb* generar_nuevo_proceso(void);

void agregar_registros_a_paquete(registros_cpu registros, t_paquete* paquete);

void mostrar_cola_ready(char* tipo_de_ready);

void* atender_interfaz(int conexion);
//pcb* recibir_desalojo(t_list* lista);
//pcb* recibir_desalojo_cpu();
void recibir_respuesta_cpu(void*arg);
void atender_solicitud_cpu(t_list* lista, pcb* pcb_desalojo,char* motivo);
void io_gen_sleep(char* instruccion, char* interfaz, char* unidades_de_trabajo, char*pid_io);
void io_stdout_write_o_stdin_read(char* instruccion, char* interfaz, char* registro_direccion, char* registro_tam, char*pid_io);
void io_fs_create_o_delete(char* instruccion, char* interfaz, char* nombre_archivo, char*pid_io);
void io_fs_truncate(char* instruccion, char* interfaz, char* nombre_archivo, char* registro_tam, char*pid_io);
void io_fs_write_o_read(char* instruccion, char* interfaz, char* nombre_archivo, char* registro_direccion, char* registro_tam, char* registro_puntero_archivo, char*pid_io);

bool wait(char *recurso, pcb *pcb_recurso, char* motivo);
bool wait_recurso(char* nombre_recurso, cola_block_recurso* cola);

bool signal(char* recurso, char* pcb);
bool signal_recurso(char* nombre_recurso, cola_block_recurso* cola);

void exit_prima(pcb* pcb_exit, char* motivo_exit);
void liberar_recursos(char* pid_exit);
void pedido_de_desalojo(char* motivo, char* pid);

cola_block_io* encontrar_cola(char* interfaz);
cola_block_recurso* encontrar_cola_recurso(char* nombre_recurso);

bool cumple_interfaz(char* interfaz, char* instruccion);
bool existe_el_recurso (char* nombre_recurso);
void quitar_de_lista_recursos_retenidos(char* nombre_recurso, char* pid);

void iniciar(pcb* pcb_desalojo, char* motivo);
void replanificar (char* motivo);

void bloquear_i(char* interfaz, pcb* pcb);
void pedido_de_desalojo_prima(char *motivo, char* pid);
void cambiar_estado(char* pcb_estado, char* nuevo_estado);
void cambiar_estado_logueable(char* estado_viejo, char* nuevo_estado, char* pid);

pcb* encontrar_pcb(char* pid);
pcb* pcb_en_cola(t_queue* cola, char* pid);

void mostrar_lista_estados_logueables();

void limpiar_kernel();
void limpiar_cola(t_queue* cola);

#endif /* CLIENT_H_ */