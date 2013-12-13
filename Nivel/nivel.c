/*
 * Sistemas Operativos - Super Mario Proc RELOADED.
 * Grupo       : C o no ser.
 * Nombre      : nivel.c.
 * Descripcion : Este archivo contiene la implementacion de las
 * funciones usadas por el nivel.
 */

#include "nivel.h"
#define TAM_EVENTO (sizeof(struct inotify_event)+24)
#define TAM_BUFFER (1024*TAM_EVENTO)//podria ser 1 en lugar de 1024? (ya que es cant.de eventos simultaneos y yo solo pregunto por un evento)

t_log  *logger;
t_list *list_personajes;
t_list *list_items;

pthread_mutex_t semSockPaq;
pthread_mutex_t semItems;


int main(int argc, char** argv) {
	tNivel nivel;
	int iSocket;
	int descriptorVigilador, descriptorInotify;
	char *configFilePath = argv[1];

	signal(SIGINT, cerrarForzado);

	pthread_mutex_init(&semSockPaq, NULL);
	pthread_mutex_init(&semItems,NULL);

	//LOG
	logger = logInit(argv, "NIVEL");

	/*
	 * FUNCION INIT
	 */
	// INICIALIZANDO GRAFICA DE MAPAS
	list_items 	    = list_create();
	list_personajes = list_create();
	nivel_gui_inicializar();
	nivel_gui_get_area_nivel(&nivel.maxRows, &nivel.maxCols);
	log_debug(logger, "Apunto de levantar el archivo de configuracion");
	levantarArchivoConf(configFilePath, &nivel);

	//SOCKETS
	iSocket = connectToServer(nivel.plataforma.IP, nivel.plataforma.port, logger);
    if (iSocket == EXIT_FAILURE) {
    	cerrarNivel("No se puede conectar con la plataforma");
    }

    nivel.plataforma.socket = iSocket;
    handshakeConPlataforma(&nivel);

	//INOTIFY
	descriptorInotify   = inotify_init();
	descriptorVigilador = inotify_add_watch(descriptorInotify, configFilePath, IN_MODIFY);
	if (descriptorVigilador == -1) {
		log_error(logger, "Error en inotify add_watch");
	}

	crearEnemigos(&nivel);

	// LANZANDO EL HILO DETECTOR DE INTERBLOQUEO
	pthread_t hiloInterbloqueo;
	pthread_create(&hiloInterbloqueo, NULL, &deteccionInterbloqueo, (void *)&nivel);

	escucharConexiones(&nivel, descriptorInotify, configFilePath);

	inotify_rm_watch(descriptorInotify, descriptorVigilador);
	close(descriptorInotify);
	log_destroy(logger);
	return EXIT_SUCCESS;
}

void handshakeConPlataforma(tNivel *pNivel) {
	tPaquete paquete;
	int largoNombre = strlen(pNivel->nombre) + 1;
	paquete.type   = N_HANDSHAKE;
	paquete.length = largoNombre;
	memcpy(paquete.payload, pNivel->nombre, largoNombre);
	enviarPaquete(pNivel->plataforma.socket, &paquete, logger, "Se envia handshake a plataforma");

	tMensaje tipoDeMensaje;
	char* payload;

	recibirPaquete(pNivel->plataforma.socket,&tipoDeMensaje,&payload,logger,"Se recibe handshake de plataforma");

	if(tipoDeMensaje != PL_HANDSHAKE){
		cerrarNivel("Nos llego mal el mensaje");
	}

	tInfoNivel infoDeNivel;
	tipoDeMensaje=N_DATOS;
	infoDeNivel.algoritmo = pNivel->plataforma.algPlanif;
	infoDeNivel.quantum	  = pNivel->plataforma.valorAlgorimo;
	infoDeNivel.delay	  = pNivel->plataforma.delay;
	serializarInfoNivel(N_DATOS, infoDeNivel, &paquete);

	enviarPaquete(pNivel->plataforma.socket, &paquete, logger, "Se envia a la plataforma los criterios de planificacion");
}

void crearEnemigos(tNivel *nivel) {
	// CREANDO Y LANZANDO HILOS ENEMIGOS
	if (nivel->cantEnemigos > 0) {
		int indexEnemigos;
		tEnemigo *aHilosEnemigos;
		log_debug(logger,"Cantidad de enemigos %d ", nivel->cantEnemigos);
		aHilosEnemigos = calloc(nivel->cantEnemigos, sizeof(tEnemigo));

		for (indexEnemigos = 0; indexEnemigos < nivel->cantEnemigos; indexEnemigos++) {
			aHilosEnemigos[indexEnemigos].ID = indexEnemigos + 1; //El numero o id de enemigo
			aHilosEnemigos[indexEnemigos].pNivel = nivel;

			if (pthread_create(&aHilosEnemigos[indexEnemigos].thread, NULL, enemigo, (void*)&aHilosEnemigos[indexEnemigos] )) {
				log_error(logger, "pthread_create: %s", strerror(errno));
				exit(EXIT_FAILURE);
			}
		}

	} else {
		nivel_gui_dibujar(list_items, nivel->nombre);
	}
}

