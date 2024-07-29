#include <stdlib.h>
#include <stdio.h>
#include <utils/utils.h>
#include "cpu.h"

int main(int argc, char* argv[]) {
    // -- HAY DOS PUERTOS, UNO DE INTERRUPT Y OTRO DE DISPATCH, PONGO PUERTO BASICO, dispatch, FALTA AGREGAR EL OTRO
    logger_dispatch = iniciar_logger("cpu.log", "CPU-DISPATCH");
	logger_interrupt = iniciar_logger("cpu.log", "CPU-INTERRUPT");
	logger = iniciar_logger("cpu.log", "CPU");
    config = iniciar_config(argv[1], logger);
    levantar_config();
	inicializar_tlb();
	conexion_memoria = realizar_handshake(cpu_config.ip_memoria, cpu_config.puerto_memoria, "cpu", "memoria");
	

	send(conexion_memoria, &identificador, sizeof(uint32_t), 0);

	recibir_operacion(conexion_memoria);
	char* aux = leer_mensaje(conexion_memoria);
	tam_pagina = atoi(aux);

	// -----------------

	if(sem_init(&sem_interrupt, 0, 0) == -1 ) {
        log_error(logger, "Hubo un problema al inicializar el sem_interrupt");
    }
	if(sem_init(&sem_instrucciones, 0, 0) == -1 ) {
        log_error(logger, "Hubo un problema al inicializar el sem_instrucciones");
    }
	if(sem_init(&sem_respuesta_kernel, 0, 0) == -1 ) {
        log_error(logger, "Hubo un problema al inicializar el sem_respuesta_kernel");
    }
	if(sem_init(&sem_fin_interrupcion, 0, 0) == -1 ) {
    	log_error(logger, "Hubo un problema al inicializar el sem_fin_interrupcion");
    }
	if(sem_init(&sem_iniciar_nuevo_proceso, 0, 1) == -1 ) {
    	log_error(logger, "Hubo un problema al inicializar el sem_iniciar_nuevo_proceso");
    }
	if(sem_init(&sem_llego_paquete_interrupt, 0, 0) == -1 ) {
    	log_error(logger, "Hubo un problema al inicializar el sem_iniciar_nuevo_proceso");
    }
	
	iniciar_hilos_cpu();

	sem_destroy(&sem_interrupt);
	sem_destroy(&sem_instrucciones);
	sem_destroy(&sem_respuesta_kernel);
	sem_destroy(&sem_fin_interrupcion);
	sem_destroy(&sem_iniciar_nuevo_proceso);
	sem_destroy(&sem_llego_paquete_interrupt);

	//list_destroy(tablas_tlb);

	free(aux);
	log_destroy(logger);
	log_destroy(logger_interrupt);
	log_destroy(logger_dispatch);
	config_destroy(config);

    return 0;
}

void iniciar_hilos_cpu(){
	int err;

    err = pthread_create(&(puertos_cpu[0]), NULL, manejarPuertoDispatch, NULL);
    if (err != 0){
    	log_error(logger, "Hubo un problema al crear el thread del puerto dispatch:[%s]", strerror(err));
    	exit(1);
    }
    log_info(logger, "El thread del puerto dispatch comenzo");

    err = pthread_create(&(puertos_cpu[1]), NULL, manejarPuertoInterrupt, NULL);
    if (err != 0){
    	log_error(logger, "Hubo un problema al crear el thread del puerto interrupt:[%s]", strerror(err));
    	exit(1);
    }
    log_info(logger, "El thread del puerto interrupt comenzo");

    pthread_join(puertos_cpu[0], NULL);
    pthread_join(puertos_cpu[1], NULL);
}

void* manejarPuertoDispatch(void *arg)
{
    int server_fd = iniciar_servidor(cpu_config.puerto_escucha_dispatch); 
	log_info(logger_dispatch, "Servidor dispatch para recibir al cliente");
	int cliente_fd = esperar_cliente(server_fd);
	
	while (1) {
		//t_list* lista = list_create();
		int cod_op = recibir_operacion(cliente_fd);
		switch (cod_op) {
		case MENSAJE:
			char* respuesta_kernel = leer_mensaje(cliente_fd);
			if(string_contains(respuesta_kernel, "CONTINUA_EJECUTANDO")) {
				continua_ejecutando = true;
				sem_post(&sem_respuesta_kernel);
			}
			free(respuesta_kernel);
			break;
		case PAQUETE:
			t_list* lista;//cambie esto
			lista = recibir_paquete(cliente_fd);
			conexion_kernel = cliente_fd;
			recibir_info(lista);
			list_destroy_and_destroy_elements(lista,(void*)element_destroy);
			break;
		case -1:
			log_error(logger_dispatch, "el cliente se desconecto. Terminando servidor");
			exit(1);
		default:
			log_warning(logger_dispatch,"Operacion desconocida. No quieras meter la pata");
			break;
		}
		//list_destroy_and_destroy_elements(lista,(void*)element_destroy);
	}
}

