#include "memoria.h"

int main(int argc, char* argv[]) {
    logger = iniciar_logger("memoria.log", "MEMORIA");
    config = iniciar_config(argv[1], logger);
    levantar_config();
	generar_espacio_memoria();

	if(sem_init(&sem_mutex_memoria, 0, 1) == -1) {
        log_error(logger, "Hubo un problema al inicializar el sem_ejecutar");
		exit(EXIT_FAILURE);
    }

    diccionario_instrucciones = dictionary_create();

    server_fd = iniciar_servidor(memoria_config.puerto_escucha); 
	log_info(logger, "Servidor listo para recibir al cliente");

	while (1) {
	pthread_t thread;
	int socket_cliente;
	log_info(logger, "Se genero un nuevo hilo");
	socket_cliente = esperar_cliente(server_fd);
		   pthread_create(&thread,
		                  NULL,
		                  (void*) atender_cliente,
		                  (void*)(intptr_t) socket_cliente);
		   pthread_detach(thread);
	}

	limpiar_memoria();

	return(0);
}

void limpiar_memoria(){
	dictionary_destroy_and_destroy_elements(diccionario_instrucciones, (void*) list_destroy_with_elements);
	bitarray_destroy(frames_libres);
    free(espacio_memoria);
	log_destroy(logger);
	config_destroy(config);
	sem_destroy(&sem_mutex_memoria);
}

void atender_cliente(int socket_cliente){

	int identificador;

	recv(socket_cliente, &identificador, sizeof(uint32_t), MSG_WAITALL);

	if(identificador == 1){
		log_info(logger, "Manejo conexion CPU");
		enviar_mensaje(memoria_config.tam_pagina, socket_cliente);
		manejarConexionCPU(socket_cliente);
	}

	if(identificador == 2){
		log_info(logger, "Manejo conexion Kernel");
		manejarConexionKernel(socket_cliente);
	}

	if(identificador == 3){
		log_info(logger, "Manejo conexion InOut");
		manejarConexionInOut(socket_cliente);
	}

	if(identificador != 1 && identificador != 2 && identificador != 3){
		log_error(logger,"Modulo Desconocido");
		}

	if(socket_cliente == -1){
		exit(1);
	}
}

void manejarConexionCPU(int cliente_cpu){

	while (1) {
		int cod_op = recibir_operacion(cliente_cpu);
		switch (cod_op) {
		case MENSAJE:
			recibir_mensaje(cliente_cpu);
			break;
		case PAQUETE:
			t_list* lista = recibir_paquete(cliente_cpu);
			recibir_info_cpu(lista, cliente_cpu);
			list_destroy_and_destroy_elements(lista, (void*) element_destroy);
			break;
		case -1:
			log_error(logger, "el cliente se desconecto. Terminando servidor");
			pthread_cancel(pthread_self());
		default:
			//log_warning(logger,"Operacion desconocida. No quieras meter la pata");
			break;
		}
	}
}