void conexionPersonaje(int iSocket, char *sPayload) {
	tPersonaje *pPersonaje;
	tSimbolo *simbolo = deserializarSimbolo(sPayload);

	pPersonaje = getPersonajeBySymbol(*simbolo);

	if (pPersonaje == NULL) {
		crearNuevoPersonaje(*simbolo);
		notificacionAPlataforma(iSocket, N_CONEXION_EXITOSA, "Se notifica a plataforma que el personaje se onecto exitosamente");
	} else {//se encontro=>el personaje ya existe
		notificacionAPlataforma(iSocket, N_PERSONAJE_YA_EXISTENTE, "Se notifica a plataforma que el personaje ya existe");
	}

	free(simbolo);
	free(sPayload);
}

void movimientoPersonaje(tNivel *pNivel, int iSocket, char *sPayload) {
	tPersonaje *pPersonaje;
	tPosicion posPersonaje;
	tMovimientoPers* movPersonaje;

	movPersonaje = deserializarMovimientoPers(sPayload);


	log_debug(logger, "<<< %s: Solicitud de movimiento del personaje %c", pNivel->nombre, movPersonaje->simbolo);

	pPersonaje = getPersonajeBySymbol(movPersonaje->simbolo);

	if (pPersonaje != NULL) {

		char symbol=(char) movPersonaje->simbolo;
		getPosPersonaje(list_items,symbol, &posPersonaje.x, &posPersonaje.y);

		if(!pPersonaje->muerto && !hayAlgunEnemigoArriba(pNivel, posPersonaje.x, posPersonaje.y)){

			pPersonaje->bloqueado=false;

			calcularMovimiento(pNivel, movPersonaje->direccion, &posPersonaje.x, &posPersonaje.y);

			MoverPersonaje(list_items,symbol, posPersonaje.x, posPersonaje.y);
			nivel_gui_dibujar(list_items, pNivel->nombre);
			notificacionAPlataforma(iSocket, N_CONFIRMACION_MOV, "Notificando a plataforma personaje movido correctamente");
		}
		else {
			matarPersonaje(pNivel, &movPersonaje->simbolo);
		}
	}

	free(sPayload);
	free(movPersonaje);

}

void posicionRecurso(tNivel *pNivel, int iSocket, char *sPayload) {
	tPaquete paquete;
	tPregPosicion *posConsultada = deserializarPregPosicion(sPayload);

	log_debug(logger, "<<< %s Personaje %c solicita la posicion del recurso %c", pNivel->nombre, posConsultada->simbolo, posConsultada->recurso);

	bool buscarRecurso(ITEM_NIVEL *item){
		return ((item->item_type == RECURSO_ITEM_TYPE) && (item->id == posConsultada->recurso) );
	}

	ITEM_NIVEL* pRecurso;
	pRecurso = list_find(list_items, (void*)buscarRecurso);

	if (pRecurso != NULL) {
		tRtaPosicion rtaPosicion;
		rtaPosicion.posX = pRecurso->posx;
		rtaPosicion.posY = pRecurso->posy;
		serializarRtaPosicion(N_POS_RECURSO, rtaPosicion, &paquete);

		enviarPaquete(iSocket, &paquete, logger, "Se envia posicion de recurso a la plataforma");

	} else {
		notificacionAPlataforma(iSocket, N_RECURSO_INEXISTENTE, "El recurso solicitado no existe");
	}

	free(posConsultada);
	free(sPayload);
}

void solicitudRecurso(tNivel *pNivel, int iSocket, char *sPayload) {
	tPregPosicion *posConsultada;
	tPaquete paquete;
	posConsultada = deserializarPregPosicion(sPayload);

	log_debug(logger, "<<< %s: Personaje %c solicita una instancia del recurso %c", pNivel->nombre, (char)posConsultada->simbolo, (char)posConsultada->recurso);
	// Calculo la cantidad de instancias
	int cantInstancias = restarInstanciasRecurso(list_items, posConsultada->recurso);

	tPersonaje *pPersonaje;
	pPersonaje = getPersonajeBySymbol(posConsultada->simbolo);

	if (cantInstancias >= 0) { //SE LO OTORGO EL RECURSO PEDIDO

		log_info(logger, "Al personaje %c se le dio el recurso %c",posConsultada->simbolo,posConsultada->recurso);


		//Agrego el recurso a la lista de recursos del personaje y lo desbloquea si estaba bloqueado
		void agregaRecursoYdesboquea(tPersonaje *personaje){
			if (personaje->simbolo == pPersonaje->simbolo) {
				personaje->bloqueado = false;
				list_add_new(personaje->recursos, &(posConsultada->recurso), sizeof(tSimbolo));
			}
		}

		list_iterate(list_personajes,(void*) agregaRecursoYdesboquea);
		serializarSimbolo(N_ENTREGA_RECURSO, posConsultada->recurso, &paquete);
		// Envio mensaje donde confirmo la otorgacion del recurso pedido
		enviarPaquete(iSocket, &paquete, logger, "Se envia confirmacion de otorgamiento de recurso a plataforma");

	} else { //ESTA BLOQUEADO

		log_info(logger,"El personaje %c se bloqueo por el recurso %c",posConsultada->simbolo,posConsultada->recurso);
		//Agrego el recurso a la lista de recursos del personaje y lo bloqueo
		void agregaRecursoYbloquea(tPersonaje *personaje) {
			if (personaje->simbolo == pPersonaje->simbolo) {
				personaje->bloqueado = true;
				list_add_new(personaje->recursos,&(posConsultada->recurso),sizeof(tSimbolo));
			}
		}

		list_iterate(list_personajes,(void*)agregaRecursoYbloquea);
	}

	free(sPayload);
	free(posConsultada);
}


