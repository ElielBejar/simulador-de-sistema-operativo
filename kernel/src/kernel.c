#include "kernel.h"

int main(int argc, char *argv[])
{

    logger = iniciar_logger("kernel.log", "KERNEL");
    config = iniciar_config(argv[1], logger);
    diccionario_conexiones_io = dictionary_create();
    diccionario_instrucciones_permitidas_io = dictionary_create();
    levantar_config();
    iniciar_colas_procesos();
    server_fd = iniciar_servidor(kernel_config.puerto_escucha);

    // --- Creamos una conexión hacia el cpu puerto dispatch, interrupt y memoria --- //

    conexion_cpu_dispatch = realizar_handshake(kernel_config.ip_cpu, kernel_config.puerto_cpu_dispatch, "kernel", "cpu_dispatch");
    conexion_cpu_interrupt = realizar_handshake(kernel_config.ip_cpu, kernel_config.puerto_cpu_interrupt, "kernel", "cpu_interrupt");
    conexion_memoria = realizar_handshake(kernel_config.ip_memoria, kernel_config.puerto_memoria, "kernel", "memoria");

    send(conexion_memoria, &identificador, sizeof(uint32_t), 0);

    enviar_mensaje("Hola soy kernel", conexion_cpu_dispatch);
    enviar_mensaje("Hola soy kernel", conexion_cpu_interrupt);
    enviar_mensaje("Hola soy kernel", conexion_memoria);

    // --- Inicializa semaforos --- //
    iniciar_semaforos();

    // --- Inicializa kernel como servidor --- //
    iniciar_hilos_kernel();

    // --- Limpia kernel --- //
    limpiar_kernel();

    // Aca habria que agregar un "terminar_prograga" quiza soluciona posible error de broken pipe
    return 0;
}

void iniciar_colas_procesos()
{
    cola_new = queue_create();
    cola_ready = queue_create();
    cola_exec = queue_create();
    cola_exit = queue_create();
    colas_block_io = list_create();
    colas_block_recursos = list_create();
    cola_ready_m = queue_create();
    lista_estados = list_create();
    lista_estados_logueables = list_create();
    lista_pids_ready = list_create();
    lista_pids_ready_m = list_create();
    lista_recursos_retenidos = list_create();

    char **recursos = string_get_string_as_array(kernel_config.recursos);
    char **instancias_recursos = string_get_string_as_array(kernel_config.instancias_recursos);

    for (int i = 0; i < string_array_size(recursos); i++)
    {
        cola_block_recurso *cola_r = malloc(sizeof(cola_block_recurso));
        cola_r->identificador = string_duplicate(recursos[i]);
        cola_r->cola = queue_create();
        cola_r->instancias_recurso = atoi(instancias_recursos[i]);
        if (sem_init(&cola_r->sem_mutex_recurso, 0, 1) == -1)
        {
            log_error(logger, "Hubo un problema al inicializar el sem_desalojo");
        }
        list_add(colas_block_recursos, cola_r);
    }

    string_array_destroy(recursos);
    string_array_destroy(instancias_recursos);

    for (int i = 0; i < 6; i++)
    {
        t_list* sublista_pids_en_estado = list_create();
        list_add_in_index(lista_estados_logueables, i, sublista_pids_en_estado);
    }
}

void iniciar_hilos_kernel()
{
    int err;

    err = pthread_create(&(hilos_kernel[0]), NULL, comenzar_servidor, NULL);
    if (err != 0)
    {
        log_error(logger, "Hubo un problema al crear el thread del servidor:[%s]", strerror(err));
        exit(1);
    }
    log_info(logger, "El thread del servidor comenzo");

    err = pthread_create(&(hilos_kernel[1]), NULL, comenzar_consola_interactiva, NULL);
    if (err != 0)
    {
        log_error(logger, "Hubo un problema al crear el thread de la consola interactiva:[%s]", strerror(err));
        exit(1);
    }
    log_info(logger, "El thread de la consola interactiva comenzo");

    err = pthread_create(&(hilos_kernel[2]), NULL, administrar_procesos_new, NULL);
    if (err != 0)
    {
        log_error(logger, "hubo un problema al intentar administrar procesos new:[%s]", strerror(err));
        exit(1);
    }
    log_info(logger, "El thread de la consola administrar procesos new");
    err = pthread_create(&(hilos_kernel[3]), NULL, planificacion_de_procesos, NULL);
    if (err != 0)
    {
        log_error(logger, "Hubo un problema al crear el thread de planificacion de procesos:[%s]", strerror(err));
        exit(1);
    }
    log_info(logger, "El thread de la consola planificacion de procesos");

    err = pthread_create(&(hilos_kernel[4]), NULL, (void *)recibir_respuesta_cpu, NULL);
    if (err != 0)
    {
        log_error(logger, "Hubo un problema al crear el thread de planificacion de procesos:[%s]", strerror(err));
        exit(1);
    }
    log_info(logger, "El thread de la consola planificacion de procesos");

    pthread_join(hilos_kernel[0], NULL);
    pthread_join(hilos_kernel[1], NULL);
    pthread_join(hilos_kernel[2], NULL);
    pthread_join(hilos_kernel[3], NULL);
    pthread_join(hilos_kernel[4], NULL);
    
}

void *comenzar_consola_interactiva(void *arg)
{
    char *leido;

    log_info(logger, "Se esta leyendo la consola \n");

    while (1)
    {
        //log_info(logger, "while 1");
        leido = readline("> "); // agregue esto
        char **info_procesada = string_split(leido, " ");
        recibir_info(info_procesada[0], info_procesada[1]);
        string_array_destroy(info_procesada);
        free(leido); // agregue esto
    }
}

void *comenzar_servidor(void *arg)
{
    
    while (1)
    {
        //log_info(logger, "while 2");
        pthread_t thread;
        int socket_cliente = esperar_cliente(server_fd);
        pthread_create(&thread,
                       NULL,
                       (void *)atender_interfaz,
                       (void *)(intptr_t)socket_cliente);
        pthread_detach(thread);
    }

    return (0);
}