void recibir_info_cpu(t_list* lista, int conexion_cpu) {
	int codigo_operacion = atoi(list_get(lista, 0));
	int pid = atoi(list_get(lista, 1));
	int direccion_fisica;

	esperar_tiempo_retardo();

	//log_info(logger, "Recibi el codigo de operacion: %d", codigo_operacion);
	//log_info(logger, "Recibi PID: %d", pid);

	switch(codigo_operacion){
		case ESCRIBIR:
			direccion_fisica = atoi(list_get(lista, 2));
			//log_info(logger, "Recibi la direccion fisica: %d", direccion_fisica);
			int tamanio_escibir = atoi(list_get(lista, 3));
			void* mensaje = list_get(lista, 4);
			//log_info(logger, "Recibi el mensaje para escribir de CPU: %s", mensaje); MODIFICAR
			escribir_memoria(conexion_cpu, direccion_fisica, mensaje, tamanio_escibir, pid);
			break;
		case LEER:
			direccion_fisica = atoi(list_get(lista, 2));
			//log_info(logger, "Recibi la direccion fisica: %d", direccion_fisica);
			int tamanio_leer = atoi(list_get(lista, 3));
			//log_info(logger, "Recibi el tamanio: %d", tamanio_leer);
			leer_memoria(conexion_cpu, direccion_fisica, tamanio_leer, pid);
			break;
		case INSTRUCCION:
			int program_counter = atoi(list_get(lista, 2));
			//log_info(logger, "Program counter: %d", program_counter);
			while(dictionary_is_empty(diccionario_instrucciones)){
				//nada, es para que no pida enviar instruccion si no se cargo el diccionario
			}
			enviar_instruccion(pid, program_counter, conexion_cpu);
			break;
		case MARCO:
			int nro_pagina = atoi(list_get(lista, 2));
			enviar_marco(pid, nro_pagina, conexion_cpu);
			break;
		case RESIZE:
			int tamanio_nuevo = atoi(list_get(lista, 2));
			redimensionar_proceso(pid, tamanio_nuevo, conexion_cpu);
			break;
		case -1:
			log_error(logger, "codigo de operacion desconocido. Terminando servidor");
			pthread_cancel(pthread_self());
			exit(1);
		default:
			//log_warning(logger,"Operacion desconocida. No quieras meter la pata");
			break;
	}

}

void enviar_instruccion(int pid, int program_counter, int conexion_cpu) {
	char* nro_pid = string_itoa(pid);

	if(dictionary_has_key(diccionario_instrucciones, nro_pid)){
		
		t_list* lista_instrucciones = dictionary_get(diccionario_instrucciones, nro_pid);
		
		if(program_counter >= list_size(lista_instrucciones)) {
			char* mensaje = string_duplicate("END_OF_PROCESS");
			t_paquete* paquete = crear_paquete();
			agregar_a_paquete(paquete, mensaje, strlen(mensaje)+1);
			enviar_paquete(paquete, conexion_cpu);
			eliminar_paquete(paquete);
		} else{
			char* instruccion = list_get(lista_instrucciones, program_counter);
			t_paquete* paquete = crear_paquete();
			agregar_a_paquete(paquete, instruccion, strlen(instruccion)+1);
			//log_info(logger, "Envio instruccion: %s", instruccion);
			enviar_paquete(paquete, conexion_cpu);
			eliminar_paquete(paquete);
		}
	}

	free(nro_pid);
}

void enviar_marco(int pid,int nro_pagina,int socket_cliente){

	//log_info(logger, "El pid: %d solicito el marco para la pagina: %d", pid, nro_pagina);

	int marco = obtener_marco( pid, nro_pagina);

	if(marco < 0){
		log_error(logger, "Error obteniendo el marco para el PID %d, pagina %d", pid, nro_pagina);
	}

	char* nro_marco = string_itoa(marco);

	t_paquete* paquete = crear_paquete();
	agregar_a_paquete(paquete, nro_marco, strlen(nro_marco)+1);
	enviar_paquete(paquete, socket_cliente);
	eliminar_paquete(paquete);

	free(nro_marco);
}

void esperar_tiempo_retardo(){
	int tiempo_espera_en_microsegundos = atoi(memoria_config.retardo_respuesta) * 1000;
	usleep(tiempo_espera_en_microsegundos);
}

void manejarConexionKernel(int cliente_kernel){

	while (1) {
		int cod_op = recibir_operacion(cliente_kernel);
		switch (cod_op) {
		case MENSAJE:
			recibir_mensaje(cliente_kernel);
			break;
		case PAQUETE:
			t_list* lista = recibir_paquete(cliente_kernel);
			recibir_info_kernel(lista);
			list_destroy_and_destroy_elements(lista, (void*) element_destroy);
			break;
		case -1:
			log_error(logger, "el cliente se desconecto. Terminando servidor");
			pthread_cancel(pthread_self());
			exit(1);
		default:
			//log_warning(logger,"Operacion desconocida. No quieras meter la pata");
			break;
		}
	}
}

