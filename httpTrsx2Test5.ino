/*
 * Simulation Quantico Board
 * Arduino 1.6.1
 * EthernetDHCP
 * ]<äf.
 */
#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetDHCP.h>

#include "httpTrsx2.h"

//Arduino MEGA pinout
#define LED1 34 //PC3
#define LED2 35 //PC2
#define LED3 36 //PC1
#define LED4 37 //PC0
#define LED5 38 //PD7
#define LED6 39 //PG2
#define RELE1 29
#define RELE2 30
#define RELE3 31
#define RELE4 32
#define RELE5 33
#define DDRxLINKSTATUS	    DDRH
#define PORTWxLINKSTATUS	PORTH
#define PORTRxLINKSTATUS	PINH
#define PINxLINKSTATUS	6

/*Simulation Quantico Board*/
#define NUM_IC 2//8
#define LTC_NUM_CELLS 6//12

float vcellF[NUM_IC][LTC_NUM_CELLS];
uint8_t ltc_connected[NUM_IC];
uint8_t stack_union[NUM_IC];
uint8_t select_2_12V[NUM_IC];
float stack_temperatureF[NUM_IC] = { 25.6, 28.15 };
float stack_voltage_ifcF[NUM_IC] = { 48.56, 12.78 };
float stack_currentF[NUM_IC] = { 256.59, 869.35 };
uint8_t uv_flag[NUM_IC][LTC_NUM_CELLS] ={
								{ 1, 0, 1, 0, 1, 0 },
								{ 0, 1, 0, 1, 0, 1 } };
uint8_t ov_flag[NUM_IC][LTC_NUM_CELLS] = {
								{ 1, 0, 1, 0, 1, 0 },
								{ 0, 1, 0, 1, 0, 1 } };
uint8_t openwire_flag[NUM_IC][LTC_NUM_CELLS];
uint8_t stack_overCurrent[NUM_IC];
uint8_t stack_overTemperature[NUM_IC];
float internal_digital_powerF[NUM_IC];
float internal_analog_powerF[NUM_IC];
float internal_die_tempF[NUM_IC];
float internal_socF[NUM_IC];
float FACTCORR_2V = +0.0042;
float FACTCORR_12V_C1 = +0.115;
float FACTCORR_12V_C2 = +0.111;
float FACTCORR_12V_C3 = +0.063;
float FACTCORR_12V_C4 = +0.077;
float ov_2v = 3.500;
float uv_2v = 1.800;
float ov_12v_S3C = 13.500;
float uv_12v_S3C = 10.0;
float shuntVoltFullScale = 0.05;
float shuntRatedCurrent = 1000;
float ov_12v = 5.5;
float uv_12v = 2.5;
void spi_deselect_devices(void)//only for QUANTICO BOARD
{
	#define WIZNET_CS 10
	#define SDCARD_CS 4
	#define LTC6804_CS 5
	pinMode(WIZNET_CS, OUTPUT);
	pinMode(SDCARD_CS, OUTPUT);
	pinMode(LTC6804_CS, OUTPUT);
	digitalWrite(WIZNET_CS, HIGH);
	digitalWrite(SDCARD_CS, HIGH);
	digitalWrite(LTC6804_CS, HIGH);
}

void USART_Init(unsigned int ubrr);
void USART_Transmit(unsigned char data);
unsigned char USART_Receive(void);
void USART_Flush(void);

#define FOSC F_CPU
#define BAUD 38400//19200
#define MYUBRR ((16e6/(16.0*BAUD))-1)

void usart_print_string(const char *p);
void usart_print_PSTRstring(const char *p);
void usart_println_string(const char *p);
void usart_println_PSTRstring(const char *p);

void USART_Init(unsigned int ubrr)
{
	/*Set baud rate */
	UBRR0H = (unsigned char) (ubrr >> 8);
	UBRR0L = (unsigned char) ubrr;
	/*Enable receiver and transmitter */
	UCSR0B = (0 << RXEN0) | (1 << TXEN0);
	/* Set frame format: 8data, 1stop bit */
	UCSR0C = (0 << USBS0) | (3 << UCSZ00);
	//UCSRC = (1<<URSEL)|(0<<USBS)|(3<<UCSZ0);
}
void USART_Transmit(unsigned char data)
{
	/* Wait for empty transmit buffer */
	while (!(UCSR0A & (1 << UDRE0)))
		;
	/* Put data into buffer, sends the data */
	UDR0 = data;
}
unsigned char USART_Receive(void)
{
	/* Wait for data to be received */
	while (!(UCSR0A & (1 << RXC0)))
		;
	/* Get and return received data from buffer */
	return UDR0;
}
//void USART_Flush( void )
//{
//    unsigned char dummy;
//    while ( UCSR0A & (1<<RXC0) ) dummy = UDR0;
//}