void* manejarPuertoInterrupt(void *arg)
{
    int server_fd = iniciar_servidor(cpu_config.puerto_escucha_interrupt); 
	log_info(logger_interrupt, "Servidor interrupt para recibir al cliente");
	int cliente_fd = esperar_cliente(server_fd);
	lista_interrupt = list_create();
	while (1) {
		int cod_op = recibir_operacion(cliente_fd);
		switch (cod_op) {
		case MENSAJE:
			char* respuesta_kernel = leer_mensaje(cliente_fd);
			if(string_equals_ignore_case(respuesta_kernel,"NO_CONTINUA_EJECUTANDO")) {
				continua_ejecutando = false;
				sem_post(&sem_respuesta_kernel);
			}
			free(respuesta_kernel);
			break;
		case PAQUETE:
			list_destroy_and_destroy_elements(lista_interrupt, (void *)element_destroy);
			lista_interrupt = recibir_paquete(cliente_fd);
			if(es_primera_interrupcion_recibida) {
				es_primera_interrupcion_recibida = false;
				sem_post(&sem_llego_paquete_interrupt);
			}
			break;
		case -1:
			log_error(logger_interrupt, "el cliente se desconecto. Terminando servidor");
			exit(1);
		default:
			log_warning(logger_interrupt,"Operacion desconocida. No quieras meter la pata");
			break;
		}
	}
	list_destroy_and_destroy_elements(lista_interrupt,(void*)element_destroy);
}

void recibir_info(t_list* lista) {
	sem_wait(&sem_iniciar_nuevo_proceso);
	pcb_proceso = malloc(sizeof(pcb));
	cargar_pcb(pcb_proceso, lista); 
	//log_info(logger, "Nuevo proceso en ejecucion: ");
	proceso_vacio = false;
	sem_post(&sem_instrucciones);
	manejar_proceso();
}

void manejar_proceso () {	
	int err;
	
	err = pthread_create(&(puertos_cpu[3]), NULL, (void*)esperar_su_desalojo, NULL);
    if (err != 0){
    	log_error(logger, "Hubo un problema al crear el thread del puerto interrupt:[%s]", strerror(err));
    	exit(1);
    }
    //log_info(logger, "Espero su desalojo");

	err = pthread_create(&(puertos_cpu[2]), NULL, (void*)empezar_a_ejecutar_proceso, NULL);
    if (err != 0){
    	log_error(logger, "Hubo un problema al crear el thread del puerto dispatch:[%s]", strerror(err));
    	exit(1);
    }
    //log_info(logger, "Empiezo a ejecutar proceso");

}

void esperar_su_desalojo() {
	sem_wait(&sem_interrupt);
	if(es_interrupcion_valida) {
		proceso_vacio = true;
		if(es_interrupcion_por_quantum)
			enviar_contexto_de_ejecucion();
		free(pcb_proceso->pid);
		free(pcb_proceso);
		sem_post(&sem_fin_interrupcion);
	} else {
		sem_post(&sem_instrucciones);
		sem_post(&sem_fin_interrupcion);
		esperar_su_desalojo();
	}
}

void empezar_a_ejecutar_proceso() {
	char* instruccion/* = string_new()*/;
	while (!proceso_vacio /*&& !string_equals_ignore_case(instruccion, "END_OF_PROCESS")*/) 
	{
		sem_wait(&sem_instrucciones);
		pedir_instruccion_a_memoria();
		instruccion = recibir_instruccion_memoria();
		//if(string_equals_ignore_case(instruccion, "END_OF_PROCESS")) {  // si se desaloja el proceso por EXIT, no se podría ingresar al pcb_proceso->pid
		//	log_info(logger, "PID: %s - Termino de ejecutar", pid);
		//}else{
			log_info(logger, "PID: %s - FETCH - Program Counter: %d", pcb_proceso->pid, pcb_proceso->registros.PC);
			pcb_proceso->registros.PC++;
			hacer_instruccion(instruccion);

			enviar_mensaje("FIN_INSTRUCCION",conexion_kernel);
			//log_info(logger,"Envio el mensaje FIN_INSTRUCCION a kernel");
			sem_wait(&sem_respuesta_kernel);
			if(continua_ejecutando) {
				sem_post(&sem_instrucciones);
			} else {
				manejar_pedido_interrupcion();
				sem_wait(&sem_fin_interrupcion);
			}
		//}
		free(instruccion);
	}
	sem_post(&sem_iniciar_nuevo_proceso);
}

void manejar_pedido_interrupcion() {
	sem_wait(&sem_llego_paquete_interrupt);
	es_primera_interrupcion_recibida = true;
	char* motivo_aux = string_duplicate(list_get(lista_interrupt, 1));

	if(string_equals_ignore_case(motivo_aux, "QUANTUM"))
		es_interrupcion_por_quantum = true;
	else
		es_interrupcion_por_quantum = false;

	//log_info(logger_interrupt, "Me llegó pedido de interrupción, su motivo es: %s \n", motivo_aux);
	
	int pid = atoi(list_get(lista_interrupt, 0));
	if(pid != atoi(pcb_proceso->pid)) {
		//log_info(logger_interrupt, "Se descartó la interrupción %s", motivo_aux);
		es_interrupcion_valida = false;
	}else {
		es_interrupcion_valida = true; 

		//if(string_equals_ignore_case(motivo_aux, "EXIT"))
			//log_info(logger, "PID: %d - Termino de ejecutar \n", pid);
	}

	sem_post(&sem_interrupt);

	free(motivo_aux);
}

void enviar_contexto_de_ejecucion() {
	char* motivo_aux = string_duplicate("QUANTUM");

	t_paquete* paquete = crear_paquete();
	agregar_pcb_a_paquete(paquete, pcb_proceso);
	agregar_a_paquete(paquete, motivo_aux, strlen(motivo_aux)+1);
	enviar_paquete(paquete, conexion_kernel); 
	eliminar_paquete(paquete);
	free(motivo_aux);
}

