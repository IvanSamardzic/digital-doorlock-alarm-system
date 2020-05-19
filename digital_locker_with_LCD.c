//included libraries
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <util/delay.h>

//preprocessor directives, which define CPU frequnecy and PORT division
//PORTA will be responsible for LCD data control
//PORTB is connected to 4x4 keypad matrix and responsible for scanning that matrix
//PORTC is declared as output PORT to control relay and buzzer
//PORTD will be responsible for sending commands to LCD

#define F_CPU 8000000UL
#define LCD_DATA PORTA
#define DATA_DDR DDRA
#define MATRIX_DATA PORTB
#define MATRIX_DDR DDRB
#define OUT_PORT PORTC
#define OUT_DDR DDRC
#define LCD_CONTROL PORTD
#define CONTROL_DDR DDRD

//preprocessor directive which defines UART speed 
#define BAUD_PRESCALE (((F_CPU / (UART_BAUDRATE * 16UL))) - 1)

//pins PD4, PD5 and PD6 are responsible for shifting LCD screen between data
//and control manipulation and for enabling communication 
#define RS 4
#define RW 5
#define EN 6

//PC5, PC6 and PC7 pins are connected to LEDs which indicates system state
//blocked state system LED -> PC5
//wait state system LED -> PD6
//ready state system LED -> PD7
#define BLOCKED 5
#define WAIT 6
#define READY 7

//forward method declarations
void PIN_init(void);
void LCD_init(void);
void LCD_send_command(unsigned char cmnd);
void LCD_send_data(unsigned char data);
void LCD_goto(unsigned char y, unsigned char x);
void LCD_print(char* str);
void LCD_clear(void);
void LCD_blink(void);
void UART_init(long UART_BAUDRATE);
void show_digit(char digit);
void UART_send_string(char string[50]);
void UART_send_char(unsigned char a);
void get_key(void);
void run_key_function(void);
void verify_password(void);
void send_sms(void);
void GSM_init(void);
void EEPROM_write(int addr, char data);
void EEPROM_read(int addr);
void block_time(void);

//system global variables
char temp, column, key = 0, number[4] = {10,10,10,10}, index, password[4],temp1,digit;
char show = 1, open = 0, match = 0, temp2, miss_match, temp4, block = 0;
char contact_number[10] = {'0','9','9','8','7','4','2','9','2','5'};
unsigned int wait;

void PIN_init(){
	
	//PIN_init() declares all PORTs as out/in and set the initial value into PORTs
	//PORTA -> LCD DATA PORT which send 8bit frame data or command to LCD screen
	//PORTB -> MATRIX PORT which controls 4x4 keypad rows and columns
	//PORTC -> OUTPUT PORT which controls buzzer and relay state
	//PORTD -> LCD_CONTROL PORT which controls communication with LCD (EN, RS and RW)
	
	DATA_DDR = 0xFF; //declare LCD data PORT as output
	LCD_DATA = 0x00; //initialise PORTA as hex 0x00
	MATRIX_DDR = 0x0F; //initially DDRB, MATRIX_DDR is declared as hex 0x0F,
	//matrix columns are connected to PORT pins PB0 - PB3, rows to PB4 - PB7
	//columns are initially at HIGH state, rows at LOW state
	MATRIX_DATA = 0x00;//PORTB initial value 0x00
	OUT_DDR = 0xFF; //PORTC declared as output
	OUT_PORT = 0x00; //initial value 0x00
	CONTROL_DDR = 0xFF; //declare LCD command PORT as output
	PORTD = 0x00; //initial PORTD value 0x00
}

void LCD_init(){
	_delay_us(10);
	LCD_send_command(0x38); //LCD initialization with 2 lines and 5*7 matrix
	LCD_send_command(0x0E); //display on, cursor blinking
	_delay_us(10); //wait for 10 microseconds
	LCD_send_command(0x01); //clear LCD screen
	_delay_us(10); //wait for 10 microseconds
}

void LCD_send_command(unsigned char cmnd){
	LCD_DATA = cmnd;
	LCD_CONTROL |= (0 << RW) | (0 << RS) | (1 << EN);
	//RS pin is cleared because cmnd will be written in command register
	_delay_us(10); //wait for 10 microseconds
	LCD_CONTROL |= (0 << RW) | (0 << RS) | (0 << EN);
	//disabling command sending
	_delay_us(100); //wait for 100 microseconds
}

