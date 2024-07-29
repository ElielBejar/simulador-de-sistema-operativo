
#include "inout.h"

int main(int argc, char *argv[]){

    logger = iniciar_logger("InOut.log", "InOut");
    inout.nombre = argv[1];
    inout.config = iniciar_config(argv[2], logger);

    configurar_interfaz();

    enviar_info_al_kernel();

    while (1){
        int cod_op = recibir_operacion(conexion_kernel);
        switch (cod_op){
        case MENSAJE:
            recibir_mensaje(conexion_kernel);
            break;
        case PAQUETE:
            t_list *lista = recibir_paquete(conexion_kernel);
            recibir_info_kernel(lista);
            list_destroy_and_destroy_elements(lista, (void*)element_destroy);
            break;
        case -1:
            log_error(logger, "el cliente se desconecto. Terminando servidor");
            exit(1);
        default:
            log_warning(logger, "Operacion desconocida. No quieras meter la pata");
            break;
        }
    }
    
    limpiar_inout();
}



// busca el archivo que tenga ese bloque inicial
archivo *obtener_archivo_por_bloque_inicial(int bloque_inicial){
    int i = 0;
    int index = -1;
    int length = list_size(lista_archivos);
    archivo *un_archivo;
    while (i < length){
        un_archivo = list_get(lista_archivos, i);
        if (config_get_int_value(un_archivo->config, "BLOQUE_INICIAL") == bloque_inicial){
            index = i;
            i = length;
        }else{
            i++;
        }
    }
    if (index == -1){
        log_info(logger, "El bloque inicial es: %d", bloque_inicial);
        log_error(logger, "No se encontro el archivo");
    }
    return un_archivo;
}

// busca el archivo que tenga el mismo nombre
archivo *obtener_archivo_por_nombre(char *nombre)
{
    int i = 0;
    int index = -1;
    int length = list_size(lista_archivos);
    archivo *un_archivo;
    while (i < length)
    {
        un_archivo = list_get(lista_archivos, i);
        if (strcmp(un_archivo->nombre_archivo, nombre) == 0)
        {
            index = i;
            i = length;
        }
        else
        {
            i++;
        }
    }
    if (index == -1)
    {
        log_error(logger, "No se encontro el archivo %s", nombre);
    }
    return un_archivo;
}

//devuelve el nombre de un archivo(usado para bitmap o archivo de bloques)
//el nombre estara compuesto por: nombre + nombre_interfaz + extension
char *crear_nombre_archivo(char *nombre, char *extension){
    char *nombre_archivo = string_new();
    string_append(&nombre_archivo, nombre);
    string_append(&nombre_archivo, inout.nombre);
    string_append(&nombre_archivo, extension);

    return nombre_archivo;
}

//devuelve la ruta del path de configuracion sumado al nombre del archivo
char *obtener_path_completo(char *nombre_archivo){
    char *path_completo = string_new();
    string_append(&path_completo, inout_config.path_base_dialfs);
    string_append(&path_completo, "/");
    string_append(&path_completo, nombre_archivo);

    return path_completo;
}

//obtiene el id de interfaz segun el nombre de interfaz
enum tipo_interfaz obtener_tipo(char *t_string)
{
    if (strcmp(t_string, "GENERICA") == 0){
        return GENERICA;
    }else if (strcmp(t_string, "STDIN") == 0){
        return STDIN;
    }else if (strcmp(t_string, "STDOUT") == 0){
        return STDOUT;
    }else if (strcmp(t_string, "DIALFS") == 0){
        return DIALFS;
    }else{
        return DEFAULT;
    }
}

//crea un archivo en el path de configuracion
FILE *crear_archivo(char *nombre_archivo){

    char *path_completo = obtener_path_completo(nombre_archivo);

    FILE *archivo = fopen(path_completo, "w+");

    if (archivo == NULL){
        log_error(logger, "Error con el archivo %s, no se pudo crear", nombre_archivo);
    }else{
        log_info(logger, "Se creo el archivo %s", nombre_archivo);
    }

    free(path_completo);

    return archivo;
}
//trunca el archivo de bloques segun la configuracion
size_t definir_tamanio_archivo_bloques(){
    int tamanio = inout_config.block_size * inout_config.block_count;
    off_t tamanio_archivo = (off_t)tamanio;

    char* path_completo = obtener_path_completo(nombre_archivo_bloques);

    if (truncate(path_completo, tamanio_archivo) == -1){
        log_error(logger, "Error definiendo el tamanio del archivo de bloques");
    }

    free(path_completo);

    return tamanio;
}