void liberarRecursosPersonajeMuerto(tNivel *pNivel, char *sPayload){
	tDesconexionPers *persDesconectado = deserializarDesconexionPers(sPayload);


	log_debug(logger, "%s: Liberando recursos del personaje %c", pNivel->nombre, persDesconectado->simbolo);

	int i;
	for (i=0; i<persDesconectado->lenghtRecursos; i++) {
			sumarRecurso(list_items,persDesconectado->recursos[i]);
			log_debug(logger, "Libere una instancia del recurso %c", persDesconectado->recursos[i]);
	}
	free(persDesconectado);
	free(sPayload);
}


void desconexionPersonaje(char *sPayload) {
	tDesconexionPers *persDesconectado;
	persDesconectado = deserializarDesconexionPers(sPayload);
	free(sPayload);

	log_debug(logger, "<<< El personaje %c se desconecto", persDesconectado->simbolo);

	// Eliminar al personaje de list_personajes
	bool buscarPersonaje(tPersonaje* pPersonaje) {
		return (pPersonaje->simbolo == persDesconectado->simbolo);
	}

	tPersonaje *personajeOut = list_remove_by_condition(list_personajes,(void*)buscarPersonaje);
	pthread_mutex_lock(&semItems);
	//agrego una instancia a list_items de todos los recursos que me manda planificador (que son todos los que no reasigno)
	int iIndexRecurso;
	for (iIndexRecurso=0; iIndexRecurso<persDesconectado->lenghtRecursos; iIndexRecurso++) {
		sumarRecurso(list_items, persDesconectado->recursos[iIndexRecurso]);
		log_debug(logger, "Libere una instancia del recurso %c", persDesconectado->recursos[iIndexRecurso]);
	}

	if (!personajeOut->muerto) {
		BorrarItem(list_items, persDesconectado->simbolo); //Si no esta muerto, sacalo
	} else {
		log_error(logger, "No se elimina al personaje %c", personajeOut->simbolo);
	}
	pthread_mutex_unlock(&semItems);
	log_debug(logger, "Libere recursos");
	free(persDesconectado);
	personaje_destroyer(personajeOut);
}

void escucharConexiones(tNivel *pNivel, int fdInotify, char* configFilePath) {
	// Variables del Poll
	struct pollfd uDescriptores[POLL_NRO_FDS];
	uDescriptores[0].fd	    = pNivel->plataforma.socket;
	uDescriptores[0].events = POLLIN;
	uDescriptores[1].fd 	= fdInotify;
	uDescriptores[1].events = POLLIN;
	int iResultadoPoll;

	// Variables iNotify
	int bytesLeidos;
	char bufferInotify[TAM_BUFFER];

	char *sPayload;
	tMensaje tipoDeMensaje;
	int iSocket = pNivel->plataforma.socket;

	while (1) {

		if ((iResultadoPoll = poll(uDescriptores, POLL_NRO_FDS, POLL_TIMEOUT)) == -1) {
			log_error(logger, "Error al escuchar en el polling");

		} else {

			if (uDescriptores[1].revents & POLLIN) { // Hay data lista para recibir
				int bytes;
				bytesLeidos = read(fdInotify, bufferInotify, TAM_BUFFER);
				struct inotify_event* evento; // VER ESTO, PUEDE FALLAR EL CAMBIO DE WHILE A FOR

				for (bytes = 0; bytes < bytesLeidos; bytes += TAM_EVENTO + evento->len) {

					evento = (struct inotify_event*) &bufferInotify[bytes];

					if (evento->mask & IN_MODIFY) {//avisar a planificador que cambio el archivo config
						actualizarInfoNivel(pNivel, iSocket, configFilePath);
					}
				}
			} //Preguntar por que si poll detecta actividad en el descriptor del inotify y este solo se acciona cuando ocurre in_modify => haria falta todo lo que sigue? o
			 //simplemente podria actualizar los datos y listo?
			if (uDescriptores[0].revents & POLLIN) { // Hay data lista para recibir

				recibirPaquete(iSocket, &tipoDeMensaje, &sPayload, logger,"Recibiendo mensaje de plataforma");

				switch(tipoDeMensaje) {
				case PL_CONEXION_PERS:
					conexionPersonaje(iSocket, sPayload);
					break;

				case PL_POS_RECURSO:
					posicionRecurso(pNivel, iSocket, sPayload);
					break;

				case PL_MOV_PERSONAJE:
					movimientoPersonaje(pNivel, iSocket, sPayload);
					break;

				case PL_SOLICITUD_RECURSO:
					solicitudRecurso(pNivel, iSocket, sPayload);
					break;

				case PL_LIBERA_RECURSOS:
					liberarRecursosPersonajeMuerto(pNivel, sPayload);
					break;

				case PL_DESCONEXION_PERSONAJE:// Un personaje termino o murio y debo liberar instancias de recursos que tenia asignado
					desconexionPersonaje(sPayload);
					break;

				default:
					break;
				} //Fin del switch

				pthread_mutex_lock(&semItems);
				nivel_gui_dibujar(list_items, pNivel->nombre);

				pthread_mutex_unlock(&semItems);
			}
		}
	}
}

