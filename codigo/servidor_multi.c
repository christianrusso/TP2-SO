#include <signal.h>
#include <errno.h>

#include "biblioteca.h"


//Estructura del aula
typedef struct {
	int posiciones[ANCHO_AULA][ALTO_AULA]; 			 			// Matriz del aula.
	int rescatistas_disponibles;								// Cantidad de rescatistas disponibles.
	int cantidad_de_personas;									// Cantidad de personas en el aula.
	int alumnos_en_salida;										// Cantidad de personas.

	int salieron;												// Indica cuantos alumnos del grupo actual ya salieron.
	bool saliendo;												// Indica si hay un grupo de alumnos saliendo del aula.

	pthread_mutex_t mutex_posicion[ANCHO_AULA][ALTO_AULA];		// Mutex para cada posicion del aula.
	pthread_mutex_t mutex_personas;								// Mutex para la cantidad de personas.
	pthread_mutex_t mutex_rescatistas;							// Mutex para la cantidad de rescatistas.
	pthread_mutex_t mutex_salida;								// Mutex para la variabale de alumnos en la salida.

	pthread_cond_t vc_rescatistas;								// Variable de condicion de los rescatistas.
	pthread_cond_t vc_salida;									// Variable de condicion para que los alumnos salgan en grupo.

} t_aula;

// Estructura con los datos que necesita el thread.
typedef struct {
    int socket;
    t_aula *el_aula;
} thdata;


void t_aula_iniciar_vacia(t_aula *un_aula){
	int error, i, j;

	for (i = 0; i < ANCHO_AULA; i++) {
		for (j = 0; j < ALTO_AULA; j++) {
			// Inicializamos cada posicion del aula en cero.
			un_aula->posiciones[i][j] = 0;
			// Creamos un mutex para cada posicion del aula.
			if (0 != (error = pthread_mutex_init(&un_aula->mutex_posicion[i][j], NULL) ))
			{
				perror("Fallo la creacion del mutex.");
				exit(error);;
			}
		}
	}

	// Creamos el mutex para la variable "cantidad_de_personas".
	if (0 != (error = pthread_mutex_init(&un_aula->mutex_personas, NULL)))
	{
		perror("Fallo la creacion del mutex");
		exit(error);
	}

	// Creamos un mutex para la variable "rescatistas_disponibles".
	if (0 != (error = pthread_mutex_init(&un_aula->mutex_rescatistas, NULL)))
	{
		perror("Fallo la creacion del mutex");
		exit(error);
	}

	// Creamos un mutex para la variable "alumnos_en_salida".
	if (0 != (error = pthread_mutex_init(&un_aula->mutex_salida, NULL)))
	{
		perror("Fallo la creacion del mutex");
		exit(error);
	}

	// Creamos una variable de condicion para los rescatistas.
	if (0 != (error = pthread_cond_init(&un_aula->vc_rescatistas, NULL)))
	{
		perror("Fallo la creacion de la variable de condicion.");
		exit(error);
	}

	if (0 != (error = pthread_cond_init(&un_aula->vc_salida, NULL)))
	{
		perror("Fallo la creacion de la variable de condicion");
		exit(error);
	}

	un_aula->cantidad_de_personas = 0;						// No hay personas.
	un_aula->rescatistas_disponibles = RESCATISTAS;			// Todos los rescatistas disponibles.
	un_aula->alumnos_en_salida = 0;							// No hay nadie en la salida.
	un_aula->saliendo = false;								// Nadie esta saliendo del aula.
	un_aula->salieron = 0;									// Idem anterior.
}


void t_aula_ingresar(t_aula *un_aula, t_persona *alumno)
{
	int fila = alumno->posicion_fila;
	int columna = alumno->posicion_columna;

	pthread_mutex_lock(&un_aula->mutex_personas);
	un_aula->cantidad_de_personas++;
	pthread_mutex_unlock(&un_aula->mutex_personas);

	pthread_mutex_lock(&un_aula->mutex_posicion[fila][columna]);
	un_aula->posiciones[alumno->posicion_fila][alumno->posicion_columna]++;
	pthread_mutex_unlock(&un_aula->mutex_posicion[fila][columna]);
}


