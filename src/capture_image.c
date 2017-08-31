// #include "address_map_arm.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include "hps_soc_system.h"
#include "socal.h"
#include "address_map_arm.h"
#include "hps.h"


#define SWITCH_BASE			  0xFF200040
#define KEY_BASE              0xFF200050
#define VIDEO_IN_BASE         0xFF203060
#define SDRAM_BASE		      0xC0000000
#define FPGA_ONCHIP_BASE      0xC8000000
#define FPGA_CHAR_BASE        0xC9000000

/* This program demonstrates the use of the D5M camera with the DE1-SoC Board
 * It performs the following: 
 * 	1. Capture one frame of video when any key is pressed.
 * 	2. Display the captured frame when any key is pressed.		  
*/
/* Note: Set the switches SW1 and SW2 to high and rest of the switches to low for correct exposure timing while compiling and the loading the program in the Altera Monitor program.
*/

/* Pointers*/
	volatile int * SDRAM_ptr = (int *) SDRAM_BASE;
	volatile int * HPS_GPIO1_ptr = (int *) HPS_GPIO1_BASE;
	volatile int * KEY_ptr				= (int *) KEY_BASE;
	volatile int * Video_In_DMA_ptr	= (int *) VIDEO_IN_BASE;
	volatile short * Video_Mem_ptr	= (short *) FPGA_ONCHIP_BASE;
	volatile int* SW_switch_ptr = (int*) SWITCH_BASE; // SW slider switch address

/* Prototyping Functions*/
	void show();
	void DecompressCompress();
	void blackAndWhite();
	void outOfSDRAM();
	void invertForward();
	void captureImage();
	void blackScreen();
	void pixelInBytes();
	void decompress(int RLE_Return);
	
	/*Global Vars*/
	int bW=0; //0 if not b&w or has been already inverted backward 	1 if inverted forward
	int bwReset=0;   //make sure screen isnt b&w
	int screen_index=0; 
	int horiz=0;
	int sdram_index=0;
	int vert=0;
	int x=0;
	int y=0;
	int sum=0;  //keep sum of color of pixels
	int average=0;  //sum of color of pixels/# of pixels
	int RLEsegments=0;
	float compRatio = 0;
	int decomp = 0;
	
	unsigned char pixelBytes[(320*240)/8];
	char screen[320*240];
	unsigned char compressed[(320*240)*3]; //3 bytes per pixel
	
int main(void)
{

	*(Video_In_DMA_ptr + 3)	= 0x4;				// Enable the video
	int SW_value;
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RLE_FLUSH_PIO_BASE, 1);
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RLE_FLUSH_PIO_BASE, 0);
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_IN_WRITE_REQ_PIO_BASE,0);
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_OUT_READ_REQ_PIO_BASE,0);
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RESULT_READY_PIO_BASE,1);
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RLE_RESET_BASE,1);
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RLE_RESET_BASE,0);
	while (1)
	{
		if(*KEY_ptr != 0){ //when button is pressed, check what switch is enabled..
			SW_value = *(SW_switch_ptr); //get the value of whatever switch is enabled
				switch(SW_value){ //match on the value of the switch
					case 0: //Capture an Image
						bwReset=0; //pic shouldnt be b&W state
						bW=0; //make sure not b&w or inverted backwards
						captureImage(); //capture the image and store it to the On-Chip Memory inside the FPGA
						while (*KEY_ptr != 0);	//while the key is pressed down dont do anything
						break;
					case 1: //Reset/Resume Video
						bwReset=0; //pic shouldnt be b&W state
						bW=0; //make sure not b&w or inverted backwards
						*(Video_In_DMA_ptr + 3)	= 0x4; // Enable the video
						while (*KEY_ptr != 0);	//while the key is pressed down dont do anything;	
						break;
					case 2: //B&W then compress
						//*(Video_In_DMA_ptr + 3) = 0x0; //stop the video
						if(bW==0){   //if the black/white function has yet to be called or if the picture is white and black
							blackAndWhite(); //call the black/white function
							invertForward(); //make picture black/white
						}
						else{ //if we've already inverted the picture forward to black/white
							*(Video_In_DMA_ptr + 3)	= 0x4;
							bW=0; //make the variable 0 so we know that bW has already been inverted backwards
						}
						while (*KEY_ptr != 0);	//while the key is pressed down dont do anything	
						break;
					case 3:
						pixelInBytes();
						DecompressCompress();
						while (*KEY_ptr != 0);	//while the key is pressed down dont do anything	
						break;
				}
		}
	}
}
void blackScreen(){
	*(Video_In_DMA_ptr + 3) = 0x0;
	for (y = 0; y < 240; y++) {
		for (x = 0; x < 320; x++) {
			*(Video_Mem_ptr + (y << 9) + x)=0x0;
		}
	}	
}