void recibir_info_kernel(t_list *lista) {
	char* motivo = list_get(lista, 0);

	esperar_tiempo_retardo();
	
	if(string_equals_ignore_case(motivo, "INICIAR_PROCESO")){
		char* path = list_get(lista, 1);
		char* pid = list_get(lista, 2);
		//log_info(logger, "Recibi el motivo: %s ", motivo);
		//log_info(logger, "Path: %s - PID: %s", path, pid);

		ejecutar_script(path, pid);
		guardar_proceso_en_memoria(pid);

	}	else if(string_equals_ignore_case(motivo, "FINALIZAR_PROCESO")){
		char* pid = list_get(lista, 1);
		//log_info(logger, "Recibi el motivo: %s - PID: %s", motivo, pid);
		
		dictionary_remove_and_destroy(diccionario_instrucciones, pid, (void*)list_destroy_with_elements);
		liberar_proceso_de_memoria(pid); 
	} else {
		// Error, te mande algo que no queria
	}
} 

void ejecutar_script(char* path, char* pid){

	//log_info(logger, "Se recibieron las instrucciones del archivo de PSEUDOCODIGO:");
	FILE *leido;
	char buffer[100];
	leido = fopen(path, "r");

	t_list* lista = list_create();

	if (leido == NULL) {
		log_error(logger, "Error en la apertura del archivo de PSEUDOCODIGO.\n");
	}

	//log_info(logger, "Instrucciones encontradas:");

	while (fgets(buffer, sizeof(buffer), leido)) {
		buffer[strcspn(buffer, "\n")] = '\0';
		char* duplicado = string_duplicate(buffer);
		list_add(lista, duplicado);
		//log_info(logger,"%s",  duplicado);
	}
	
	fclose(leido);

	dictionary_put(diccionario_instrucciones, pid, lista);
}

void manejarConexionInOut(int cliente_InOut){
	
	while (1) {
		int cod_op = recibir_operacion(cliente_InOut);
		switch (cod_op) {
		case MENSAJE:
			recibir_mensaje(cliente_InOut);
			break;
		case PAQUETE:
			t_list* lista = recibir_paquete(cliente_InOut);
			recibir_info_io(lista, cliente_InOut);
			list_destroy_and_destroy_elements(lista, (void*) element_destroy);
			break;
		case -1:
			pthread_cancel(pthread_self());
		default:
			//log_warning(logger,"Operacion desconocida. No quieras meter la pata");
			break;
		}
	}	
}

void recibir_info_io(t_list* lista, int conexion) {
	int codigo_operacion = atoi(list_get(lista, 0));
	int pid = atoi(list_get(lista, 1)); 
	int direccion_fisica = atoi(list_get(lista, 2));

	esperar_tiempo_retardo();

	// log_info(logger, "Recibi el codigo de operacion: %d", codigo_operacion);
	// log_info(logger, "Recibi la direccion fisica: %d", direccion_fisica);
	// log_info(logger, "Recibi el PID: %d", pid);

	switch(codigo_operacion){
		case ESCRIBIR:
			int tamanio_escribir = atoi(list_get(lista, 3));
			void* mensaje = list_get(lista, 4);
			//log_info(logger, "Recibi el mensaje para escribir de IO: %s", mensaje); hay que modificar
			escribir_memoria(conexion, direccion_fisica, mensaje, tamanio_escribir, pid);
			break;
		case LEER:
			int tamanio_leer = atoi(list_get(lista, 3));
			//log_info(logger, "Recibi la instruccion de leer fragmento de tamanio: %d", tamanio);
			leer_memoria(conexion, direccion_fisica, tamanio_leer, pid);
			break;
		case -1:
			log_error(logger, "codigo de operacion desconocido. Terminando servidor");
			pthread_cancel(pthread_self());
			exit(1);
		default:
			//log_warning(logger,"Operacion desconocida. No quieras meter la pata");
			break;
	}
	
}

