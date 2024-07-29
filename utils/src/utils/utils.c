#include <utils/utils.h>

void decir_hola(char* quien) {
    printf("Hola desde %s!!\n", quien);
}

t_log* iniciar_logger(char *file, char *process_name)
{ 
	t_log* logger;

	if((logger = log_create(file, process_name, 1, LOG_LEVEL_INFO)) == NULL){
		printf("[ERROR] No se pudo crear el logger,\n");
		exit(1);
	}

	log_info(logger, "Iniciando el logger de %s \n", file);

	return logger;
}

t_config* iniciar_config(char *path, t_log* logger)
{

	t_config* nuevo_config;
	
	if((nuevo_config = config_create(path)) == NULL){
		log_error(logger," No se pudo crear un nuevo config \n");
		exit(1);
	}
		
	return nuevo_config;
}

int iniciar_servidor(char* PUERTO)
{
	int socket_servidor;

	struct addrinfo hints, *server_info; // *p;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;


	getaddrinfo(NULL, PUERTO, &hints, &server_info);

	// Creamos el socket de escucha del servidor
	

	socket_servidor = socket(server_info->ai_family,
                    server_info->ai_socktype,
                    server_info->ai_protocol);

	// Asociamos el socket a un puerto
	bind(socket_servidor, server_info->ai_addr, server_info->ai_addrlen);

	// Escuchamos las conexiones entrantes
	listen(socket_servidor, SOMAXCONN);

	freeaddrinfo(server_info);

	return socket_servidor;
}

int esperar_cliente(int socket_servidor)
{

	uint32_t handshake;
	uint32_t resultOk = 0;
	uint32_t resultError = -1;

	// Aceptamos un nuevo cliente

	int socket_cliente = accept(socket_servidor, NULL, NULL);

	recv(socket_cliente, &handshake, sizeof(uint32_t), MSG_WAITALL);

	if(handshake == 1)
   		send(socket_cliente, &resultOk, sizeof(uint32_t), 0);
	else
   		send(socket_cliente, &resultError, sizeof(uint32_t), 0);

	log_info(logger, "Se conecto un cliente!");

	return socket_cliente;
}

int recibir_operacion(int socket_cliente)
{
	int cod_op;
	if(recv(socket_cliente, &cod_op, sizeof(int), MSG_WAITALL) > 0)
	{
		return cod_op;
	}
	else
	{
		close(socket_cliente);
		return -1;
	}
}

void* recibir_buffer(int* size, int socket_cliente)
{
	void * buffer;

	recv(socket_cliente, size, sizeof(int), MSG_WAITALL);
	buffer = malloc(*size);
	recv(socket_cliente, buffer, *size, MSG_WAITALL);

	return buffer;
}

void recibir_mensaje(int socket_cliente)
{
	int size;
	char* buffer = recibir_buffer(&size, socket_cliente);
	//log_info(logger, "Me llego el mensaje %s", buffer);
	free(buffer);
}

void recibir_mensaje_prima(int socket_cliente, t_log* logger_portatil)
{
	int size;
	char* buffer = recibir_buffer(&size, socket_cliente);
	//log_info(logger_portatil, "Me llego el mensaje %s", buffer);
	free(buffer);
}

char* leer_mensaje(int socket_cliente) {
	int size;
	char* buffer = recibir_buffer(&size, socket_cliente);
	//log_info(logger, "Leo el mensaje: %s", buffer);
	return buffer;
}


t_list* recibir_paquete(int socket_cliente)
{
	int size;
	int desplazamiento = 0;
	void * buffer;
	t_list* valores = list_create();
	int tamanio;
	buffer = recibir_buffer(&size, socket_cliente);
	while(desplazamiento < size)
	{
		memcpy(&tamanio, buffer + desplazamiento, sizeof(int));
		desplazamiento+=sizeof(int);
		char* valor = malloc(tamanio);
		memcpy(valor, buffer+desplazamiento, tamanio);
		desplazamiento+=tamanio;
		list_add(valores, valor);
	}
	free(buffer);
	return valores;
}

int crear_conexion(char *ip, char* puerto)
{
	struct addrinfo hints;
	struct addrinfo *server_info;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	getaddrinfo(ip, puerto, &hints, &server_info);

	// Ahora vamos a crear el socket.
	int socket_cliente = socket(server_info->ai_family,
                    server_info->ai_socktype,
                    server_info->ai_protocol);

	// Ahora que tenemos el socket, vamos a conectarlo

	connect(socket_cliente, server_info->ai_addr, server_info->ai_addrlen);

	freeaddrinfo(server_info);

	return socket_cliente;
}

