/**
  ******************************************************************************
  * @file    dmm_link.h
  * @brief   El contrato entre los dos nucleos, en la SRAM4. Unico fichero que
  *          comparten el CM4 --que mide-- y el CM7 --que transmite--.
  *
  *  Descendiente directo de bm_mbox.h del ejercicio 1_ADC/02, que solo llevaba
  *  VREFINT_CAL. Aqui crece a cuatro zonas porque los dos nucleos trabajan de
  *  verdad, en vez de dormir uno de los dos.
  *
  *      0x000  contrato de arranque   CM7 escribe una vez, CM4 lee
  *      0x040  mando                  CM7 escribe, CM4 aplica y acusa
  *      0x080  telemetria             CM4 escribe con seqlock, CM7 lee
  *      0x100  fallos y contadores    CM4 escribe, CM7 publica
  *
  *  ---------------------------------------------------------------------------
  *  POR QUE LA SRAM4, otra vez
  *  ---------------------------------------------------------------------------
  *
  *  Las mismas tres razones del 1_ADC/02, que siguen siendo buenas:
  *
  *  1. Los dos nucleos la ven con el MISMO numero. Verificado leyendo por el
  *     puerto de depuracion de cada uno (AP 0 es el CM7, AP 3 el CM4).
  *  2. Ningun enlazador coloca nada en ella. El CM4 tiene su RAM en 0x10000000
  *     --alias del D2, recortada a 256K-- y el CM7 la suya en el D1.
  *
  *     **Ojo con el matiz, que se comprobo y no era lo que se suponia:** el
  *     script del CM7 SI declara la region
  *
  *         RAM_D3 (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
  *
  *     Lo que no hace es asignarle ninguna seccion, y eso es lo que deja el
  *     buzon a salvo. Verificado en el mapa: la unica aparicion de una
  *     direccion 0x38xxxxxx en Dmm_Eth_H755_CM7.map es la cabecera de la
  *     propia region.
  *
  *     La consecuencia practica es que esto hay que volver a mirarlo si alguien
  *     anade un "> RAM_D3" al linker: el solape seria silencioso, igual que lo
  *     fue el del heap de lwIP dentro del Rx_Pool. La comprobacion es de una
  *     linea:
  *
  *         grep -n "RAM_D3" CM7/STM32H755ZITX_FLASH.ld
  *
  *     tiene que devolver SOLO la linea de MEMORY, nunca una de SECTIONS.
  *  3. **No tiene bit de habilitacion de reloj.** No aparece en AHB4ENR ni en
  *     ningun otro ENR: esta siempre disponible. Un buzon que no hay que
  *     encender no se puede olvidar de encender desde uno de los dos lados
  *     --y en este chip los ENR son por nucleo, asi que habria dos sitios
  *     donde olvidarlo--.
  *
  *  Y en este proyecto se suma una cuarta: la SRAM3 esta ocupada por los
  *  descriptores del Ethernet, el Rx_Pool y el heap de lwIP, con dos colisiones
  *  que el enlazador no detecta y que ya se pagaron (INTEGRACION.md 3.2). No es
  *  sitio para meter nada mas.
  *
  *  ---------------------------------------------------------------------------
  *  LA TRAMPA NUEVA: ESTA REGION TIENE QUE SER NO CACHEABLE PARA EL CM7
  *  ---------------------------------------------------------------------------
  *
  *  En el 1_ADC/02 el CM7 escribia el buzon sin caches --no habia HAL, no las
  *  encendia--. Aqui el CM7 lleva HAL, lwIP y D-Cache, y 0x38000000 cae en la
  *  region "SRAM" del mapa de memoria por defecto del ARMv7-M: normal,
  *  cacheable, write-back.
  *
  *  Sin una region de MPU que lo impida, el CM7 lee la primera medida y se
  *  queda con ella PARA SIEMPRE. Sin un error, sin un fallo, sin una pista: la
  *  telemetria sale por la red con un numero perfectamente plausible que no
  *  cambia nunca.
  *
  *  El arreglo va en mpu_config.c, como una tercera region junto a las dos que
  *  ya cubren la SRAM3:
  *
  *      BaseAddress  = DMM_LINK_BASE      (alineada al tamano, que es lo que
  *      Size         = MPU_REGION_SIZE_64KB   pide el MPU del ARMv7-M)
  *      IsCacheable  = MPU_ACCESS_NOT_CACHEABLE
  *
  *  y, como todo lo de la MPU en este repositorio, **antes de
  *  SCB_EnableDCache()**. Ver INTEGRACION.md 3.5.
  *
  *  La alternativa --SCB_InvalidateDCache_by_Addr() en cada lectura-- exige
  *  alineacion a 32 bytes y acertar en todos los sitios. Es la misma clase de
  *  fallo intermitente que costo el heap de lwIP dentro del Rx_Pool.
  *
  *  El CM4 no necesita nada: no tiene cache de datos. Pero sus escrituras
  *  pasan por un buffer de escritura, asi que __DSB() antes de publicar.
  ******************************************************************************
  */