void generar_espacio_memoria() {
	espacio_memoria = malloc(atoi(memoria_config.tam_memoria));
	registro_tabla_de_paginas = dictionary_create();

	int tam_memoria = atoi(memoria_config.tam_memoria);
    int tam_pagina = atoi(memoria_config.tam_pagina);
    int cant_paginas = tam_memoria / tam_pagina;

    // calculo cuantos bytes necesito para todas las paginas (sumo 7 para redondear para arriba)
	// por ejemplo: 12 pag / 8 = 1 => 12 + 7 = 19 pag / 8 = 2
	int tamanio_in_bytes = (cant_paginas + 7) / 8;

    // le doy dinamicamente memoria al bitarray de n chars (un byte)
	char* bitarray_memory = calloc(tamanio_in_bytes, sizeof(char));

    // se crea el bitarray, usando la memoria de la anterior linea, el tamanio calculado
	// previamente y el modo LSB_FIRST (el bit menos significativo se almacena primero)
	frames_libres = bitarray_create_with_mode(bitarray_memory, tamanio_in_bytes, LSB_FIRST);
}

// busca el marco en la tabla de paginas, si lo encuentra lo devuelve, sino retorna -1
int obtener_marco(int pid, int nro_pagina){

    // Obtengo la tabla de páginas del proceso a partir del PID
	char* nroPid = string_itoa(pid);
    tabla_de_paginas* tabla_proceso = dictionary_get(registro_tabla_de_paginas, nroPid);
    if (tabla_proceso == NULL) {
        // Si no se encuentra la tabla, retorna -1 indicando error
		log_info(logger, "Tabla_proceso null");
		free(nroPid);
        return -1;
    }

    // Obtener el marco correspondiente al número de página
	char* nroPag = string_itoa(nro_pagina);
    int* marco = dictionary_get(tabla_proceso->frame_segun_pagina, nroPag);
    
	if (marco == NULL) {
        // Si no se encuentra el marco, retorna -1 indicando error
		log_info(logger, "Marco inexistente");
		free(nroPid);
		free(nroPag);
        return -1;
    }

	log_info(logger, "Acceso a Tabla de Páginas: PID: %d - Pagina: %d - Marco: %d", pid, nro_pagina, *marco);

    free(nroPid);
	free(nroPag);
	return *marco;
}

// crea una tabla de paginas nueva para el proceso y lo guarda en el registro
void guardar_proceso_en_memoria(char* pid){

    // Crear y configurar la tabla de páginas
    tabla_de_paginas* tabla = malloc(sizeof(tabla_de_paginas));
    tabla->cant_paginas = 0;
    tabla->frame_segun_pagina = dictionary_create();

    log_info(logger, "Creacion tabla de paginas: PID: %d Tamanio: %d", atoi(pid), tabla->cant_paginas);

    // Me guardo la tabla de páginas del proceso en el registro
    dictionary_put(registro_tabla_de_paginas, pid, tabla);
}

// se encarga de liberar los marcos del proceso y eliminar la tabla del proceso
void liberar_proceso_de_memoria(char* pid){

    // Obtener la tabla de páginas del proceso a partir del PID
    tabla_de_paginas* tabla_proceso = dictionary_get(registro_tabla_de_paginas, pid);
    if (tabla_proceso == NULL) {
        log_error(logger, "No se encontró la tabla de páginas para el proceso PID: %d", atoi(pid));
        return;
    }

    log_info(logger, "Destruccion tabla de paginas: PID: %d Tamanio: %d", atoi(pid), tabla_proceso->cant_paginas);

    // Liberar los marcos utilizados por el proceso
    for (int i = 0; i < tabla_proceso->cant_paginas; i++) {
		char* iPid = string_itoa(i);
        int* marco = dictionary_get(tabla_proceso->frame_segun_pagina, iPid);
        if (marco != NULL) {
            bitarray_clean_bit(frames_libres, *marco);
        }
		free(iPid);
    }

    // Destruir la tabla de páginas del proceso
    dictionary_destroy_and_destroy_elements(tabla_proceso->frame_segun_pagina, (void*) element_destroy);

    // Eliminar la entrada del registro global
    dictionary_remove_and_destroy(registro_tabla_de_paginas, pid, (void*) element_destroy);
}