void LCD_send_data(unsigned char data){
	LCD_DATA = data;
	LCD_CONTROL |= (0 << RW) | (1 << RS) | (1 << EN);
	//RS pin is set because data will be written in LCD data register
	_delay_us(10); //wait for 10 microseconds
	LCD_CONTROL |= (0 << RW) | (1 << RS) | (0 << EN);
	//disabling command sending
	_delay_us(100); //wait for 100 microseconds
}

void LCD_goto(unsigned char y, unsigned char x){
	unsigned char addr[] = {0x80, 0xC0 0x94, 0xD4};
	LCD_send_command(addr[y - 1 + x - 1]); //force cursor to the (x,y) position
	//x represent row (0 or 1), y represent character (0 to 15)
	_delay_ms(10); //wait for 10 miliseconds
}

void LCD_print(char* str){
	while(str > 0){ //wait while string pointer goes to the string end
		LCD_send_data(*str++); //write a char and increment a pointer
	}
}

void LCD_clear(){
	LCD_send_command(0x01); //force LCD to clear all text from the screen
	_delay_ms(100); //wait for 100 miliseconds
}

void LCD_blink(){
	LCD_send_command(0x08); //display and cursor off
	_delay_ms(250); //wait for 10 miliseconds
	LCD_send_command(0x0E); //display on, cursor blinking
	_delay_ms(250); //wait for 10 miliseconds
}

void UART_init(long UART_BAUDRATE){
	UCSRB |= (1 << RXEN) | (1 << TXEN) | (1 << RXCIE);
	//enabling transreceiving and enabling interrupt on the RXC flag in UCSRA
	UCSRC |= (1 << URSEL) | (1 << UCSZ0) | (1 << UCSZ1); //enabling URSEL enablae 
	//frame bit changing, UCSZ0 and UCSZ1 set declare 8bit frame UART communication
	UBBRL = BAUD_PRESCALE; //setting lower UART baud rate reg
	UBBRH = (BAUD_PRESCALE >> 8); //setting upper UART baud rate reg
}

void get_key(void){
	
	//get_key() function  is used to read the pressed key and this information is 
	//passed to the run_key_function(), refreshes the columns and reads the rows
	//for detecting the pressed key, whenever a key is pressed this default value
	//is replaced by the currently pressed key
	
	for(column = 1, temp = 1; column <= 4; temp *= 2, column++){
		//column variable is setting offset picking 1st, 2nd, 3rd or 4th tactile sw
		//column goes to 4 because there are four tactile switches
		//temp variable is controlling active column
		//temp is multiplies each time by 2 
		//temp -> 0x01 -> column1
		//temp -> 0x02 -> column2
		//temp -> 0x04 -> column3
		//temp -> 0x08 -> column4
		
		MATRIX_DATA &= 0xF0; //disable all columns without disturbing remaining pins
		MATRIX_DATA |= temp; //enable particular column leaving remaining PORTB pins
		switch(PINB & 0xF0){ //read rows data and identify the key
			case 0x10:{ //if row 1 is activated (1, 2, 3, clear)
				key = column; 
			}	break; //exit from the switch
			case 0x20:{ //if row 2 is activated (4, 5, 6, change)
				key = 4 + column; 
			} 	break; //exit from the switch
			case 0x40:{ //if row3 is activated (7, 8, 9, set)
				key = 8 + column;
			}	break; //exit from the switch
			case 0x80:{ //if row4 is activated (reset, 0, show/hide, open/close)
				key = 12 + column;
			}	break: //exit from the switch
		}
		if(key > 0){
			run_key_function();
			do{
				display();
			}
			while((PINB & 0xF0) != 0); //wait for keyrelease, any switch is pressed
			key = 0; //setting key value out of range
		}
	}
}