// envia a memoria la direccion fisica y el tamanio de lo que quiere recibir
t_list *peticion_lectura_memoria(char *pid, int registro_direccion, int tamanio){
    char *cod_operacion = string_itoa(PETICION_LECTURA);
    char *str_registro_direccion = string_itoa(registro_direccion);
    char *str_tamanio = string_itoa(tamanio);
    t_paquete *paquete = crear_paquete();

    agregar_a_paquete(paquete, cod_operacion, strlen(cod_operacion) + 1);
    agregar_a_paquete(paquete, pid, strlen(pid) + 1);
    agregar_a_paquete(paquete, str_registro_direccion, strlen(str_registro_direccion) + 1);
    agregar_a_paquete(paquete, str_tamanio, strlen(str_tamanio) + 1);
    enviar_paquete(paquete, conexion_memoria);

    t_list *lista;

    int cod_op = recibir_operacion(conexion_memoria);
    switch (cod_op){
    case MENSAJE:
        recibir_mensaje(conexion_memoria);
        break;
    case PAQUETE:
        lista = recibir_paquete(conexion_memoria);
        break;
    case -1:
        log_error(logger, "el cliente se desconecto. Terminando servidor");
        exit(1);
    default:
        log_warning(logger, "Operacion desconocida. No quieras meter la pata");
        break;
    }
    free(cod_operacion);
    free(str_registro_direccion);
    free(str_tamanio);
    eliminar_paquete(paquete);

    return lista;
}

// encuentra el primer bloque ocupado desde un index del bitmap
int bloque_ocupado_desde(int desde){
    int contador = desde;
    //esto es por el caso excepcional de si desde = 0
    //
    int index_bit = -1;
    int length_bitmap = bitarray_get_max_bit(bitmap);
    while (contador < length_bitmap){
        if (bitarray_test_bit(bitmap, contador) == 1){
            index_bit = contador;
            contador = length_bitmap;
        }
        else{
            contador++;
        }
    }
    return index_bit;
}

// indica cuantos bloques ocupa los bytes pasados
int bytes_a_bloques(int bytes){
    double bloques_sin_redondear = (double)bytes / inout_config.block_size;
    if(bloques_sin_redondear == 0){
        bloques_sin_redondear = 1;
    }
    return (int)ceil(bloques_sin_redondear);
}

//devuelve cuantos bloques libres del bitmap hay
int cantidad_bloques_libres(){
    int contador = 0;
    for(int i = 0; i<bitarray_get_max_bit(bitmap); i++){
        if(bitarray_test_bit(bitmap, i) == 0){
            contador++;
        }
    }
    return contador;
}

// devuelve en que posicion del bitmap hay un bloque libre, si no hay devuelve -1
int posicion_bloque_libre(){
    int contador = 0;
    int index_bit = -1;
    int length_bitmap = bitarray_get_max_bit(bitmap);
    while (contador < length_bitmap){
        if (bitarray_test_bit(bitmap, contador) == 0){
            index_bit = contador;
            contador = length_bitmap;
        }else{
            contador++;
        }
    }
    return index_bit;
}

//pongo en 0 los bits del bitmap desde bloque_incial la cantidad de veces pasada por "bloques"
void bitmap_clean_n_bits(int bloque_inicial, int bloques){
    for (int i = 0; i < bloques; i++){
        bitarray_clean_bit(bitmap, bloque_inicial + i);
    }
}

//pone en 1 los bits del bitmap desde bloque_inicial la cantidad de veces pasada por "bloques"
void bitmap_set_n_bits(int bloque_inicial, int bloques){
    for (int i = 0; i < bloques; i++){
        bitarray_set_bit(bitmap, bloque_inicial + i);
    }
}