void escribir_memoria(int socket_cliente, int direccion_fisica, void* info, int tamanio, int pid){

	int tamanio_pagina = atoi(memoria_config.tam_pagina);
    int tamanio_escribir = tamanio;
	int desplazamiento = direccion_fisica % tamanio_pagina;
	bool hubo_error = false;

	int marco = direccion_fisica / tamanio_pagina;
	int desp_escrito = 0;

	verificar_limite_memoria(direccion_fisica, tamanio, socket_cliente);

	sem_wait(&sem_mutex_memoria);

	while (tamanio > 0) {

		if (marco >= 0){
			
			// calculo el espacio disponible en la página actual y veo
			// los bytes que tengo que escribir
			int espacio_disponible = tamanio_pagina - desplazamiento;
			int bytes_a_escribir = tamanio < espacio_disponible ? tamanio : espacio_disponible;
			
			// escribo los bytes en la memoria
			memcpy(espacio_memoria + (marco * tamanio_pagina) + desplazamiento, info + desp_escrito, bytes_a_escribir);
			
			// unicamente para ver que ocurre en cada marco
			//char* contenido_marco = string_new();
			//contenido_marco[bytes_a_escribir] = '/0';
			//memcpy(contenido_marco ,espacio_memoria + marco * tamanio_pagina, bytes_a_escribir);
			
			//char* log_marco = mem_hexstring(contenido_marco, bytes_a_escribir);
			//log_info(logger, "Escritura en marco %d (%d byte/bytes): %s", marco, bytes_a_escribir, log_marco);
			
			//free(log_marco);
			//free(contenido_marco);
			
			// avanzo en los bytes ya escritos en memoria
			desp_escrito += bytes_a_escribir;
			
			// actualizo los contadores
			tamanio -= bytes_a_escribir;
			desplazamiento = 0;

			if(tamanio > 0) {
				int marco_aux = obtener_siguiente_marco(pid, marco);
				//log_info(logger, "PID: %d - marco_act: %d - marco_sig: %d", pid, marco, marco_aux);
				marco = marco_aux;
			}

		} else if (marco == -1) {
			
			// ocurrio un error al buscar el marco
            hubo_error = true;
            break;
        }
		
	}
	
	sem_post(&sem_mutex_memoria);

	if (hubo_error) {
		log_error(logger, "Error obteniendo el marco para el PID %d, MARCO %d", pid, marco);
		enviar_mensaje("OUT_OF_MEMORY", socket_cliente);
    } else {
		log_info(logger, "PID: %d - Accion: ESCRIBIR - Direccion fisica: %d - Tamanio: %d",pid, direccion_fisica, tamanio_escribir);
		enviar_mensaje("OK", socket_cliente);
	}
}

void leer_memoria(int socket_cliente, int direccion_fisica, int tamanio, int pid){
	int tamanio_pagina = atoi(memoria_config.tam_pagina);
	int tamanio_leer = tamanio;
    int desplazamiento = direccion_fisica % tamanio_pagina;
	int marco = direccion_fisica / tamanio_pagina;
    char* mensaje = string_new();
	mensaje[tamanio] = '\0'; // sin esto al parecer se traia caracteres de mas
	bool hubo_error = false;

	verificar_limite_memoria(direccion_fisica, tamanio, socket_cliente);

	sem_wait(&sem_mutex_memoria);

    while (tamanio > 0) {

		if (marco >= 0){
			
			// calculo el espacio disponible en la página actual
			int espacio_disponible = tamanio_pagina - desplazamiento;
			int bytes_a_leer = tamanio < espacio_disponible ? tamanio : espacio_disponible;
			char* nuevo = string_new();
			nuevo[bytes_a_leer] = '\0'; // sin esto al parecer se traia caracteres de mas
			
			// leeo los bytes de la memoria
			memcpy(nuevo ,espacio_memoria + (marco * tamanio_pagina) + desplazamiento, bytes_a_leer);
			
			string_append(&mensaje, nuevo);

			// unicamente para ver que ocurre en el marco
			//char* contenido_marco = string_new();
			//memcpy(contenido_marco ,espacio_memoria + marco * tamanio_pagina, bytes_a_leer);
			//char* log_marco = mem_hexstring(contenido_marco, bytes_a_leer);
			//log_info(logger, "Lectura en marco %d: %s", marco, log_marco);
			
			//free(log_marco);
			//free(contenido_marco);
			free(nuevo);

			tamanio -= bytes_a_leer;
			desplazamiento = 0;

			if(tamanio > 0) {
				int marco_aux = obtener_siguiente_marco(pid, marco);
				//log_info(logger, "PID: %d - marco_act: %d - marco_sig: %d", pid, marco, marco_aux);
				marco = marco_aux;
			}

		} else if (marco == -1) {
            // ocurrio un error al buscar el marco
            hubo_error = true;
            break;
        }
    }
	
	sem_post(&sem_mutex_memoria);


	if (hubo_error) {
		log_error(logger, "Error obteniendo el marco para el PID %d, marco %d", pid, marco);
		enviar_mensaje("OUT_OF_MEMORY", socket_cliente);
    }
	else{
		t_paquete* paquete = crear_paquete(); 
		agregar_a_paquete(paquete, mensaje, strlen(mensaje)+1);
		
		log_info(logger, "PID: %d - Accion: LEER - Direccion fisica: %d - Tamanio: %d",pid, direccion_fisica, tamanio_leer);
		log_info(logger, "Mensaje leido en memoria: |%s|", mensaje);

		enviar_paquete(paquete, socket_cliente);
		eliminar_paquete(paquete);
	}

	free(mensaje);
}