void enviar_mensaje(char* mensaje, int socket_cliente)
{
	t_paquete* paquete = malloc(sizeof(t_paquete));

	paquete->codigo_operacion = MENSAJE;
	paquete->buffer = malloc(sizeof(t_buffer));
	paquete->buffer->size = strlen(mensaje) + 1;
	paquete->buffer->stream = malloc(paquete->buffer->size);
	memcpy(paquete->buffer->stream, mensaje, paquete->buffer->size);

	int bytes = paquete->buffer->size + 2*sizeof(int);

	void* a_enviar = serializar_paquete(paquete, bytes);

	send(socket_cliente, a_enviar, bytes, 0);

	free(a_enviar);
	eliminar_paquete(paquete);
}

void* serializar_paquete(t_paquete* paquete, int bytes)
{
	void * magic = malloc(bytes);
	int desplazamiento = 0;

	memcpy(magic + desplazamiento, &(paquete->codigo_operacion), sizeof(int));
	desplazamiento+= sizeof(int);
	memcpy(magic + desplazamiento, &(paquete->buffer->size), sizeof(int));
	desplazamiento+= sizeof(int);
	memcpy(magic + desplazamiento, paquete->buffer->stream, paquete->buffer->size);
	desplazamiento+= paquete->buffer->size;

	return magic;
}

void crear_buffer(t_paquete* paquete)
{
	paquete->buffer = malloc(sizeof(t_buffer));
	paquete->buffer->size = 0;
	paquete->buffer->stream = NULL;
}

t_paquete* crear_paquete(void)
{
	t_paquete* paquete = malloc(sizeof(t_paquete));
	paquete->codigo_operacion = PAQUETE;
	crear_buffer(paquete);
	return paquete;
}

void agregar_a_paquete(t_paquete* paquete, void* valor, int tamanio)
{
	paquete->buffer->stream = realloc(paquete->buffer->stream, paquete->buffer->size + tamanio + sizeof(int));

	memcpy(paquete->buffer->stream + paquete->buffer->size, &tamanio, sizeof(int));
	memcpy(paquete->buffer->stream + paquete->buffer->size + sizeof(int), valor, tamanio);

	paquete->buffer->size += tamanio + sizeof(int);
}

void enviar_paquete(t_paquete* paquete, int socket_cliente)
{
	int bytes = paquete->buffer->size + 2*sizeof(int);
	void* a_enviar = serializar_paquete(paquete, bytes);

	send(socket_cliente, a_enviar, bytes, 0);

	free(a_enviar);
}

void eliminar_paquete(t_paquete* paquete)
{
	free(paquete->buffer->stream);
	free(paquete->buffer);
	free(paquete);
}

void liberar_conexion(int socket_cliente)
{
	close(socket_cliente);
}

int realizar_handshake(char* ip, char* puerto, char* quien_realiza_solicitud, char* con_quien_se_conecta){
	int conexion = crear_conexion(ip, puerto);
	
	uint32_t handshake = 1;
	uint32_t result;

	int a = send(conexion, &handshake, sizeof(uint32_t), MSG_NOSIGNAL);
	if (a == -1) log_error(logger, "socket send failed");
	recv(conexion, &result, sizeof(uint32_t), MSG_WAITALL);

	if(result == -1){
		log_error(logger, "No se pudo realizar el handshake \n");
	}else if (result == 0) {
		log_info(logger, "Se realizo el handshake solicitado por: %s, con: %s ", quien_realiza_solicitud, con_quien_se_conecta);
	}
	
	return conexion;
}

char** separar_instruccion(char* cadena, char* separador){
	char* instrucciones;
	instrucciones = cadena;
	char** array_instrucciones = string_split(instrucciones, separador);
	return array_instrucciones;
}

void terminar_programa(int conexion, t_log* logger, t_config* config)
{
	/* Y por ultimo, hay que liberar lo que utilizamos (conexion, log y config) 
	  con las funciones de las commons y del TP mencionadas en el enunciado */
	config_destroy(config);
	log_destroy(logger);
	close(conexion);
	
}