void run_key_function(void){
	
	//switch statement with 16 cases, function is assigned with one case for each key
	//as there are 16 keys in the keypad, key value ranges from 1 to 16 and by default
	//the key value is set out of this range during initialization of the key variable
	
	switch(key){
		case 1:{ // if key1 is pressed
			number[index] = 1; //index position number in number array is 1
			index++; //increment index variable
		}	break; //exit from the switch
		
		case 2:{ //if key 2 is pressed
			number[index] = 2;
			index++; //increment index variable
		}	break; //exit from the switch
		
		case 3:{ ////if key 3 is pressed
			number[index] = 3;
			index++; //increment index variable
		}	break; //exit from the switch
		
		case 4:{ //if key clear is pressed
			index--; //decrement index variable
			number[index] = 10;
			match = 0;
		}	break; //exit from the switch
		
		case 5:{ //if key 4 is pressed
			number[index] = 4;
			index++; //increment index variable
		}	break; //exit from the switch
		
		case 6:{ //if key 5 is pressed
			number[index] = 5;
			index++; //increment index variable
		}	break; //exit from the switch
		
		case 7:{ //if key 6 is pressed
			number[index] = 6;
			index++; //increment index variable
		}	break; //exit from the switch
		
		case 8:{ //if key change is pressed
			if(match == 1){ //if match variable is set
				for(temp2 = 0; temp2 <= 3; temp2++){
					//clear all password digits
					number[temp2] = 10;
				}
			}
		}	break; //exit from the switch
		
		case 9:{ //if key 7 is pressed
			number[index] = 7;
			index++; //increment index variable
		}	break; //exit from the switch
		
		case 10:{ //if key 8 is pressed
			number[index] = 8;
			index++; //increment index variable
		}	break; //exit from the switch
		
		case 11:{
			//key 11
			number[index] = 9;
			index++; //increment index variable
		}	break; //exit from the switch
		
		case 12:{ //if key set is pressed
			if(match == 1){ //check the match variable
				for(temp2 = 0; temp2 <= 3; temp2++){
					//password array digits (4) will be erased and set
					//as numbers written in the number array
                    password[temp2] = number[temp2];
                }
				//in EEPROM memory write new password 4-digit sequence
                for(temp2 = 0; temp2 <= 3; temp2++){
                    EEPROM_write(temp2,password[temp2]);
                }
            }

		}	break; //exit from the switch
		
		case 13:{ //if key reset is pressed
			index = 0; //index variable which stands as a number array pointer
			for(temp2 = 0; temp2 <= 3; temp2++){
				//erase all digits, update the number array with default values
				number[temp2] = 10;
			}
			OUT_PORT &= 0xFD; //turn off the buzzer
		}	break; //exit from the switch
		
		case 14:{ //if key 0 is pressed
			number[index] = 0;
			index++; //increment index variable
		}	break; //exit from the switch
		
		case 15:{ //if key show/hide is pressed
			show++; //increment show variable and do the modulo2 operation
			show %= 2; //show variable is constrained with modulo operator
			//it has an interval [0,1], prehaps it can be declared as a boolen
		}	break; //exit from the switch
		
		case 16:{ //if key open/close is pressed
			verify_password(); //go to verification function
            if(match == 1) //if password matches
            {
				if(open == 0) //if the door is closed
                {
                    open = 1; //update door status, door opened
                    OUT_PORT |= 0X01; //activate relay
                }
                else //if the door is opened
                {
                    open = 0;   //update door status, door closed
                    OUT_PORT &= 0XFE; //turn off the relay
                    for(temp2 = 0; temp2 <= 3; temp2++)
                    { 
						//update the number array with default values
                        number[temp2]=10;
                    }
                    match = 0; //update the match variable
                }    
				//else the door will open if open key is pressed
				miss_match=0; //if there are any missmatches before opening
            }
            else //if password does not match
            {
                miss_match++; //there is miss match in guessing the password
                for(temp2 = 0; temp2 <= 3; temp2++)
                {
					//update the number array with default values
                    number[temp2] = 10;
                }
                display();
                if(miss_match == 3)
                {
					//if the person does not guess the password 3 times in a row
                    block = 1; //increment the block variable
                    OUT_PORT |= 0X02; //buzzer on
					OUT_PORT &= 0X1F; //turn off the LEDs
					OUT_PORT |= 0X20; //blocked indicator
					send_sms(); //send an SMS
					EEPROM_write(10,block); //update the EEPROM register
					_delay_ms(3000); //about blocked condition
					OUT_PORT &= 0XFD;  //turn off the buzzer after 3 seconds
					TCCR1B = 0X0D;  //start timer
                }
                else //if wrong attempts is less than 3 times
                {
					OUT_PORT |= 0X02; //turn on the buzzer
					OUT_PORT &= 0X1F; //turn off the LEDs
					OUT_PORT |= 0X40; //wait indicator
					_delay_ms(2000); //wait for 2 seconds
					OUT_PORT &= 0XFD; //turn off the buzzer
					OUT_PORT &= 0X1F; //turn off the LEDs
					OUT_PORT |= 0X80; //ready indicator
                }
            }
            index = 0;
		}	break; //exit from the switch
	}
}

