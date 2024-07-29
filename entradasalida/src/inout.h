#ifndef INOUT_H_
#define INOUT_H_

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<dirent.h>
#include<math.h>
#include<commons/log.h>
#include<semaphore.h>
#include<commons/string.h>
#include<commons/config.h>
#include<commons/bitarray.h> 
#include<commons/collections/queue.h>
#include<commons/temporal.h>
#include<readline/readline.h>
#include<readline/history.h>
#include<pthread.h>
#include<sys/time.h>
#include<sys/mman.h>
#include"utils/utils.h"

#define INSTRUCCIONES_GENERICA "IO_GEN_SLEEP"
#define INSTRUCCIONES_STDIN "IO_STDIN_READ"
#define INSTRUCCIONES_STDOUT "IO_STDOUT_WRITE"
#define INSTRUCCIONES_DIALFS "IO_FS_CREATE IO_FS_DELETE IO_FS_TRUNCATE IO_FS_WRITE IO_FS_READ"
#define TIEMPO_UNIDAD_TRABAJO_DIALFS 1



t_log* logger;
uint32_t identificador = 3;

typedef struct{
	char* tipo_interfaz;
    int tiempo_unidad_trabajo;
    char* ip_kernel;
    char* puerto_kernel;
    char* ip_memoria;
    char* puerto_memoria;
    char* path_base_dialfs;
    int block_size;
    int block_count;
    int retraso_compactacion;
}inout_configuracion;

//tipo de dato interfaz para iniciar una interfaz
typedef struct{
    char* nombre;
    t_config* config;
}interfaz;

char* instrucciones_permitidas;

enum tipo_interfaz{
    GENERICA,
    STDIN,
    STDOUT,
    DIALFS,
    DEFAULT
};

//enums para los codigos de operación que se le envía a memoria
enum{
    PETICION_ESCRITURA,
    PETICION_LECTURA
};

interfaz inout;
inout_configuracion inout_config;
int conexion_kernel;
int conexion_memoria;//solo se usa si no es una interfaz generica

//solo se usa si es un dialfs

t_bitarray* bitmap;

t_list* lista_archivos;

void* datos_bloques;//los datos que se escriben en los bloques

char* datos_bitmap;//memoria mapeada con el archivo bitmap
char* nombre_archivo_bitmap;//se libera cuando termina de levantarse el filesystem
char* nombre_archivo_bloques;//se libera cuando termina de levantarse el filesystem
typedef struct {
    char* nombre_archivo;
    t_config* config;
}archivo;

//---------------------------

archivo* obtener_archivo_por_bloque_inicial(int bloque_inicial);
archivo* obtener_archivo_por_nombre(char* nombre);

char* crear_nombre_archivo(char* nombre, char* extension);
char* obtener_path_completo(char* nombre_archivo);

enum tipo_interfaz  obtener_tipo(char* tipo);

FILE* crear_archivo(char* nombre_archivo);

size_t definir_tamanio_archivo_bloques();

t_list* peticion_lectura_memoria(char* pid, int registro_direccion, int tamanio);

int bloque_ocupado_desde(int desde);
int bytes_a_bloques(int bytes);
int cantidad_bloques_libres();
int posicion_bloque_libre();

void bitmap_clean_n_bits(int bloque_inicial, int bloques);
void bitmap_set_n_bits(int bloque_inicial, int bloques);
void compactar_file_system(char* pid, archivo* archivo, int nuevo_tamanio);
void configurar_interfaz();
void conectarse_memoria();
void enviar_info_al_kernel();
void esperar_unidades_trabajo(int unidades);
void inicializar_archivo_bloques(FILE* archivo_bitmap);
void inicializar_bitarray(FILE* archivo_bitmap);
void inicializar_bitmap(FILE* archivo_bitmap);
void iniciar_file_system();
void levantar_config_dialfs();
void levantar_config_general();
void levantar_config_memoria();
void levantar_config_unidad_trabajo();
void limpiar_inout();
void limpiar_n_bytes_bloques(int byte_inicial, int cantidad_bytes);
void obtener_archivos();
void peticion_escritura_memoria(char* pid, int registro_direccion, char* mensaje);
void recibir_info_kernel(t_list* lista);

//instrucciones propias de las interfaces:

void io_gen_sleep(int unidades_trabajo);

void io_stdin_read(char* pid, int registro_direccion, int registro_tamanio);

void io_stdout_write(char* pid, int direccion_fisica, int tamanio_cadena);

void io_fs_create(char* pid, char* nombre_archivo);
void io_fs_delete(char* pid, char* nombre_archivo);
void io_fs_truncate(char* pid, char* nombre_archivo, int nuevo_tamanio);
void io_fs_write(char* pid, char* nombre_archivo, int direccion_memoria, int tamanio, int puntero_archivo);
void io_fs_read(char* pid, char* nombre_archivo, int direccion, int tamanio, int puntero_archivo);
#endif /* CLIENT_H_ */