void agregar_pcb_a_paquete(t_paquete* paquete, pcb* pcb_a_enviar) {
	char* program_counter = string_itoa(pcb_a_enviar->program_counter);
	char* quantum = string_itoa(pcb_a_enviar->quantum);

    agregar_a_paquete(paquete, pcb_a_enviar->pid, strlen(pcb_a_enviar->pid)+1);
    agregar_a_paquete(paquete, program_counter, strlen(program_counter)+1);
    agregar_a_paquete(paquete, quantum, strlen(quantum)+1);
    agregar_registros_a_paquete(pcb_a_enviar->registros, paquete);

	free(program_counter);
	free(quantum);
}

void agregar_registros_a_paquete(registros_cpu registros, t_paquete* paquete) {
	char* PC = string_itoa(registros.PC);
	char* AX = string_itoa(registros.AX);
	char* BX = string_itoa(registros.BX);
	char* CX = string_itoa(registros.CX);
	char* DX = string_itoa(registros.DX);
	char* EAX = string_itoa(registros.EAX);
	char* EBX = string_itoa(registros.EBX);
	char* ECX = string_itoa(registros.ECX);
	char* EDX = string_itoa(registros.EDX);
	char* SI = string_itoa(registros.SI);
	char* DI = string_itoa(registros.DI);

    agregar_a_paquete(paquete, PC, strlen(PC)+1);
    agregar_a_paquete(paquete, AX, strlen(AX)+1);
    agregar_a_paquete(paquete, BX, strlen(BX)+1);
    agregar_a_paquete(paquete, CX, strlen(CX)+1);
    agregar_a_paquete(paquete, DX, strlen(DX)+1);
    agregar_a_paquete(paquete, EAX, strlen(EAX)+1);
    agregar_a_paquete(paquete, EBX, strlen(EBX)+1);
    agregar_a_paquete(paquete, ECX, strlen(ECX)+1);
    agregar_a_paquete(paquete, EDX, strlen(EDX)+1);
    agregar_a_paquete(paquete, SI, strlen(SI)+1);
    agregar_a_paquete(paquete, DI, strlen(DI)+1);

	free(PC);
	free(AX);
	free(BX);
	free(CX);
	free(DX);
	free(EAX);
	free(EBX);
	free(ECX);
	free(EDX);
	free(SI);
	free(DI);
}

void cargar_registros(pcb* pcb, t_list* lista) {
	pcb->registros.PC = atoi(list_get(lista, 0));
	pcb->registros.AX = atoi(list_get(lista, 1));
	pcb->registros.BX = atoi(list_get(lista, 2));
	pcb->registros.CX = atoi(list_get(lista, 3));
	pcb->registros.DX = atoi(list_get(lista, 4));
	pcb->registros.EAX = atoi(list_get(lista, 5));
	pcb->registros.EBX = atoi(list_get(lista, 6));
	pcb->registros.ECX = atoi(list_get(lista, 7));
	pcb->registros.EDX = atoi(list_get(lista, 8));
	pcb->registros.SI = atoi(list_get(lista, 9));
	pcb->registros.DI = atoi(list_get(lista, 10));
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
}

void cargar_pcb(pcb* nuevo_pcb, t_list* lista){
	nuevo_pcb->pid = string_duplicate(list_get(lista, 0));
	nuevo_pcb->program_counter = atoi(list_get(lista, 1));
	nuevo_pcb->quantum = atof(list_get(lista, 2));
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	list_remove_and_destroy_element(lista, 0, (void*)element_destroy);
	cargar_registros(nuevo_pcb, lista);	
}

void levantar_pcb(pcb* pcb) {
	log_info(logger, "Pid: %s", pcb->pid);
	log_info(logger, "Program counter: %d", pcb->program_counter);
	log_info(logger, "Quantum: %f", pcb->quantum);
	log_info(logger, "PC: %d", pcb->registros.PC);
	log_info(logger, "AX: %d", pcb->registros.AX);
	log_info(logger, "BX: %d", pcb->registros.BX);
	log_info(logger, "CX: %d", pcb->registros.CX);
	log_info(logger, "DX: %d", pcb->registros.DX);
	log_info(logger, "EAX: %d", pcb->registros.EAX);
	log_info(logger, "EBX: %d", pcb->registros.EBX);
	log_info(logger, "ECX: %d", pcb->registros.ECX);
	log_info(logger, "EDX: %d", pcb->registros.EDX);
	log_info(logger, "SI: %d", pcb->registros.SI);
	log_info(logger, "DI: %d", pcb->registros.DI);
}

void element_destroy(void* self){
	free(self);
}

void list_destroy_with_elements(void* list) {
    list_destroy_and_destroy_elements((t_list*)list, element_destroy);
}