#ifndef DMM_LINK_H
#define DMM_LINK_H

#include <stddef.h>     /* offsetof. Sin HAL nadie lo arrastra: hay que pedirlo */

#include "stm32h7xx.h"

/** Base de la SRAM4 (dominio D3, 64 KB). Misma direccion para los dos nucleos. */
#define DMM_LINK_BASE     0x38000000UL

/** Tamano de la region de MPU que hay que declarar no cacheable en el CM7. */
#define DMM_LINK_SIZE     0x10000UL     /* 64 KB, la SRAM4 entera */

/** Marca de "esto lo ha escrito el CM7", no basura de arranque. */
#define DMM_LINK_MAGIC    0x444D4D31UL  /* "DMM1" */

/** Version del contrato. Se sube cuando cambia la disposicion de la struct. */
#define DMM_LINK_VERSION  1u

/* Direccion del valor de fabrica de VREFINT.
   Detras de un #if a proposito, igual que en bm_mbox.h: asi el CM4 no puede
   leerla ni por descuido, porque no compila. La ventana de memoria de sistema
   no esta mapeada desde el CM4 --el puente entre dominios expone los bancos de
   flash, la AXI SRAM y los perifericos de D1, pero no esa ventana-- y leerla
   desde alli es un BusFault preciso que escala a HardFault.

   Un comentario que diga "no leas esto desde el CM4" es un comentario que
   alguien se salta: ya paso una vez en el 1_ADC/02, con una lectura suelta en
   la consola que sobrevivio al arreglo del modulo del ADC y volvio a llevarse
   la placa al mismo HardFault. Que lo impida el compilador. */
#if defined(CORE_CM7)
#define DMM_VREFINT_CAL_ADDR   ((const uint16_t *)0x1FF1E860UL)
#endif

/* ---------------------------------------------------------------------------
   Estructura compartida

   Todos los campos son uint32_t o int32_t A PROPOSITO. Los dos nucleos los
   compilan con toolchains distintas del mismo GCC y con opciones distintas; con
   tipos de 32 bits y sin huecos, la disposicion no puede desplazarse entre un
   lado y el otro. No hay enteros de 16 bits, ni bitfields, ni bool.

   Los microvoltios y los mili-LSB son la misma convencion del resto del
   repositorio: aritmetica entera y la coma se pone al imprimir. newlib-nano
   excluye "%f" salvo que se enlace con -u _printf_float, y el sintoma de
   olvidarlo es que no se imprime nada.
   --------------------------------------------------------------------------- */