void t_aula_liberar(t_aula *un_aula, t_persona *alumno)
{

	int personas = 0;

	pthread_mutex_lock(&un_aula->mutex_salida);
	un_aula->alumnos_en_salida++;

	// Si vengo despues del quinto me duermo hasta que se vayan todos.
	while (un_aula->alumnos_en_salida > 5)
		pthread_cond_wait(&un_aula->vc_salida, &un_aula->mutex_salida);

	// Veo si el grupo con el que salgo es de 5 personas o menos.
	pthread_mutex_lock(&un_aula->mutex_personas);
	personas = (un_aula->cantidad_de_personas > 5)?(5):(un_aula->cantidad_de_personas);
	pthread_mutex_unlock(&un_aula->mutex_personas);

	// Si soy el que completa el grupo, aviso que hay que salir
	if (un_aula->alumnos_en_salida == personas)
		un_aula->saliendo = true;
	else // Sino, espero a que se complete el grupo.
		while (!un_aula->saliendo)
			pthread_cond_wait(&un_aula->vc_salida, &un_aula->mutex_salida);

	// Disminuyo la cantidad de personas en el aula.
	pthread_mutex_lock(&un_aula->mutex_personas);
	un_aula->cantidad_de_personas--;
	pthread_mutex_unlock(&un_aula->mutex_personas);

	// Salio uno mas, y si es el ultimo, reseteo los valores.
	un_aula->salieron++;
	if (un_aula->salieron == personas)
	{
		un_aula->saliendo = false;
		un_aula->salieron = 0;
		un_aula->alumnos_en_salida -= personas;
	}

	pthread_mutex_unlock(&un_aula->mutex_salida);

	// Avisa que puede salir el siguiente del grupo o que todo el grupo ya salio.
	pthread_cond_signal(&un_aula->vc_salida);
}


static void terminar_servidor_de_alumno(int socket_fd, t_aula *aula, t_persona *alumno)
{
	printf(">> Se interrumpió la comunicación con una consola.");
	close(socket_fd);
	if (alumno != NULL)
		t_aula_liberar(aula, alumno);

	pthread_exit(NULL);
}


t_comando intentar_moverse(t_aula *el_aula, t_persona *alumno, t_direccion dir){
	int fila = alumno->posicion_fila;
	int columna = alumno->posicion_columna;
	alumno->salio = direccion_moverse_hacia(dir, &fila, &columna);
	
	bool entre_limites = (fila >= 0) && (columna >= 0) && (fila < ALTO_AULA) && (columna < ANCHO_AULA);
	     
	pthread_mutex_lock(&el_aula->mutex_posicion[fila][columna]);
	bool hayEspacio = (el_aula->posiciones[fila][columna] < MAXIMO_POR_POSICION);
	pthread_mutex_unlock(&el_aula->mutex_posicion[fila][columna]);

	bool pudo_moverse = alumno->salio || (entre_limites && hayEspacio);
	
	if (pudo_moverse)
	{
		int vieja_fila = alumno->posicion_fila;
		int vieja_columna = alumno->posicion_columna;

		// Estos mutex hay que tomarlos en diferentes ordenes para evitar el deadlock
		//ORDEN: Arriba a abajo / Izquierda a derecha
		if (vieja_columna < columna){		// Voy de izquierda a derecha.
			pthread_mutex_lock(&el_aula->mutex_posicion[vieja_fila][vieja_columna]);	
			pthread_mutex_lock(&el_aula->mutex_posicion[fila][columna]);
		}else if(columna < vieja_columna){  // Voy de derecha a izquierda.
			pthread_mutex_lock(&el_aula->mutex_posicion[fila][columna]);
			pthread_mutex_lock(&el_aula->mutex_posicion[vieja_fila][vieja_columna]);
		}else if(vieja_fila < fila){ 		// Voy hacia abajo
			pthread_mutex_lock(&el_aula->mutex_posicion[vieja_fila][vieja_columna]);
			pthread_mutex_lock(&el_aula->mutex_posicion[fila][columna]);
		}else{ 								// Voy hacia arriba	
			pthread_mutex_lock(&el_aula->mutex_posicion[fila][columna]);
			pthread_mutex_lock(&el_aula->mutex_posicion[vieja_fila][vieja_columna]);
		}

		if (!alumno->salio)
			el_aula->posiciones[fila][columna]++;
	
		el_aula->posiciones[vieja_fila][vieja_columna]--;

		pthread_mutex_unlock(&el_aula->mutex_posicion[vieja_fila][vieja_columna]);
		pthread_mutex_unlock(&el_aula->mutex_posicion[fila][columna]);

		alumno->posicion_fila = fila;
		alumno->posicion_columna = columna;
	}

	return pudo_moverse;
}