void pedir_instruccion_a_memoria() {
	t_paquete* paquete_a_enviar = crear_paquete();

	char* cod_op = string_itoa(INSTRUCCION);
	agregar_a_paquete(paquete_a_enviar, cod_op, strlen(cod_op)+1);
	agregar_a_paquete(paquete_a_enviar, pcb_proceso->pid, strlen(pcb_proceso->pid)+1);

	int aux_PC = pcb_proceso->registros.PC;
	char* PC = string_itoa(aux_PC);
	agregar_a_paquete(paquete_a_enviar, PC, strlen(PC)+1);
	enviar_paquete(paquete_a_enviar, conexion_memoria);
	//log_info(logger, "Pedi siguiente instruccion : %d - a memoria - PID: %s",pcb_proceso->registros.PC, pcb_proceso->pid);
	eliminar_paquete(paquete_a_enviar);

	free(cod_op);
	free(PC);
}

char* recibir_instruccion_memoria(){
	int cod_op=recibir_operacion(conexion_memoria);
	char* instruccion;

	switch (cod_op)
	{
	case MENSAJE:
		recibir_mensaje(conexion_memoria);
		break;
	case PAQUETE:
		t_list* lista/* = list_create()*/;
		lista = recibir_paquete(conexion_memoria);
		instruccion = string_duplicate(list_get(lista, 0));
		list_destroy_and_destroy_elements(lista,(void*)element_destroy);
		break;
	default:
		log_error(logger, "LLEGO BASURA");
		exit(1);
		break;
	}

	return instruccion;
}

void hacer_instruccion (char* instruccion) {
	char** datos;
	datos = string_split(instruccion, " ");

	if(string_equals_ignore_case(datos[0], "SET")) {
		set(datos[1], datos[2]);
	}
	else if(string_equals_ignore_case(datos[0], "MOV_IN")) {
		mov_in(datos[0], datos[1], datos[2]);
	}
	else if(string_equals_ignore_case(datos[0], "MOV_OUT")) {
		mov_out(datos[0], datos[1], datos[2]);
	}
	else if(string_equals_ignore_case(datos[0], "SUM")) {
		sum(datos[1], datos[2]);
	}
	else if(string_equals_ignore_case(datos[0], "SUB")) {
		sub(datos[1], datos[2]);
	}
	else if(string_equals_ignore_case(datos[0], "JNZ")) {
		jnz(datos[1], datos[2]);
	}
	
	else if(string_equals_ignore_case(datos[0], "RESIZE")) {
		resize(datos[1]);
	}
	else if(string_equals_ignore_case(datos[0], "COPY_STRING")) {
		copy_string(datos[1]);
	}
	
	else if(string_equals_ignore_case(datos[0], "WAIT")) {
		wait(datos[0],datos[1]);
	}
	else if(string_equals_ignore_case(datos[0], "SIGNAL")) {
		signal(datos[0],datos[1]);
	}

	else if(string_equals_ignore_case(datos[0], "IO_GEN_SLEEP")) {
		io_gen_sleep(datos[0], datos[1], datos[2]);
	}
	else if(string_equals_ignore_case(datos[0], "IO_STDOUT_WRITE") || string_equals_ignore_case(datos[0], "IO_STDIN_READ")) {
		io_stdout_write_o_stdin_read(datos[0], datos[1], datos[2], datos[3]);
	}
	else if(string_equals_ignore_case(datos[0], "IO_FS_CREATE") || string_equals_ignore_case(datos[0], "IO_FS_DELETE")) {
		io_fs_create_o_delete(datos[0], datos[1], datos[2]);
	}
	else if(string_equals_ignore_case(datos[0], "IO_FS_TRUNCATE")) {
		io_fs_truncate(datos[0], datos[1], datos[2], datos[3]);
	}
	else if(string_equals_ignore_case(datos[0], "IO_FS_WRITE") || string_equals_ignore_case(datos[0], "IO_FS_READ")) {
		io_fs_write_o_read(datos[0], datos[1], datos[2], datos[3], datos[4], datos[5]);
	}

	else if(string_equals_ignore_case(datos[0], "EXIT")) {
		exit_prima();
	}

	string_array_destroy(datos);
}

// ----------- TLB ----------- //

void inicializar_tlb() {
	// --- Inicializo la lista con la cantidad de entradas por archivo config --- //
	
	int nro_entradas = atoi(cpu_config.cantidad_entradas_tlb);
	if(nro_entradas == 0) {
		log_info(logger, "No esta habilitada la TLB");
	} else {
		log_info(logger, "Esta habilitada la TLB y cuenta con %s entradas", cpu_config.cantidad_entradas_tlb);
		tablas_tlb = list_create();
		
		for(int i = 0; nro_entradas > i; i++) {
			base_tlb* entrada = malloc(sizeof(base_tlb));
			entrada->marco = 0;	
			entrada->pid = string_duplicate("0");
			entrada->pagina = 0;
			list_add(tablas_tlb, entrada);	
		}
	}	
}

int buscar_en_tlb(char* pid, int pagina, bool imprimo) {
	
    bool cumple_la_entrada(base_tlb* entrada){
        return string_equals_ignore_case(entrada->pid, pid) && (pagina == entrada->pagina);
    };

	if(list_is_empty(tablas_tlb)){
		if(imprimo) log_info(logger, "PID: %s - TLB MISS - Pagina: %d", pid, pagina);
		return -1;
	}
	else {
		if(list_any_satisfy(tablas_tlb, (void*)cumple_la_entrada)) {
			if(imprimo) log_info(logger, "PID: %s - TLB HIT - Pagina: %d", pid, pagina);
			base_tlb* entrada_tlb = list_find(tablas_tlb, (void*)cumple_la_entrada);
			return entrada_tlb->marco;
		} else {
			if(imprimo) log_info(logger, "PID: %s - TLB MISS - Pagina: %d", pid, pagina);
			return -1;
		}
	}
	

}

