#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <stdbool.h>
#define INTERVAL_SEC 0 /* second */
#define INTERVAL_MS 100 /* miliseconds */
#define BAP_BUFFERSIZE 255
#define BAP_CHANNELS 4



#define GET_ALL				0x01
#define MOST_CATALOG_VERSION	       	0x02
#define FUNCTION_LIST			0x03
#define FBLOCK_AVAILABILITY		0x04
#define FSG_CONTROL			0x0D
#define FSG_SETUP			0x0E
#define FSG_OPERATION_STATE		0x0F
#define DESTINATION_LIST		0x10
#define ASG_CAPACITY			0x11



unsigned char bap_id;

unsigned char bap_data[BAP_CHANNELS][BAP_BUFFERSIZE];
unsigned char bap_tx_data[BAP_BUFFERSIZE];

unsigned short bap_seq_number[BAP_CHANNELS];
unsigned char bap_opcode[BAP_CHANNELS], bap_lsg_id[BAP_CHANNELS], bap_func_id[BAP_CHANNELS]; 
unsigned short bap_data_len[BAP_CHANNELS];
unsigned char fct_id_ctr=0;


typedef struct {
    unsigned short can_id : 11;
    unsigned char opcode : 3;
    unsigned char lsg_id : 6;
    unsigned char fct_id : 6;
    unsigned short data_len : 13;
    unsigned char *data;
} bap_t;


/*
typedef struct {
    unsigned char VERSION_CONTROL_data[6];
    unsigned char UNKNOWN1_data[2];
    unsigned char DEVICE_SERVICE_SUPPORT_data[8];
    unsigned char FCT04;
    unsigned char FCT0E[4];
    unsigned char FCT0F;
    unsigned char FCT13;
    unsigned char FCT14;
    unsigned char FCT17;
    unsigned char FCT19;
    unsigned char wtf[20];
    unsigned char FCT1A[8];
    unsigned char FCT22;

} OCU_t;
*/



typedef struct {
    unsigned char MOST_CATALOG_VERSION_data[6];
    unsigned char WTF_data[2];
    unsigned char FUNCTION_LIST_data[8];
    unsigned char FBLOCK_AVAILABILITY_data;
    unsigned char FSG_CONTROL_data;
    unsigned char FSG_SETUP_data;
    unsigned char FSG_OPERATION_STATE_data;
    unsigned char ASG_CAPACITY_data[2];
} OCU_t;



OCU_t OCUdata = {
     {0x03, 0x00, 0x37, 0x00, 0x03,0x00},	//MOST_CATALOG_VERSION
     {0x08,0x00},				//??? 
     {0x38,0x07,0xC0,0x00,0x00,0x00,0x00,0x00},	//FUNCTION_LIST
     0x0A,					//FBLOCK_AVAILABILITY
     0x00,					//FSG_CONTROL
     0x00,					//FSG_SETUP
     0x00,					//FSG_OPERATION_STATE
     {0x01,0x00}				//ASG_CAPACITY
};



int s,read_can_port;
unsigned char heartbeat_counter, second_counter; 
volatile unsigned char timeout_counter;
timer_t update_timer;

//CAN Variables



void send_can_frame(unsigned int id, unsigned char len, unsigned char *data) {
    struct can_frame frame;
    int i;

    frame.can_id  = id;
    frame.can_dlc = len;
    memcpy(frame.data, data, 8);
/*
    fprintf(stdout,"Send->%lX %d ",frame.can_id, frame.can_dlc);
                    for (i=0; i<frame.can_dlc; i++) printf("%02X", frame.data[i]);
                    fprintf(stdout,"\n");
		    fflush(stdout);
*/    
write(s, &frame, sizeof(struct can_frame));


}

void send_bap_single_byte(unsigned char data, unsigned char importance, unsigned char fct_ind) {

    unsigned char temp[3];
    
    temp[0]=importance;
    temp[1]=fct_ind;
    temp[2]=data;
    send_can_frame(0x6FD,3,temp);

}



