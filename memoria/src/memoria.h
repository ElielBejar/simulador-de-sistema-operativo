#ifndef MEMORIA_H_
#define MEMORIA_H_

#include<stdio.h>
#include<stdlib.h>
#include<math.h>
#include<string.h>
#include<commons/log.h>
#include<commons/config.h>
#include<commons/collections/dictionary.h>
#include<commons/bitarray.h>
#include<commons/memory.h>
#include"utils/utils.h"
#include<pthread.h>
#include<unistd.h>
#include<semaphore.h>

t_log* logger;
t_config* config;

void* espacio_memoria;

int server_fd;

sem_t sem_mutex_memoria;
typedef struct {
    int cant_paginas;
    t_dictionary* frame_segun_pagina; // key = nro pagina, data = frame en memoria
}tabla_de_paginas;

t_dictionary* registro_tabla_de_paginas; // key = pid, data = tabla de paginas del proceso

t_bitarray* frames_libres;

void levantar_config(void);
void atender_cliente(int socket_cliente);

void manejarConexionCPU(int socket_CPU);
void manejarConexionKernel(int socket_Kernel);
void manejarConexionInOut(int socket_InOut);

void recibir_info_kernel(t_list *lista);
void recibir_info_cpu(t_list* lista, int conexion_cpu);
void recibir_info_io(t_list* lista, int conexion);

void ejecutar_script(char* path, char* pid);

void generar_espacio_memoria(void);
void limpiar_memoria(void);

void verificar_limite_memoria(int direccion_fisica, int size, int socket_cliente);

void escribir_memoria(int socket_cliente, int direccion_fisica, void* info, int tamanio, int pid);
void leer_memoria(int socket_cliente, int direccion_fisica, int size, int pid);

void guardar_proceso_en_memoria(char* pid);
void liberar_proceso_de_memoria(char* pid);

void redimensionar_proceso(int pid, int tamanio_nuevo, int socket_cliente);
tabla_de_paginas* ampliar_proceso(tabla_de_paginas* tab, int cant_nuevas_paginas, int socket_cliente);
tabla_de_paginas* reducir_proceso(tabla_de_paginas* tab, int cant_paginas_a_liberar, int socket_cliente);

int obtener_marco(int pid, int nro_pagina);
int obtener_siguiente_marco(int pid, int anterior_marco);
int buscar_pagina_marco(tabla_de_paginas* tabla, int marco);

void enviar_marco(int pid,int nro_pagina,int socket_cliente);
void enviar_instruccion(int pid, int program_counter, int conexion_cpu);
void esperar_tiempo_retardo(void);

typedef struct{
	char* puerto_escucha;
    char* tam_memoria;
    char* tam_pagina;
    char* path_instrucciones;
    char* retardo_respuesta;
}memoria_configuracion;

enum tipo_instruccion{
    ESCRIBIR,
    LEER,
    INSTRUCCION,
    MARCO,
    RESIZE
};

t_dictionary* diccionario_instrucciones;

memoria_configuracion memoria_config;

#endif /* CLIENT_H_ */