void reemplazar_en_tlb(char* pid, int marco, int nro_pagina) { /// CHECKEAR, ES PROBABLE QUE ESTO ESTE MAL

	bool cumple_la_entrada(base_tlb* entrada){
        return string_equals_ignore_case(entrada->pid, pid) && nro_pagina == entrada->pagina;
    };

	base_tlb* entrada_tlb = list_find(tablas_tlb, (void*)cumple_la_entrada);

	// falta contemplar el caso en el que no esta en la tlb
	
	if(string_equals_ignore_case("FIFO", cpu_config.algoritmo_tlb)) {
		if(entrada_tlb == NULL) {
			base_tlb* aux = list_remove(tablas_tlb, 0);

			entrada_tlb = malloc(sizeof(base_tlb));
			entrada_tlb->marco = marco;
			entrada_tlb->pagina = nro_pagina;
			entrada_tlb->pid = string_duplicate(pid);

			list_add(tablas_tlb, entrada_tlb);

			free(aux->pid);
			free(aux);

		} else {
			// si ya esta no hago nada
		}
	}

	if(string_equals_ignore_case("LRU", cpu_config.algoritmo_tlb)) {

		if(entrada_tlb == NULL) { // No esta en la tlb

			base_tlb* aux1 = list_remove(tablas_tlb, 0); // Borro el ultimo que ingrese
			entrada_tlb = malloc(sizeof(base_tlb));
			entrada_tlb->marco = marco;
			entrada_tlb->pagina = nro_pagina;
			entrada_tlb->pid = string_duplicate(pid);

			list_add(tablas_tlb, entrada_tlb); // Pongo la nueva entrada como la mas reciente

			free(aux1->pid);
			free(aux1);

		} else { // Esta en la tlb
            base_tlb *aux = malloc(sizeof(base_tlb));
			aux->marco = entrada_tlb->marco;
			aux->pagina = entrada_tlb->pagina;
			aux->pid = string_duplicate(entrada_tlb->pid);
 			list_remove_element(tablas_tlb, entrada_tlb);// lo borro y lo pongo como el mas reciente
			free(entrada_tlb->pid);
			free(entrada_tlb);
			list_add(tablas_tlb, aux); 
			//free(entrada_tlb->pid);ya se libera en list_remove_element
			//free(entrada_tlb); ya se libera en list_remove_element
		}
	}
}
// ----------- Instrucciones ----------- //
void set(char* registro, char* valor) {
	log_info(logger, "PID: %s - Ejecutando: SET - %s %s", pcb_proceso->pid, registro, valor);
	setear_valor(registro, atoi(valor));
}

void sum(char* registro_destino, char* registro_origen) {
	log_info(logger, "PID: %s - Ejecutando: SUM - %s %s", pcb_proceso->pid, registro_destino, registro_origen);
	int valor = obtener_valor(registro_destino);
	valor += obtener_valor(registro_origen);
	setear_valor(registro_destino, valor);
}

void sub(char* registro_destino, char* registro_origen) {
	log_info(logger, "PID: %s - Ejecutando: SUB - %s %s", pcb_proceso->pid, registro_destino, registro_origen);
	int valor = obtener_valor(registro_destino);
	valor -= obtener_valor(registro_origen);
	setear_valor(registro_destino, valor);
}

void jnz(char* registro, char* instruccion) {
	log_info(logger, "PID: %s - Ejecutando: JNZ - %s %s", pcb_proceso->pid, registro, instruccion);
	if(obtener_valor(registro) != 0){
		pcb_proceso->registros.PC = atoi(instruccion);
	}
}

// --- Desde aca arriba hasta abajo hay que corregir (Funciones resize y copy_string, junto a sus respectivas subfunciones) --- //

void resize(char* nuevo_tam) {
	log_info(logger, "PID: %s - Ejecutando: RESIZE - %s", pcb_proceso->pid, nuevo_tam);

	char* RESIZE_aux = string_itoa(RESIZE);

	t_paquete* paquete = crear_paquete();
	agregar_a_paquete(paquete, RESIZE_aux, strlen(RESIZE_aux)+1);
	agregar_a_paquete(paquete, pcb_proceso->pid, strlen(pcb_proceso->pid)+1);
	agregar_a_paquete(paquete, nuevo_tam, strlen(nuevo_tam)+1);
	enviar_paquete(paquete, conexion_memoria);
	eliminar_paquete(paquete);

	recibir_operacion(conexion_memoria);

	char* respuesta = leer_mensaje(conexion_memoria);
	if(string_equals_ignore_case(respuesta, "OUT_OF_MEMORY")) {
		char* motivo = string_duplicate("OUT_OF_MEMORY");
		t_paquete* paquete = crear_paquete();
		agregar_pcb_a_paquete(paquete, pcb_proceso);
		agregar_a_paquete(paquete, motivo, strlen(motivo)+1);
		enviar_paquete(paquete, conexion_kernel);
		eliminar_paquete(paquete);
		free(motivo);
	}

	free(RESIZE_aux);
	free(respuesta);
}