void bap_send(bap_t bap_msg) {
    if(!bap_msg.data) return;
    struct can_frame frame;
    frame.can_id = bap_msg.can_id;
    

    if (bap_msg.data_len>6) {
	unsigned char seqno=0,ii=0;
	
	frame.can_dlc = 8;
	frame.data[0] = 0x80;
	frame.data[0] += bap_msg.data_len>>8;
	frame.data[1] = bap_msg.data_len&0xff;
	frame.data[2] = bap_msg.opcode<<4;
	frame.data[2] += bap_msg.lsg_id>>2;
	frame.data[3] = bap_msg.lsg_id<<6;
	frame.data[3] += bap_msg.fct_id;
	memcpy(frame.data+4, bap_msg.data, 4);
	write(s, &frame, sizeof(struct can_frame));
	
	
	for(ii=0; ii<=(bap_msg.data_len-4)/7; ii++) {
	unsigned short bytesLeft = (bap_msg.data_len - 4) - ii*7;
	    frame.data[0] = 0xC0;
	    frame.data[0] += seqno&0x0f;
	    seqno++;
	    if(bytesLeft>=7) {
		memcpy(frame.data+1, bap_msg.data+ii*7+4, 7);
		frame.can_dlc = 8;
		write(s, &frame, sizeof(struct can_frame));
	    } else if(bytesLeft) {
		memcpy(frame.data+1, bap_msg.data+ii*7+4, bytesLeft);
		frame.can_dlc = bytesLeft+1;
		write(s, &frame, sizeof(struct can_frame));

	    }


	}


    } else { //bap short frame
	frame.can_dlc = bap_msg.data_len+2;
	frame.data[0] = bap_msg.opcode<<4;
	frame.data[0] += bap_msg.lsg_id>>2;
	frame.data[1] = bap_msg.lsg_id<<6;
	frame.data[1] += bap_msg.fct_id;
	memcpy(frame.data+2, bap_msg.data, bap_msg.data_len);
	write(s, &frame, sizeof(struct can_frame));
    }





}





static void timer_handler(int sig, siginfo_t *si, void *uc)
{   //Every 200msec

bap_t bap;
//unsigned char bap_tx_data[BAP_BUFFERSIZE];
unsigned char bap_tx_data[] = {0xff,0xff,0xff,0xff,0xff};
//unsigned char climatron[] = {0x03,0x00,0x01,0x00,0x03,0x00,0x08,0x00,0x38,0x07,0x47,0xBF,0x7C,0x03,0x40,0x00,0x0A,0x10,0x02,0x00,0x00,0x00,0x00,
//    0x00,0x00,0x00,0x01,0x03,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00 };

//unsigned temp_data[

bap.can_id = 0x6fd;
bap.lsg_id=0x37;
bap.opcode=3;
bap.fct_id=0;

switch(fct_id_ctr) {
    case 1:
	{
	bap.fct_id = ASG_CAPACITY;
	bap.data_len=sizeof(OCUdata.ASG_CAPACITY_data);
	bap.data = (unsigned char*)&OCUdata.ASG_CAPACITY_data;

	}

    break;

    case 10:
    case 30:
    case 50:
    break;
//	bap.fct_id = AIR_CIRCULATION_AUTOMATIC;
//	bap.data_len=sizeof(climatron.AIR_CIRCULATION_AUTOMATIC_data);
//    	bap.data = (unsigned char*)&climatron.AIR_CIRCULATION_AUTOMATIC_data;

    case 05:
	{
	bap.fct_id = MOST_CATALOG_VERSION;
	bap.data_len=sizeof(OCUdata.MOST_CATALOG_VERSION_data);
	bap.data = (unsigned char*)&OCUdata.MOST_CATALOG_VERSION_data;

	}

    break;
    /*
    case 15:
	{
	unsigned char temp[]={0x00};
	bap.fct_id = 0x14;
	bap.data_len=sizeof(temp);
    	bap.data = temp;
	}

    break;

    case 20:
	{
	unsigned char temp[]={0x0a,'J','a','n','u','s', 'z',' ','G','S','M' };
	bap.fct_id = 0x16;
	bap.data_len=sizeof(temp);
    	bap.data = temp;
	}

    break;
    case 21:
	{
	unsigned char temp[]={0x0F,0x32,0x36,0x30,0x30,0x33,0x32,0x36,0x36,
	0x34,0x30,0x37,0x37,0x36,0x37,0x36,0x0F,0x33,0x35,0x37,0x39,0x36,0x37,0x30,0x35,0x34,0x33,0x30,0x31,0x33,0x36,0x30,0x00};
	bap.fct_id = 0x12;
	bap.data_len=sizeof(temp);
    	bap.data = temp;
	}

    break;
    */
    default:
    break;

}




fct_id_ctr++;
//if(bap.fct_id==1) {
//    bap.data_len=sizeof(climatron);
//    bap.data = (unsigned char*)&climatron;
//bap.opcode=4;
//bap_send(bap);



//} else if(bap.fct_id==2) {
    //bap.fct_id=2;
//    bap.data_len=sizeof(climatron.VERSION_CONTROL_data);
//    bap.data = (unsigned char*)&climatron.VERSION_CONTROL_data;
//bap.opcode=0;
if(bap.fct_id) bap_send(bap);

//} else {
//    bap.data_len=sizeof(bap_tx_data);
//    bap.data=bap_tx_data;
//bap.opcode=3;
//}
if(fct_id_ctr>0x3f) fct_id_ctr=0;

//bap_send(bap);

    
//    if(++timeout_counter>25) {// ponad 5s
//	--timeout_counter;
//	puts("Timeout!");
	
//    } else {
//	send_tv_heartbeat_frame();
//	if (++second_counter==5) {
	    //puts("1 second Timer!");
//	    send_heartbeat();
//	    second_counter=0;
//	}
//    }
}