void *atender_interfaz(int conexion)
{
    log_info(logger, "Servidor recibio nuevo cliente interfaz");
    recibir_operacion(conexion);
    t_list *lista /*= list_create()*/;
    lista = recibir_paquete(conexion);
    char *id_interfaz = list_remove(lista, 0);
    char *instrucciones_permitidas = list_remove(lista, 0);

    log_info(logger, "Interfaz identificada, nombre: %s, sus instrucciones permitidas son: %s", id_interfaz, instrucciones_permitidas);
    dictionary_put(diccionario_conexiones_io, id_interfaz, &conexion);

    if (!dictionary_has_key(diccionario_instrucciones_permitidas_io, id_interfaz))
    {
        dictionary_put(diccionario_instrucciones_permitidas_io, id_interfaz, instrucciones_permitidas);

        // --- Cola de bloqueo, contiene la cola de io con sus procesos en bloqueado, su identificador y un semaforo que indica si esta libre o no --- //
        log_info(logger, "Inicializo estructuras requeridas para: %s", id_interfaz);

        cola_block_io *cola_aux = malloc(sizeof(cola_block_io));
        cola_aux->cola = queue_create();
        cola_aux->identificador = string_duplicate(id_interfaz);

        if (sem_init(&cola_aux->habilitacion_cola, 0, 1) == -1)
        {
            log_error(logger, "Hubo un problema al inicializar el habilitacion_cola");
        }
        if (sem_init(&cola_aux->sem_hay_proceso, 0, 0) == -1)
        {
            log_error(logger, "Hubo un problema al inicializar el sem_hay_proceso");
        }

        list_add(colas_block_io, cola_aux);
    }
    
    list_destroy_and_destroy_elements(lista, (void *)element_destroy);
    
    //t_list* lista_paquete = list_create(); 
    t_list* lista_paquete;

    while (1)
    {
        //log_info(logger, "while 3");
        int cod_op = recibir_operacion(conexion);
        switch (cod_op)
        {
        case MENSAJE:
            // --- Por ahora solo esta para que reciba el fin de io_gen_sleep, va a haber que separar toda la logica en una funcion aparte --- //
            // log_info(logger, "Recibi un mensaje de una io");
            char *mensaje = leer_mensaje(conexion);
            if (string_equals_ignore_case(mensaje, "END_OF_IO_GEN_SLEEP"))
            {
                cola_block_io *cola = encontrar_cola(id_interfaz);

                // sem_wait(&cola->sem_hay_proceso);

                pcb *pcb_desbloqueado = queue_pop(cola->cola);
                // --- Cuando inicio paso a ready segun su planificador --- //

                iniciar(pcb_desbloqueado, "END_OF_IO_GEN_SLEEP");
                // --- Se libera la io para otros procesos --- //
                // sem_post(&cola->habilitacion_cola);
            }
            else if (string_equals_ignore_case(mensaje, "END_OF_IO_STDOUT_WRITE"))
            {
                cola_block_io *cola = encontrar_cola(id_interfaz);

                // sem_wait(&cola->sem_hay_proceso);

                pcb *pcb_desbloqueado = queue_pop(cola->cola);
                // --- Cuando inicio paso a ready segun su planificador --- //

                iniciar(pcb_desbloqueado, "END_OF_IO_STDOUT_WRITE");
                // --- Se libera la io para otros procesos --- //
                // sem_post(&cola->habilitacion_cola);
            }
            else if (string_equals_ignore_case(mensaje, "END_OF_IO_STDIN_READ"))
            {
                cola_block_io *cola = encontrar_cola(id_interfaz);

                // sem_wait(&cola->sem_hay_proceso);

                pcb *pcb_desbloqueado = queue_pop(cola->cola);
                // --- Cuando inicio paso a ready segun su planificador --- //

                iniciar(pcb_desbloqueado, "END_OF_IO_STDIN_READ");
                // --- Se libera la io para otros procesos --- //
                // sem_post(&cola->habilitacion_cola);
            }
            else if (string_equals_ignore_case(mensaje, "END_OF_IO_FS_CREATE"))
            {
                cola_block_io *cola = encontrar_cola(id_interfaz);

                // sem_wait(&cola->sem_hay_proceso);

                pcb *pcb_desbloqueado = queue_pop(cola->cola);
                // --- Cuando inicio paso a ready segun su planificador --- //

                iniciar(pcb_desbloqueado, "END_OF_IO_FS_CREATE");
                // --- Se libera la io para otros procesos --- //
                // sem_post(&cola->habilitacion_cola);
            }
            else if (string_equals_ignore_case(mensaje, "END_OF_IO_FS_TRUNCATE"))
            {
                cola_block_io *cola = encontrar_cola(id_interfaz);

                // sem_wait(&cola->sem_hay_proceso);

                pcb *pcb_desbloqueado = queue_pop(cola->cola);
                // --- Cuando inicio paso a ready segun su planificador --- //

                iniciar(pcb_desbloqueado, "END_OF_IO_FS_TRUNCATE");
                // --- Se libera la io para otros procesos --- //
                // sem_post(&cola->habilitacion_cola);
            }
            else if (string_equals_ignore_case(mensaje, "END_OF_IO_FS_WRITE"))
            {
                cola_block_io *cola = encontrar_cola(id_interfaz);

                // sem_wait(&cola->sem_hay_proceso);

                pcb *pcb_desbloqueado = queue_pop(cola->cola);
                // --- Cuando inicio paso a ready segun su planificador --- //

                iniciar(pcb_desbloqueado, "END_OF_IO_FS_WRITE");
                // --- Se libera la io para otros procesos --- //
                // sem_post(&cola->habilitacion_cola);
            }
            else if (string_equals_ignore_case(mensaje, "END_OF_IO_FS_READ"))
            {
                cola_block_io *cola = encontrar_cola(id_interfaz);

                // sem_wait(&cola->sem_hay_proceso);

                pcb *pcb_desbloqueado = queue_pop(cola->cola);
                // --- Cuando inicio paso a ready segun su planificador --- //

                iniciar(pcb_desbloqueado, "END_OF_IO_FS_READ");
                // --- Se libera la io para otros procesos --- //
                // sem_post(&cola->habilitacion_cola);
            }
            else if (string_equals_ignore_case(mensaje, "END_OF_IO_FS_DELETE"))
            {
                cola_block_io *cola = encontrar_cola(id_interfaz);

                // sem_wait(&cola->sem_hay_proceso);

                pcb *pcb_desbloqueado = queue_pop(cola->cola);
                // --- Cuando inicio paso a ready segun su planificador --- //

                iniciar(pcb_desbloqueado, "END_OF_IO_FS_DELETE");
                // --- Se libera la io para otros procesos --- //
                // sem_post(&cola->habilitacion_cola);
            }

            free(mensaje);
            break;
        case PAQUETE:
            lista_paquete = recibir_paquete(conexion);
            conexion_io = conexion;
            log_info(logger, "Me llegaron los siguientes valores:\n");
            list_iterate(lista_paquete, (void *)iterator);
            list_clean(lista_paquete);
            break;
        case -1:
            log_error(logger, "el cliente se desconecto. Terminando servidor");
            return 0;
        default:
            log_warning(logger, "Operacion desconocida. No quieras meter la pata");
            break;
        }
    }

    list_destroy_and_destroy_elements(lista_paquete, (void *)element_destroy);
    free(id_interfaz);
}

void recibir_respuesta_cpu(void *arg)
{
    char *motivo;
    while (1)
    {
        int cod_op = recibir_operacion(conexion_cpu_dispatch);
        switch (cod_op)
        {
        case MENSAJE:
            char* mensaje = leer_mensaje(conexion_cpu_dispatch);

            if (fin_por_quantum && !desaloja_por_instruccion)
            {
                // log_info(logger, "Envio el mensaje NO_CONTINUA_EJECUTANDO a cpu");
                enviar_mensaje("NO_CONTINUA_EJECUTANDO", conexion_cpu_interrupt);
                sem_post(&sem_desalojo_quantum);
            }
            else
            {
                if (desaloja_por_instruccion)
                {
                    // log_info(logger, "Envio el mensaje NO_CONTINUA_EJECUTANDO a cpu");
                    enviar_mensaje("NO_CONTINUA_EJECUTANDO", conexion_cpu_interrupt);
                    replanificar(motivo);
                    free(motivo);
                }
                else if (desaloja_por_usuario)
                {
                    // log_info(logger, "Envio el mensaje NO_CONTINUA_EJECUTANDO a cpu");
                    enviar_mensaje("NO_CONTINUA_EJECUTANDO", conexion_cpu_interrupt);
                    sem_post(&sem_caso_exit_usuario);
                    // sem_wait(&sem_caso_exit_usuario_p);
                    //  replanificar("INTERRUPTED_BY_USER");

                    desaloja_por_usuario = false;
                }
                else
                {
                    // sem_wait(&sem_caso_exit_usuario);
                    // log_info(logger, "Envio el mensaje CONTINUA_EJECUTANDO a cpu");
                    enviar_mensaje("CONTINUA_EJECUTANDO", conexion_cpu_dispatch);
                }
            }

            free(mensaje);
            break;
        case PAQUETE:
            t_list *lista /*= list_create()*/;

            lista = recibir_paquete(conexion_cpu_dispatch);
            pcb *pcb_desalojo = queue_peek(cola_exec);
            free(pcb_desalojo->pid);
            cargar_pcb(pcb_desalojo, lista);

            motivo = list_remove(lista, 0);
            atender_solicitud_cpu(lista, pcb_desalojo, motivo);

            list_destroy_and_destroy_elements(lista, (void *)element_destroy);
            break;
        case -1:
            log_error(logger, "el servidor se desconectó. Termino servidor");
            exit(1);
        default:
            log_warning(logger, "Operacion desconocida. No quieras meter la pata");
            break;
        }
    }
}

// ----

void cambiar_estado(char *pid, char *nuevo_estado)
{
    char* estado_viejo;

    if (string_equals_ignore_case(nuevo_estado, "NEW"))
    {
        list_add(lista_estados, nuevo_estado);
        // NO HAGO NINGUN LOG YA QUE ESTA DENTRO DE GENERAR_PROCESO

        estado_viejo = string_new();
        cambiar_estado_logueable(estado_viejo, nuevo_estado, pid);

        free(estado_viejo);
    }
    else
    {
        sem_wait(&mutex_lista_estados);

        estado_viejo = list_remove(lista_estados, (atoi(pid)-1));
        log_info(logger, "PID: %s - Estado Anterior: %s - Estado Actual: %s", pid, estado_viejo, nuevo_estado);

        cambiar_estado_logueable(estado_viejo, nuevo_estado, pid);

        free(estado_viejo);

        list_add_in_index(lista_estados, (atoi(pid) - 1), nuevo_estado);

        sem_post(&mutex_lista_estados);
    }

}