typedef struct
{
  /* --- 0x000 - contrato de arranque ---------------------------------------
     Lo escribe el CM7 una sola vez, antes de soltar al CM4, y no vuelve a
     tocarlo. El CM4 gira sobre `magic` y no arranca hasta verlo.

     `magic` se escribe EL ULTIMO, con un __DSB() delante y otro detras, para
     que una marca valida implique siempre un contenido valido. Es el mismo
     patron de bm_mbox.h, y aqui pesa mas porque el CM4 depende de estos
     valores para configurar el ADC: arrancar con adc_ker_ck_hz a cero daria un
     BOOST y un PRESC equivocados sin dar ningun error. */
  volatile uint32_t magic;           /**< DMM_LINK_MAGIC cuando el resto vale  */
  volatile uint32_t version;         /**< DMM_LINK_VERSION                     */
  /* VREFINT_CAL, de 0x1FF1E860. En 16 bits en este chip, no en 12: por eso la
     macro de ST __LL_ADC_CALC_VREFANALOG_VOLTAGE() devuelve un VDDA 16x mayor
     y hay que despejarlo a mano --cal * 3300000 / raw16--.

     EL CM4 YA NO LO CONSUME. Al hacer el CM7 la conversion a voltios, la
     tercera trampa del 1_ADC/02 --que esta direccion no es legible desde el
     CM4-- desaparece del camino de datos: el CM4 publica LSB y no necesita
     saber cuanto vale VDDA.

     Se conserva en el contrato por dos razones, ninguna de ellas "por si
     acaso": el CM7 lo necesita para convertir y este es el sitio donde vive la
     configuracion de la corrida, y sobre todo porque asi TODO el estado de
     arranque --calibracion, relojes y marca-- se lee de un tiron con un solo
     STM32_Programmer_CLI -r32 sobre 0x38000000, que es como se depura en este
     repositorio. Repartirlo en dos sitios costaria esa lectura.

     El guardia #if defined(CORE_CM7) sobre la direccion se queda igualmente:
     protege contra que alguien devuelva la conversion al CM4 sin darse cuenta
     de lo que eso implica. */
  volatile uint32_t vrefint_cal;
  volatile uint32_t sysclk_hz;       /**< para INFO y para el SysTick del CM4  */
  volatile uint32_t hclk_hz;
  volatile uint32_t adc_ker_ck_hz;   /**< per_ck tras el mux, que escribe el
                                          CM7 en RCC->D3CCIPR.ADCSEL. El CM4
                                          elige con esto el PRESC y el BOOST,
                                          que si son suyos                     */
  volatile uint32_t rsv0[10];        /**< hasta 0x040                          */

  /* --- 0x040 - mando -------------------------------------------------------
     Lo escribe el CM7 cuando llega un comando por UDP; lo aplica el CM4 en su
     lazo. No es una llamada: es una peticion con acuse.

     El protocolo es de un solo escritor por campo, que es lo que permite no
     usar semaforo. El CM7 escribe los parametros PRIMERO y `cmd_seq` DESPUES,
     con __DSB() entre medias; el CM4 compara cmd_seq con cmd_ack, aplica, y
     copia cmd_seq en cmd_ack. Mientras no coincidan, el CM7 sabe que el cambio
     esta en vuelo y puede decirlo en la respuesta UDP en vez de mentir.

     Que el acuse exista importa: cambiar el OSR de 1 a 1024 alarga la medida
     en curso, y sin acuse el CM7 no tiene forma de saber si la siguiente
     muestra que lee corresponde a la configuracion vieja o a la nueva. */
  volatile uint32_t cmd_seq;         /**< lo incrementa el CM7 al pedir cambio */
  volatile uint32_t cmd_ack;         /**< el CM4 copia cmd_seq al aplicarlo    */
  volatile uint32_t cmd_osr;         /**< potencia de dos, 1..1024             */
  volatile uint32_t cmd_nsamples;    /**< muestras por medida, 1..1024         */
  volatile uint32_t cmd_period_ms;   /**< 0 = tan rapido como se pueda         */
  volatile uint32_t cmd_run;         /**< 0 = parado, 1 = midiendo             */
  volatile uint32_t rsv1[10];        /**< hasta 0x080                          */

  /* --- 0x080 - telemetria --------------------------------------------------
     Lo escribe el CM4 con seqlock; lo lee el CM7 en su lazo. Una sola ranura,
     y esa es una decision de diseno, no una simplificacion:

     El CM7 PUEDE PERDERSE UNA MEDIDA sin consecuencias --esto es un
     multimetro, no un osciloscopio-- y el CM4 NUNCA ESPERA al CM7. Con esas
     dos reglas, una ranura basta y no hace falta anillo ni control de flujo.
     La direccion contraria --que el CM4 se bloquee si el CM7 va lento-- seria
     mucho peor: meteria jitter en el momento de muestreo, que es justo lo que
     este proyecto quiere medir con precision.

     Si algun dia hace falta el stream de muestras crudas, esto se convierte en
     un anillo y aparecen contadores de escritura y lectura. Es otro ejercicio.

     ESTA ZONA VA EN LSB, NO EN MICROVOLTIOS, y es deliberado.

     El CM4 publica agregados en el dominio del convertidor --media, minimo,
     maximo y sigma en LSB-- y la conversion a voltios la hace el CM7. Tres
     razones, en orden de peso:

     1. El CM7 tiene FPU de doble precision, va al doble de reloj y es
        superescalar; el CM4 va a 200 MHz con FPU de precision simple. Dar la
        aritmetica al nucleo debil mientras el fuerte hace de copiador de bytes
        es el reparto al reves. Hoy da igual --promediar enteros es trivial--
        pero en cuanto el procesado crezca (RMS de alterna, armonicos, una FFT)
        el reparto se invierte y hay que mover codigo entre nucleos.
     2. La validacion de este proyecto se hace en LSB. Las cifras publicadas
        del 1_ADC/02 --sigma de VREFINT a OSR 1 y a OSR 256-- estan en LSB, asi
        que publicar en LSB hace la comparacion directa y sin redondeos de por
        medio.
     3. El CM4 deja de necesitar VDDA, y por tanto deja de necesitar
        VREFINT_CAL. La tercera trampa del 1_ADC/02 --que esa direccion no es
        legible desde el CM4-- se vuelve irrelevante en el camino de datos.

     DOS CANALES EN LA MISMA RANURA. Una lectura completa son las dos cosas:
     la senal en A0 y VREFINT medido con la MISMA configuracion. El CM7
     despeja VDDA de VREFINT y con el convierte A0, que es como se obtiene la
     referencia de verdad en este chip (los canales internos son del ADC3 y de
     nadie mas: RM0399 tabla 211).

     Que VREFINT viaje en CADA lectura, y no solo cuando alguien pide VDDA, es
     lo que permite medir el efecto del CM7 sobre el rail mientras corre lwIP
     --que es la medida que justifica este ejercicio-- sin comandos especiales.
     Cuesta poco: ADC1 y ADC3 son perifericos independientes y el CM4 los
     arranca a la vez. */
  volatile uint32_t seq;             /**< impar = escritura en curso           */
  volatile uint32_t sample_id;       /**< monotono, para detectar perdidas     */
  volatile uint32_t t_ms;            /**< SysTick del CM4. La marca de tiempo
                                          la pone quien mide, no quien
                                          transmite: son dos bases de tiempo
                                          distintas y derivan                  */
  volatile uint32_t n;               /**< muestras promediadas                 */
  volatile uint32_t osr;             /**< sobremuestreo efectivo               */
  volatile uint32_t meas_ms;         /**< duracion de la medida                */

  /* Canal de senal: ADC1 INP15 = PA3 = A0. Todo en LSB de 16 bits. */
  volatile uint32_t sig_mean_q8;     /**< media con 8 bits de fraccion         */
  volatile uint32_t sig_min;
  volatile uint32_t sig_max;
  volatile uint32_t sig_sigma_milli; /**< desviacion tipica en mili-LSB        */

  /* Canal de referencia: ADC3 INP19 = VREFINT. Misma configuracion. */
  volatile uint32_t ref_mean_q8;     /**< de aqui despeja el CM7 el VDDA real  */
  volatile uint32_t ref_min;
  volatile uint32_t ref_max;
  volatile uint32_t ref_sigma_milli; /**< la cifra comparable con 1_ADC/02     */

  volatile uint32_t rsv3[18];        /**< hasta 0x100                          */

  /* --- 0x100 - fallos y contadores -----------------------------------------
     El CM4 no tiene UART en este proyecto --el USART3 es del CM7-- asi que
     este bloque es su unica voz. bm_fault.c escribe aqui en vez de imprimir, y
     el CM7 lo saca por el serie y por UDP.

     Un nucleo mudo que cuenta su propio HardFault por la red es la mejor
     prueba que tiene este buzon: funciona cuando el escritor ya esta muerto.

     El manejador de fallos NO PUEDE BLOQUEAR esperando a que el CM7 lo lea.
     Escribe, pone la bandera y se queda en su bucle. Es la version de aquel
     tope en la espera de TXE de BM_Putc: perder el aviso es mejor que colgarse
     dando el aviso, porque un cuelgue mudo es justo el sintoma que este
     mecanismo viene a eliminar. */
  volatile uint32_t fault_flag;      /**< != 0 cuando el resto del bloque vale */
  volatile uint32_t fault_cfsr;
  volatile uint32_t fault_hfsr;
  volatile uint32_t fault_bfar;      /**< direccion que provoco el fallo       */
  volatile uint32_t fault_pc;        /**< instruccion que fallo (ver el .map)  */
  volatile uint32_t fault_lr;        /**< quien la llamo                       */
  volatile uint32_t meas_count;      /**< medidas completadas por el CM4       */
  volatile uint32_t err_count;       /**< medidas abortadas                    */

  /* En que punto esta el CM4. Es lo primero que se mira cuando no llega
     telemetria, y responde de un vistazo a la pregunta que en el 1_ADC/02
     costaba una sonda: "no contesta" puede ser que no arrancara, que no
     encontrara al CM7, que el ADC no calibrara o que se fuera a un fallo, y
     los cuatro sintomas son identicos desde fuera.

     El CM4 NO ESCRIBE NADA AQUI ANTES DE VER `magic`. El CM7 limpia esta zona
     al arrancar --para que una corrida vieja no se lea como actual-- y solo
     despues publica la marca; si el CM4 escribiera antes, el CM7 le borraria
     el estado. Que el estado se quede en DMM_M4_BOOT significa exactamente
     eso: el CM4 nunca vio la marca. */
  volatile uint32_t m4_state;        /**< uno de los DMM_M4_* de abajo         */
  volatile int32_t  m4_init_rc;      /**< codigo del paso que fallo; 0 si no   */
} DMM_Link_t;

