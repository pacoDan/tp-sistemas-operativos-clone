/*
 * Sistemas Operativos - Super Mario Proc RELOADED.
 * Grupo       : C o no ser.
 * Nombre      : personaje.h.
 * Descripcion : Este archivo contiene los prototipos de las
 * funciones usadas por el personaje.
 */

#ifndef PERSONAJE_H_
#define PERSONAJE_H_

#include <commons/collections/list.h>
#include <commons/config.h>
#include <commons/string.h>
#include <commons/log.h>

#include <ginyu/config.h>
#include <ginyu/list.h>
#include <ginyu/log.h>
#include <ginyu/sockets.h>
#include <ginyu/protocolo.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

typedef struct PersonajeGlobal{
	tSimbolo simbolo;
	int vidas;
	int vidasMaximas;
	unsigned short puertoOrquestador;
	char * nombre;
	char * ipOrquestador;
	int reintentos;
	t_list *lPersonajes;
} personajeGlobal_t;


typedef struct PersonajeIndividual{
	int socketPlataforma;
	tSimbolo recursoActual; //el numero del recurso actual basandonos en la lista de recursos por nivel
	int posX;
	int posY;
	int posRecursoX;
	int posRecursoY;
	bool bienTerminado;
	bool murioEnNivel;
	tDirMovimiento ultimoMovimiento;
	char *nomNivel;
	t_list *Objetivos;
	pthread_t *thread;
} personajeIndividual_t;



void enviarMensajeFinDeTurno(personajeIndividual_t *pPersonajePorNivel);



bool consultarReinicio();
void matarHilosExceptoYo(char *nivel);
void asesinarme();
void deboMorir();
void desconectarPersonajeDeTodoNivel();
void matarHilos();
void enviarDesconexionATodos();
void enviarDesconexion();

void liberarHilos();
void notificarFinPlanNiveles();

void enviarPaqueteFinPlan(personajeIndividual_t* personajePorNivel);

void cargarArchivoConfiguracion(char* archivoConfiguracion);

void obtenerIpYPuerto(char *dirADividir, char * ip,  char * puerto);

void *jugar(void *args);

void destruirSemaforos();

void desconectarPersonaje(personajeIndividual_t* personajePorNivel);

bool personajeEstaMuerto(bool murioPersonaje);

bool conseguiRecurso(personajeIndividual_t* personajePorNivel);

void moverAlPersonaje(personajeIndividual_t* personajePorNivel);

void solicitarRecurso(personajeIndividual_t* personajePorNivel, char *recurso);

tDirMovimiento calcularYEnviarMovimiento(personajeIndividual_t *personajePorNivel);

void crearTodosLosHilos();

void reiniciarHilo(personajeIndividual_t* personajePorNivel);

void reiniciarTodosLosHilos();

void recibirMensajeTurno(personajeIndividual_t *personajePorNivel);

void pedirPosicionRecurso(personajeIndividual_t* personajePorNivel, char* recurso);

void handshake_plataforma(personajeIndividual_t* personajePorNivel);

void cerrarConexion(int * socketPlataforma);

void calculaMovimiento(personajeIndividual_t *personajePorNivel);

bool tieneMovimientoVertical(personajeIndividual_t personajePorNivel);

bool tieneMovimientoHorizontal(personajeIndividual_t personajePorNivel);

void moverHorizontal(personajeIndividual_t *personajePorNivel);

void moverVertical(personajeIndividual_t *personajePorNivel);

void actualizaPosicion(tDirMovimiento* movimiento, personajeIndividual_t *personajePorNivel);

void seMuereSinSenal(personajeIndividual_t *personajePorNivel);

void sig_aumentar_vidas();

void restarVida();

void muertoPorSenial();

void _liberar_personaje_individual(personajeIndividual_t* personaje);


#endif /* PERSONAJE_H_ */