void cambiar_estado_logueable(char* estado_viejo, char* nuevo_estado, char* pid)
{
    bool tiene_el_mismo_pid(char* pid_elemento)
    {
        return string_equals_ignore_case(pid_elemento, pid);
    };

    char* estado = estado_viejo;
    int flag = 1, numero_estado;
    t_list* sublista_pids/* = list_create()*/;

    while(flag != 0)
    {
        //log_info(logger, "while 5");
        if (string_equals_ignore_case(estado, "NEW"))
        {
            sublista_pids = list_remove(lista_estados_logueables, 0);
            numero_estado = 0;
        } 
        else if(string_equals_ignore_case(estado, "READY"))
        {
            sublista_pids = list_remove(lista_estados_logueables, 1);
            numero_estado = 1;
        }
        else if(string_equals_ignore_case(estado, "READY+"))
        {
            sublista_pids = list_remove(lista_estados_logueables, 2);
            numero_estado = 2;
        }
        else if(string_equals_ignore_case(estado, "EXEC"))
        {
            sublista_pids = list_remove(lista_estados_logueables, 3);
            numero_estado = 3;
        }
        else if(string_contains(estado, "BLOCK"))
        {
            sublista_pids = list_remove(lista_estados_logueables, 4);
            numero_estado = 4;
        }
        else if(string_equals_ignore_case(estado, "EXIT"))
        {
            sublista_pids = list_remove(lista_estados_logueables, 5);
            numero_estado = 5;
        }

        if(flag == 1) // quitar pid de estado anterior (a menos que no exista estado_viejo)
        {
            if(!string_is_empty(estado))
            {
                list_remove_and_destroy_by_condition(sublista_pids, (void *)tiene_el_mismo_pid, (void*)element_destroy);
                list_add_in_index(lista_estados_logueables, numero_estado, sublista_pids);
            }
            estado = nuevo_estado;
            flag = 2;
        }
        else // agregar pid a estado nuevo
        {
            char* pid_a_sublista = string_duplicate(pid);
            list_add(sublista_pids, pid_a_sublista);
            list_add_in_index(lista_estados_logueables, numero_estado, sublista_pids);
            flag = 0;
        }
    }
    
}

void recibir_info(char *motivo, char *posible_argumento)
{
    if (string_equals_ignore_case(motivo, "EJECUTAR_SCRIPT"))
    {
        log_info(logger, "EJECUTAR_SCRIPT : %s", posible_argumento);
        ejecutar_script(posible_argumento);
    }
    else if (string_equals_ignore_case(motivo, "INICIAR_PROCESO"))
    {
        log_info(logger, "INICIAR_PROCESO: %s", posible_argumento);
        pcb *pcb_nuevo = generar_nuevo_proceso();

        queue_push(cola_new, pcb_nuevo);
        char* estado_nuevo = string_duplicate("NEW");
        cambiar_estado(pcb_nuevo->pid, estado_nuevo);

        mandar_pedido(conexion_memoria, motivo, posible_argumento, pcb_nuevo->pid);

        sem_post(&sem_hay_procesos_en_new);
    }
    else if (string_equals_ignore_case(motivo, "FINALIZAR_PROCESO"))
    {
        log_info(logger, "FINALIZAR_PROCESO: %s", posible_argumento);
        pcb *pcb_exit = encontrar_pcb(posible_argumento);
        char *anterior_estado = list_get(lista_estados, atoi(pcb_exit->pid) - 1);

        if (string_equals_ignore_case(anterior_estado, "EXEC"))
        { // YA QUE ESTARIA EN CPU, ENTONCES HAY QUE DESALOJARLO (PRIMERO ESPERO A Q FINALICE LA INSTRUCCION)
            desaloja_por_usuario = true;

            sem_wait(&sem_caso_exit_usuario);

            pedido_de_desalojo_prima(motivo, pcb_exit->pid);
        }

        exit_prima(pcb_exit, "INTERRUPTED_BY_USER");
    }
    else if (string_equals_ignore_case(motivo, "DETENER_PLANIFICACION"))
    {
        log_info(logger, " DETENER_PLANIFICACION");
        sem_wait(&sem_planificacion);
    }
    else if (string_equals_ignore_case(motivo, "INICIAR_PLANIFICACION"))
    {
        log_info(logger, "INICIAR_PLANIFICACION");
        sem_post(&sem_planificacion);
    }
    else if (string_equals_ignore_case(motivo, "PROCESO_ESTADO"))
    {
        log_info(logger, "PROCESO_ESTADO");
        mostrar_lista_estados_logueables();
    }
    else if (string_equals_ignore_case(motivo, "MULTIPROGRAMACION"))
    {
        log_info(logger, "MULTIPROGRAMACION");
        int nuevo_grado = atoi(posible_argumento);
        if(grado_multiprogramacion > nuevo_grado){
            for(int i = 0; grado_multiprogramacion - nuevo_grado > i; i++){
                sem_wait(&sem_multiprogramacion);
            }
        }else if( nuevo_grado > grado_multiprogramacion) {
            for(int i = 0; nuevo_grado - grado_multiprogramacion > i; i++){
                sem_post(&sem_multiprogramacion);
            }
        }
    }
    else
    {
        log_error(logger, "Solicitud de tarea no reconocida: %s, vuelva a intentarlo", motivo);
    }
}

// UNICAMENTE USAR CUANDO EL PROCESO VA A SER FINALIZADO

pcb *encontrar_pcb(char *pid)
{
    char *estado_proceso = list_get(lista_estados, (atoi(pid) - 1));

    // TODOS ESTOS HACEN UN QUEUE_POP ASI QUE VA A HABER QUE PROTEJER A LAS LISTAS PARA QUE NO SAQUEN ELEMENTO DE COLA VACIA
    // COMO EL GRADO DE MULTIPROGRAMACION VARIA SEGUN READY, READY+ Y EXEC HACEMOS SEM_POST AL RETIRARLOS DE LAS MISMAS

    if (string_equals_ignore_case(estado_proceso, "NEW"))
    {
        return pcb_en_cola(cola_new, pid);
    }
    if (string_equals_ignore_case(estado_proceso, "READY"))
    {
        sem_post(&sem_multiprogramacion);
        return pcb_en_cola(cola_ready, pid);
    }
    if (string_equals_ignore_case(estado_proceso, "READY+"))
    {
        sem_post(&sem_multiprogramacion);
        return pcb_en_cola(cola_ready_m, pid);
    }
    if (string_equals_ignore_case(estado_proceso, "EXEC"))
    {
        sem_post(&sem_multiprogramacion);
        return pcb_en_cola(cola_exec, pid);
    }
    // EN EL SIGUIENTE CASO ES DONDE CAMBIA EL TIPO DE ESTADO, FORMATO: "BLOCK - IO/RECURSO: NOMBRE"
    else
    {
        char **array = string_split(estado_proceso, " ");
        int tam = string_array_size(array);
        char *nombre = array[tam - 1];
        // ACA ME RAMIFICO ENTRE IO O RECURSO
        pcb *pcb_finalizar;
        
        if (string_contains(estado_proceso, "IO"))
        {
            cola_block_io *cola_io = encontrar_cola(nombre);

            sem_wait(&cola_io->sem_hay_proceso);

            pcb_finalizar = pcb_en_cola(cola_io->cola, pid);
        }
        else
        { // Si no contiene IO, pero si block entonces es recurso
            cola_block_recurso *cola_recurso = encontrar_cola_recurso(nombre);

            sem_wait(&cola_recurso->sem_mutex_recurso);

            pcb_finalizar = pcb_en_cola(cola_recurso->cola, pid);
            sem_post(&cola_recurso->sem_mutex_recurso);
            cola_recurso->instancias_recurso++;
            quitar_de_lista_recursos_retenidos(nombre, pcb_finalizar->pid);
        }

        string_array_destroy(array);
        return pcb_finalizar;
    }
}

pcb *pcb_en_cola(t_queue *cola, char *pid)
{
    int tam_cola = queue_size(cola);

    pcb *pcb_buscado;
    pcb *pcb_encontrado;

    // Como estamos usando colas para retirar un proceso vamos a tener que recorrerla por completo, sacando el pcb y volviendolo a poner en caso de que no corresponda
    log_info(logger, "busco proceso para finalizar, pid: %s", pid);

    for (int i = 0; tam_cola > i; i++)
    {
        pcb_encontrado = queue_pop(cola);

        if (string_equals_ignore_case(pcb_encontrado->pid, pid))
        {
            pcb_buscado = pcb_encontrado;
            log_info(logger, "encontre el pid: %s", pcb_encontrado->pid);
        }
        else
        {
            queue_push(cola, pcb_encontrado);
        }
    }

    return pcb_buscado;
}

void mostrar_cola_ready(char *tipo_de_ready)
{

    char *pids_ready = string_new();

    if (string_equals_ignore_case(tipo_de_ready, "READY"))
    {

        for (int i = 0; list_size(lista_pids_ready) > i; i++)
        {
            char *pid_proceso_ready = list_remove(lista_pids_ready, i);

            string_append_with_format(&pids_ready, "%s ", pid_proceso_ready);

            list_add_in_index(lista_pids_ready, i, pid_proceso_ready);
        }

        log_info(logger, "Cola Ready: %s", pids_ready);
    }
    else
    {

        for (int i = 0; list_size(lista_pids_ready_m) > i; i++)
        {
            char *pid_proceso_ready_m = list_remove(lista_pids_ready_m, i);

            string_append_with_format(&pids_ready, "%s ", pid_proceso_ready_m);

            list_add_in_index(lista_pids_ready_m, i, pid_proceso_ready_m);
        }

        log_info(logger, "Cola Ready Prioridad: %s", pids_ready);
    }
    free(pids_ready);
}