/* Estados del CM4. Los escribe el, los lee el CM7 y salen por INFO y por el
   puerto serie. */
#define DMM_M4_BOOT       0u   /**< no ha llegado a ver `magic`                */
#define DMM_M4_RUNNING    1u   /**< midiendo; es el unico estado bueno         */
#define DMM_M4_RCC_FAIL   2u   /**< el buzon traia relojes imposibles          */
#define DMM_M4_ADC_FAIL   3u   /**< BM_ADC_Init fallo; el paso, en m4_init_rc  */
#define DMM_M4_FAULT      4u   /**< HardFault; el volcado, en fault_*          */

/** Marca de "el volcado de fallo de abajo es de esta corrida". */
#define DMM_FAULT_MAGIC   0x46574C54UL   /* "FWLT" */

/**
  * "No cambies este parametro" en cmd_period_ms.
  *
  * Los demas campos de la orden usan el cero para eso --un OSR de cero o un
  * numero de muestras de cero no significan nada-- pero **en el periodo el cero
  * es un valor legitimo**: quiere decir "tan rapido como puedas". Hace falta un
  * centinela distinto, y tiene que ser uno que no se pueda pedir por accidente.
  *
  * Esto no es hipotetico: la primera version mandaba 0xFFFFFFFF sin declararlo
  * y el CM4 lo copiaba tal cual en su periodo, asi que un simple "OSR 256"
  * dejaba la siguiente medida a cuarenta y nueve dias vista. El comando
  * contestaba OK y la telemetria se paraba, que es la peor combinacion posible.
  */