void colocar_mascara(t_aula *el_aula, t_persona *alumno){
	printf("Esperando rescatista. Ya casi %s!\n", alumno->nombre);

	// Esperamos a que haya algun rescatista disponible para colocar la mascara.
	pthread_mutex_lock(&el_aula->mutex_rescatistas);
	while(el_aula->rescatistas_disponibles < 1)
		pthread_cond_wait(&el_aula->vc_rescatistas, &el_aula->mutex_rescatistas);

	el_aula->rescatistas_disponibles--;
	pthread_mutex_unlock(&el_aula->mutex_rescatistas);

	alumno->tiene_mascara = true;

	pthread_mutex_lock(&el_aula->mutex_rescatistas);
	el_aula->rescatistas_disponibles++;
	pthread_mutex_unlock(&el_aula->mutex_rescatistas);

	pthread_cond_signal(&el_aula->vc_rescatistas);
}


void *atendedor_de_alumno(void *estruc){

	thdata *estructura = (thdata *) estruc;
	int socket_fd = estructura->socket;
	t_aula  *el_aula   = estructura->el_aula;

	t_persona alumno;
	t_persona_inicializar(&alumno);
	
	if (recibir_nombre_y_posicion(socket_fd, &alumno) != 0) {
		if (estructura != NULL) free(estructura);
		terminar_servidor_de_alumno(socket_fd, NULL, NULL);
	}
	
	printf("Atendiendo a %s en la posicion (%d, %d)\n", 
			alumno.nombre, alumno.posicion_fila, alumno.posicion_columna);
		
	t_aula_ingresar(el_aula, &alumno);
	
	/// Loop de espera de pedido de movimiento.
	for(;;) {
		t_direccion direccion;
		
		/// Esperamos un pedido de movimiento.
		if (recibir_direccion(socket_fd, &direccion) != 0) {
			/* O la consola cortó la comunicación, o hubo un error. Cerramos todo. */
			if (estructura != NULL) free(estructura);
			terminar_servidor_de_alumno(socket_fd, el_aula, &alumno);
		}
		
		/// Tratamos de movernos en nuestro modelo
		bool pudo_moverse = intentar_moverse(el_aula, &alumno, direccion);

		printf("%s se movio a: (%d, %d)\n", alumno.nombre, alumno.posicion_fila, alumno.posicion_columna);

		enviar_respuesta(socket_fd, pudo_moverse ? OK : OCUPADO);			
		
		if (alumno.salio)
			break;
	}

	colocar_mascara(el_aula, &alumno);
	t_aula_liberar(el_aula, &alumno);
	enviar_respuesta(socket_fd, LIBRE);
	printf("Listo, %s es libre!\n", alumno.nombre);
	if (estructura != NULL) free(estructura);
	pthread_exit(NULL);
}


void crear_thread_servidor(int socket_client, t_aula *el_aula){
	thdata *estructura = malloc(sizeof(thdata));

	estructura->socket = socket_client;
	estructura->el_aula = el_aula;

	// Creamos el thread encargado de atender al cliente.
	pthread_t atendedor;
	int thr = pthread_create(&atendedor, NULL, atendedor_de_alumno, (void*) estructura);

	if(thr){
		perror("No se pudo crear el thread");
		exit(1);
	}
}


int main(void){
	//signal(SIGUSR1, signal_terminar);
	int socketfd_cliente, socket_servidor, socket_size;
	struct sockaddr_in local, remoto;

	/* Crear un socket de tipo INET con TCP (SOCK_STREAM). */
	if ((socket_servidor = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("creando socket");
	}

	/* Crear nombre, usamos INADDR_ANY para indicar que cualquiera puede conectarse aquí. */
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = htons(PORT);
	
	if (bind(socket_servidor, (struct sockaddr *)&local, sizeof(local)) == -1) {
		perror("haciendo bind");
	}

	/* Escuchar en el socket y permitir 5 conexiones en espera. */
	if (listen(socket_servidor, 5) == -1) {
		perror("escuchando");
	}
	
	t_aula el_aula;
	t_aula_iniciar_vacia(&el_aula);

	/// Aceptar conexiones entrantes.
	socket_size = sizeof(remoto);
	for(;;)
	{
		if (-1 == (socketfd_cliente = accept(socket_servidor, (struct sockaddr*) &remoto, (socklen_t*) &socket_size)))
		{			
			printf("!! Error al aceptar conexion\n");
		}
		else
		{
			crear_thread_servidor(socketfd_cliente, &el_aula);
		}
	}

	return 0;
}