//se usa en io_fs_truncate, compacta todo el file system y deja al final el archivo a ampliar
void compactar_file_system(char* pid, archivo *archivo_truncado, int nuevo_tamanio)
{
    log_info(logger, "PID: %s - Inicio Compactación.", pid);

    int tamanio_archivo_truncado = config_get_int_value(archivo_truncado->config, "TAMANIO_ARCHIVO");
    void *datos_archivo_truncado = malloc(tamanio_archivo_truncado); // guardo aca los datos del archivo a ampliar
    int bloque_inicial = config_get_int_value(archivo_truncado->config, "BLOQUE_INICIAL");
    int byte_inicio = bloque_inicial * inout_config.block_size;
    memcpy(datos_archivo_truncado, (datos_bloques + byte_inicio), tamanio_archivo_truncado);
    limpiar_n_bytes_bloques(byte_inicio, tamanio_archivo_truncado);

    // ahora sacamos del bitmap los bloques que ocupaba el file a ampliar:
    int bloques_ocupados_truncado = bytes_a_bloques(tamanio_archivo_truncado);
    bitmap_clean_n_bits(bloque_inicial, bloques_ocupados_truncado);

    msync(datos_bloques, inout_config.block_count * inout_config.block_size, MS_SYNC);
    msync(datos_bitmap, sizeof(datos_bitmap), MS_SYNC);

    int primer_bloque_libre = posicion_bloque_libre();
    int bloque_ocupado;
    archivo *archivoo; //= malloc(sizeof(archivo));
    if (primer_bloque_libre != -1){
        bloque_ocupado = bloque_ocupado_desde(primer_bloque_libre);
    }else{
        bloque_ocupado = -1;
    }
    // va compactando hasta que no haya mas ocupados(devuelve -1)
    while (bloque_ocupado != -1){
        archivoo = obtener_archivo_por_bloque_inicial(bloque_ocupado);

        int tamanio_archivo = config_get_int_value(archivoo->config, "TAMANIO_ARCHIVO");
        int byte_inicial_ocupado = bloque_ocupado * inout_config.block_size;
        int byte_inicial_libre = primer_bloque_libre * inout_config.block_size;
        int bloques_ocupados = bytes_a_bloques(tamanio_archivo);
        
        void* datos_archivo = malloc(tamanio_archivo);

        memcpy(datos_archivo, (datos_bloques + byte_inicial_ocupado), tamanio_archivo);

        limpiar_n_bytes_bloques(byte_inicial_ocupado, tamanio_archivo);

        memcpy((datos_bloques + byte_inicial_libre), datos_archivo, tamanio_archivo);

        bitmap_clean_n_bits(bloque_ocupado, bloques_ocupados);
        bitmap_set_n_bits(primer_bloque_libre, bloques_ocupados);

        msync(datos_bloques, inout_config.block_count * inout_config.block_size, MS_SYNC);
        msync(datos_bitmap, sizeof(datos_bitmap), MS_SYNC);

        char* str_primer_bloque_libre = string_itoa(primer_bloque_libre);

        config_set_value(archivoo->config, "BLOQUE_INICIAL", str_primer_bloque_libre);
        config_save(archivoo->config);

        primer_bloque_libre = posicion_bloque_libre();
        if (primer_bloque_libre != -1){
            bloque_ocupado = bloque_ocupado_desde(primer_bloque_libre);
        }else{
            bloque_ocupado = -1;
        }
        free(datos_archivo);
        free(str_primer_bloque_libre);
    }

    //se terminaron de compactar todos los archivos menos el que se va a truncar
    
    int ultimo_bloque_libre = posicion_bloque_libre();
    int ultimo_byte_inicial = ultimo_bloque_libre*inout_config.block_size;
    memcpy((datos_bloques+ultimo_byte_inicial), datos_archivo_truncado, tamanio_archivo_truncado);
    bitmap_set_n_bits(ultimo_bloque_libre, bytes_a_bloques(nuevo_tamanio));

    msync(datos_bloques, inout_config.block_count * inout_config.block_size, MS_SYNC);
    msync(datos_bitmap, sizeof(datos_bitmap), MS_SYNC);

    char* str_bloque_inicial = string_itoa(ultimo_bloque_libre);
    char* str_nuevo_tamanio = string_itoa(nuevo_tamanio);

    config_set_value(archivo_truncado->config, "BLOQUE_INICIAL", str_bloque_inicial);
    config_set_value(archivo_truncado->config, "TAMANIO_ARCHIVO", str_nuevo_tamanio);
    config_save(archivo_truncado->config);

    free(datos_archivo_truncado);
    free(str_bloque_inicial);
    free(str_nuevo_tamanio);

    useconds_t tiempo_microsegundos = inout_config.retraso_compactacion * 1000;
    usleep(tiempo_microsegundos);

    log_info(logger, "PID: %s - Fin Compactación.", pid);
}

//levanta el config de la interfaz, hace las conexiones necesarias y determina las instrucciones permitidas
void configurar_interfaz(void){

    levantar_config_general();

    conexion_kernel = realizar_handshake(inout_config.ip_kernel, inout_config.puerto_kernel, inout.nombre, "kernel");

    switch (obtener_tipo(inout_config.tipo_interfaz)){
    case GENERICA:
        levantar_config_unidad_trabajo();
        instrucciones_permitidas = INSTRUCCIONES_GENERICA;
        break;

    case STDIN:
        levantar_config_memoria();
        instrucciones_permitidas = INSTRUCCIONES_STDIN;
        conectarse_memoria();
        break;

    case STDOUT:
        levantar_config_unidad_trabajo();
        levantar_config_memoria();
        instrucciones_permitidas = INSTRUCCIONES_STDOUT;
        conectarse_memoria();
        break;

    case DIALFS:
        levantar_config_unidad_trabajo();
        levantar_config_memoria();
        levantar_config_dialfs();
        instrucciones_permitidas = INSTRUCCIONES_DIALFS;
        conectarse_memoria();
        iniciar_file_system();
        break;

    default:
        perror("Se ha ingresado una interfaz no admitida");
        exit(1);
        break;
    }
}