#define DMM_CMD_KEEP      0xFFFFFFFFUL

#define DMM_LINK   ((DMM_Link_t *)DMM_LINK_BASE)

/* Las direcciones de las cuatro zonas, comprobadas en compilacion.

   No es ceremonia: este fichero lo compilan DOS toolchains distintas, y el
   relleno de las zonas se ajusta a mano. Anadir un campo sin recortar el
   relleno desplaza todo lo que va detras, y el sintoma en la placa seria que
   el CM7 lee la telemetria desde el sitio equivocado --numeros plausibles,
   ningun error--. Justo la clase de fallo que este repositorio ya ha pagado
   dos veces con solapes que el enlazador no detecta.

   Si uno de estos salta, ajusta el rsvN de la zona anterior; no muevas las
   direcciones. Estan documentadas en el README y en el volcado de INFO. */
_Static_assert(offsetof(DMM_Link_t, magic)      == 0x000, "zona de arranque movida");
_Static_assert(offsetof(DMM_Link_t, cmd_seq)    == 0x040, "zona de mando movida");
_Static_assert(offsetof(DMM_Link_t, seq)        == 0x080, "zona de telemetria movida");
_Static_assert(offsetof(DMM_Link_t, fault_flag) == 0x100, "zona de fallos movida");
_Static_assert(sizeof(DMM_Link_t) <= DMM_LINK_SIZE,       "el buzon no cabe en la SRAM4");