void levantarArchivoConf(char* pathConfigFile, tNivel *pNivel) {

	t_config *configNivel;
	tPosicion posCaja;
	int iCaja = 1;
	bool hayCajas = false;
	char* sLineaCaja;
	char** aCaja;
	char* datosPlataforma;

	configNivel = config_try_create(pathConfigFile, "Nombre,Recovery,Enemigos,Sleep_Enemigos,Algoritmo,Quantum,Retardo,TiempoChequeoDeadlock,Plataforma");

	sLineaCaja = string_from_format("Caja%i", iCaja);

	while (config_has_property(configNivel, sLineaCaja)) {
		hayCajas = true;
		aCaja = config_get_array_value(configNivel, sLineaCaja);
		free(sLineaCaja);

		posCaja.x  = atoi(aCaja[3]);
		posCaja.y  = atoi(aCaja[4]);

		if (posCaja.y > pNivel->maxRows || posCaja.y > pNivel->maxCols || posCaja.y < 1 || posCaja.y < 1) {
			char* messageLimitErr;
			messageLimitErr = string_from_format(
				"La caja %s excede los limites de la pantalla. CajaPos=(%d,%d) - Limites=(%d,%d)",
				iCaja, posCaja.x, posCaja.y, pNivel->maxCols, pNivel->maxRows
			);
			cerrarNivel(messageLimitErr);
		}

		pthread_mutex_lock(&semItems); //TODO ver semaforo
		// Si la validacion fue exitosa creamos la caja de recursos
		CrearCaja(list_items, *aCaja[1],atoi(aCaja[3]),atoi(aCaja[4]),atoi(aCaja[2]));
		pthread_mutex_unlock(&semItems);

		iCaja++;
		sLineaCaja = string_from_format("Caja%i", iCaja);
	};

	if (!hayCajas) {
		cerrarNivel("No hay cajas disponibles");
	}

	log_info(logger, "Archivo correcto, se procede a levantar los valores");

	pNivel->cantRecursos   = list_size(list_items);

	pNivel->deadlock.recovery  = config_get_int_value(configNivel, "Recovery");
	pNivel->deadlock.checkTime = config_get_int_value(configNivel, "TiempoChequeoDeadlock");

	pNivel->nombre = string_duplicate(config_get_string_value(configNivel,"Nombre"));

	pNivel->cantEnemigos  = config_get_int_value(configNivel, "Enemigos");

	pNivel->sleepEnemigos = config_get_int_value(configNivel, "Sleep_Enemigos");

	char* algoritmoAux;
	algoritmoAux   = config_get_string_value(configNivel, "Algoritmo");

	if (strcmp(algoritmoAux, "RR") == 0) {
		pNivel->plataforma.algPlanif = RR;
		pNivel->plataforma.valorAlgorimo = config_get_int_value(configNivel, "Quantum");
	} else {
		pNivel->plataforma.algPlanif = SRDF;
	}

	free(algoritmoAux);

	pNivel->plataforma.delay = config_get_int_value(configNivel, "Retardo");

	datosPlataforma = config_get_string_value(configNivel, "Plataforma");

	char ** aDatosPlataforma = string_split(datosPlataforma, ":");
	strcpy(pNivel->plataforma.IP, aDatosPlataforma[0]);
	pNivel->plataforma.port  = atoi(aDatosPlataforma[1]);

	config_destroy(configNivel);
}

void actualizarInfoNivel(tNivel *pNivel, int iSocket, char* configFilePath) {
	char* algoritmoAux;
	t_config *configNivel;

	configNivel  = config_create(configFilePath);
	while(!config_has_property(configNivel,"Algoritmo")) {
		usleep(10);
		config_destroy(configNivel);
	}

	algoritmoAux = config_get_string_value(configNivel, "Algoritmo");

	if (strcmp(algoritmoAux,"RR") == 0) {
		pNivel->plataforma.algPlanif = RR;
		pNivel->plataforma.valorAlgorimo = config_get_int_value(configNivel, "Quantum");
	} else {
		pNivel->plataforma.algPlanif = SRDF;
	}

	pNivel->plataforma.delay = config_get_int_value(configNivel, "Retardo");
	log_debug(logger, "Se produjo un cambio en el archivo configuracion, Algoritmo: %s quantum:%i retardo:%i",
			algoritmoAux, pNivel->plataforma.valorAlgorimo, pNivel->plataforma.delay);
	free(algoritmoAux);
	config_destroy(configNivel);

	tPaquete paquete;
	//ENVIANDO A PLATAFORMA NOTIFICACION DE ALGORITMO ASIGNADO
	tInfoNivel infoDeNivel;
	infoDeNivel.algoritmo = pNivel->plataforma.algPlanif;
	infoDeNivel.quantum   = pNivel->plataforma.valorAlgorimo;
	infoDeNivel.delay     = pNivel->plataforma.delay;
	//serializacion propia porque la de protocolo no me andaba bien

	serializarInfoNivel(N_ACTUALIZACION_CRITERIOS, infoDeNivel, &paquete);
	enviarPaquete(iSocket, &paquete, logger, "Notificando cambio de algoritmo a plataforma");
}