//hace la conexion a memoria con sockets
void conectarse_memoria(void){
    conexion_memoria = realizar_handshake(inout_config.ip_memoria, inout_config.puerto_memoria, inout.nombre, "Memoria");
    send(conexion_memoria, &identificador, sizeof(uint32_t), 0);
}

// arma el paquete y le envía al kernel el nombre y las instrucciones permitidas
void enviar_info_al_kernel(){
    t_paquete *paquete = crear_paquete();
    agregar_a_paquete(paquete, inout.nombre, strlen(inout.nombre) + 1);
    agregar_a_paquete(paquete, instrucciones_permitidas, strlen(instrucciones_permitidas) + 1);
    enviar_paquete(paquete, conexion_kernel);
    eliminar_paquete(paquete);
}

//la interfaz se queda esperando n unidades de TIEMPO_UNIDAD_TRABAJO del config
void esperar_unidades_trabajo(int unidades){
    int tiempo_unidad = inout_config.tiempo_unidad_trabajo;
    useconds_t tiempo_milisegundos = tiempo_unidad * unidades;
    useconds_t tiempo_microsegundos = tiempo_milisegundos * 1000;
    usleep(tiempo_microsegundos);
}

//trunca y mapea el archivo de bloques del filesystem
void inicializar_archivo_bloques(FILE* archivo_bloques){
    archivo_bloques = crear_archivo(nombre_archivo_bloques);
    size_t tamanio = definir_tamanio_archivo_bloques();
    datos_bloques = mmap(NULL, tamanio, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(archivo_bloques), 0);
    msync(datos_bloques, tamanio, MS_SYNC);
}

//inicializa y mapea el bitarray del bitmap del filesystem
void inicializar_bitarray(FILE* archivo_bitmap){
    size_t tamanio_bytes_bitmap = inout_config.block_count / 8;
    datos_bitmap = (char *)mmap(NULL, tamanio_bytes_bitmap, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(archivo_bitmap), 0);
    if (datos_bitmap == MAP_FAILED){
        perror("Error al mapear el archivo en memoria");
        return;
    }
    bitmap = bitarray_create_with_mode((char *)datos_bitmap, tamanio_bytes_bitmap, MSB_FIRST);
    msync(datos_bitmap, tamanio_bytes_bitmap, MS_SYNC);
}

//inicializa y trunca el archivo bitmap del filesystem, llama también a inicializar_bitarray()
void inicializar_bitmap(FILE *archivo_bitmap){
    archivo_bitmap = crear_archivo(nombre_archivo_bitmap);
    size_t tamanio_bytes_bitmap = inout_config.block_count / 8;
    char* path_completo = obtener_path_completo(nombre_archivo_bitmap);
    if (truncate(path_completo, tamanio_bytes_bitmap) == -1){
        log_error(logger, "Error definiendo el tamanio del archivo de bitmap");
    }

    free(path_completo);

    inicializar_bitarray(archivo_bitmap);
}

//crea los archivos y llama a las funciones necesarias para levantar el filesystem
void iniciar_file_system(){

    nombre_archivo_bloques = crear_nombre_archivo("bloques", ".dat");
    nombre_archivo_bitmap = crear_nombre_archivo("bitmap", ".dat");

    char* path_completo_bloques = obtener_path_completo(nombre_archivo_bloques);
    char* path_completo_bitmap = obtener_path_completo(nombre_archivo_bitmap);

    FILE* archivo_bloques = fopen(path_completo_bloques, "r");
    FILE* archivo_bitmap = fopen(path_completo_bitmap, "r");

    if (archivo_bloques == NULL){
        inicializar_archivo_bloques(archivo_bloques);
    }else{
        log_info(logger, "El archivo de bloques ya existe.\n");
        archivo_bloques = fopen(path_completo_bloques, "r+");
        size_t tamanio_bloques = definir_tamanio_archivo_bloques();
        datos_bloques = mmap(NULL, tamanio_bloques, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(archivo_bloques), 0);
        msync(datos_bloques, tamanio_bloques, MS_SYNC);
    }

    if (archivo_bitmap == NULL){
        inicializar_bitmap(archivo_bitmap);
    }else{
        log_info(logger, "El archivo bitmap ya existe.\n");
        archivo_bitmap = fopen(path_completo_bitmap, "r+");
        inicializar_bitarray(archivo_bitmap);
    }

    obtener_archivos();

    free(path_completo_bloques);
    free(path_completo_bitmap);
    free(nombre_archivo_bloques);
    free(nombre_archivo_bitmap);
    //fclose(archivo_bitmap);
    //fclose(archivo_bloques);
}