/* ---------------------------------------------------------------------------
   El seqlock, escrito una sola vez y compartido por los dos nucleos

   Va aqui y no en bm_link.c / dmm_link.c a proposito: es el protocolo, no una
   implementacion. Con una copia en cada lado seria cuestion de tiempo que una
   de las dos perdiera un __DSB() en algun refactor, y el sintoma --un campo
   descolocado de vez en cuando-- no se parece a un problema de barreras.

   Un solo escritor (el CM4) y un solo lector (el CM7). Eso es lo que permite
   que no haya semaforo ni HSEM en el camino de datos: el HSEM se queda como
   bandera de arranque, que es para lo que ya estaba en el ejercicio 01.
   --------------------------------------------------------------------------- */

#if defined(CORE_CM4)
/**
  * @brief  Abre la ventana de escritura. Deja `seq` impar.
  */
__STATIC_INLINE void DMM_Link_BeginWrite(void)
{
  DMM_LINK->seq++;
  __DSB();
}

/**
  * @brief  Cierra la ventana de escritura. Deja `seq` par.
  */
__STATIC_INLINE void DMM_Link_EndWrite(void)
{
  __DSB();
  DMM_LINK->seq++;
  __DSB();
}
#endif /* CORE_CM4 */

#if defined(CORE_CM7)
/**
  * @brief  Copia la telemetria de forma consistente.
  * @param  out    destino, una copia local no volatil
  * @param  tries  maximo de reintentos antes de rendirse
  * @retval numero de reintentos consumidos, o un valor negativo si se agotaron
  *
  *  Devuelve los reintentos en vez de solo "bien/mal" porque ese numero es un
  *  dato de validacion: si crece, el CM4 esta publicando mas rapido de lo que
  *  el lazo de lwIP consigue copiar, y hay que bajar el ritmo o agregar varias
  *  medidas por datagrama. Un contador que solo dice "fallo" no avisa antes de
  *  que falle.
  *
  *  No bloquea nunca: el lazo del CM7 tiene que atender tambien DHCP y ARP.
  */
__STATIC_INLINE int DMM_Link_Read(DMM_Link_t *out, uint32_t tries)
{
  uint32_t i;

  for (i = 0u; i < tries; i++)
  {
    uint32_t s0 = DMM_LINK->seq;

    if ((s0 & 1u) != 0u)
    {
      continue;                 /* escritura en curso */
    }

    __DMB();

    out->sample_id       = DMM_LINK->sample_id;
    out->t_ms            = DMM_LINK->t_ms;
    out->n               = DMM_LINK->n;
    out->osr             = DMM_LINK->osr;
    out->meas_ms         = DMM_LINK->meas_ms;
    out->sig_mean_q8     = DMM_LINK->sig_mean_q8;
    out->sig_min         = DMM_LINK->sig_min;
    out->sig_max         = DMM_LINK->sig_max;
    out->sig_sigma_milli = DMM_LINK->sig_sigma_milli;
    out->ref_mean_q8     = DMM_LINK->ref_mean_q8;
    out->ref_min         = DMM_LINK->ref_min;
    out->ref_max         = DMM_LINK->ref_max;
    out->ref_sigma_milli = DMM_LINK->ref_sigma_milli;

    __DMB();

    if (DMM_LINK->seq == s0)
    {
      return (int)i;            /* la ventana no se movio: la copia vale */
    }
  }

  return -1;
}
#endif /* CORE_CM7 */

#endif /* DMM_LINK_H */