void timer_enable()
{
    struct sigevent te;
    struct itimerspec its;
    struct sigaction sa;
    int signal_num = SIGALRM;

    /* Set up signal handler. */
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = timer_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(signal_num, &sa, NULL);

    /* Set and enable alarm */
    te.sigev_notify = SIGEV_SIGNAL;
    te.sigev_signo = signal_num;
    timer_create(CLOCK_REALTIME, &te, &update_timer);

    its.it_interval.tv_sec = INTERVAL_SEC;
    its.it_interval.tv_nsec = INTERVAL_MS * 2000000;
    its.it_value.tv_sec = INTERVAL_SEC;
    its.it_value.tv_nsec = INTERVAL_MS * 200000;
    timer_settime(update_timer, 0, &its, NULL);

}

void handle_bap_data(unsigned short id, unsigned char channel) 
{

	int i;
	fprintf(stdout,"BAP Frame @%03X: opcode:%02X LSG ID:%02X FCT ID:%02X Data len:%03X channel: %d ",id, bap_opcode[channel], bap_lsg_id[channel], bap_func_id[channel], bap_data_len[channel],channel);
	for (i=0; i<bap_data_len[channel]; i++) printf("%02X ", bap_data[channel][i]);
	fprintf(stdout,"\n");
	for (i=0; i<bap_data_len[channel]; i++) {
	    if ((bap_data[channel][i] > 0x1F) && (bap_data[channel][i] < 0x7F)) printf("%c", bap_data[channel][i]); else printf(".");
	}
	fprintf(stdout,"\n");
	fflush(stdout);
	
	if(bap_lsg_id[channel]==0x37) {
	    bap_t bap;
	    bap.can_id = 0x6fd;
	    bap.lsg_id=0x37;
	    bap.fct_id=bap_func_id[channel];
	    bap.opcode=4;
	    switch(bap.fct_id) {
	    case GET_ALL:
		bap.data_len=sizeof(OCUdata);
		bap.data = (unsigned char*)&OCUdata;
	    break;
	    case ASG_CAPACITY:
	    {
		//unsigned char temp[]={0xF3,0x01,0x00,0x41,0x00,0x01,0x04,0x07,0x35,0x30,0x37,0x30,0x39,0x34,0x37};
		//bap.data_len=sizeof(temp);
		//bap.data = (unsigned char*)&temp;
		//bap.data_len=bap_data_len[channel];
		//bap.data = (unsigned char*)&bap_data[channel];
		bap.data_len=sizeof(OCUdata.ASG_CAPACITY_data);
		bap.data = (unsigned char*)&OCUdata.ASG_CAPACITY_data;
	    }
	    break;

	    case DESTINATION_LIST:
	    {
		//unsigned char temp[]={0x15,0x02,0x00,0x41,0x00,0x01,0x04,0x0d,0x4F, 0x6E, 0x4C, 0x69, 0x6E, 0x65, 0x20, 0x44, 0x65, 0x73, 0x74, 0x20, 0x32};
		
		//unsigned char temp[]={0x15,  0x01 ,0x00,  0x41, 0x00 ,0x01,  0xF6,0x0d, 0x4F,0x6E,0x4C,0x69,0x6E,0x65,0x20,0x44,0x65,0x73,0x74,0x20,0x33};
		//^--simple
		//unsigned char temp[]={0x15,  0x01 ,0x00,  0x42, 0x00 ,0x01,  0xc9 ,    0x70, 0xb8, 0x0c,0x03,    0xac ,0x70, 0xff , 0x00,   0x04,0x01,0x4};
		//^--metoda 2, Miękinia 													^   ^-icon id
		//																|-immediate dest

		unsigned char temp[]={0x15,  0x01 ,0x00,  0x40, 0x00 ,0x01,  0xdd ,  7, 'P','a','c','a','n', 'o', 'w',   0x70, 0xb8, 0x0c,0x03,    0xac ,0x70, 0xff , 0x00,   0x02,0x04, \
					9,'E','x','t','e','n','s','i','o','n',  1,0,0,0,  0x5, 7, 'C','z','e','r','s','k','a', 2, '3','3', 3, 'U','l','m', 2, 'P','L', \
					5, '5','0','3','2','0', 2, 'P','L', 3, '9','9','7'};
		unsigned char aid = 0;  //?? unused?
		unsigned char tid = 0;  //??
		unsigned short TotE = 0; //?
		unsigned char ra = 0; //Record Address 
		//TRANSMIT_RECADDR_DETAILED_INFO = 0
		//TRANSMIT_RECADDR_NAME_INFO = 1
		//TRANSMIT_RECADDR_LIMITED_INFO = 2
		//TRANSMIT_RECADDR_POS_INFO = 15
		bool s; //TRANSMIT_SHIFT
		bool d; //TRANSMIT_DIRECTION
		bool tp; //TRANSMIT_POS
		bool is; //TRANSMIT_INDEX_SIZE
		unsigned char bap_mode; //1=BAP_MODE_STATUS_ARRAY 0=BAP_MODE_CHANGED_ARRAY

		unsigned short start; //?
		unsigned short elem; //?


		bap.data_len=sizeof(temp);
		bap.data = (unsigned char*)&temp;
		temp[0] = bap_data[channel][0];
		//bap.data_len=bap_data_len[channel];
		//bap.data = (unsigned char*)&bap_data[channel];
		//bap.data_len=sizeof(OCUdata.ASG_CAPACITY_data);
		//bap.data = (unsigned char*)&OCUdata.ASG_CAPACITY_data;
	    }
	    break;


	    /*
	    case 0x1B:
		{
		unsigned char temp[]={0x80,01,02,07,03,01,02,03,04,05,06,07,8,9,10,11,12,13,14};
		bap.fct_id = 0x1B;
		bap.data_len=sizeof(temp);
	    	bap.data = temp;
		}

	    break;
	    */

//	    case PRESENTATION_CONTROL:
//		bap.data_len=sizeof(climatron.PRESENTATION_CONTROL_data);
//		bap.data = (unsigned char*)&climatron.PRESENTATION_CONTROL_data;
//	    break;
//	    case POP_UP_TIME:
//		bap.data_len=sizeof(climatron.POP_UP_TIME_data);
//		bap.data = (unsigned char*)&climatron.POP_UP_TIME_data;
//	    break;
	    default:
	    break;

	    }


	    
	     bap_send(bap);


	}

}