void *administrar_procesos_new(void *arg)
{
    grado_multiprogramacion = atoi(kernel_config.grado_multiprogramacion);

    if (sem_init(&sem_multiprogramacion, 0, grado_multiprogramacion) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_multiprogramacion");
    }

    while (1)
    {
        //log_info(logger, "while 6");
        // --- Agrego esto asi evitamos espera activa, y ademas podemos tener mejor control del nivel de multiprogramacion --- //
        sem_wait(&sem_hay_procesos_en_new);

        sem_wait(&sem_multiprogramacion);

        pcb *pcb_aux = queue_pop(cola_new);
        queue_push(cola_ready, pcb_aux);
        list_add(lista_pids_ready, pcb_aux->pid);

        char* estado_nuevo = string_duplicate("READY");
        cambiar_estado(pcb_aux->pid, estado_nuevo);

        sem_post(&sem_procesos_en_ready);
        // --- Al generar un nuevo proceso si es el primero replanifico (a penas entra a ready lo paso a exec), sino no --- //
        if (es_primer_proceso)
        {
            replanificar("NEW");
            es_primer_proceso = false;
        }
    }
}

void *planificacion_de_procesos(void *arg)
{
    log_info(logger, "Inicio a planificar en %s", kernel_config.algoritmo_planificacion);

    t_temporal *cronometro;

    pthread_t thread_q;
    pthread_t thread_p;

    while (1)
    {
        //log_info(logger, "while 7");
        sem_wait(&sem_ejecutar);

        desaloja_por_instruccion = false;

        pcb *pcb_aux = queue_peek(cola_exec);
        //log_info(logger, pcb_aux->pid);

        t_paquete *paquete_a_enviar = crear_paquete();
        agregar_pcb_a_paquete(paquete_a_enviar, pcb_aux);
        enviar_paquete(paquete_a_enviar, conexion_cpu_dispatch);
        eliminar_paquete(paquete_a_enviar);

        // log_info(logger, "PID: %s - Envié el proceso a cpu", pcb_aux->pid);

        if (string_equals_ignore_case(kernel_config.algoritmo_planificacion, "RR") || string_equals_ignore_case(kernel_config.algoritmo_planificacion, "VRR"))
        {
            bool termino_sleep = false;
            bool replanifico = false;
            cronometro = temporal_create();

            void* habilitacion_quantum(void* arg)
            {
                //log_info(logger, "habilito quantum, ARRANCO SLEEP EN: %f", pcb_aux->quantum);

                usleep(pcb_aux->quantum * 1000);
                pthread_cancel(thread_p);
                termino_sleep = true;
                //log_info(logger, "TERMINO EL SLEEP");

                if(!replanifico){ 
                    if (!desaloja_por_instruccion)
                    {
                        fin_por_quantum = true;

                        if (esperar_fin_instruccion) // para un caso MUY especifico
                            sem_post(&sem_pausa_conteo_quantum);

                        
                        pedido_de_desalojo("QUANTUM", pcb_aux->pid);

                        fin_por_quantum = false;
                        sem_post(&sem_desalojo_planificacion);
                    }
                }else {

                    log_info(logger, "Replanificó entonces no hay fin de quantum");

                }
                return 0;
            };

            void* habilitacion_instruccion(void* arg)
            {
                //log_info(logger, "habilito plani");

                

                while(1)
                {

                    //log_info(logger, "El cronometro inicia en: %d", temporal_gettime(cronometro));

                    if(termino_sleep){
                        
                        //log_info(logger,"termino el sleep, corto hab_instruccion");
                        break;
                    }

                    sem_wait(&termino_la_instruccion);

                    if(termino_sleep){
                        
                        //log_info(logger,"termino el sleep, corto hab_instruccion");
                        break;
                    }

                    temporal_stop(cronometro);
                    //log_info(logger, "pause el cronometro en: %ld", temporal_gettime(cronometro));
                    sem_post(&sem_pausa_conteo_quantum);


                    sem_wait(&sem_esperar_instruccion);

                    if(termino_sleep){
                        
                        //log_info(logger,"termino el sleep, corto hab_instruccion");
                        break;
                    }

                    if (!desaloja_por_instruccion)
                    {
                        temporal_resume(cronometro);
                    }
                    else
                    {
                        pthread_cancel(thread_q);
                        replanifico = true;
                        
                        //log_info(logger, "Replanificó");

                        if (string_equals_ignore_case(kernel_config.algoritmo_planificacion, "VRR"))
                        {
                            pcb_aux = queue_peek(cola_exec);
                            pcb_aux->quantum = pcb_aux->quantum - temporal_gettime(cronometro); // se actualiza el quantum
                            
                            log_error(logger, "PID: %s - QUANTUM ACTUALIZADO: %f - Consumi: %ld", pcb_aux->pid, pcb_aux->quantum, temporal_gettime(cronometro));
                        }
                        sem_post(&sem_pausa_conteo_quantum);
                        sem_post(&sem_desalojo_planificacion);
                        break;
                    }
                }

                //log_info(logger,"termino el sleep, return hab_instruccion");
                return 0;
            };
            
            
            pthread_create(&thread_q,
                       NULL,
                       (void *)habilitacion_quantum, NULL);
            pthread_detach(thread_q);

            pthread_create(&thread_p,
                       NULL,
                       (void *)habilitacion_instruccion,NULL);
            pthread_detach(thread_p);

            sem_wait(&sem_desalojo_planificacion);
            temporal_destroy(cronometro);
        }
            /*
            int quantum = pcb_aux->quantum;
            t_temporal *cronometro = temporal_create();

            while (quantum >= temporal_gettime(cronometro))
            {
                //log_info(logger, "while 8");
                if (esperar_fin_instruccion)
                {
                    temporal_stop(cronometro);
                    sem_post(&sem_pausa_conteo_quantum);

                    sem_wait(&sem_esperar_instruccion);

                    if (!desaloja_por_instruccion)
                    {
                        temporal_resume(cronometro);
                    }
                    else
                    {
                        if (string_equals_ignore_case(kernel_config.algoritmo_planificacion, "VRR"))
                        {
                            pcb_aux = queue_peek(cola_exec);
                            pcb_aux->quantum = quantum - temporal_gettime(cronometro); // se actualiza el quantum
                            log_error(logger, "PID: %s - QUANTUM ACTUALIZADO: %f", pcb_aux->pid, pcb_aux->quantum);
                        }

                        sem_post(&sem_pausa_conteo_quantum);
                        break;
                    }
                }
            }

            if (!desaloja_por_instruccion)
            {
                fin_por_quantum = true;

                if (esperar_fin_instruccion) // para un caso MUY especifico
                    sem_post(&sem_pausa_conteo_quantum);

                pedido_de_desalojo("QUANTUM", pcb_aux->pid);

                fin_por_quantum = false;
            }

            temporal_destroy(cronometro);
        }*/
    }
}

void pedido_de_desalojo(char *motivo, char* pid)
{
    t_paquete *paquete = crear_paquete();
    agregar_a_paquete(paquete, pid, strlen(pid) + 1);
    agregar_a_paquete(paquete, motivo, strlen(motivo) + 1);
    enviar_paquete(paquete, conexion_cpu_interrupt);
    eliminar_paquete(paquete);

    if (string_equals_ignore_case(motivo, "QUANTUM"))
    {
        sem_wait(&sem_desalojo_quantum);

        if (!desaloja_por_instruccion)
        {
            log_info(logger, "PID: %s - Desalojado por fin de Quantum", pid);
            replanificar(motivo);
        }
        else
        {
            sem_post(&sem_verificar_desalojo_quantum);
        }
    }
}

void pedido_de_desalojo_prima(char *motivo, char *pid)
{

    t_paquete *paquete = crear_paquete();
    agregar_a_paquete(paquete, pid, strlen(pid) + 1);
    agregar_a_paquete(paquete, motivo, strlen(motivo) + 1);
    enviar_paquete(paquete, conexion_cpu_interrupt);
    eliminar_paquete(paquete);
}