int obtener_siguiente_marco(int pid, int anterior_marco) {
	char* nroPid = string_itoa(pid);
	tabla_de_paginas* tabla = dictionary_get(registro_tabla_de_paginas, nroPid);
	int pagina_marco_anterior = buscar_pagina_marco(tabla, anterior_marco);
	int pagina_marco = pagina_marco_anterior + 1;
	free(nroPid);
	return obtener_marco(pid, pagina_marco);
}

int buscar_pagina_marco(tabla_de_paginas* tabla, int marco) {
	int tam = tabla->cant_paginas;
	
	for(int i = 0; tam>i; i++) {
		
		char* nro_pag = string_itoa(i);
		int* marco_aux = dictionary_get(tabla->frame_segun_pagina, nro_pag);
		
		free(nro_pag);
		
		if(marco == *marco_aux){
			return i;
		}
	}

	return -2;
}

void redimensionar_proceso(int pid, int tamanio_nuevo, int socket_cliente){
	char* nroPid = string_itoa(pid);
    tabla_de_paginas* tab = dictionary_get(registro_tabla_de_paginas, nroPid);

	// calculo las paginas que necesito a partir del nuevo tamanio
	// como necesito que redondee para arriba, hago el calculo en doble para
	// que guarde la parte fraccionaria, redondeo con ceil y lo paso a entero
	double paginas_necesarias_double = (double) tamanio_nuevo / (double) atoi(memoria_config.tam_pagina);
    int paginas_necesarias = ceil(paginas_necesarias_double);
	int tamanio_viejo = tab->cant_paginas * (double) atoi(memoria_config.tam_pagina);

    if (tab->cant_paginas > paginas_necesarias){
		int cant_paginas_a_liberar = tab->cant_paginas - paginas_necesarias;

        log_info(logger, "Reducción de proceso: PID: %d Tamaño Actual: %d Tamaño a Reducir: %d", pid, tamanio_viejo, tamanio_nuevo);

        tab = reducir_proceso(tab, cant_paginas_a_liberar, socket_cliente);
		tab->cant_paginas -= cant_paginas_a_liberar;
		dictionary_put(registro_tabla_de_paginas, nroPid, tab);
    }
    else if (tab->cant_paginas < paginas_necesarias){
        int cant_nuevas_paginas = paginas_necesarias - tab->cant_paginas;

        log_info(logger, "Ampliación de proceso: PID: %d Tamaño Actual: %d Tamaño a Ampliar: %d", pid,  tamanio_viejo, tamanio_nuevo);

        tab = ampliar_proceso(tab, cant_nuevas_paginas, socket_cliente);
		tab->cant_paginas += cant_nuevas_paginas;
		dictionary_put(registro_tabla_de_paginas, nroPid, tab);
    }

	free(nroPid);
}