void bap_receive(struct can_frame frame_rd) 
{

	    if(frame_rd.data[0]& 0b10000000) { //>6bytes long
			unsigned char channel = (frame_rd.data[0]>>4)&0b00000011;
			if(frame_rd.data[0]& 0b01000000) { //consecutive frame
			    if((bap_seq_number[channel]&0x0f)==(frame_rd.data[0]&0x0f)) { //keep it in sync
				memcpy(bap_data[channel]+(bap_seq_number[channel]*7+4),frame_rd.data+1,7);
				bap_seq_number[channel]++;
				if((bap_seq_number[channel]*7+4)>bap_data_len[channel]) handle_bap_data(frame_rd.can_id,channel); 
				
			    }

			} else { //start frame
			    bap_opcode[channel]=(frame_rd.data[2]>>4)&0b00000111;
			    bap_lsg_id[channel]=(frame_rd.data[3]>>6)&0b00000011;
			    bap_lsg_id[channel]+=(frame_rd.data[2]<<2)&0b00111100;
			    bap_func_id[channel]=(frame_rd.data[3])&0b00111111;
			    bap_data_len[channel]=(frame_rd.data[0]&0x0F)<<8;
			    bap_data_len[channel]+=frame_rd.data[1];
			    memcpy(bap_data[channel],frame_rd.data+4,bap_data_len[channel]);
			    bap_seq_number[channel]=0;
			    if (bap_data_len[channel]<5) handle_bap_data(frame_rd.can_id,channel);
			
			}
		    
		    } else { //short frame
			    unsigned char test[]={'a','b','c','d','e','f','g'};
			    bap_opcode[3]=(frame_rd.data[0]>>4)&0b00000111;
			    bap_lsg_id[3]=(frame_rd.data[1]>>6)&0b00000011;
			    bap_lsg_id[3]+=(frame_rd.data[0]<<2)&0b00111100;
			    bap_func_id[3]=(frame_rd.data[1])&0b00111111;
			    bap_data_len[3]=frame_rd.can_dlc-2;
			    memcpy(bap_data[3],frame_rd.data+2,bap_data_len[3]);
			    //memcpy(&bap_data[3],&test,bap_data_len[3]);
			    
			    handle_bap_data(frame_rd.can_id,3);
			    
		    }
}