void replanificar(char *motivo)
{
    sem_wait(&sem_planificacion);

    if (string_equals_ignore_case(motivo, "QUANTUM"))
    {
        sem_wait(&sem_replanificacion_quantum);
    }

    if (string_equals_ignore_case(kernel_config.algoritmo_planificacion, "FIFO") || string_equals_ignore_case(kernel_config.algoritmo_planificacion, "RR"))
    {
        sem_wait(&sem_procesos_en_ready);

        pcb *pcb_nuevo = queue_pop(cola_ready);
        list_remove(lista_pids_ready, 0);
        if(!queue_is_empty(cola_exec))
        {
            queue_clean(cola_exec);
            //pcb* a_liberar = queue_pop(cola_exec);//aca2
            //free(a_liberar->pid);
            //free(a_liberar);
            //log_info(logger, "libere pcb en replanificar");
        }

        queue_push(cola_exec, pcb_nuevo);

        char* estado_nuevo = string_duplicate("EXEC");
        cambiar_estado(pcb_nuevo->pid, estado_nuevo);
    }
    else if (string_equals_ignore_case(kernel_config.algoritmo_planificacion, "VRR"))
    {
        if (!queue_is_empty(cola_ready_m) || (queue_is_empty(cola_ready) && (string_contains(motivo, "IO") || string_equals_ignore_case(motivo, "WAIT"))))
        {
            sem_wait(&sem_procesos_en_ready_m);

            pcb *pcb_nuevo = queue_pop(cola_ready_m);
            list_remove(lista_pids_ready_m, 0);
            
            if(!queue_is_empty(cola_exec))
            {
                queue_clean(cola_exec);
                //pcb* a_liberar = queue_pop(cola_exec);//aca2
                //free(a_liberar->pid);
                //free(a_liberar);
                //log_info(logger, "libere pcb en replanificar");
            }

            queue_push(cola_exec, pcb_nuevo);

            char* estado_nuevo = string_duplicate("EXEC");
            cambiar_estado(pcb_nuevo->pid, estado_nuevo);
        }
        else
        {
            sem_wait(&sem_procesos_en_ready);

            pcb *pcb_nuevo = queue_pop(cola_ready);
            list_remove(lista_pids_ready, 0);
            
            if(!queue_is_empty(cola_exec))
            {
                queue_clean(cola_exec);
                //pcb* a_liberar = queue_pop(cola_exec);//aca2
                //free(a_liberar->pid);
                //free(a_liberar);
                //log_info(logger, "libere pcb en replanificar");
            }
            
            queue_push(cola_exec, pcb_nuevo);

            char* estado_nuevo = string_duplicate("EXEC");
            cambiar_estado(pcb_nuevo->pid, estado_nuevo);
        }
    }

    sem_post(&sem_ejecutar);
    sem_post(&sem_planificacion);
}

void bloquear_i(char *interfaz, pcb *pcb_block)
{
    cola_block_io *cola_interfaz = encontrar_cola(interfaz);

    // sem_wait(&cola_interfaz->habilitacion_cola);
    // sem_post(&cola_interfaz->sem_hay_proceso);

    queue_push(cola_interfaz->cola, pcb_block);

    char* estado_nuevo = string_duplicate("BLOCK");
    cambiar_estado(pcb_block->pid, estado_nuevo);
}