void copy_string(char* tam) {

	int direccion_logica_a_leer = obtener_valor("SI");
	int direccion_fisica_a_leer = obtener_direccion_fisica(direccion_logica_a_leer, pcb_proceso->pid);

	char* str_dir_fisica_leer = string_itoa(direccion_fisica_a_leer);
	//char* puntero_leer = string_itoa(obtener_valor("SI"));

	// es lo mismo que leer memoria pero ahora el tamaño  y la direccion_fisica los recibe por parametro
	char* dato_apuntado = leer_valor_memoria_prima(str_dir_fisica_leer, tam);

	int direccion_logica_a_escribir = obtener_valor("DI");
	int direccion_fisica_a_escribir = obtener_direccion_fisica(direccion_logica_a_escribir, pcb_proceso->pid);

	escribir_en_memoria(dato_apuntado, strlen(dato_apuntado), direccion_fisica_a_escribir, true);

	free(str_dir_fisica_leer);
	free(dato_apuntado);
}


// --- Desde aca Abajo hasta Arriba hay que corregir (Funciones resize y copy_string, junto a sus respectivas subfunciones) --- //

// --- Desde aca arriba hasta abajo hay que corregir (Funciones mov_in y mov_out, junto a sus respectivas subfunciones) --- //

void mov_in(char* instruccion, char* registro_datos, char* registro_direccion) {
	char* leido = leer_valor_memoria(registro_direccion, registro_datos); // mando direccion logica
	if(!string_equals_ignore_case(leido, "ERROR")){
		//log_info(logger, "PID: %s - Accion: LEER - Direccion Fisica: %s - Valor: %s", pcb_proceso->pid, registro_direccion, leido);
		set(registro_datos, leido);
	}
	free(leido);
}

char* leer_valor_memoria(char* registro_direccion, char*registro_datos){
	int direccion_logica = obtener_valor(registro_direccion);
	int direccion_fisica_aux = obtener_direccion_fisica(direccion_logica, pcb_proceso->pid);
	
	if(direccion_fisica_aux != -1){
		char* cod_op = string_itoa(LEER);
		char* direccion_fisica = string_itoa(direccion_fisica_aux);

		t_paquete* paquete = crear_paquete();
		agregar_a_paquete(paquete, cod_op, strlen(cod_op)+1);
		agregar_a_paquete(paquete, pcb_proceso->pid, strlen(pcb_proceso->pid)+1);
		agregar_a_paquete(paquete, direccion_fisica, strlen(direccion_fisica)+1);

		char* tam_lectura = string_itoa(sizeof(obtener_valor(registro_datos)));

		agregar_a_paquete(paquete, tam_lectura, strlen(tam_lectura)+1);
		enviar_paquete(paquete, conexion_memoria);
		eliminar_paquete(paquete);

		char* leido = recibir_leido_memoria();

		log_info(logger, "PID: %s - Accion: LEER - Direccion Fisica: %s - Valor: %s", pcb_proceso->pid, direccion_fisica, leido);

		free(cod_op);
		free(tam_lectura);
		free(direccion_fisica);

		return leido;
	}
	else {
		return "ERROR";
	}

}

char* leer_valor_memoria_prima(char* puntero_direccion, char* tam_lectura) {
	
		char* cod_op = string_itoa(LEER);

		t_paquete* paquete = crear_paquete();
		agregar_a_paquete(paquete, cod_op, strlen(cod_op)+1);
		agregar_a_paquete(paquete, pcb_proceso->pid, strlen(pcb_proceso->pid)+1);
		agregar_a_paquete(paquete, puntero_direccion, strlen(puntero_direccion)+1);
		agregar_a_paquete(paquete, tam_lectura, strlen(tam_lectura)+1);
		enviar_paquete(paquete, conexion_memoria);
		eliminar_paquete(paquete);

		char* leido = recibir_leido_memoria();

		free(cod_op);

		return leido;
}

char* recibir_leido_memoria() {
	recibir_operacion(conexion_memoria);
	t_list* lista;
	lista = recibir_paquete(conexion_memoria);
	char* leido = string_duplicate(list_get(lista, 0));
	list_destroy_and_destroy_elements(lista,(void*)element_destroy);
	return leido;
}

int obtener_marco_memoria(int nro_pagina_aux) {
	char* cod_op = string_itoa(MARCO);
	char* nro_pagina = string_itoa(nro_pagina_aux);

	t_paquete* paquete = crear_paquete();
	agregar_a_paquete(paquete, cod_op, strlen(cod_op)+1);
	agregar_a_paquete(paquete, pcb_proceso->pid, strlen(pcb_proceso->pid)+1);
	agregar_a_paquete(paquete, nro_pagina, strlen(nro_pagina)+1);
	enviar_paquete(paquete, conexion_memoria);
	eliminar_paquete(paquete);

	int marco = recibir_marco_memoria();
	if(marco!= 1){
		log_info(logger, "PID: %s - OBTENER MARCO - Pagina: %s - Marco: %d", pcb_proceso->pid, nro_pagina, marco);
	}

	free(cod_op);
	free(nro_pagina);

	return marco;
}

int recibir_marco_memoria() {
	recibir_operacion(conexion_memoria); 
	t_list* lista;
	lista = recibir_paquete(conexion_memoria);
	int marco = atoi(list_get(lista, 0));
	list_destroy_and_destroy_elements(lista,(void*)element_destroy);

	if(marco == -1) {

		char* motivo = string_duplicate("DENIED_ACCESS");
		t_paquete*paquete = crear_paquete();
		agregar_pcb_a_paquete(paquete, pcb_proceso);
		agregar_a_paquete(paquete, motivo, strlen(motivo)+1);
		enviar_paquete(paquete, conexion_kernel);
		eliminar_paquete(paquete);

	}

	return marco;
}

