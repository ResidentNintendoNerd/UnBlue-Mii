#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <memory>
#include <wiiuse/wpad.h>
#include <ogc/video.h>
#include <iostream>
#include <fstream>
#include <ogcsys.h>
#include <gccore.h>
#include <fat.h>
#include <cstring>
#include <unistd.h>
#include <dirent.h>
#include <isfs/isfs.h>
#include <mii.h>
using namespace std;
// Welcome to me trying to figure out how the hell C++/Wii dev works

// I would like to thank discord user thepikachugamer of Nintendo Homebrew for hard carrying me through this

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

void init(){

	VIDEO_Init();
	WPAD_Init();
	
	rmode = VIDEO_GetPreferredMode(NULL);
	
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	console_init(xfb,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
	
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
	
	printf("\x1b[0;0H");
}

// Throw error (unless no error given) and exit program
void die(const char *msg) {
	if (strcmp("", msg) != 0) {
		perror(msg);
	}
	printf("Exiting in five seconds...");
	sleep(5);
	fatUnmount(0);
	ISFS_Deinitialize();
	exit(0);
}

bool can_open_root_fs() {
	DIR *root = opendir("/");
	if (root) {
		closedir(root);
	return true;
	}
	return false;
}

void initialise_fat() {
	if (!fatInitDefault()){ die("Unable to initialise FAT subsystem.\n"); }
	if (!can_open_root_fs()){ die("Unable to open SD root filesystem.\n"); }
	if (chdir("/")){ die("Could not change to SD root directory.\n"); }
}

// From the Mii Data page on Wiibrew (converted to C, not by me obs)
// Calculate CRC16 from a buffer of chars
unsigned short crc16 (const unsigned char* bytes, unsigned int length) {
	unsigned short crc = 0x0000;
	for (unsigned int byteIndex = 0; byteIndex < length; byteIndex++) {
		for (int bitIndex = 7; bitIndex >= 0; bitIndex--) {
			crc = (((crc << 1) | ((bytes[byteIndex] >> bitIndex) & 0x1)) ^
			(((crc & 0x8000) != 0) ? 0x1021 : 0)); 
		}
	}
	for (int counter = 16; counter > 0; counter--) {
		crc = ((crc << 1) ^ (((crc & 0x8000) != 0) ? 0x1021 : 0));
	}
	return crc;
 }

// Thank you other random discord user from Nintendo Homebrew
// Aligns a pointer p to 32-bit
unsigned char* alignPtr(unsigned char* p) { return (unsigned char*)(((uintptr_t)p + 31u) & ~31u); }

int main(int argc, char **argv) {
	init();

	ISFS_Initialize();
	
	initialise_fat();

	Mii * miis;

	miis = loadMiis_Wii();
	
	unsigned char ids[4] = {};
	
	printf("UnBlue Mii v1.0\n\n");
	printf("Searching for saved System ID...\n\n");
	sleep(3);
	
	// Check if there is a .txt file present named "systemID.txt"
	ifstream f("/apps/UnBlue Mii/systemID.txt", ios::binary);
	if (f) {
		printf("Saved System ID found. Setting System ID for all Miis...\n\n");
		sleep(3);
		// Something something read ID value from systemID.txt
		for (int i=0; i<4; i++) {
			f >> ids[i];
		}
		if (!f.good()) { f.close(); die("Unable to read from systemID.txt."); }
	}
	else {
		printf("Saved System ID not found. Searching for Mii named \"UnBlue Mii\"...\n\n");
		sleep(3);
		// Check if there's a Mii named "UnBlue Mii" and save it's system ID if present
		for(int i=0; i<NoOfMiis; i++) {
			if (strcmp("UnBlue Mii", miis[i].name) == 0) {
				printf("Mii found. Setting System ID...\n\n");
				ids[0] = (unsigned char)miis[i].systemID0;
				ids[1] = (unsigned char)miis[i].systemID1;
				ids[2] = (unsigned char)miis[i].systemID2;
				ids[3] = (unsigned char)miis[i].systemID3;
				sleep(2);
				// Save System ID to file
				printf("Saving System ID to file...\n\n");
				ofstream sysID("/apps/UnBlue Mii/systemID.txt", ios::binary);
				if (sysID.is_open()){
					for (int j=0; j<4; j++) {
						sysID << ids[j];
					}
					sysID.close();
					printf("Done! You can delete UnBlue Mii now!\n\n");
				}
				else {
					die("Unable to open file.\n");
				}
				sleep(3);
			}
		}
		// If any ID variable is unchanged (Mii was not found), then abort program
		if (ids[0] == 0 && ids[1] == 0 && ids[2] == 0 && ids[3] == 0) {
			printf("Mii named \"UnBlue Mii\" not found! Please make one in the Mii Channel.\nPress HOME to exit.\n");
			while (true) {
				WPAD_ScanPads();
				u32 pressed = WPAD_ButtonsDown(0);
				if (pressed & WPAD_BUTTON_HOME) {die("");}
				VIDEO_WaitVSync();
			}
		}
	}
	f.close();
	
	// Only change System IDs if variables were changed (just in case)
	if (ids[0] != 0 && ids[1] != 0 && ids[2] != 0 && ids[3] != 0) {
		// hex editing time babey
		// Open Wii's Mii database file
		printf("Opening Mii database!\n\n");
		sleep(3);
		s32 miiDB = ISFS_Open("/shared2/menu/FaceLib/RFL_DB.dat", ISFS_OPEN_RW); // why is a file an int??
		// HAVE NONE OF YOU PEOPLE HEARD OF DOCUMENTATION????
		// GOOGLE CAN'T HELP ME IF I'M USING SOMETHING WII-SPECIFIC
		if (miiDB>0) {
			printf("Editing Mii database!\n\n");
			
			// Read entire database file into a buffer
			const int buffer_size = 779968; // Size of RFL_DB.dat
			vector<unsigned char> miiDBBuffer(buffer_size + 31); // +31 bc pointer shenanigains
			unsigned char* miiDBBufferPtr = miiDBBuffer.data(); 
			
			// Align pointer to 32-bit
			unsigned char* miiDBBufferPtrAligned = alignPtr(miiDBBufferPtr);
			sleep(3);
			
			s32 testRead = ISFS_Read(miiDB, miiDBBufferPtrAligned, buffer_size);
			if (testRead<0) { ISFS_Close(miiDB); die("Unable to read Mii database!\n"); }
			
			// Set initial offset (beginning of first Mii's System ID)
			s32 offset = 32;
			for(int i=0; i<NoOfMiis; i++) {
				// Loop over one Mii
				// Start at the ID, change the next four bytes, jump to the next 
				for (int j=0; j<4; j++){
					miiDBBufferPtrAligned[offset] = (unsigned char)ids[j];
					offset+=1;
				}
				offset+=70;
			}
			
			// Apparently the database file includes its own checksum (CRC16)
			const int crc16Buffer_size = 127454; // 127454 = 0x1f1de, size of bytes to have their checksum calculated
			
			// Calc CRC16
			unsigned short checksum = crc16(miiDBBufferPtrAligned, crc16Buffer_size); //pleaseworkpleaseworkpleasework
			
			// Break checksum into two bytes (thanks Google AI) and write to miiDBBuffer
			unsigned char highByte = (checksum >> 8) & 0xFF;
			unsigned char lowByte = checksum & 0xFF;
			
			miiDBBuffer[127454] = highByte;
			miiDBBuffer[127455] = lowByte;
			
			// Write miiDBBuffer back to RFL_DB.dat
			ISFS_Seek(miiDB, 0, SEEK_SET);
			s32 testWrite = ISFS_Write(miiDB, miiDBBufferPtrAligned, buffer_size);
			if (testWrite<0) { ISFS_Close(miiDB); die("Unable to write back to Mii database!\n"); }
		}
		else {
			ISFS_Close(miiDB);
			die("Unable to open Mii database!\n");
		}
		ISFS_Close(miiDB);
	}
	else {
		die("Unexpected error! Did you tamper with systemID.txt?");
	}
	
	printf("\nAll done! Press HOME to exit.\n\n");
	
	while (true) {
		WPAD_ScanPads();
		u32 pressed = WPAD_ButtonsDown(0);
		
		if (pressed & WPAD_BUTTON_HOME) {die("");}
		
		VIDEO_WaitVSync();
	}

	return 0;
}