tabla_de_paginas* ampliar_proceso(tabla_de_paginas* tab, int cant_nuevas_paginas, int socket_cliente){
	bool hubo_error = false;

    for (int i = 0; i < cant_nuevas_paginas ; i++) {

        // Buscar un marco libre
        int* marco_libre;
		marco_libre = (int*)malloc(sizeof(int));
		*marco_libre = -1;

		int total_marcos = atoi(memoria_config.tam_memoria) / atoi(memoria_config.tam_pagina);

		for (int j = 0; j < total_marcos; j++) {

			// busco un bit desocupado (que sea 0)
			// cuando lo encuentro lo seteo (lo cambio a 1)
			if (!bitarray_test_bit(frames_libres, j)) {
				*marco_libre = j;
				bitarray_set_bit(frames_libres, j);
				break;
			}
		}

		//log_info(logger, "marco encontrado para ampliar: %d", *marco_libre);

        if (*marco_libre == -1) {
            log_error(logger, "No hay marcos libres disponibles para la ampliación del proceso");
			hubo_error = true;
			free(marco_libre);
            break;
        }

        // Guardar el marco en la tabla de páginas
		char* cant_paginas_nuevas = string_itoa(tab->cant_paginas + i);
        dictionary_put(tab->frame_segun_pagina, cant_paginas_nuevas, marco_libre);
		free(cant_paginas_nuevas);
    }

	// si hubo error (no pudo obtener todos los marcos) le notifica el error
	// sino le envia OK
	if (hubo_error) {
		enviar_mensaje("OUT_OF_MEMORY", socket_cliente);
	}
	else {
		enviar_mensaje("OK", socket_cliente);
	}

	return tab;
}

// libera n cantidad de marcos (arrancando del ultimo guardado)
tabla_de_paginas* reducir_proceso(tabla_de_paginas* tab, int cant_paginas_a_liberar, int socket_cliente){
    for(int i = 0; i < cant_paginas_a_liberar; i++){
		char* cant_paginas_nuevas = string_itoa(tab->cant_paginas - i);
        int* marco = dictionary_remove(tab->frame_segun_pagina, cant_paginas_nuevas);
        if (marco != NULL) {
            bitarray_clean_bit(frames_libres, *marco);
			free(marco);
        }
		free(cant_paginas_nuevas);
    }

	enviar_mensaje("OK", socket_cliente);
	return tab;
}

void verificar_limite_memoria(int direccion_fisica, int size, int socket_cliente){
	int final_de_dirreccion = abs(direccion_fisica) + abs(size);
	int direccion_final_de_memoria = atoi(memoria_config.tam_memoria);

	if(final_de_dirreccion > direccion_final_de_memoria){
		log_error(logger, "OUT_OF_MEMORY");
		enviar_mensaje("OUT_OF_MEMORY", socket_cliente);
	}
}

void levantar_config(void){
	memoria_config.puerto_escucha = config_get_string_value(config, "PUERTO_ESCUCHA");
	memoria_config.tam_memoria = config_get_string_value(config, "TAM_MEMORIA");
	memoria_config.tam_pagina = config_get_string_value(config, "TAM_PAGINA");
	memoria_config.path_instrucciones = config_get_string_value(config, "PATH_INSTRUCCIONES");
	memoria_config.retardo_respuesta = config_get_string_value(config, "RETARDO_RESPUESTA");


	log_info(logger, "Se cargaron los siguientes datos de configuracion:");
    log_info(logger, "PUERTO_ESCUCHA: %s", memoria_config.puerto_escucha);
    log_info(logger, "TAM_MEMORIA: %s", memoria_config.tam_memoria);
    log_info(logger, "TAM_PAGINA: %s", memoria_config.tam_pagina);
    log_info(logger, "PATH_INSTRUCCIONES: %s", memoria_config.path_instrucciones);
    log_info(logger, "RETARDO_RESPUESTA: %s", memoria_config.retardo_respuesta);
}