int obtener_pagina(int direccion_logica) {
	int nro_pagina = direccion_logica / tam_pagina;
	return nro_pagina;
}

int obtener_direccion_fisica(int direccion_logica, char* pid) {
	char* cantidad_entradas_tlb_aux = string_duplicate(cpu_config.cantidad_entradas_tlb);
	int nro_pagina = obtener_pagina(direccion_logica);
	int marco;

	if(atoi(cantidad_entradas_tlb_aux) != 0) {
		marco = buscar_en_tlb(pid, nro_pagina, true);
	}

	if(marco == -1 || atoi(cantidad_entradas_tlb_aux) == 0){
		marco = obtener_marco_memoria(nro_pagina);
	}
	
	if(marco != -1) {
		int desplazamiento = direccion_logica - nro_pagina * tam_pagina;
		int direccion_fisica = marco*tam_pagina + desplazamiento;

		if(atoi(cpu_config.cantidad_entradas_tlb) != 0)
			reemplazar_en_tlb(pid, marco, nro_pagina);
		
		free(cantidad_entradas_tlb_aux);
		return direccion_fisica;
	} else {
		free(cantidad_entradas_tlb_aux);
		return -1;
	}
	
}


void mov_out(char* instruccion, char* registro_direccion, char* registro_datos) {
	
	log_info(logger, "PID: %s - Ejecutando: %s - %s %s", pcb_proceso->pid, instruccion, registro_direccion, registro_datos);
	int valor = obtener_valor(registro_datos);
	int* leido = &valor;
	int tamanio = obtener_tamanio_registro(registro_datos);

	int direccion_logica = obtener_valor(registro_direccion);
	int direccion_fisica = obtener_direccion_fisica(direccion_logica, pcb_proceso->pid);

	if(direccion_fisica != -1){
		escribir_en_memoria(leido, tamanio, direccion_fisica, false);
		log_info(logger, "PID: %s - Accion: ESCRIBIR - Direccion Fisica: %d - Valor: %d", pcb_proceso->pid, direccion_fisica, valor);
	}
}

void escribir_en_memoria(void* dato, int tamanio, int direccion_fisica_aux, bool es_string) {
    t_paquete* paquete = crear_paquete();
    char* cod = string_itoa(ESCRIBIR);
    char* direccion_fisica = string_itoa(direccion_fisica_aux);
    char* tam = string_itoa(tamanio);

    agregar_a_paquete(paquete, cod, strlen(cod)+1);
    agregar_a_paquete(paquete, pcb_proceso->pid, strlen(pcb_proceso->pid)+1);
    agregar_a_paquete(paquete, direccion_fisica, strlen(direccion_fisica)+1);
    agregar_a_paquete(paquete, tam, strlen(tam)+1);

    // si es string agrego 1 por el /0
	int barra_cero = es_string? 1:0;
	agregar_a_paquete(paquete, dato, tamanio + barra_cero);

    enviar_paquete(paquete, conexion_memoria);
    eliminar_paquete(paquete);
    recibir_operacion(conexion_memoria);
    recibir_mensaje(conexion_memoria);

    free(cod);
    free(direccion_fisica);
    free(tam);
}


// --- Desde aca abajo hasta arriba hay que corregir (Funciones mov_in y mov_out, junto a sus respectivas subfunciones) --- //


void io_gen_sleep(char* instruccion, char* interfaz, char* unidades_de_trabajo) {
	log_info(logger, "PID: %s - Ejecutando: %s - %s %s", pcb_proceso->pid, instruccion, interfaz, unidades_de_trabajo);

	t_paquete* paquete_a_enviar = crear_paquete();
	agregar_pcb_a_paquete(paquete_a_enviar, pcb_proceso);
	agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion)+1);
	agregar_a_paquete(paquete_a_enviar, interfaz, strlen(interfaz)+1);
	agregar_a_paquete(paquete_a_enviar, unidades_de_trabajo, strlen(unidades_de_trabajo)+1);
	enviar_paquete(paquete_a_enviar, conexion_kernel);
	eliminar_paquete(paquete_a_enviar);
}

void io_stdout_write_o_stdin_read(char* instruccion, char* interfaz, char* registro_direccion, char* registro_tam) {
	log_info(logger, "PID: %s - Ejecutando: %s - %s %s %s", pcb_proceso->pid, instruccion, interfaz, registro_direccion, registro_tam);

	int direccion_logica = obtener_valor(registro_direccion);
	int direccion_fisica_aux = obtener_direccion_fisica(direccion_logica, pcb_proceso->pid);
	char* direccion_fisica = string_itoa(direccion_fisica_aux);
	char* tam = string_itoa(obtener_valor(registro_tam));

	t_paquete* paquete_a_enviar = crear_paquete();
	agregar_pcb_a_paquete(paquete_a_enviar, pcb_proceso);
	agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion)+1);
	agregar_a_paquete(paquete_a_enviar, interfaz, strlen(interfaz)+1);
	agregar_a_paquete(paquete_a_enviar, direccion_fisica, strlen(direccion_fisica)+1);
	agregar_a_paquete(paquete_a_enviar, tam, strlen(tam)+1);
	enviar_paquete(paquete_a_enviar, conexion_kernel);
	eliminar_paquete(paquete_a_enviar);

	free(tam);
	free(direccion_fisica);
}