void *enemigo(void * args) {

	tEnemigo *enemigo;
	enemigo = (tEnemigo *)args;

	//Variables de movimiento
	int contMovimiento = 3;
	char ultimoMov     = 'a';
	char dirMov	       = 'b';
	//Variables de persecucion de victima
	tPersonaje* persVictima;
	bool estaEnRecurso = false;

	bool esUnRecurso(ITEM_NIVEL *itemNiv){
		return (itemNiv->item_type==RECURSO_ITEM_TYPE && itemNiv->posx==enemigo->posX && itemNiv->posy==enemigo->posY);
	}

	pthread_mutex_lock(&semItems);

	do {
		enemigo->posX = 1 + (rand() % enemigo->pNivel->maxCols);
		enemigo->posY = 1 + (rand() % enemigo->pNivel->maxRows);
		estaEnRecurso = list_any_satisfy(list_items,(void*)esUnRecurso);
	} while (estaEnRecurso);

	CreateEnemy(list_items, enemigo->ID, enemigo->posX, enemigo->posY);
	log_info(logger, "Se crea el enemigo %d", enemigo->ID);
	pthread_mutex_unlock(&semItems);


	bool movimientoAleatorio;

	while (1) {

		movimientoAleatorio = analizarMovimientoDeEnemigo();

		if (movimientoAleatorio) {
			////MOVIMIENTO EN L
			/*
			 * Para hacer el movimiento de caballo uso la var ultimoMov que puede ser:
			 * 		a:el ultimo movimiento fue horizontal por primera vez
			 * 		b:el ultimo movimiento fue horizontal por segunda vez
			 * 		c:el ultimo movimiento fue vertical por primera vez
			 *
			 * La variable dirMov que indicara en que direccion se esta moviendo
			 * 		a:abajo-derecha
			 * 		b:abajo-izquierda
			 * 		c:arriba-derecha
			 * 		d:arriba-izquierda
			*/
			if(enemigo->posY<1){ //se esta en el limite vertical superior
				if((enemigo->posX<1)||(dirMov=='c'))
					dirMov='a';
				if((enemigo->posX>enemigo->pNivel->maxCols)||(dirMov=='d'))
					dirMov='b';
			}
			if(enemigo->posY>enemigo->pNivel->maxRows){ //se esta en el limite vertical inferior
				if((enemigo->posX<1)||(dirMov=='a'))
					dirMov='c';
				if((enemigo->posX>enemigo->pNivel->maxCols)||(dirMov=='b'))
					dirMov='d';
			}
			if(enemigo->posX<=0){ //se esta en el limite horizontal izquierdo
				if(dirMov=='b')
					dirMov='a';
				else
					dirMov='c';
			}
			if(enemigo->posX>enemigo->pNivel->maxCols){
				if(dirMov=='a')
					dirMov='b';
				else
					dirMov='d';
			}
			//calculando el movimiento segun lo anterior y la direccion con la que viene
			switch(dirMov){
			case'a':
				if(ultimoMov=='a'){
					contMovimiento=1;
					ultimoMov='b';
				}else{
					if(ultimoMov=='c'){
						contMovimiento=1;
						ultimoMov='a';
					}else{
						contMovimiento=4;
						ultimoMov='c';
					}
				}
				break;
			case'b':
				if(ultimoMov=='a'){
					contMovimiento=3;
					ultimoMov='b';
				}else{
					if(ultimoMov=='c'){
						contMovimiento=3;
						ultimoMov='a';
					}else{
						contMovimiento=4;
						ultimoMov='c';
					}
				}
				break;
			case 'c':
				if(ultimoMov=='a'){
					contMovimiento=1;
					ultimoMov='b';
				}else{
					if(ultimoMov=='c'){
						contMovimiento=1;
						ultimoMov='a';
					}else{
						contMovimiento=2;
						ultimoMov='c';
					}
				}
				break;
			case 'd':
				if(ultimoMov=='a'){
					contMovimiento=3;
					ultimoMov='b';
				}else{
					if(ultimoMov=='c'){
						contMovimiento=3;
						ultimoMov='a';
					}else{
						contMovimiento=2;
						ultimoMov='c';
						}
					}
				break;
			}
			actualizaPosicion(&contMovimiento, &(enemigo->posX),&(enemigo->posY));
			void esUnRecurso(ITEM_NIVEL *ite){
				if ((ite->item_type==RECURSO_ITEM_TYPE)&&((ite->posx==enemigo->posX)&&(ite->posy==enemigo->posY))){
					if(ultimoMov=='a'||ultimoMov=='b')
						enemigo->posY++;
					else
						enemigo->posX--;
				}
			}
			list_iterate(list_items,(void*)esUnRecurso);
		} else { //ELEGIR O PERSEGUIR A LA VICTIMA

			////PERSECUCION DE PERSONAJE
			ITEM_NIVEL *victima = asignarVictima(enemigo);


			persVictima = getPersonajeBySymbol((tSimbolo)victima->id);

			if(!persVictima->bloqueado){

				acercarmeALaVictima(enemigo, victima, &contMovimiento);

				actualizaPosicion(&contMovimiento, &(enemigo->posX),&(enemigo->posY));

				evitarRecurso(enemigo);

				evitarOrigen(enemigo);

//				if(hayAlgunEnemigoArriba(enemigo->pNivel, victima->posx, victima->posy)){
//					if(!persVictima->muerto){
//						persVictima->muerto=true;
//						MoveEnemy(list_items, enemigo->ID, enemigo->posX,enemigo->posY);
//						nivel_gui_dibujar(list_items, enemigo->pNivel->nombre);
//					}
//				}
			}


		}


		pthread_mutex_lock(&semItems);
		MoveEnemy(list_items, enemigo->ID, enemigo->posX,enemigo->posY);
		nivel_gui_dibujar(list_items, enemigo->pNivel->nombre);
		pthread_mutex_unlock(&semItems);
		usleep(enemigo->pNivel->sleepEnemigos);

	} //Fin de while(1)
	pthread_exit(NULL );
}


////FUNCIONES ENEMIGOS
_Bool alcanceVictima(tEnemigo *enemigo, ITEM_NIVEL *victima){
	return (victima->posx==enemigo->posX) && (victima->posy==enemigo->posY);
}