void iniciar(pcb *pcb_desalojo, char *motivo)
{
    if (string_equals_ignore_case(motivo, "QUANTUM") || !string_equals_ignore_case(kernel_config.algoritmo_planificacion, "VRR"))
    {
        if(string_equals_ignore_case(kernel_config.algoritmo_planificacion, "VRR"))
        {
            pcb_desalojo->quantum = atof(kernel_config.quantum);
        }

        queue_push(cola_ready, pcb_desalojo);
        list_add(lista_pids_ready, pcb_desalojo->pid);

        char* estado_nuevo = string_duplicate("READY");
        cambiar_estado(pcb_desalojo->pid, estado_nuevo);

        mostrar_cola_ready("READY");

        if (string_equals_ignore_case(motivo, "QUANTUM"))
        {
            sem_post(&sem_replanificacion_quantum);
        }

        sem_post(&sem_procesos_en_ready);
    }
    else
    {
        queue_push(cola_ready_m, pcb_desalojo);
        list_add(lista_pids_ready_m, pcb_desalojo->pid);

        char* estado_nuevo = string_duplicate("READY+");
        cambiar_estado(pcb_desalojo->pid, estado_nuevo);

        mostrar_cola_ready("READY+");

        sem_post(&sem_procesos_en_ready_m);
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void atender_solicitud_cpu(t_list *lista, pcb *pcb_desalojo, char *motivo)
{
    bool no_existe_interfaz = false;
    bool no_cumplio_recurso = false;
    if (!string_equals_ignore_case(motivo, "QUANTUM")) // SI MOTIVO ES UNA INSTRUCCION
    {
        if (string_contains(kernel_config.algoritmo_planificacion, "RR") && !fin_por_quantum)
        {
            sem_post(&termino_la_instruccion);

            esperar_fin_instruccion = true;
            sem_wait(&sem_pausa_conteo_quantum);
            esperar_fin_instruccion = false;

        }

        if(string_equals_ignore_case(kernel_config.algoritmo_planificacion, "VRR") && fin_por_quantum)
        {
            pcb_desalojo->quantum = atof(kernel_config.quantum);
        }

        queue_clean(cola_exec);
        //pcb* a_liberar = queue_pop(cola_exec);//aca2
        //free(a_liberar->pid);
        //free(a_liberar);
        //log_info(logger, "libere pcb");
        
        queue_push(cola_exec, pcb_desalojo);

        //cambiar_estado(pcb_desalojo->pid, "EXEC"); // POR QUE ESTO SI YA ESTÁ EN EXEC??

        if (string_contains(motivo, "IO"))
        {
            char *interfaz = list_get(lista, 0);
            if (cumple_interfaz(interfaz, motivo))
            {
                no_existe_interfaz = false;
                desaloja_por_instruccion = true;

                if (string_contains(kernel_config.algoritmo_planificacion, "RR"))
                {
                    if (!fin_por_quantum)
                    {
                        sem_post(&sem_esperar_instruccion);
                        sem_wait(&sem_pausa_conteo_quantum);
                    }
                    else
                    {
                        sem_post(&sem_desalojo_quantum);
                        sem_wait(&sem_verificar_desalojo_quantum);
                    }
                }

                bloquear_i(interfaz, pcb_desalojo);
                log_info(logger, "PID: %s - Bloqueado por: %s", pcb_desalojo->pid, interfaz);

                sem_wait(&sem_planificacion); // PARA EL DETENER_PLANIFICACION
                sem_post(&sem_planificacion);

                log_info(logger, "PID: %s - Solicito ejecutar %s - INTERFAZ: %s", pcb_desalojo->pid, motivo, interfaz);
                if (string_equals_ignore_case(motivo, "IO_GEN_SLEEP"))
                {
                    char *unidades_de_trabajo = list_get(lista, 1);
                    io_gen_sleep(motivo, interfaz, unidades_de_trabajo, pcb_desalojo->pid);
                }
                else if (string_equals_ignore_case(motivo, "IO_STDOUT_WRITE") || string_equals_ignore_case(motivo, "IO_STDIN_READ"))
                {
                    char *direccion_fisica = list_get(lista, 1);
                    char *registro_tam = list_get(lista, 2);
                    io_stdout_write_o_stdin_read(motivo, interfaz, direccion_fisica, registro_tam, pcb_desalojo->pid);
                }
                else if (string_equals_ignore_case(motivo, "IO_FS_CREATE") || string_equals_ignore_case(motivo, "IO_FS_DELETE"))
                {
                    char *nombre_archivo = list_get(lista, 1);
                    io_fs_create_o_delete(motivo, interfaz, nombre_archivo, pcb_desalojo->pid);
                }
                else if (string_equals_ignore_case(motivo, "IO_FS_TRUNCATE"))
                {
                    char *nombre_archivo = list_get(lista, 1);
                    char *registro_tam = list_get(lista, 2);
                    io_fs_truncate(motivo, interfaz, nombre_archivo, registro_tam, pcb_desalojo->pid);
                }
                else if (string_equals_ignore_case(motivo, "IO_FS_WRITE") || string_equals_ignore_case(motivo, "IO_FS_READ"))
                {
                    char *nombre_archivo = list_get(lista, 1);
                    char *direccion_fisica = list_get(lista, 2);
                    char *registro_tam = list_get(lista, 3);
                    char *registro_puntero_archivo = list_get(lista, 4);
                    io_fs_write_o_read(motivo, interfaz, nombre_archivo, direccion_fisica, registro_tam, registro_puntero_archivo, pcb_desalojo->pid);
                }

                // ACA EL PROCESO SE VA A IR A BLOQUEADO, COMO TENGO LA INTERFAZ APROVECHO PARA CAMBIAR EL ESTADO
                char *estado = string_new();
                string_append_with_format(&estado, "BLOCK - IO: %s", interfaz);
                cambiar_estado(pcb_desalojo->pid, estado);

                pedido_de_desalojo(motivo, pcb_desalojo->pid);
            }
            else
            {
                log_info(logger, "PID: %s - No se ha encontrado la interfaz %s solicitada", pcb_desalojo->pid, interfaz);
                no_existe_interfaz = true;
            }
        }
        else
        {
            if (string_equals_ignore_case(motivo, "WAIT"))
            {
                char *recurso = list_get(lista, 0);
                no_cumplio_recurso = wait(recurso, pcb_desalojo, motivo);
            }
            else if (string_equals_ignore_case(motivo, "SIGNAL"))
            {
                char *recurso = list_get(lista, 0);
                no_cumplio_recurso = signal(recurso, pcb_desalojo->pid);
                free(motivo);
            }
        }

        if (no_cumplio_recurso || no_existe_interfaz || string_equals_ignore_case(motivo, "OUT_OF_MEMORY") || string_equals_ignore_case(motivo, "DENIED_ACCESS") || string_equals_ignore_case(motivo, "EXIT")) 
        {
            desaloja_por_instruccion = true;

            if (string_contains(kernel_config.algoritmo_planificacion, "RR"))
            {
                if (!fin_por_quantum)
                {
                    sem_post(&sem_esperar_instruccion);
                    sem_wait(&sem_pausa_conteo_quantum);
                }
                else
                {
                    sem_post(&sem_desalojo_quantum);
                    sem_wait(&sem_verificar_desalojo_quantum);
                }
            }

            if (no_cumplio_recurso)
            {
                exit_prima(pcb_desalojo, "INVALID_RESOURCE");
                free(motivo);
                motivo = string_duplicate("INVALID_RESOURCE");
            }
            else if (no_existe_interfaz)
            {
                exit_prima(pcb_desalojo, "INVALID_INTERFACE");
                free(motivo);
                motivo = string_duplicate("INVALID_INTERFACE");
            }
            else if (string_equals_ignore_case(motivo, "OUT_OF_MEMORY"))
            {
                exit_prima(pcb_desalojo, "OUT_OF_MEMORY");
            }
            else if (string_equals_ignore_case(motivo, "DENIED_ACCESS"))
            {
                exit_prima(pcb_desalojo, "DENIED_ACCESS");
            }
            else
                exit_prima(pcb_desalojo, "SUCCESS");

            pedido_de_desalojo(motivo, pcb_desalojo->pid);
        }
    }
    else // SI MOTIVO ES QUANTUM
    {
        iniciar(pcb_desalojo, motivo);
        free(motivo);
    }
}

void exit_prima(pcb *pcb_exit, char *motivo_exit)
{
    // solicito a memoria que libere todas las estructuras de este proceso
    // aca no puedo limpiar la cola exec porque replanifico en pedido de desalojo

    char *motivo = string_duplicate("FINALIZAR_PROCESO");

    liberar_recursos(pcb_exit->pid);

    t_paquete *paquete = crear_paquete();
    agregar_a_paquete(paquete, motivo, strlen(motivo) + 1);
    agregar_a_paquete(paquete, pcb_exit->pid, strlen(pcb_exit->pid) + 1);
    enviar_paquete(paquete, conexion_memoria);
    eliminar_paquete(paquete);

    free(motivo);

    queue_push(cola_exit, pcb_exit);
    sem_post(&sem_multiprogramacion);
    char *estado_anterior = list_get(lista_estados, atoi(pcb_exit->pid) - 1);


    char* estado_nuevo = string_duplicate("EXIT");
    cambiar_estado(pcb_exit->pid, estado_nuevo);

    log_info(logger, "Finaliza el proceso %s - Motivo: %s", pcb_exit->pid, motivo_exit);

    if (string_equals_ignore_case(estado_anterior, "EXEC") && (string_equals_ignore_case(motivo_exit, "INTERRUPTED_BY_USER")) /*|| string_equals_ignore_case(motivo_exit, "INVALIDA_INTERFACE") || string_equals_ignore_case(motivo_exit, "INVALID_RESOURCE") || string_equals_ignore_case(motivo_exit, "OUT_OF_MEMORY") || string_equals_ignore_case(motivo_exit, "DENIED_ACCESS"))*/)
    {
        //free(estado_anterior);
        replanificar(motivo_exit);
    }
}

void liberar_recursos(char *pid_exit)
{

    bool pid_retiene_instancia_recurso(recursos_retenidos * recurso_aleatorio_retenido)
    {
        return string_equals_ignore_case(recurso_aleatorio_retenido->pid, pid_exit);
    };

    recursos_retenidos *instancia_retenida;
    while ((instancia_retenida = list_find(lista_recursos_retenidos, (void *)pid_retiene_instancia_recurso)) != NULL){
        //log_info(logger, "while 9");
        signal(instancia_retenida->recurso, pid_exit);
    }
        
}

/*
pcb *recibir_desalojo_cpu()     NO SE ESTA USANDO
{
    recibir_operacion(conexion_cpu_dispatch);
    t_list *lista = list_create();
    lista = recibir_paquete(conexion_cpu_dispatch);
    pcb *pcb_desalojado = recibir_desalojo(lista);
    list_destroy(lista);
    return pcb_desalojado;
}

pcb *recibir_desalojo(t_list *lista)    NO SE ESTA USANDO
{
    pcb *nuevo_pcb = malloc(sizeof(pcb));
    cargar_pcb(nuevo_pcb, lista);
    list_destroy(lista);
    return nuevo_pcb;
}
*/

cola_block_io *encontrar_cola(char *interfaz)
{

    bool cumple_la_interfaz(cola_block_io * cola_aleatoria)
    {
        return string_equals_ignore_case(cola_aleatoria->identificador, interfaz);
    };

    cola_block_io *cola = list_find(colas_block_io, (void *)cumple_la_interfaz);
    return cola;
}

void io_gen_sleep(char *instruccion, char *interfaz, char *unidades_de_trabajo, char *pid_io)
{
    int *conexion = dictionary_get(diccionario_conexiones_io, interfaz);

    t_paquete *paquete_a_enviar = crear_paquete();
    agregar_a_paquete(paquete_a_enviar, pid_io, strlen(pid_io) + 1);
    agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion) + 1);
    agregar_a_paquete(paquete_a_enviar, unidades_de_trabajo, strlen(unidades_de_trabajo) + 1);
    enviar_paquete(paquete_a_enviar, *conexion);
    eliminar_paquete(paquete_a_enviar);
}

void io_stdout_write_o_stdin_read(char *instruccion, char *interfaz, char *direccion_fisica, char *registro_tam, char *pid_io)
{
    int *conexion = dictionary_get(diccionario_conexiones_io, interfaz);

    t_paquete *paquete_a_enviar = crear_paquete();
    agregar_a_paquete(paquete_a_enviar, pid_io, strlen(pid_io) + 1);
    agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion) + 1);
    agregar_a_paquete(paquete_a_enviar, direccion_fisica, strlen(direccion_fisica) + 1);
    agregar_a_paquete(paquete_a_enviar, registro_tam, strlen(registro_tam) + 1);
    //log_info(logger, "El tamaño a leer es de: %s", registro_tam);
    enviar_paquete(paquete_a_enviar, *conexion);
    eliminar_paquete(paquete_a_enviar);
}

void io_fs_create_o_delete(char *instruccion, char *interfaz, char *nombre_archivo, char *pid_io)
{
    int *conexion = dictionary_get(diccionario_conexiones_io, interfaz);

    t_paquete *paquete_a_enviar = crear_paquete();
    agregar_a_paquete(paquete_a_enviar, pid_io, strlen(pid_io) + 1);
    agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion) + 1);
    // agregar_a_paquete(paquete_a_enviar, interfaz, strlen(interfaz)+1); //aparentemente no le sirve a io
    agregar_a_paquete(paquete_a_enviar, nombre_archivo, strlen(nombre_archivo) + 1);
    enviar_paquete(paquete_a_enviar, *conexion);
    eliminar_paquete(paquete_a_enviar);
}

void io_fs_truncate(char *instruccion, char *interfaz, char *nombre_archivo, char *registro_tam, char *pid_io)
{
    int *conexion = dictionary_get(diccionario_conexiones_io, interfaz);

    t_paquete *paquete_a_enviar = crear_paquete();
    agregar_a_paquete(paquete_a_enviar, pid_io, strlen(pid_io) + 1);
    agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion) + 1);
    // agregar_a_paquete(paquete_a_enviar, interfaz, strlen(interfaz)+1); no le sirve a inout
    agregar_a_paquete(paquete_a_enviar, nombre_archivo, strlen(nombre_archivo) + 1);
    agregar_a_paquete(paquete_a_enviar, registro_tam, strlen(registro_tam) + 1);
    enviar_paquete(paquete_a_enviar, *conexion);
    eliminar_paquete(paquete_a_enviar);
}