void read_port()
{
    struct can_frame frame_rd;
    int recvbytes = 0,i;


    read_can_port = 1;
    while(read_can_port)
    {
        struct timeval timeout = {1, 0};
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(s, &readSet);

        if (select((s + 1), &readSet, NULL, NULL, &timeout) >= 0)
        {
            if (!read_can_port)
            {
                break;
            }
            if (FD_ISSET(s, &readSet))
            {
                recvbytes = read(s, &frame_rd, sizeof(struct can_frame));
                if(recvbytes)
                {
                    switch(frame_rd.can_id) {
		    //case 0x6da:
		    //case 0x63c:
		    //case 0x6c6:
		    case 0x6b7:
		    //send_tst_frame();
		    //fprintf(stdout,"Rcv<-%lX %d ",frame_rd.can_id, frame_rd.can_dlc);
                    //for (i=0; i<frame_rd.can_dlc; i++) printf("%02X", frame_rd.data[i]);
                    //fprintf(stdout,"\n");
		    //fflush(stdout);
		    bap_receive(frame_rd);
		    break;














		    case 0x575: //ignition
			//printf("ignition: %02X\n",frame_rd.data[0]);
			//if(frame_rd.data[0]&0x01) timeout_counter = 0; 
			if(frame_rd.data[0]&0x02) timeout_counter = 0;
		    break;
		    case 0x661: //radio on/off
			if(frame_rd.data[0]&0x01) timeout_counter = 0; 
		    break;
		    }//end switch can ID
                    
                }
            }
        }

    }

}


int main(void) {

    int nbytes;
    struct sockaddr_can addr;
    struct can_frame frame;
    struct ifreq ifr;
    char *ifname = "can0";

    if((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
	perror("Error while opening socket");
	return -1;
    }

    strcpy(ifr.ifr_name, ifname);
    ioctl(s, SIOCGIFINDEX, &ifr);
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    printf("%s at index %d\n", ifname, ifr.ifr_ifindex);
    if(bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	perror("Error in socket bind");
	return -2;
    }






bap_t bap;

bap.can_id = 0x6fd;
bap.lsg_id=0x37;
bap.fct_id=MOST_CATALOG_VERSION;
bap.data_len=sizeof(OCUdata.MOST_CATALOG_VERSION_data);
bap.data = (unsigned char*)&OCUdata.MOST_CATALOG_VERSION_data;
bap.opcode=0;
bap_send(bap);




    timer_enable();
//    init_tuner();
    while(1) read_port();
}