_Bool analizarMovimientoDeEnemigo(){

	int cantPersonajesNoBloqueados;
	bool personajeBloqueado(tPersonaje* personaje){
		return (personaje->bloqueado==false);
	}

	cantPersonajesNoBloqueados = list_count_satisfying(list_personajes,(void*)personajeBloqueado);

	return (cantPersonajesNoBloqueados==0);
}

//Devuelve true si alcance la victima y false en caso contrario
_Bool acercarmeALaVictima(tEnemigo *enemigo, ITEM_NIVEL *item, int *contMovimiento){

	//Elijo el eje por el que me voy a acercar
	if(enemigo->posY == item->posy){
		if(enemigo->posX < item->posx){
			*contMovimiento=1;
		}
		if(enemigo->posX > item->posx){
			*contMovimiento=3;
		}
		if(enemigo->posX == item->posx){
			return true;
		}
	}
	else{ //acercarse por fila
		if(enemigo->posY < item->posy)
			*contMovimiento=4;
		if(enemigo->posY > item->posy)
			*contMovimiento=2;
	}
	return false;

}

ITEM_NIVEL *asignarVictima(tEnemigo *enemigo) {
	int iDistanciaFinal;
	int iDistanciaMenor = 999999;
	int iIndexItems, iCantidadItems;
	ITEM_NIVEL *itemReturn = NULL;

	pthread_mutex_lock(&semItems);
	iCantidadItems = list_size(list_items);

	for (iIndexItems = 0; iIndexItems < iCantidadItems; iIndexItems++) {
		ITEM_NIVEL *item = list_get(list_items, iIndexItems);

		if (esUnPersonaje(item)) {
			iDistanciaFinal = calcularDistancia(enemigo, item);

			if (iDistanciaFinal < iDistanciaMenor) {
				itemReturn = item;
				iDistanciaMenor = iDistanciaFinal;
			}
		}
	}
	pthread_mutex_unlock(&semItems);

	return itemReturn;
}

_Bool esUnPersonaje(ITEM_NIVEL *item){
	return (item->item_type==PERSONAJE_ITEM_TYPE);

}

int calcularDistancia(tEnemigo *enemigo, ITEM_NIVEL *item){

	int terminoEnX = abs(item->posx - enemigo->posX);
	int terminoEnY = abs(item->posy - enemigo->posY);

	return (terminoEnX + terminoEnY);
}

//Buscar en list_items y me devuelve el personaje que cumple la condicion
ITEM_NIVEL *getItemVictimaSiExiste(tEnemigo *enemigo){
	ITEM_NIVEL *personajeItem;
	bool esUnPersonaje(ITEM_NIVEL *item){
		return (item->item_type==PERSONAJE_ITEM_TYPE)&&((item->posx==enemigo->posX)&&(item->posy==enemigo->posY));
	}
	personajeItem = list_find(list_items, (void *)esUnPersonaje);
	return personajeItem;
}

//Busca en la lista de items una victima
ITEM_NIVEL *getItemById(char id_victima){
	int i;
	for(i=0;i<list_size(list_items);i++){
		ITEM_NIVEL *item=list_get(list_items,i);
		if(item->id==id_victima)
			return item;
	}
	return NULL;
}

//Mueve el enemigo atras en x si se paro en un recurso
void evitarRecurso(tEnemigo *enemigo){

	void esUnRecurso(ITEM_NIVEL *item){
		if ((item->item_type==RECURSO_ITEM_TYPE)&&((item->posx==enemigo->posX)&&(item->posy==enemigo->posY)))
			enemigo->posX--;
	}
	list_iterate(list_items,(void*)esUnRecurso);
}

void evitarOrigen(tEnemigo *enemigo){

	if(enemigo->posX==0 && enemigo->posY==0){
		enemigo->posX++;
	}
}

void CrearNuevoPersonaje(tPersonaje *pjNew, tSimbolo simbolo){
	pjNew->simbolo  = simbolo;
	pjNew->bloqueado  = false;
	pjNew->recursos = list_create();
	list_add_new(list_personajes,(void*)pjNew,sizeof(tPersonaje));
	pthread_mutex_unlock(&semItems);
	CrearPersonaje(list_items, (char)simbolo, INI_X, INI_Y);
	pthread_mutex_unlock(&semItems);
	log_info(logger, "<<< Se agrego al personaje %c a la lista", simbolo);
}

_Bool hayAlgunEnemigoArriba(tNivel *pNivel, int posPerX, int posPerY) {

	int i;
	int posEnemyX, posEnemyY;
	for (i = 1; i <= pNivel->cantEnemigos; i++) {
		getPosEnemy(list_items, i, &posEnemyX, &posEnemyY);
		if (posPerX == posEnemyX && posPerY == posEnemyY)
			return true;
	}
	return false;
}

//Busca en la list_personajes
tPersonaje *getPersonajeBySymbol(tSimbolo simbolo){
	tPersonaje *unPersonaje;
	bool buscaPersonaje(tPersonaje* personaje){
		return (personaje->simbolo==simbolo);
	}
	unPersonaje=(tPersonaje *)list_find(list_personajes,(void*)buscaPersonaje);
	return unPersonaje;
}