void verify_password(void){
	
	//entered number is read and stored in the array form, when a key corresponding
	//to numbers 0 to 9 is pressed, the number is stored in the current address 
	//location of the array and the address pointer is incremented through a variable
	//index, so when next key is pressed, it is stored in EEPROM at next location
	//array size is limited to 4 and entering beyond this limit leads to a run-time 
	//error, so,before storing the entered digit the value od should be verified 
	//whether it is less than or equal to 3 and this is optional
	
	match = 1; //assume password matches
	for(temp2 = 0; temp2 <= 3; temp2++){
		//compare the data of each address location od entered number and password
		if(number[temp2] != password[temp2]){
			//if there is a miss match at any address location
			match = 0; //password does not match
			LCD_send_command(0x80); //force LCD cursor to first line
			LCD_print("Pass not matches");
			temp2 = 10; //exit from the loop
		}
	}
}

void display(void){
	//there is an option to show or hide the password while entering, the display
	//function uses one loop and it can be modifed easily if the number of digit is
	//varied, if the show option == 1, then digits are shown as they are and if
	//show option == 0, then the the LCD screen is blinking
	for(digit = 4; digit >= 1; digit--){
		if(number[digit-1] != 10){
			if(show == 1){
				LCD_send_command(0x80); //force LCD to 1st line
				LCD_print("Enter_the_pass: ");
				LCD_send_command(0xC2); //force LCD cursor to 2nd line and 3rd char
				show_digit(number[digit - 1]); //show each digit 
			}
		}
	}
}

void show_digit(char digit){
	//show_digit() has one char parameter digit, in switch-case structure it is
	//scanned which digit user wants to print, when one number is detected, it is
	//printed on LCD screen with LCD_print() function calling
	switch(digit){
		case 1:{
			LCD_print("1"); //print the string
		}	break; //exit from the switch
		case 2:{
			LCD_print("2"); //print the string
		}	break; //exit from the switch
		case 3:{
			LCD_print("3"); //print the string
		}	break; //exit from the switch
		case 4:{ 
			LCD_print("4"); //print the string
		}	break; //exit from the switch
		case 5:{
			LCD_print("5"); //print the string
		}	break; //exit from the switch
		case 6:{
			LCD_print("6"); //print the string
		}	break; //exit from the switch
		case 7:{
			LCD_print("7"); //print the string
		}	break; //exit from the switch
		case 8:{
			LCD_print("8"); //print the string
		}	break; //exit from the switch
		case 9:{
			LCD_print("9"); //print the string
		}	break; //exit from the switch
		case 0:{
			LCD_print("0"); //print the string
		}	break; //exit from the switch
	}
}

void EEPROM_write(int addr, char data){
	do{}
	while((EECR & (0 << EEWE)) == 1); //wait while EEWE bit in EECR reg is clear
	do{}
	while((SPMCR & (0 << SPMEN)) == 1);//wait while SPMEN bit in SPMCR reg is clear
	//set an address into lower and upper EEPROM address reg
	EEARH = addr / 256; 
	EEARL = addr % 256;
	EEDR = data; //set the data into EEPROM data reg
	EECR = (1 << EEMWE) | (0 << EEWE); //set the EEMWE and clear EEWE bits in EECR
	EECR = (1 << EEMWE) | (1 << EEWE); //set the EEWE bit and data will be written
	//in the EEPROM memory at the EEAR address
}

char EEPROM_read(int addr){
	int data; 
	do{}
	while((EECR & (0 << EEWE)) == 1); //wait while EEWE in EECR is clear
	//set the address where data is stored in EEPROM memory
	EEARH = addr / 256;
	EEARL = addr % 256;
	EECR |= (1 << EERE); //write 1 to EERE to enable read operation from add
	data = EEDR; //read EEDR reg
	return data;
}

void gsm_initialization(void){
	//system will send SMS after 3 wrong attempts, the SMS is sent through the GSM
	//module connected to the microcontroller, COMPORT is directly connected to the uC
	//in real-time, the TTL level TXand RX pins of the GSM module are connected to uC
	//the communication parameters are baud rate = 9600 bits per second,bitframe = 8bit
	//no parity bit
	
    UART_send_string("ATE0\r\n"); //Disable echoing of commands
    _delay_ms(500); //wait for 500 miliseconds
    UART_send_string("AT+CMGF=1\r\n"); //Message format = text mode
    _delay_ms(500); //wait for 500 miliseconds
    UART_send_string("AT+CMGD=1,4\r\n"); //Delete all the messages
    _delay_ms(500); //wait for 500 miliseconds
    UART_send_string("AT+GSMBUSY=1\r\n");
    _delay_ms(500); //Busy mode enabled to reject incoming calls
}