//levanta todas las secciones del config que sean del dialfs
void levantar_config_dialfs(void){
    inout_config.path_base_dialfs = config_get_string_value(inout.config, "PATH_BASE_DIALFS");
    inout_config.block_size = config_get_int_value(inout.config, "BLOCK_SIZE");
    inout_config.block_count = config_get_int_value(inout.config, "BLOCK_COUNT");
    inout_config.retraso_compactacion = config_get_int_value(inout.config, "RETRASO_COMPACTACION");

    log_info(logger, "PATH_BASE_DIALFS: %s", inout_config.path_base_dialfs);
    log_info(logger, "BLOCK_SIZE: %d", inout_config.block_size);
    log_info(logger, "BLOCK_COUNT: %d", inout_config.block_count);
}

//levanta las secciones del config que todas las interfaces necesitan
void levantar_config_general(){
    inout_config.tipo_interfaz = config_get_string_value(inout.config, "TIPO_INTERFAZ");
    inout_config.ip_kernel = config_get_string_value(inout.config, "IP_KERNEL");
    inout_config.puerto_kernel = config_get_string_value(inout.config, "PUERTO_KERNEL");

    log_info(logger, "Se cargaron los siguientes datos de configuracion:");
    log_info(logger, "TIPO_INTERFAZ: %s", inout_config.tipo_interfaz);
    log_info(logger, "IP_KERNEL: %s", inout_config.ip_kernel);
    log_info(logger, "PUERTO_KERNEL: %s", inout_config.puerto_kernel);
}

//levanta las secciones del config para conectarse con la memoria
void levantar_config_memoria(void){
    inout_config.ip_memoria = config_get_string_value(inout.config, "IP_MEMORIA");
    inout_config.puerto_memoria = config_get_string_value(inout.config, "PUERTO_MEMORIA");

    log_info(logger, "IP_MEMORIA: %s", inout_config.ip_memoria);
    log_info(logger, "PUERTO_MEMORIA: %s", inout_config.puerto_memoria);
}

//levanta la sección del config de TIEMPO_UNIDAD_TRABAJO
void levantar_config_unidad_trabajo(void){
    inout_config.tiempo_unidad_trabajo = config_get_int_value(inout.config, "TIEMPO_UNIDAD_TRABAJO");

    log_info(logger, "TIEMPO_UNIDAD_TRABAJO: %d", inout_config.tiempo_unidad_trabajo);
}

//libera la memoria de los punteros globales
void limpiar_inout(){
   log_destroy(logger);
   config_destroy(inout.config);
   free(inout.nombre);
   free(instrucciones_permitidas);

}

//pone en byte nulo desde el byte_inicial las veces cantidad_bytes en el archivo de bloques
void limpiar_n_bytes_bloques(int byte_inicial, int cantidad_bytes){
    for (int i = 0; i < cantidad_bytes; i++){
        memset((datos_bloques + byte_inicial + i), '\0', 1);
    }
}

// se recuperan los archvios del file system y se ponen en la lista de archivos, usa dirent.h
void obtener_archivos(){
    DIR *dir;
    struct dirent *ent;
    lista_archivos = list_create();

    // Abre el directorio
    if ((dir = opendir(inout_config.path_base_dialfs)) != NULL){

        // Itera sobre los archivos en el directorio
        while ((ent = readdir(dir)) != NULL){
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0){
                if (strcmp(ent->d_name, nombre_archivo_bloques) != 0 && strcmp(ent->d_name, nombre_archivo_bitmap) != 0){
                    archivo *un_archivo = malloc(sizeof(archivo));

                    un_archivo->nombre_archivo = string_duplicate(ent->d_name);
                    char* path_completo = obtener_path_completo(un_archivo->nombre_archivo);

                    un_archivo->config = config_create(path_completo);

                    free(path_completo);

                    list_add(lista_archivos, un_archivo);
                    int bytes_archivo = config_get_int_value(un_archivo->config, "TAMANIO_ARCHIVO");
                    int bloques_ocupados = bytes_a_bloques(bytes_archivo);
                    int bloque_inicial = config_get_int_value(un_archivo->config, "BLOQUE_INICIAL");
                    for (int i = bloque_inicial; i < (bloque_inicial + bloques_ocupados); i++){
                        bitarray_set_bit(bitmap, i);
                    }
                    log_info(logger, "Se agrego a la lista de archivos: %s", un_archivo->nombre_archivo);
                }
            }
        }
    }
    free(dir);
    // habria que eliminar la memoria de los componentes del ent
    free(ent);
}