void crearNuevoPersonaje (tSimbolo simbolo) {
	tPersonaje *pPersonajeNuevo = malloc(sizeof(tPersonaje));
	pPersonajeNuevo->simbolo = simbolo;
	pPersonajeNuevo->bloqueado = false;
	pPersonajeNuevo->muerto = false;
	pPersonajeNuevo->listoParaPerseguir = false;
	pPersonajeNuevo->recursos = list_create();
	list_add(list_personajes, pPersonajeNuevo);
	pthread_mutex_unlock(&semItems);
	CrearPersonaje(list_items, (char)simbolo, INI_X, INI_Y);
	pthread_mutex_unlock(&semItems);
	log_info(logger, "<<< Se agrego al personaje %c a la lista", simbolo);
}

void notificacionAPlataforma(int iSocket, tMensaje tipoMensaje, char *msjInfo) {
	tPaquete paquete;
	paquete.type   = tipoMensaje;
	paquete.length = 0;
	enviarPaquete(iSocket, &paquete, logger, msjInfo);
}

void calcularMovimiento(tNivel *pNivel, tDirMovimiento direccion, int *posX, int *posY) {
	switch (direccion) {
		case arriba:
			if (*posY > 1) {
				(*posY)--;
			}
			break;
		case abajo:
			if (*posY < pNivel->maxRows) {
				(*posY)++;
			}
			break;
		case izquierda:
			if (*posX > 1) {
				(*posX)--;
			}
			break;
		case derecha:
			if (*posX < pNivel->maxCols) {
				(*posX)++;
			}
			break;
		default:
			log_error(logger, "ERROR: no detecto una direccion de movimiento valida: %d", direccion);
			break;
	}
}

void matarPersonaje(tNivel *pNivel, tSimbolo *simboloItem){

	tPaquete paquete;

	log_debug(logger, "-> Un enemigo alcanzo al personaje %c <-", *simboloItem);
	log_debug(logger, "Eliminando al personaje...");
	bool buscarPersonaje(tPersonaje* perso){return(perso->simbolo==*simboloItem);}
	tPersonaje *personajeOut = list_remove_by_condition(list_personajes,(void*)buscarPersonaje);

	pthread_mutex_unlock(&semItems);
	BorrarItem(list_items, *simboloItem);
	nivel_gui_dibujar(list_items, pNivel->nombre);
	pthread_mutex_unlock(&semItems);


	paquete.type=N_MUERTO_POR_ENEMIGO;
	memcpy(paquete.payload, simboloItem,sizeof(tSimbolo));
	paquete.length=sizeof(tSimbolo);
	//TODO en plataforma tiene que tener el socket
	//enviarPaquete(pNivel->plataforma.,&paquete,logger,"enviando notificacion de muerte de personaje a plataforma");
	personaje_destroyer(personajeOut);
}