void send_sms(void){
	//through a series of commands, the concact number and the message are sent to the GSM
	//module and the module sends the message
	
	gsm_initialization();
	UART_send_string("AT + CMGS="); //command to send SMS
	UART_send_char(""); //command format
	for(temp4 = 0;temp4 <= 9; temp4++){
		//send contact number
		UART_send_char(contact_number[temp4]);
	}
	UART_send_char(""); //command format
	UART_send_char(13); //command format 
	_delay_ms(300); //wait for 300 miliseconds
	UART_send_string("Alert:"); //send the text
	UART_send_char(13); //new line indication=carriage return
	UART_send_string("Wrong password is entered for 3 times"); //text
	UART_send_char(13); //new line indication=carriage return
	UART_send_string("System is blocked for one hour.");
	UART_send_char(26); //command format indicating end of text and send SMS
	UART_send_char(13); //command ending format
	UART_send_char(10);
}

void UART_send_string(char string[50]){
	for(temp4 = 0; string[temp4] != 0; temp4++){
		UART_send_char(string[temp4]);
	}
}

void UART_send_char(unsigned char a){
	do{}
	while((UCSRA & (1 << UDRE)) == 0); //wait UDRE to be set, then transmission is over
	//buffer is empty and new transmission can be executed
	UCSRA |= (1 << TXC); this flag is set when the entire frame from TX buffer is 
	//shiffted out and ther is no new data currently present in the buffer (UDR)
	UDR = a;
	do{}
	while((UCSRA & (1 << TXC)) == 0); //wait while TXC is not automatically cleared
}

void block_time(){
	
	//after three wrong attempts is a row, the system will ne blocked for 1 hour
	//when the system is blocked, to avoid recovery made by a power failure or if the
	//unauthorized user tries to reset the system, the information related to blocking
	//and the time passed after blocking the system should be stored in the EEPROM
	//after every reset or the normal switch on the system, this information is read
	//and if the system is previously under a blocced state then,this blocking continues
	//for its remaining time and then the system resumes, the EEPROM related operations
	//are performed in the interrupt sub routine od compare match interruput od the 
	//16bit timer
	
	wait++; //increment seconds count
	if(wait == 10){ //blocking time = 10 sec, it can be up to 65,535 seconds
		TCCR1B = 0x00; //stop the timer
		wait = 0; //if the blocking time is completed
		block = 0; //system is unblocked or resumed
		miss_match = 0; //reset the miss_match counting variables 
		EEPROM_write(10,block); //update EEPROM register
		EEPROM_write(11,0);
		EEPROM_write(12,0);
		OUT_PORT &= 0x1F; 
		OUT_PORT |= 0x80;
	}
	else{
		//if blocking time is not completed, update EEPROM register
		EEPROM_write(11, wait % 256); 
		EEPROM_write(12, wait % 256);
	}
}

int main(void){
	PIN_init(); //initilise PORTs
	LCD_init(); //initialise LCD screen
	//1 second equivalent imported in Timer1 output compare register
	OCR1AH = 0x3D;
	OCR1AL = 0x09; 
	TIMSK |= (1 << OCIE1A); //enable Timer1 output compare match interrupt
	//interrupt will be executed if I bit in SREG is enabled and when a compare 
	//match in TImer1 occurs, when OCF1A is set in TIFR register
	SREG |= 0x80; //write hex 0x80 into SREG, enabling  I bit which causses enabling
	//global interrupt, also it cane be written as SREG |= (1 << I);
	UART_init(9600); //declare baudrate at 9600 bits per second
	OUT_PORT |= (1 << WAIT); //wait LED is at HIGH state
	delay_ms(15000); //waiting for GSM module start up
	for(temp1 = 0; temp1 <= 3; temp1++){
		password[temp1] = EEPROM_read(temp1);
	}
	block = EEPROM_read(10);
	if(block == 1){
		//if block variable is 1
		OUT_PORT &= (0 << WAIT); //wait LED is at LOW state
		OUT_PORT |= (1 << BLOCKED); //blocked LED is at HIGH state
		wait = EEPROM_read(11); //time lapse after blocking
		wait *= 256; //multiplay wait variable value with 256
		temp1 = EEPROM_read(12);
		wait += temp1;
		TCCR1B = 0x0D; //setting CS12 and CS10 causses clk/1024 prescale rate
		//CTC mode selected with setting WGM12 at HIGH state, eventually timer starts
	}
	else{
		//if block variable is 0
		//ready LED, wait LED and blocked LED are at LOW state
		OUT_PORT &= (0 << READY) | (0 << WAIT) | (0 << BLOCKED); 
		//ready LED is at HIGH state
		OUT_PORT |= (1 << READY); 
	}
	while(1){ //do it forever
		if(block == 0){ //check block variable
			get_key();
			display();
		}
	}
}