void io_fs_write_o_read(char *instruccion, char *interfaz, char *nombre_archivo, char *direccion_fisica, char *registro_tam, char *registro_puntero_archivo, char *pid_io)
{
    int *conexion = dictionary_get(diccionario_conexiones_io, interfaz);

    t_paquete *paquete_a_enviar = crear_paquete();
    agregar_a_paquete(paquete_a_enviar, pid_io, strlen(pid_io) + 1);
    agregar_a_paquete(paquete_a_enviar, instruccion, strlen(instruccion) + 1);
    // agregar_a_paquete(paquete_a_enviar, interfaz, strlen(interfaz)+1); aparentemente io no lee esto
    agregar_a_paquete(paquete_a_enviar, nombre_archivo, strlen(nombre_archivo) + 1);
    agregar_a_paquete(paquete_a_enviar, direccion_fisica, strlen(direccion_fisica) + 1);
    agregar_a_paquete(paquete_a_enviar, registro_tam, strlen(registro_tam) + 1);
    agregar_a_paquete(paquete_a_enviar, registro_puntero_archivo, strlen(registro_puntero_archivo) + 1);
    enviar_paquete(paquete_a_enviar, *conexion);
    eliminar_paquete(paquete_a_enviar);
}

bool cumple_interfaz(char *interfaz, char *instruccion)
{

    bool tiene_el_mismo_nombre(cola_block_io * cola_aleatoria)
    {
        return string_equals_ignore_case(cola_aleatoria->identificador, interfaz);
    };

    bool existe_la_interfaz()
    {
        return NULL != list_find(colas_block_io, (void *)tiene_el_mismo_nombre);
    };

    bool es_accion_permitida()
    {
        char *instrucciones_permitidas = dictionary_get(diccionario_instrucciones_permitidas_io, interfaz);
        return string_contains(instrucciones_permitidas, instruccion);
    };

    return existe_la_interfaz() && es_accion_permitida();
}

bool wait(char *recurso, pcb *pcb_recurso, char* motivo)
{
    if (existe_el_recurso(recurso))
    {
        cola_block_recurso *cola_recurso = encontrar_cola_recurso(recurso);
        bool va_a_bloqueado = wait_recurso(recurso, cola_recurso);
        if (va_a_bloqueado)
        {
            desaloja_por_instruccion = true;

            if (string_contains(kernel_config.algoritmo_planificacion, "RR"))
            {
                if (!fin_por_quantum)
                {
                    sem_post(&sem_esperar_instruccion);
                    sem_wait(&sem_pausa_conteo_quantum);
                }
                else
                {
                    sem_post(&sem_desalojo_quantum);
                    sem_wait(&sem_verificar_desalojo_quantum);
                }
            }

            queue_push(cola_recurso->cola, pcb_recurso);
            log_info(logger, "PID: %s - Bloqueado por: %s", pcb_recurso->pid, recurso);

            pedido_de_desalojo("WAIT", pcb_recurso->pid);

            char *estado = string_new();
            string_append_with_format(&estado, "BLOCK - RECURSO: %s", recurso);
            cambiar_estado(pcb_recurso->pid, estado);
        }
        else
        {
            if (string_contains(kernel_config.algoritmo_planificacion, "RR"))
            {
                if (!fin_por_quantum)
                    sem_post(&sem_esperar_instruccion);
            }

            free(motivo);
        }

        recursos_retenidos *instancia_retenida = malloc(sizeof(recursos_retenidos));
        instancia_retenida->pid = string_duplicate(pcb_recurso->pid);
        instancia_retenida->recurso = string_duplicate(recurso);
        list_add(lista_recursos_retenidos, instancia_retenida);

        return false;
    }
    else
    {
        log_info(logger, "No se ha encontrado el recurso %s solicitado por PID %s", recurso, pcb_recurso->pid);
        free(motivo);
        return true;
    }
}

cola_block_recurso *encontrar_cola_recurso(char *nombre_recurso)
{
    bool cumple_recurso(cola_block_recurso * cola_aleatoria)
    {
        return string_equals_ignore_case(cola_aleatoria->identificador, nombre_recurso);
    };

    cola_block_recurso *cola = list_find(colas_block_recursos, (void *)cumple_recurso);
    return cola;
}

bool wait_recurso(char *nombre_recurso, cola_block_recurso *cola)
{
    sem_wait(&cola->sem_mutex_recurso);

    cola->instancias_recurso--;
    sem_post(&cola->sem_mutex_recurso);
    return cola->instancias_recurso < 0;
}

bool existe_el_recurso(char *nombre_recurso)
{
    bool tiene_el_mismo_nombre(cola_block_recurso * cola_aleatoria)
    {
        return string_equals_ignore_case(cola_aleatoria->identificador, nombre_recurso);
    };

    return list_any_satisfy(colas_block_recursos, (void *)(tiene_el_mismo_nombre));
}

bool signal(char *recurso, char *pid)
{
    if (existe_el_recurso(recurso))
    {
        cola_block_recurso *cola_recurso = encontrar_cola_recurso(recurso);
        bool se_desbloquea_un_proceso = signal_recurso(recurso, cola_recurso);
        if (se_desbloquea_un_proceso)
        {
            if (string_contains(kernel_config.algoritmo_planificacion, "RR"))
            {
                if (!fin_por_quantum)
                    sem_post(&sem_esperar_instruccion);
            }

            pcb *pcb_desbloqueado = queue_pop(cola_recurso->cola);
            log_info(logger, "PID: %s - Ha salido de la cola de bloqueo del recurso: %s", pid, recurso);

            iniciar(pcb_desbloqueado, "LIBERACION DE RECURSOS");
        }

        quitar_de_lista_recursos_retenidos(recurso, pid);

        return false;
    }
    else
    {
        log_info(logger, "No se ha encontrado el recurso %s solicitado por PID %s", recurso, pid);
        return true;
    }
}

bool signal_recurso(char *nombre_recurso, cola_block_recurso *cola)
{
    sem_wait(&cola->sem_mutex_recurso);

    cola->instancias_recurso++;
    sem_post(&cola->sem_mutex_recurso);
    return cola->instancias_recurso <= 0;
}

void quitar_de_lista_recursos_retenidos(char *nombre_recurso, char *pid)
{
    bool concuerdan_pid_y_recurso(recursos_retenidos * recurso_aleatorio_retenido)
    {
        return string_equals_ignore_case(recurso_aleatorio_retenido->recurso, nombre_recurso) && string_equals_ignore_case(recurso_aleatorio_retenido->pid, pid);
    };

    recursos_retenidos* pid_recurso = list_remove_by_condition(lista_recursos_retenidos, (void *)concuerdan_pid_y_recurso);
    free(pid_recurso->pid);
    free(pid_recurso->recurso);
    free(pid_recurso);
}

void mostrar_lista_estados_logueables()
{
    t_list* sublista_estados /*= list_create()*/;
    for(int i = 0; 6 > i; i++)
    {
        char* pids_en_estado;

        switch(i)
        {
            case 0:
                pids_en_estado = string_duplicate("Procesos en NEW:");
                break;
            case 1:
                pids_en_estado = string_duplicate("Procesos en READY:");
                break;
            case 2:
                pids_en_estado = string_duplicate("Procesos en READY+:");
                break;
            case 3:
                pids_en_estado = string_duplicate("Procesos en EXEC:");
                break;
            case 4:
                pids_en_estado = string_duplicate("Procesos en BLOCK:");
                break;
            case 5:
                pids_en_estado = string_duplicate("Procesos en EXIT:");
                break;
        }

        sublista_estados = list_get(lista_estados_logueables, i);

        for(int j = 0; list_size(sublista_estados) > j; j++)
        {
            char* pid_aux = list_get(sublista_estados, j);
            //char* pid = string_duplicate(pid_aux);
            string_append_with_format(&pids_en_estado," %s", pid_aux);
            //free(pid);
        }
        
        log_info(logger, "%s", pids_en_estado);

        free(pids_en_estado);
    }
}

pcb *generar_nuevo_proceso()
{
    pcb *nuevo_pcb = malloc(sizeof(pcb));
    nuevo_pcb->pid = string_itoa(contador_pid);
    contador_pid++;
    nuevo_pcb->program_counter = 0;
    nuevo_pcb->quantum = atof(kernel_config.quantum);
    nuevo_pcb->registros.AX = 0;
    nuevo_pcb->registros.BX = 0;
    nuevo_pcb->registros.CX = 0;
    nuevo_pcb->registros.DX = 0;
    nuevo_pcb->registros.EAX = 0;
    nuevo_pcb->registros.EBX = 0;
    nuevo_pcb->registros.ECX = 0;
    nuevo_pcb->registros.EDX = 0;
    nuevo_pcb->registros.SI = 0;
    nuevo_pcb->registros.PC = 0;
    log_info(logger, "Se crea el proceso %s en NEW", nuevo_pcb->pid);
    return nuevo_pcb;
}