void io_fs_create_o_delete(char* instruccion, char* interfaz, char* nombre_archivo) {
	log_info(logger, "PID: %s - Ejecutando: %s - %s %s", pcb_proceso->pid, instruccion, interfaz, nombre_archivo);

	t_paquete* paquete_a_enviar = crear_paquete();
	agregar_pcb_a_paquete(paquete_a_enviar, pcb_proceso);
	agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion)+1);
	agregar_a_paquete(paquete_a_enviar, interfaz, strlen(interfaz)+1);
	agregar_a_paquete(paquete_a_enviar, nombre_archivo, strlen(nombre_archivo)+1);
	enviar_paquete(paquete_a_enviar, conexion_kernel);
	eliminar_paquete(paquete_a_enviar);
}

void io_fs_truncate(char* instruccion, char* interfaz, char* nombre_archivo, char* registro_tam) {
	log_info(logger, "PID: %s - Ejecutando: %s - %s %s %s", pcb_proceso->pid, instruccion, interfaz, nombre_archivo, registro_tam);

	int tam_aux = obtener_valor(registro_tam);
	char* tam = string_itoa(tam_aux);

	t_paquete* paquete_a_enviar = crear_paquete();
	agregar_pcb_a_paquete(paquete_a_enviar, pcb_proceso);
	agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion)+1);
	agregar_a_paquete(paquete_a_enviar, interfaz, strlen(interfaz)+1);
	agregar_a_paquete(paquete_a_enviar, nombre_archivo, strlen(nombre_archivo)+1);
	agregar_a_paquete(paquete_a_enviar, tam, strlen(tam)+1);
	enviar_paquete(paquete_a_enviar, conexion_kernel);
	eliminar_paquete(paquete_a_enviar);

	free(tam);
}

void io_fs_write_o_read(char* instruccion, char* interfaz, char* nombre_archivo, char* registro_direccion, char* registro_tam, char* registro_puntero_archivo) {
	log_info(logger, "PID: %s - Ejecutando: %s - %s %s %s %s %s", pcb_proceso->pid, instruccion, interfaz, nombre_archivo, registro_direccion, registro_tam, registro_puntero_archivo);

	int direccion_logica = obtener_valor(registro_direccion);
	int direccion_fisica_aux = obtener_direccion_fisica(direccion_logica, pcb_proceso->pid);
	char* direccion_fisica = string_itoa(direccion_fisica_aux);
	int direccion_archivo_aux = obtener_valor(registro_puntero_archivo);
	char* direccion_archivo = string_itoa(direccion_archivo_aux);
	int tamanio_aux = obtener_valor(registro_tam);
	char* tamanio = string_itoa(tamanio_aux);

	t_paquete* paquete_a_enviar = crear_paquete();
	agregar_pcb_a_paquete(paquete_a_enviar, pcb_proceso);
	agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion)+1);
	agregar_a_paquete(paquete_a_enviar, interfaz, strlen(interfaz)+1);
	agregar_a_paquete(paquete_a_enviar, nombre_archivo, strlen(nombre_archivo)+1);
	agregar_a_paquete(paquete_a_enviar, direccion_fisica, strlen(direccion_fisica)+1);
	agregar_a_paquete(paquete_a_enviar, tamanio, strlen(tamanio)+1);
	agregar_a_paquete(paquete_a_enviar, direccion_archivo, strlen(direccion_archivo)+1);
	enviar_paquete(paquete_a_enviar, conexion_kernel);
	eliminar_paquete(paquete_a_enviar);

	free(direccion_fisica);
	free(direccion_archivo);
	free(tamanio);
}

void wait(char* instruccion, char* recurso) {
	log_info(logger, "PID: %s - Ejecutando: WAIT - %s", pcb_proceso->pid, recurso);

	t_paquete* paquete_a_enviar = crear_paquete();
	agregar_pcb_a_paquete(paquete_a_enviar, pcb_proceso);
	agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion)+1);
	agregar_a_paquete(paquete_a_enviar, recurso, strlen(recurso)+1);
	enviar_paquete(paquete_a_enviar, conexion_kernel);
	eliminar_paquete(paquete_a_enviar);
}

void signal(char* instruccion, char* recurso) {
	log_info(logger, "PID: %s - Ejecutando: SIGNAL - %s", pcb_proceso->pid, recurso);

	t_paquete* paquete_a_enviar = crear_paquete();
	agregar_pcb_a_paquete(paquete_a_enviar, pcb_proceso);
	agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion)+1);
	agregar_a_paquete(paquete_a_enviar, recurso, strlen(recurso)+1);
	enviar_paquete(paquete_a_enviar, conexion_kernel);
	eliminar_paquete(paquete_a_enviar);
}


void exit_prima() {
	log_info(logger, "PID: %s - Ejecutando: EXIT", pcb_proceso->pid);

	char* instruccion = string_duplicate("EXIT");

	t_paquete* paquete_a_enviar = crear_paquete();
	agregar_pcb_a_paquete(paquete_a_enviar, pcb_proceso);
	agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion)+1);
	enviar_paquete(paquete_a_enviar, conexion_kernel);
	eliminar_paquete(paquete_a_enviar);

	free(instruccion);
}

// ----------- Fin Instrucciones ----------- //