// envia a la memoria lo que hay que escribir y en que dirección de memoria
void peticion_escritura_memoria(char *pid, int registro_direccion, char *mensaje){
    t_paquete *paquete = crear_paquete();

    char *cod_operacion = string_itoa(PETICION_ESCRITURA);
    char *str_registro_direccion = string_itoa(registro_direccion);
    char *tamanio = string_itoa(strlen(mensaje));

    agregar_a_paquete(paquete, cod_operacion, strlen(cod_operacion) + 1);
    agregar_a_paquete(paquete, pid, strlen(pid) + 1);
    agregar_a_paquete(paquete, str_registro_direccion, strlen(str_registro_direccion) + 1);
    agregar_a_paquete(paquete, tamanio, strlen(tamanio) + 1);
    agregar_a_paquete(paquete, mensaje, strlen(mensaje) + 1);
    enviar_paquete(paquete, conexion_memoria);
    //log_info(logger, "Se envio la peticion para escribir en la memoria");
    int cod_op = recibir_operacion(conexion_memoria);
    //log_info(logger, "Me llego la respuesta de memoria, cod: %d", cod_op);
    switch (cod_op){
    case MENSAJE:
        recibir_mensaje(conexion_memoria);
        break;
    case PAQUETE:
        // no le debería llegar ningún paquete
        break;
    case -1:
        log_error(logger, "el cliente se desconecto. Terminando servidor");
        exit(1);
    default:
        log_warning(logger, "Operacion desconocida. No quieras meter la pata");
        break;
    }

    free(cod_operacion);
    free(str_registro_direccion);
    free(tamanio);
    eliminar_paquete(paquete);
}

// recibe la información del kernel y manda a ejecutar la instrucción correspondiente
void recibir_info_kernel(t_list *lista)
{
    char *pid = list_get(lista, 0);
    char *instruccion = list_get(lista, 1);

    log_info(logger, "PID: %s - Operacion: %s", pid, instruccion);

    if (string_equals_ignore_case(instruccion, "IO_GEN_SLEEP"))
    {
        io_gen_sleep(atoi(list_get(lista, 2)));
    }
    else if (string_equals_ignore_case(instruccion, "IO_STDIN_READ"))
    {
        io_stdin_read(pid, atoi(list_get(lista, 2)), atoi(list_get(lista, 3))); // ACA LEVANTAS CHARS NO INTS
    }
    else if (string_equals_ignore_case(instruccion, "IO_STDOUT_WRITE"))
    {
        io_stdout_write(pid, atoi(list_get(lista, 2)), atoi(list_get(lista, 3)));
    }
    else if (string_equals_ignore_case(instruccion, "IO_FS_CREATE"))
    {
        io_fs_create(pid, list_get(lista, 2));
    }
    else if (string_equals_ignore_case(instruccion, "IO_FS_DELETE"))
    {
        io_fs_delete(pid, list_get(lista, 2));
    }
    else if (string_equals_ignore_case(instruccion, "IO_FS_TRUNCATE"))
    {
        io_fs_truncate(pid, list_get(lista, 2), atoi(list_get(lista, 3)));
    }
    else if (string_equals_ignore_case(instruccion, "IO_FS_WRITE"))
    {
        io_fs_write(pid, list_get(lista, 2), atoi(list_get(lista, 3)), atoi(list_get(lista, 4)), atoi(list_get(lista, 5)));
    }
    else if (string_equals_ignore_case(instruccion, "IO_FS_READ"))
    {
        io_fs_read(pid, list_get(lista, 2), atoi(list_get(lista, 3)), atoi(list_get(lista, 4)), atoi(list_get(lista, 5)));
    }
}

//instrucciones propias de las interfaces:

//espera n unidades de trabajo
void io_gen_sleep(int unidades_trabajo){
    esperar_unidades_trabajo(unidades_trabajo);
    enviar_mensaje("END_OF_IO_GEN_SLEEP", conexion_kernel);
}

// lee una entrada por teclado y guarda hasta un tamaño, luego lo envía a memoria
void io_stdin_read(char *pid, int direccion_fisica, int tamanio_cadena){

    char *leido;
    leido = readline("> ");

    char *leido_limitado = string_new();
    string_n_append(&leido_limitado, leido, tamanio_cadena);

    //log_info(logger, "El tamaño maximo cadena es: %d", tamanio_cadena);
    //log_info(logger, "La direccion fisica es: %d", direccion_fisica);
    //log_info(logger, "La cadena leida y acotada es: %s", leido_limitado);
    //log_info(logger, "El tamanio de la cadena es: %d", string_length(leido_limitado));
    peticion_escritura_memoria(pid, direccion_fisica, leido_limitado);

    //log_info(logger, "Se ejecuto IO_STDIN_READ");

    enviar_mensaje("END_OF_IO_STDIN_READ", conexion_kernel);

    free(leido_limitado);
    free(leido);
}

//imprime en la consola el contenido en memoria desde la direccion fisica con un tamanio de cadena determinado
void io_stdout_write(char *pid, int direccion_fisica, int tamanio_cadena){
    t_list *lista;

    lista = peticion_lectura_memoria(pid, direccion_fisica, tamanio_cadena);
    char *mensaje = list_get(lista, 0);
    log_info(logger, "%s" , mensaje);

    enviar_mensaje("END_OF_IO_STDOUT_WRITE", conexion_kernel);

    list_destroy_and_destroy_elements(lista, (void*)element_destroy);
}