void captureImage(){
	*(Video_In_DMA_ptr + 3) = 0x0;
	for (y = 0; y < 240; y++) {
		for (x = 0; x < 320; x++) {
			short temp2 = *(Video_Mem_ptr + (y << 9) + x);
			*(Video_Mem_ptr + (y << 9) + x) = temp2; //capture the current image in the buffer and store it in the buffer
		}
	}	
}

void invertForward(){ //bW==0
	short pixelVal;
	for (y = 0; y < 240; y++) {
		for (x = 0; x < 320; x++) {
			if(bwReset==1){ //if we've already inverted it
				if(*(Video_Mem_ptr + (y << 9) + x)==0x0) //make all the pixels that are black into white
					*(Video_Mem_ptr + (y << 9) + x)=0xFFFF;
				else //make all the pixels that are white into black
					*(Video_Mem_ptr + (y << 9) + x)=0x0;
			}
			else{
				pixelVal = *(Video_Mem_ptr + (y << 9) + x); //get the current pixel value
				if(average<pixelVal) //is the average of colors lower than the current pixel value?
					*(Video_Mem_ptr + (y << 9) + x)=0xFFFF; //then make the pixel black
				else //if the average of colors is not lower than the current pixel value
					*(Video_Mem_ptr + (y << 9) + x)=0x00; //then make the pixel white
			}
		}
	}	
	bwReset=0; //new
}

void blackAndWhite(){
	captureImage();
	for (y = 0; y < 240; y++) {
		for (x = 0; x < 320; x++) {
			sum = sum + *(Video_Mem_ptr + (y << 9) + x); //sum up the pixel values
			//top 5 are red then 6 green then bottom 5 are blue
		}
	}
	bW=1; //The screen will be Black/White
	average=sum/76800; //divide sum of pixel color values by # of pixels
	sum=0; //make the sum 0 so we can call blackandWhite again
}

void pixelInBytes(){
	int ct=0;
	unsigned int arrayIndex=0;
	unsigned char data=0;
	for (y=0;y<240; y++) {
		for (x=0;x<320; x++) {
			data = (*(Video_Mem_ptr + (y << 9) + x)==0x0)? (data>>1) : ((data>>1)| 0x80);
			if(ct<7)
				ct++;
			else{
				ct=0;
				pixelBytes[arrayIndex]= data;
				arrayIndex++;
				data=0;
			}
		}
	}	
}

void outOfSDRAM(){
	int pixel=0;
	for (y=0;y<240; y++) {
		for (x=0;x<320; x++) {
			pixel = *(SDRAM_ptr+x+320*y);
			*(Video_Mem_ptr + (y << 9) + x-8) = (pixel==1)? 0xFFFF : 0x0000;
		}
	}
	printf("Compression Ratio %f.\n", decomp/compRatio);
	decomp = 0;
	compRatio = 0;
}

void decompress(int RLE_Return){
	char bitVal = (RLE_Return & 0x00800000)>>23;
	int quantity=(RLE_Return&0x007FFFFF);
	compRatio = compRatio+24;
	decomp=decomp+quantity;
	int i;
	for(i=0;i<quantity;i++) {
		*(SDRAM_ptr +sdram_index)=bitVal;
		sdram_index++;
	}
}

void DecompressCompress(){
	long int compressCt=0;
	long int enc=0;
	screen_index=0; 
	horiz=0;
	vert=0;
	blackScreen();
	printf("Encoding\n");
	while(compressCt<=(9600)){
			alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_IN_WRITE_REQ_PIO_BASE,1);
			alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+ODATA_PIO_BASE, pixelBytes[compressCt]);
			alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_IN_WRITE_REQ_PIO_BASE,0);
			compressCt++;
		if(alt_read_byte(RESULT_READY_PIO_BASE+ALT_FPGA_BRIDGE_LWH2F_OFST)==0){
			alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_OUT_READ_REQ_PIO_BASE,1);
			decompress(alt_read_word(ALT_FPGA_BRIDGE_LWH2F_OFST+IDATA_PIO_BASE));
			enc+=3;
			alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_OUT_READ_REQ_PIO_BASE,0);				
		}
	}
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RLE_FLUSH_PIO_BASE, 1);
	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RLE_FLUSH_PIO_BASE, 0);
	alt_write_byte(FIFO_OUT_READ_REQ_PIO_BASE+ALT_FPGA_BRIDGE_LWH2F_OFST,1);
	decompress(alt_read_word(IDATA_PIO_BASE+ALT_FPGA_BRIDGE_LWH2F_OFST));
	alt_write_byte(FIFO_OUT_READ_REQ_PIO_BASE+ALT_FPGA_BRIDGE_LWH2F_OFST,0);
	printf("%ld RLE segments encoded.\n", enc/3);
	sdram_index=0;
	outOfSDRAM();
}