int obtener_valor(char* un_registro) {
	if(string_equals_ignore_case(un_registro, "PC")) {
		return pcb_proceso->registros.PC;
	}
	else if(string_equals_ignore_case(un_registro, "AX")) {
			return pcb_proceso->registros.AX;
	}
	else if(string_equals_ignore_case(un_registro, "BX")) {
			return pcb_proceso->registros.BX;
	}	
	else if(string_equals_ignore_case(un_registro, "CX")) {
			return pcb_proceso->registros.CX;
	}	
	else if(string_equals_ignore_case(un_registro, "DX")) {
			return pcb_proceso->registros.DX;
	}
	else if(string_equals_ignore_case(un_registro, "EAX")) {
			return pcb_proceso->registros.EAX;
	}	
	else if(string_equals_ignore_case(un_registro, "EBX")) {
			return pcb_proceso->registros.EBX;
	}	
	else if(string_equals_ignore_case(un_registro, "ECX")) {
			return pcb_proceso->registros.ECX;
	}	
	else if(string_equals_ignore_case(un_registro, "EDX")) {
			return pcb_proceso->registros.EDX;
	}	
	else if(string_equals_ignore_case(un_registro, "SI")) {
			return pcb_proceso->registros.SI;
	}
	else if(string_equals_ignore_case(un_registro, "DI")) {
			return pcb_proceso->registros.DI;
	}
	else {
		return 0; // seguir desde aca
	}
	
}

void setear_valor(char* registro, int valor) {
	if(string_equals_ignore_case(registro, "PC")) {
		pcb_proceso->registros.PC = valor;
	}
	else if(string_equals_ignore_case(registro, "AX")) {
		pcb_proceso->registros.AX = valor;
	}
	else if(string_equals_ignore_case(registro, "BX")) {
		pcb_proceso->registros.BX = valor;
	}
	else if(string_equals_ignore_case(registro, "CX")) {
		pcb_proceso->registros.CX = valor;
	}
	else if(string_equals_ignore_case(registro, "DX")) {
		pcb_proceso->registros.DX = valor;
	}
	else if(string_equals_ignore_case(registro, "EAX")) {
		pcb_proceso->registros.EAX = valor;
	}
	else if(string_equals_ignore_case(registro, "EBX")) {
		pcb_proceso->registros.EBX = valor;
	}
	else if(string_equals_ignore_case(registro, "ECX")) {
		pcb_proceso->registros.ECX = valor;
	}
	else if(string_equals_ignore_case(registro, "EDX")) {
		pcb_proceso->registros.EDX = valor;
	}
	else if(string_equals_ignore_case(registro, "SI")) {
		pcb_proceso->registros.SI = valor;
	}
	else if(string_equals_ignore_case(registro, "DI")) {
		pcb_proceso->registros.DI = valor;
	}
}

int obtener_tamanio_registro(char* un_registro) {
    if (string_equals_ignore_case(un_registro, "PC")) {
        return sizeof(pcb_proceso->registros.PC);
    }
    else if (string_equals_ignore_case(un_registro, "AX")) {
        return sizeof(pcb_proceso->registros.AX);
    }
    else if (string_equals_ignore_case(un_registro, "BX")) {
        return sizeof(pcb_proceso->registros.BX);
    }    
    else if (string_equals_ignore_case(un_registro, "CX")) {
        return sizeof(pcb_proceso->registros.CX);
    }    
    else if (string_equals_ignore_case(un_registro, "DX")) {
        return sizeof(pcb_proceso->registros.DX);
    }
    else if (string_equals_ignore_case(un_registro, "EAX")) {
        return sizeof(pcb_proceso->registros.EAX);
    }    
    else if (string_equals_ignore_case(un_registro, "EBX")) {
        return sizeof(pcb_proceso->registros.EBX);
    }    
    else if (string_equals_ignore_case(un_registro, "ECX")) {
        return sizeof(pcb_proceso->registros.ECX);
    }    
    else if (string_equals_ignore_case(un_registro, "EDX")) {
        return sizeof(pcb_proceso->registros.EDX);
    }    
    else if (string_equals_ignore_case(un_registro, "SI")) {
        return sizeof(pcb_proceso->registros.SI);
    }
    else if (string_equals_ignore_case(un_registro, "DI")) {
        return sizeof(pcb_proceso->registros.DI);
    }
    else {
        return 0; // Registro no encontrado
    }
}

void iterator(char* value) {
	log_info(logger,"%s", value);
}

void levantar_config(void){
	cpu_config.ip_memoria = config_get_string_value(config, "IP_MEMORIA");
	cpu_config.puerto_memoria = config_get_string_value(config, "PUERTO_MEMORIA");
	cpu_config.puerto_escucha_dispatch = config_get_string_value(config, "PUERTO_ESCUCHA_DISPATCH");
	cpu_config.puerto_escucha_interrupt = config_get_string_value(config, "PUERTO_ESCUCHA_INTERRUPT");
	cpu_config.cantidad_entradas_tlb = config_get_string_value(config, "CANTIDAD_ENTRADAS_TLB");
	cpu_config.algoritmo_tlb = config_get_string_value(config, "ALGORITMO_TLB");


	log_info(logger, "Se cargaron los siguientes datos de configuracion:");
    log_info(logger, "IP_MEMORIA: %s", cpu_config.ip_memoria);
    log_info(logger, "PUERTO_MEMORIA: %s", cpu_config.puerto_memoria);
    log_info(logger, "PUERTO_ESCUCHA_DISPATCH: %s", cpu_config.puerto_escucha_dispatch);
    log_info(logger, "PUERTO_ESCUCHA_INTERRUPT: %s", cpu_config.puerto_escucha_interrupt);
    log_info(logger, "CANTIDAD_ENTRADAS_TLB: %s", cpu_config.cantidad_entradas_tlb);
    log_info(logger, "ALGORITMO_TLB: %s", cpu_config.algoritmo_tlb);
}