//crea un archivo en el filesystem
void io_fs_create(char* pid, char *nombre_archivo){

    log_info(logger, "PID: %s - Crear Archivo: %s", pid, nombre_archivo);

    esperar_unidades_trabajo(TIEMPO_UNIDAD_TRABAJO_DIALFS);
    int posicion_bloque_archivo = posicion_bloque_libre();
    if (posicion_bloque_archivo != -1){

        char* path_completo = obtener_path_completo(nombre_archivo);

        FILE *archivo_metadata = fopen(path_completo, "r+");

        if (archivo_metadata == NULL){
            archivo_metadata = crear_archivo(nombre_archivo);
            t_config *config_metadata = config_create(path_completo);

            char* str_posicion_bloque_archivo = string_itoa(posicion_bloque_archivo);

            config_set_value(config_metadata, "BLOQUE_INICIAL", str_posicion_bloque_archivo);
            free(str_posicion_bloque_archivo);

            config_set_value(config_metadata, "TAMANIO_ARCHIVO", "0");
            config_save(config_metadata);
            archivo *nuevo_archivo = malloc(sizeof(archivo));
            nuevo_archivo->nombre_archivo = string_duplicate(nombre_archivo);

            nuevo_archivo->config = config_metadata;
            list_add(lista_archivos, nuevo_archivo);
            bitarray_set_bit(bitmap, posicion_bloque_archivo);

        }else{
            log_info(logger, "El archivo %s ya existe", nombre_archivo);
        }

        fclose(archivo_metadata);
        free(path_completo);
   
        log_info(logger, "El bloque asignado para el archivo fue: %d", posicion_bloque_archivo);
        enviar_mensaje("END_OF_IO_FS_CREATE", conexion_kernel);
    }
}

//borra un archivo en el filesystem
void io_fs_delete(char* pid, char *nombre_archivo){

    log_info(logger, "PID: %s - Eliminar Archivo: %s", pid, nombre_archivo);

    esperar_unidades_trabajo(TIEMPO_UNIDAD_TRABAJO_DIALFS);

    archivo *archivo = obtener_archivo_por_nombre(nombre_archivo);

    // elimino la seccion del void*
    int tamanio_archivo = config_get_int_value(archivo->config, "TAMANIO_ARCHIVO");
    int bloque_inicial = config_get_int_value(archivo->config, "BLOQUE_INICIAL");
    int block_size = inout_config.block_size;
    int byte_inicial = bloque_inicial * block_size;
    int tamanio_archivo_bloques = block_size * inout_config.block_count;
    for (int i = 0; i < tamanio_archivo; i++){
        memset((datos_bloques + byte_inicial + i), '\0', 1);
    }
    msync(datos_bloques, tamanio_archivo_bloques, MS_SYNC);

    // elimino los unos del bitmap
    int bloques_archivo = bytes_a_bloques(tamanio_archivo);
    size_t tamanio_bytes_bitmap = inout_config.block_count / 8;
    for (int i = 0; i < bloques_archivo; i++){
        bitarray_clean_bit(bitmap, (bloque_inicial + i));
    }
    msync(datos_bitmap, tamanio_bytes_bitmap, MS_SYNC);

    // elimino el struct archivo
    list_remove_element(lista_archivos, archivo);
    config_destroy(archivo->config);
    free(archivo->nombre_archivo);
    free(archivo);

    // elimino el archivo en si
    remove(obtener_path_completo(nombre_archivo));


    enviar_mensaje("END_OF_IO_FS_DELETE", conexion_kernel);

}