void *deteccionInterbloqueo (void *parametro) {

	tNivel *pNivel;
	pNivel = (tNivel *)parametro;

	extern t_list* list_items;
	tPaquete paquete;

	tSimbolo* caja;
	tPersonaje *personaje;
	tSimbolo personajeSimbolo;

	tInfoInterbloqueo deadlock = pNivel->deadlock;


	int i,j,k,fila;
	ITEM_NIVEL* itemRec = NULL;
	struct timespec dormir;
	dormir.tv_sec  = (time_t)(deadlock.checkTime/1000);
	dormir.tv_nsec = (long)((deadlock.checkTime%1000) * 1000000);

	int cantPersonajes;
	int **matAsignacion = NULL;
	int **matSolicitud  = NULL;
	int *vecSatisfechos = NULL;
	t_caja *vecCajas    = NULL;
	int indice,cantPersonajesSatisfechos;
	char encontrado;

	while(1){
	    cantPersonajes = list_size(list_personajes);
		indice=0;
		cantPersonajesSatisfechos=0;

		if (cantPersonajes!=0) {
			matAsignacion = (int**) malloc(sizeof(int*) * cantPersonajes);
			matSolicitud  = (int**) malloc(sizeof(int*) * cantPersonajes);
			vecSatisfechos= (int*)  malloc(sizeof(int)  * cantPersonajes);
			vecCajas = (t_caja*) malloc(sizeof(t_caja) * pNivel->cantRecursos);

			for (i=0; i<cantPersonajes; i++) {
				matAsignacion[i] = (int*) malloc(sizeof(int) * pNivel->cantRecursos);
				matSolicitud[i]  = (int*) malloc(sizeof(int) * pNivel->cantRecursos);
			}

		    pthread_mutex_lock (&semItems);// Nadie mueve un pelo hasta que no se evalue el interbloqueo
			// Inicializando matrices
			for (i=0; i<cantPersonajes; i++) {

				for (j=0; j<pNivel->cantRecursos; j++) {
					matAsignacion[i][j] = 0;
					*(*(matSolicitud+i)+j) = 0;
				}
				*(vecSatisfechos+i)=-1;
			}
			// Llenando las matrices con la info
			for (i=0; i<list_size(list_items); i++) {
				itemRec = list_get(list_items,i);

				if (itemRec->item_type == RECURSO_ITEM_TYPE) {
					vecCajas[indice].simbolo = itemRec->id;
					vecCajas[indice].cantidadInstancias = itemRec->quantity;
					indice++;
				}
			}

			for(i=0; i<cantPersonajes; i++) { // Recorro la lista de personajes y por cada personaje
				personaje=list_get(list_personajes,i);

				for (j=0; j<list_size(personaje->recursos); j++) { // Recorro su lista de recursos y por cada recurso asignados:
					caja = list_get(personaje->recursos,j);

					for (k=0; k<pNivel->cantRecursos; k++) {               // Recorro a vecCajas (vector de recursos en list_items) viendo por c/u de sus elementos
						if (vecCajas[k].simbolo == (char)*caja) {  // Si coincide con el recurso recorrido con j (esto es para que las matrices mantengan los indices)
							// Me fijo si es el ultimo elemento de la lista(=>es el por el que el personaje esta bloqueado)
							if ((j == list_size(personaje->recursos)-1) && (personaje->bloqueado==true)) {
								matSolicitud[i][k]+=1;             // Entonces lo pongo en la ubicacion i k de la matriz de solicitud
							} else {
								matAsignacion[i][k]+=1;
							}
						}
					}
				}
			}

			//detectando el interbloqueo
			for (i=0; i<cantPersonajes; i++) {
				fila = 0;
				for (j=0;j<pNivel->cantRecursos;j++) {
					if ((matSolicitud[i][j]-vecCajas[j].cantidadInstancias) <= 0) {
						fila++;
					}
				}//recDisponibles[j])<=0) fila++;}
				if ((fila == pNivel->cantRecursos) && (vecSatisfechos[i] != 0)) { //los recursos disponibles satisfacen algun proceso
					vecSatisfechos[i] = 0;
					for (j=0; j<pNivel->cantRecursos; j++) {
						vecCajas[j].cantidadInstancias +=matAsignacion[i][j];//recDisponibles[j] += matAsignacion[i][j];
					}
				    cantPersonajesSatisfechos++;
				    i = -1;//para volver a recorrer la matriz con los recursos disponibles actualizados
			   	}
			}
			if (cantPersonajesSatisfechos==cantPersonajes) {
				log_debug(logger, "no hay interbloqueo");
				//NO HAY INTERBLOQUEO
			} else {
				log_debug(logger,"hay interbloqueo");
				if (deadlock.recovery==1) {//notificar a plataforma, entonces vecSatisfechos contendra -1-->el personaje quedo interbloqueado
					encontrado='0';

					while (encontrado == '0') {
						log_debug(logger,"en el while");
						if (vecSatisfechos[i] != 0) {
							//cargar el simbolo de ese personaje
							log_debug(logger,"en el if del while");
							personaje = list_get(list_personajes,i);
							personajeSimbolo = personaje->simbolo;
							log_debug(logger,"en el if del while2");
							encontrado = '1';
						}
						log_debug(logger,"saliendo del if");
					}
					log_debug(logger,"saliendo del while");
					log_debug(logger,"antes de enviar paquete con perso(caja):%c",personaje);
					pthread_mutex_lock(&semSockPaq); //TODO sacar
					paquete.type = N_MUERTO_POR_DEADLOCK;
					paquete.length = sizeof(tSimbolo);
					paquete.payload[0] = personajeSimbolo;
					//TODO socket para al pNivel
//					enviarPaquete(sockete,&paquete,logger,"enviando notificacion de bloqueo de personajes a plataforma");
					pthread_mutex_unlock(&semSockPaq);
					//eliminar personaje y devolver recursos
					log_debug(logger, "El personaje %c se elimino por participar en un interbloqueo", paquete.payload[0]);
//					pthread_mutex_lock(&semItems);
//					liberarRecsPersonaje2((char) personaje->simbolo); //Libera recursos solo en DESCONEXIN_PERS
//					pthread_mutex_unlock(&semItems);
				}
			}
			pthread_mutex_unlock (&semItems);
			for (i=1;i<cantPersonajes;i++) {//i=1 o 0?
				free(matSolicitud[i]);
				free(matAsignacion[i]);
			}

			free(matAsignacion);
			free(matSolicitud);
			free(vecSatisfechos);
			free(vecCajas);
		}
		nanosleep(&dormir,NULL);
	}
	return 0;
}

//TODO esta no sirve
void liberarRecsPersonaje(char id){
	tPersonaje* personaje;

	bool buscarPersonaje(tPersonaje* perso){return(perso->simbolo==id);}
	//eliminar al personaje de list_personajes y devolverlo para desasignar sus recursos:
	personaje=list_find(list_personajes,(void*)buscarPersonaje);
	list_remove_by_condition(list_personajes,(void*)buscarPersonaje);//elimina el personaje de la lista list_personajes

	void desasignar(char* id1){
		ITEM_NIVEL* itemAux;
		bool buscarRecurso(ITEM_NIVEL* item1){return (item1->id==*id1);}
		itemAux=list_find(list_items,(void*)buscarRecurso);
		itemAux->quantity++;
	}
	list_iterate(personaje->recursos,(void*)desasignar);
	BorrarPersonaje(list_items, id);
	personaje_destroyer(personaje);
}

void actualizaPosicion(int *contMovimiento, int *posX, int *posY) {

	switch (*contMovimiento) {
	case 1:
		(*posX)++; //DERECHA
		break;
	case 2:
		(*posY)--; //ARRIBA
		break;
	case 3:
		(*posX)--; //IZQUIERDA
		break;
	case 4:
		(*posY)++; //ABAJO
		break;
	}
}


//--------------------------------------Señal SIGINT
void cerrarForzado(int sig) {
	cerrarNivel("Cerrado Forzoso Nivel.");
}

void cerrarNivel(char* msjLog) {
	log_trace(logger, msjLog);
	nivel_gui_terminar();
	printf("%s\n", msjLog);
	exit(EXIT_FAILURE);
}
//--------------------------------------Señal SIGINT

static void personaje_destroyer(tPersonaje *personaje) {
	list_destroy_and_destroy_elements(personaje->recursos, free);
	free(personaje);
}