//ISR(USART_RX_vect)
//{
//    uint8_t _udr_rx = UDR0;
//    //uint8_t u = UDR;
//}

void usart_print_string(const char *p)
{
	while (*p)
	{
		USART_Transmit(*p);
		p++;
	}
}

void usart_println_string(const char *p)
{
	usart_print_string(p);
	USART_Transmit('\n');
}

#if defined(__GNUC__) && defined(__AVR__)
    #include <avr/pgmspace.h>

    void usart_print_PSTRstring(const char *p)
    {
        char c;

        while (1)
        {
            c = pgm_read_byte_near(p++);
            if (c == '\0')
                break;
            else
                USART_Transmit(c);
            //p++;
        }
    }

    void usart_println_PSTRstring(const char *p)
    {
        usart_print_PSTRstring(p);
        USART_Transmit('\n');
    }
#endif

/*NIC setup*/
uint8_t IP[4] = { 192, 168, 1, 60 };	//fallback
uint8_t MAC[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

/*Connect to server - setting*/
//char server[] = "api.quanticoservices.net";
//uint8_t IPaddr_server[] ={ 192, 168, 1, 48 };
IPAddress IPaddr_server(192, 168, 1, 48);

void UART_setup(void)
{
#if defined(__AVR__) && defined(__GNUC__)
		USART_Init(MYUBRR);
    #else
#endif
}
void UART_printChar(char c)
{
#if defined(__AVR__) && defined(__GNUC__)
		USART_Transmit( c);
	#else
#endif
}
#if defined(__AVR__) && defined(__GNUC__)
 	 
    void UART_print(char *str, int8_t mode)
    {
        if (mode == 0)
        {
        	usart_print_string(str);
        }
        else
        {
			#ifdef FS_STRING
        		usart_print_PSTRstring(str);
			#else
        		usart_print_string(str);
			#endif
        }
    }
    
    void UART_println(char *str, int8_t mode)
    {
    	UART_print(str, mode);
    	UART_printChar('\n');
    }
    
	#define UART_printStrk(s) do{UART_print(s, 1);}while(0)
	#define UART_printStr(s) do{UART_print(s, 0);}while(0)
    //
	#define UART_printlnStrk(s) do{UART_println(s, 1); }while(0)
	#define UART_printlnStr(s) do{UART_println(s, 0); }while(0)
#else
void UART_printStr(char *str)
{
}
#endif


inline int8_t NIC_linkStatus(void)
{
	return !ReadPin(PORTRxLINKSTATUS, PINxLINKSTATUS);
}
int8_t NIC_getLinkStatus(void)
{
	static int8_t sm0;
	static int8_t link_prev;
	static unsigned long millis_last;
	static int8_t NIC_link = -1;
//
	int8_t cod_ret = 0;
	int8_t link_status;

	static unsigned long K;
	if (sm0 == 0)
	{
		link_status = NIC_linkStatus();
		if (NIC_link != link_status)
		{
			link_prev = link_status;
			millis_last = millis();
			sm0++;

			if (link_status == 0)
			{
				K = 30;
			}
			else
			{
				K = 1000;
			}
		}
	}
	else if (sm0 == 1)
	{
		if (millis() - millis_last >= K)
		{
			if (link_prev == NIC_linkStatus())
			{
				NIC_link = link_prev;
			}
			sm0 = 0x00;
		}
	}
	return NIC_link;
}

// Just a utility function to nicely format an IP address.
const char* ip_to_str(const uint8_t *ipAddr)
{
	static char buf[16];
	sprintf(buf, "%d.%d.%d.%d\0", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
	return buf;
}
//return
//0: IP no configured
//1: IP is configured via DHCP
int8_t eth_DHCP_leased(void)
{
//	static DhcpState DHCP_state = DhcpStateNone;
//	DhcpState state = EthernetDHCP.poll();
//	if (state == DhcpStateLeased)
//		return 1;
//	else
//		return 0;
	static DhcpState prevState = DhcpStateNone;

	DhcpState state = EthernetDHCP.poll();
	//
	static unsigned long prevTime = 0;
	if (prevState != state)
	{
		UART_printStrk(FS("\n"));

		switch (state)
		{
			case DhcpStateDiscovering:
				UART_printStrk(FS("Discovering servers."));
				break;
			case DhcpStateRequesting:
				UART_printStrk(FS("Requesting lease."));
				break;
			case DhcpStateRenewing:
				UART_printStrk(FS("Renewing lease."));
				break;
			case DhcpStateLeased:
			{
				UART_printStrk(FS("Obtained lease!"));

				const byte *ipAddr = EthernetDHCP.ipAddress();

				UART_printStrk(FS("My IP address is: "));
				UART_printlnStr((char*) ip_to_str(ipAddr));

				UART_printStrk(FS("\n"));
				break;
			}
		}
		prevState = state;
	}
	else if ((state != DhcpStateLeased) && (millis() - prevTime) > 300)
	{
		prevTime = millis();
		UART_printStrk(FS("."));
	}

	if (state == DhcpStateLeased)
		return 1;
	else
		return 0;
}

#define TRSX_NUMMAX 2
TRSX trsx[TRSX_NUMMAX];
EthernetClient client;	//1 instance

void setup(void)
{
	digitalWrite(LED1, LOW);
	pinMode(LED1, OUTPUT);
	digitalWrite(LED2, LOW);
	pinMode(LED2, OUTPUT);
	digitalWrite(LED3, LOW);
	pinMode(LED3, OUTPUT);
	digitalWrite(LED4, LOW);
	pinMode(LED4, OUTPUT);
	digitalWrite(LED5, LOW);
	pinMode(LED5, OUTPUT);
	digitalWrite(LED6, LOW);
	pinMode(LED6, OUTPUT);

	digitalWrite(RELE1, LOW);
	pinMode(RELE1, OUTPUT);
	digitalWrite(RELE2, LOW);
	pinMode(RELE2, OUTPUT);
	digitalWrite(RELE3, LOW);
	pinMode(RELE3, OUTPUT);
	digitalWrite(RELE4, LOW);
	pinMode(RELE4, OUTPUT);
	digitalWrite(RELE5, LOW);
	pinMode(RELE5, OUTPUT);

	ConfigInputPin(DDRxLINKSTATUS, PINxLINKSTATUS);
	PinTo1(PORTWxLINKSTATUS, PINxLINKSTATUS);

	delay(1500);
	spi_deselect_devices();	//only for QUANTICO BOARD
	UART_setup();

	#ifdef HTTPTRSX_DEBUG
	httpTrsx_UARTdebug_setPrintFx (UART_print);
	httpTrsx_UARTdebug_setPrintlnFx (UART_println);
	httpTrsx_UARTdebug_setPrintCharFx(UART_printChar);
	#endif

	/* Set trsx[0] for Tx */
	httpTrsx_setClient(&trsx[0], (Client*) &client);
	httpTrsx_setupServerByIP(&trsx[0], IPaddr_server, 80);
	httpTrsx_setURI(&trsx[0], "/jsondecodeTx.php");
	httpTrsx_setHost(&trsx[0], "192.168.1.48");
	httpTrsx_setHdrLine(&trsx[0], "api_key_write: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpZCI6IjE1MzU0MjczNTVfcGFibG8iLCJkZXZpY2VfaWQiOiI1YjdmMjc3ZmVmNGFkNjgxYjIwM2I0NDQiLCJlbWFpbCI6InBhYmxvZG9uYXlyZUBnbWFpbC5jb20iLCJpYXQiOjE1NjQwODgwMjR9.G8BWFQ1O_KH4hVfibYSlGd-UqQLdWZ1d_sxonbhqANc");
	httpTrsx_setExecMode(&trsx[0], EM_RUN_ONCE);
	#ifdef HTTPTRSX_DEBUG
	httpTrsx_UARTdebug_enabled(&trsx[0], TRUE);
	#endif

	/* Set trsx[1] for Rx */
	httpTrsx_setClient(&trsx[1], (Client*) &client);
	httpTrsx_setupServerByIP(&trsx[1], IPaddr_server, 80);
	httpTrsx_setURI(&trsx[1], "/jsondecodeRx.php");
	httpTrsx_setHost(&trsx[1], "192.168.1.48");
	httpTrsx_setHdrLine(&trsx[1],"api_key_read: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpZCI6IjE1MzU0MjczNTVfcGFibG8iLCJkZXZpY2VfaWQiOiI1YjdmMjc3ZmVmNGFkNjgxYjIwM2I0NDQiLCJlbWFpbCI6InBhYmxvZG9uYXlyZUBnbWFpbC5jb20iLCJpYXQiOjE1NjQwODgwMjR9.G8BWFQ1O_KH4hVfibYSlGd-UqQLdWZ1d_sxonbhqANc");
	httpTrsx_setExecMode(&trsx[1], EM_RUN_ONCE);
	#ifdef HTTPTRSX_DEBUG
	httpTrsx_UARTdebug_enabled(&trsx[1], TRUE);
	#endif
}

static uint16_t stack_voltage(char *buff)
{
	strcpy(buff, "\"d13\":");
	json_cNumericArr(stack_voltage_ifcF, CNUM_FLOAT, NUM_IC, (buff + strlen(buff)));
	return strlen(buff);
}
static uint16_t stack_current(char *buff)
{
	strcpy(buff, "\"d14\":");
	json_cNumericArr(stack_currentF, CNUM_FLOAT, NUM_IC, (buff + strlen(buff)));
	return strlen(buff);
}
static uint16_t stack_temperature(char *buff)
{
	strcpy(buff, "\"d15\":");
	json_cNumericArr(stack_temperatureF, CNUM_FLOAT, NUM_IC, (buff + strlen(buff)));
	return strlen(buff);
}
static uint16_t allstacks_uvFlag(char *buff)
{
	strcpy(buff, "\"d10\":");
	json_cNumericArrBi(uv_flag, CNUM_UINT8, NUM_IC, LTC_NUM_CELLS, (buff + strlen(buff)));
	return strlen(buff);
}
static uint16_t allstacks_ovFlag(char *buff)
{
	strcpy(buff, "\"d11\":");
	json_cNumericArrBi(ov_flag, CNUM_UINT8, NUM_IC, LTC_NUM_CELLS, (buff + strlen(buff)));
	return strlen(buff);
}

struct _ethSend
{
		unsigned stack_voltage :1;
		unsigned stack_current :1;
		unsigned stack_temperature :1;
		unsigned allstacks_uvFlag :1;
		unsigned allstacks_ovFlag :1;
		unsigned __a :3;
} ethSend;

void enableFlagS(void)
{
	ethSend.stack_voltage = 1;
	ethSend.stack_current = 1;
	ethSend.stack_temperature = 1;
	ethSend.allstacks_uvFlag = 1;
	ethSend.allstacks_ovFlag = 1;
}
#define NUMMAX 5
PTRFX_retUINT16_T_arg1_PCHAR ethSend_pfx[NUMMAX] = { stack_voltage, stack_current, stack_temperature, allstacks_uvFlag, allstacks_ovFlag, };

//
static struct _ethTrsxTx
{
	uint8_t idx;
	uint8_t bit;
	int8_t sm0;
} ethTrsxTx;
const struct _ethTrsxTx ethTrsxTx_Zeroes ={ 0 };
//
static inline void ethTrsxTx_job_reset(void)
{
	ethTrsxTx = ethTrsxTx_Zeroes;
}
/*
 * job = 1 =  Finish all outcomming message (all flags was verifed)
 * trsx = 1 = End 1 Transaction
 */
struct ETHTRSX_JOB_CODRET
{
	int8_t job;
	int8_t trsx;
};
struct ETHTRSX_JOB_CODRET ethTrsxTx_job(void)	//especific User
{
	struct ETHTRSX_JOB_CODRET cod_ret = {0,0};
	//
	char buff[20];
	int8_t i, n;
	static uint8_t *pf = (uint8_t*) &ethSend;
	//
	#define JSONCSTR_MAXSIZE (400)
	char jsonCstr[JSONCSTR_MAXSIZE];
	char *pjsonCstr;
	uint16_t nbytes;
	int8_t doTx;
	int8_t found0;
	int8_t httpTrsx_rpta;
	//
	jsonCstr[0] = '\0';
	if (ethTrsxTx.sm0 == 0)
	{
		//load-balance
		if (ethTrsxTx.idx < 3)
			n = 1;
		else
			n = 2;

		//build the message: json format
		pjsonCstr = &jsonCstr[0];
		nbytes = 0;
		found0 = 1;
		doTx = 0;
		//
		strcpy(pjsonCstr, "{");
		pjsonCstr++;
		for (i = 0; i < n; i++)
		{
			if (*pf & (1 << ethTrsxTx.bit))
			{
				BitTo0(*pf, ethTrsxTx.bit);
				ethTrsxTx.bit++;
				//
				if (doTx == 1)//there was at least 1 message founded
				{
					strcat(pjsonCstr, ",");
					pjsonCstr++;
				}
				//
				nbytes = ethSend_pfx[ethTrsxTx.idx](pjsonCstr);
				pjsonCstr += nbytes;
				//
				doTx = 1;//there is at least 1 message. Stay signed
			}
			if (++ethTrsxTx.idx >= NUMMAX)//all flags?
			{
				//
				ethTrsxTx.idx = 0x00;
				pf = (uint8_t*) &ethSend;
				ethTrsxTx.bit = 0;
				//
				cod_ret.job = 1;
				break;
			}
			else
			{
				if (ethTrsxTx.idx % 8 == 0)
				{
					pf++;
					ethTrsxTx.bit = 0;
				}
			}
		}

		if (doTx == 1)
		{
			strcat(pjsonCstr, "}");
			ethTrsxTx.sm0++;
		}
	}
	if (ethTrsxTx.sm0 == 1)
	{
		//after the frist outcoming json message, don't care address buffer
		httpTrsx_rpta = httpTrsx_job(&trsx[0], 0, jsonCstr, strlen(jsonCstr), (char*)NULL, 0);
		if ((httpTrsx_rpta == 1) || (httpTrsx_rpta == -1))
		{
			ethTrsxTx.sm0 = 0x00;
			httpTrsx_setExecMode(&trsx[0], EM_RUN_ONCE);
			cod_ret.trsx = 1;
			UART_printlnStrk(FS("TX End"));
			//delay(500);
		}
	}
	return cod_ret;
}
///////////////////////////////////////////////
static struct _ethTrsxRx
{
	//uint8_t idx;
	//uint8_t bit;
	int8_t sm0;
} ethTrsxRx;
const struct _ethTrsxRx ethTrsxRx_Zeroes ={ 0 };
//
static inline void ethTrsxRx_job_reset(void)
{
	ethTrsxRx = ethTrsxRx_Zeroes;
}

struct ETHTRSX_JOB_CODRET ethTrsxRx_job(void)
{
	struct ETHTRSX_JOB_CODRET cod_ret = {0,0};

	//Rx
	char stream[64];
	JSON json;
	int8_t httpTrsx_rpta;
	int8_t jsonDecode_rpta;
	int8_t ledStatus;
	static int8_t ledStatus_last;

	//if (ethTrsxRx.sm0 == 0)
	//{
		httpTrsx_rpta = httpTrsx_job(&trsx[1], 0, (char*)NULL, 0, stream, sizeof(stream));
		if (httpTrsx_rpta > 0)
		{
			do
			{
				json.name = (char*) NULL;
				jsonDecode_rpta = jsonDecode(stream, sizeof(stream), &json);
				if (json.name != NULL)
				{
					UART_printStrk(FS("\n"));
					if (strcmp(json.name, "ch0") == 0)
					{
						/*TODO*/
						 //double doub = strtod(json.strval, NULL);//etc etc
					}
					else if (strcmp(json.name, "rele") == 0)
					{
						double vrele[5];
						int numEl = cCstrToDecimalArr((char *)json.strval,  vrele, sizeof(vrele)/sizeof(vrele[0]));
						((int)vrele[0] == 1)? digitalWrite(RELE1,HIGH): digitalWrite(RELE1,LOW);
						((int)vrele[1] == 1)? digitalWrite(RELE2,HIGH): digitalWrite(RELE2,LOW);
						((int)vrele[2] == 1)? digitalWrite(RELE3,HIGH): digitalWrite(RELE3,LOW);
						((int)vrele[3] == 1)? digitalWrite(RELE4,HIGH): digitalWrite(RELE4,LOW);
						((int)vrele[4] == 1)? digitalWrite(RELE5,HIGH): digitalWrite(RELE5,LOW);
					}
					else if (strcmp(json.name, "led") == 0)
					{
						double vled[6];
						int numEl = cCstrToDecimalArr((char *)json.strval,  vled, sizeof(vled)/sizeof(vled[0]));
						((int)vled[0] == 1)? digitalWrite(LED1,HIGH): digitalWrite(LED1,LOW);
						((int)vled[1] == 1)? digitalWrite(LED2,HIGH): digitalWrite(LED2,LOW);
						((int)vled[2] == 1)? digitalWrite(LED3,HIGH): digitalWrite(LED3,LOW);
						((int)vled[3] == 1)? digitalWrite(LED4,HIGH): digitalWrite(LED4,LOW);
						((int)vled[4] == 1)? digitalWrite(LED5,HIGH): digitalWrite(LED5,LOW);
						((int)vled[5] == 1)? digitalWrite(LED6,HIGH): digitalWrite(LED6,LOW);
					}
				}
			} while (jsonDecode_rpta == 0);
		}
		if ((httpTrsx_rpta == 1) || (httpTrsx_rpta == -1))
		{
			//ethTrsxRx.sm0 = 0x00;
			httpTrsx_setExecMode(&trsx[1], EM_RUN_ONCE);
			cod_ret.trsx = 1;
			cod_ret.job = 1;
			//
			UART_printlnStrk(FS("FIN RX"));
			//delay(500);
		}
	//}
	return cod_ret;
}

//void eth_SPI_access(void)
//{
//    SPI.endTransaction();
//    SPI_deselectAllperipherals();
//    SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
//    //SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
//    //SPI.begin();
//}

void eth_job(void)
{
	//eth_SPI_access();
	static unsigned long millis_last;
	static int8_t sm0, sm1;
	struct ETHTRSX_JOB_CODRET cod_ret={0,0};


	if (sm0 == 0)
	{
		if (NIC_getLinkStatus() == 1)
		{
			UART_printlnStr("EthernetDHCP.begin(MAC, 1)");
			//
			EthernetDHCP.begin(MAC, 1);
			ethTrsxTx_job_reset();
			ethTrsxRx_job_reset();
			for (int i = 0; i < TRSX_NUMMAX; i++)
			{
				httpTrsx_job_reset(&trsx[i]);
			}
			//
			millis_last = millis();
			sm0++;
		}
	}
	else if (sm0 == 1)
	{
		if ((millis() - millis_last) > 2000)
		{
			sm0++;
		}
	}
	else
	{
		if (NIC_getLinkStatus() == 0)
		{
			client.flush();
			client.stop();
			sm0 = 0x00;
		}

		if (sm0 == 2)
		{
			if (eth_DHCP_leased() == 1)
			{
				//prepare Tx
				enableFlagS();
				httpTrsx_setExecMode(&trsx[0], EM_RUN_ONCE);
				//prepare Rx
				httpTrsx_setExecMode(&trsx[1], EM_RUN_ONCE);
				//
				sm1 = 0x00;
				sm0++;
			}
		}
		else if (sm0 == 3)
		{
//			if (NIC_getLinkStatus() == 0)
//			{
//				client.flush();
//				client.stop();
//				sm0 = 0x00;
//			}
//			else
//			{
				//
				if (eth_DHCP_leased() == 1)
				{
					if (sm1 == 0)
					{
						cod_ret = ethTrsxTx_job();//tx
						if (cod_ret.job == 1)//Write job finished?
						{
							//reload flags
							enableFlagS();
						}
						if (cod_ret.trsx == 1)//1 trsx finished?
						{
							sm1++;//conmutar a Rx
						}
					}
					else if (sm1 == 1)
					{
						cod_ret = ethTrsxRx_job();//tx
						if (cod_ret.job == 1)//Read job finished?
						{
							sm1 = 0x00;
						}
						if (cod_ret.trsx == 1)//1 trsx finished?
						{
							//sm1 = 0x00;//conmutar a Tx
						}

					}
				}
				//
//			}
		}
	}
}

void loop(void)
{
	eth_job();
}