//modifica el tamanio de un archivo del filesystem
void io_fs_truncate(char* pid, char* nombre_archivo, int nuevo_tamanio){

    log_info(logger, "PID: %s - Truncar Archivo: %s - Tamaño: %d", pid, nombre_archivo, nuevo_tamanio);

    esperar_unidades_trabajo(TIEMPO_UNIDAD_TRABAJO_DIALFS);

    archivo* archivo = obtener_archivo_por_nombre(nombre_archivo);
    int tamanio_actual = config_get_int_value(archivo->config, "TAMANIO_ARCHIVO");
    int bloque_inicial = config_get_int_value(archivo->config, "BLOQUE_INICIAL");

    if(tamanio_actual < nuevo_tamanio){
        int cant_bloques_libres = cantidad_bloques_libres();
        int bloques_ocupados_archivo = bytes_a_bloques(tamanio_actual);
        int bloques_nuevo_tamanio = bytes_a_bloques(nuevo_tamanio);

        if(bloques_nuevo_tamanio <= (cant_bloques_libres + bloques_ocupados_archivo)){
            //en esta condicion se puede ampliar el archivo
            //hay que checkear si es necesario compactar:
            int bloque_final = bloque_inicial + bloques_ocupados_archivo;
            int prox_bloq_inicial = bloque_ocupado_desde(bloque_final);
            if(prox_bloq_inicial == -1){
                prox_bloq_inicial = bitarray_get_max_bit(bitmap);
            }
            int bloques_libres_contiguos = prox_bloq_inicial - bloque_final;

            if(bloques_nuevo_tamanio <= (bloques_libres_contiguos+bloques_ocupados_archivo)){
                //no es necesario compactar
                bitmap_set_n_bits(bloque_final, (bloques_nuevo_tamanio-bloques_ocupados_archivo));
                msync(datos_bitmap, sizeof(datos_bitmap), MS_SYNC);

                char* string_nuevo_tamanio = string_itoa(nuevo_tamanio);

                config_set_value(archivo->config, "TAMANIO_ARCHIVO", string_nuevo_tamanio);
                config_save(archivo->config);  

                free(string_nuevo_tamanio);              
            }else{
                // es necesario compactar
                compactar_file_system(pid, archivo, nuevo_tamanio);

            }
            
        }else{
            //no se puede ampliar
            log_error(logger, "Espacio insuficiente para ampliar el archivo: %s", archivo->nombre_archivo);
        }
          
    }else{
        int byte_inicial = inout_config.block_size*bloque_inicial;
        int bytes_a_limpiar = tamanio_actual - nuevo_tamanio;
        limpiar_n_bytes_bloques((byte_inicial + nuevo_tamanio), bytes_a_limpiar);
        
        int bloques_nuevos_ocupados = bytes_a_bloques(nuevo_tamanio);
        int bloques_a_desocupar = bytes_a_bloques(tamanio_actual) - bytes_a_bloques(nuevo_tamanio);
        bitmap_clean_n_bits((bloque_inicial + bloques_nuevos_ocupados), bloques_a_desocupar);

        char* string_nuevo_tamanio = string_itoa(nuevo_tamanio);

        config_set_value(archivo->config, "TAMANIO_ARCHIVO", string_nuevo_tamanio);
        config_save(archivo->config);

        free(string_nuevo_tamanio);

    }

    enviar_mensaje("END_OF_IO_FS_TRUNCATE", conexion_kernel);

}

//escribe en un archivo del filesystem el contenido en memoria en una direccion con un tamaño determinado
void io_fs_write(char* pid, char* nombre_archivo, int direccion_memoria, int tamanio, int puntero_archivo){

    log_info(logger, "PID: %s - Escribir Archivo: %s - Tamaño a Escribir: %d - Puntero Archivo: %d", 
            pid, nombre_archivo, tamanio, puntero_archivo);
    
    esperar_unidades_trabajo(TIEMPO_UNIDAD_TRABAJO_DIALFS);

    archivo* archivo = obtener_archivo_por_nombre(nombre_archivo);
    int bloque_inicial = config_get_int_value(archivo->config, "BLOQUE_INICIAL"); 
    int tamanio_archivo = config_get_int_value(archivo->config, "TAMANIO_ARCHIVO");

    if((puntero_archivo + tamanio) <= tamanio_archivo){
    t_list *lista = peticion_lectura_memoria(pid, direccion_memoria, tamanio);
    char* mensaje = list_get(lista, 0);//no se si genera memory leaks

    memcpy((datos_bloques + bloque_inicial*inout_config.block_size + puntero_archivo), mensaje, tamanio);
    msync(datos_bloques, sizeof(datos_bloques), MS_SYNC);

    list_destroy_and_destroy_elements(lista, (void*)element_destroy);

    }else{
        log_error(logger, "El archivo no tiene suficiente tamanio");
    }

    enviar_mensaje("END_OF_IO_FS_WRITE", conexion_kernel);
}

//lee el contenido de un archivo desde el puntero archivo y lo envia a memoria en una direccion con un tamaño
void io_fs_read(char* pid, char* nombre_archivo, int direccion, int tamanio, int puntero_archivo){

    log_info(logger, "PID: %s - Leer Archivo: %s - Tamaño a Leer: %d - Puntero Archivo: %d",
             pid, nombre_archivo, tamanio, puntero_archivo);

    esperar_unidades_trabajo(TIEMPO_UNIDAD_TRABAJO_DIALFS);

    archivo* archivo = obtener_archivo_por_nombre(nombre_archivo);
    int bloque_inicial = config_get_int_value(archivo->config, "BLOQUE_INICIAL"); 
    
    char* datos_archivo = malloc(tamanio);
    memcpy(datos_archivo, (datos_bloques + bloque_inicial*inout_config.block_size + puntero_archivo), tamanio);
    peticion_escritura_memoria(pid, direccion, datos_archivo);

    free(datos_archivo);

    enviar_mensaje("END_OF_IO_FS_READ", conexion_kernel);
}