void mandar_pedido(int conexion, char *motivo, char *posible_argumento, char *pid)
{
    t_paquete *paquete = crear_paquete();
    agregar_a_paquete(paquete, motivo, strlen(motivo) + 1);
    if (!string_equals_ignore_case(posible_argumento, ""))
        agregar_a_paquete(paquete, posible_argumento, strlen(posible_argumento) + 1);
    agregar_a_paquete(paquete, pid, strlen(pid) + 1);
    enviar_paquete(paquete, conexion);
    eliminar_paquete(paquete);
}

void ejecutar_script(char *path)
{

    log_info(logger, "Se recibieron las siguientes instrucciones del archivo de PSEUDOCODIGO:");
    FILE *leido;
    char buffer[100];
    int longitud;
    leido = fopen(path, "r");
    if (leido == NULL)
    {
        log_error(logger, "Error en la apertura del archivo de PSEUDOCODIGO.\n");
    }
    else
    {
        while (fgets(buffer, sizeof(buffer), leido))
        {
            //log_info(logger, "while 10");
            buffer[strcspn(buffer, "\n")] = '\0';
            longitud = strlen(buffer) + 1;
            log_info(logger, " Longitud: %d - Instruccion: %s", longitud, buffer);
            char **info_procesada = separar_instruccion(buffer, " ");
            recibir_info(info_procesada[0], info_procesada[1]);
            string_array_destroy(info_procesada);
        }
    }
}

void levantar_config(void)
{
    kernel_config.puerto_escucha = config_get_string_value(config, "PUERTO_ESCUCHA");
    kernel_config.ip_memoria = config_get_string_value(config, "IP_MEMORIA");
    kernel_config.puerto_memoria = config_get_string_value(config, "PUERTO_MEMORIA");
    kernel_config.ip_cpu = config_get_string_value(config, "IP_CPU");
    kernel_config.puerto_cpu_dispatch = config_get_string_value(config, "PUERTO_CPU_DISPATCH");
    kernel_config.puerto_cpu_interrupt = config_get_string_value(config, "PUERTO_CPU_INTERRUPT");
    kernel_config.algoritmo_planificacion = config_get_string_value(config, "ALGORITMO_PLANIFICACION");
    kernel_config.quantum = config_get_string_value(config, "QUANTUM");
    kernel_config.recursos = config_get_string_value(config, "RECURSOS");
    kernel_config.instancias_recursos = config_get_string_value(config, "INSTANCIAS_RECURSOS");
    kernel_config.grado_multiprogramacion = config_get_string_value(config, "GRADO_MULTIPROGRAMACION");

    log_info(logger, "Se cargaron los siguientes datos de configuracion:");
    log_info(logger, "PUERTO_ESCUCHA: %s", kernel_config.puerto_escucha);
    log_info(logger, "IP_MEMORIA: %s", kernel_config.ip_memoria);
    log_info(logger, "PUERTO_MEMORIA: %s", kernel_config.puerto_memoria);
    log_info(logger, "IP_CPU: %s", kernel_config.ip_cpu);
    log_info(logger, "PUERTO_CPU_DISPATCH: %s", kernel_config.puerto_cpu_dispatch);
    log_info(logger, "PUERTO_CPU_INTERRUPT: %s", kernel_config.puerto_cpu_interrupt);
    log_info(logger, "ALGORITMO_PLANIFICACION: %s", kernel_config.algoritmo_planificacion);
    log_info(logger, "QUANTUM: %s", kernel_config.quantum);
    log_info(logger, "RECURSOS: %s", kernel_config.recursos);
    log_info(logger, "INSTANCIAS_RECURSOS: %s", kernel_config.instancias_recursos);
    log_info(logger, "GRADO_MULTIPROGRAMACION: %s", kernel_config.grado_multiprogramacion);
}

void inicializar(t_log *logger)
{
    logger = iniciar_logger("kernel.log", "KERNEL");
}

void iterator(char *value)
{
    log_info(logger, "%s", value);
}

void limpiar_cola(t_queue* cola)
{
    int tam = queue_size(cola);
    for(int a = 0;tam > a; a++)
    {
        pcb* pcb_aux = queue_pop(cola);
        free(pcb_aux->pid);
        free(pcb_aux);
    }
    queue_destroy(cola);
}

void limpiar_kernel()
{
    sem_destroy(&sem_ejecutar);
    sem_destroy(&sem_hay_procesos_en_new);
    sem_destroy(&sem_planificacion);
    sem_destroy(&sem_procesos_en_ready);
    sem_destroy(&sem_procesos_en_ready_m);
    sem_destroy(&sem_esperar_instruccion);
    sem_destroy(&sem_desalojo_quantum);
    sem_destroy(&sem_replanificacion_quantum);
    sem_destroy(&sem_pausa_conteo_quantum);
    sem_destroy(&sem_caso_exit_usuario);
    sem_destroy(&sem_caso_exit_usuario_p);
    sem_destroy(&mutex_lista_estados);

    limpiar_cola(cola_new);
    limpiar_cola(cola_ready);
    limpiar_cola(cola_ready_m);
    limpiar_cola(cola_exec);
    limpiar_cola(cola_exit);

    
    int tam_colas_block_recursos = list_size(colas_block_recursos);
    for(int i = 0; tam_colas_block_recursos > i; i++)
    {
        cola_block_recurso* recurso = list_remove(colas_block_recursos, i);
        free(recurso->identificador);
        sem_destroy(&recurso->sem_mutex_recurso);
        limpiar_cola(recurso->cola);
    }
    list_destroy(colas_block_recursos);

    int tam_colas_block_io = list_size(colas_block_io);
    for(int k = 0; tam_colas_block_io > k; k++)
    {
        cola_block_io* interfaz = list_remove(colas_block_recursos, k);
        free(interfaz->identificador);
        sem_destroy(&interfaz->sem_hay_proceso);
        limpiar_cola(interfaz->cola);
    }
    list_destroy(colas_block_io);

    for(int j = 0; 6 > j; j++)
    {
        t_list* lista_free;
        lista_free = list_remove(lista_estados_logueables, j);
        list_destroy_and_destroy_elements(lista_free, (void *)element_destroy);
    }
    list_destroy(lista_estados_logueables);

    int tam_lista_recursos_retenidos = list_size(lista_recursos_retenidos);
    for(int l = 0; tam_lista_recursos_retenidos > l; l++)
    {
        recursos_retenidos* elemento_recurso = list_remove(lista_recursos_retenidos,l);
        free(elemento_recurso->pid);
        free(elemento_recurso->recurso);
        free(elemento_recurso);
    }
    list_destroy(lista_recursos_retenidos);
    list_destroy_and_destroy_elements(lista_estados, (void *)element_destroy);
    list_destroy_and_destroy_elements(lista_pids_ready, (void *)element_destroy);
    list_destroy_and_destroy_elements(lista_pids_ready_m, (void *)element_destroy);

    log_info(logger, "ENTRASTE ACA"); // AL PARECER NO TERMINA DE EJECUTAR TODO EL CODIGO DESPUES DEL CTRL+C

    log_destroy(logger);
    config_destroy(config);
}

void iniciar_semaforos()
{
    if (sem_init(&sem_ejecutar, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_ejecutar");
    }
    if (sem_init(&sem_hay_procesos_en_new, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_hay_procesos_en_new");
    }
    if (sem_init(&sem_planificacion, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_planificacion");
    }
    if (sem_init(&sem_procesos_en_ready, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_procesos_en_ready");
    }
    if (sem_init(&sem_procesos_en_ready_m, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_procesos_en_ready_m");
    }
    if (sem_init(&sem_esperar_instruccion, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el esperar_instruccion");
    }
    if (sem_init(&sem_desalojo_quantum, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_desalojo_quantum");
    }
    if (sem_init(&sem_replanificacion_quantum, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_replanificacion_quantum");
    }
    if (sem_init(&sem_pausa_conteo_quantum, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_pausa_conteo_quantum");
    }
    if (sem_init(&sem_caso_exit_usuario, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_caso_exit_usuario");
    }
    if (sem_init(&sem_caso_exit_usuario_p, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_caso_exit_usuario_p");
    }
    if (sem_init(&sem_verificar_desalojo_quantum, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_verificar_desalojo_quantum");
    }
    if (sem_init(&mutex_lista_estados, 0, 1) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el mutex_lista_estados");
    }
    if (sem_init(&sem_desalojo_planificacion, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_desalojo_planificacion");
    }
    if (sem_init(&termino_la_instruccion, 0, 0) == -1)
    {
        log_error(logger, "Hubo un problema al inicializar el sem_procesos_en_ready_m");
    }
}