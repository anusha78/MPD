/*
 * File:
 *    mpdLibTest.c
 *
 * Description:
 *    Simply evolving program to test the mpd library
 *
 */


#include "mpdConfig.h"

#include "jvme.h"
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
/* Include MPD definitions */
#include "mpdLib.h"

int 
main(int argc, char *argv[]) 
{
  int h,i,j,k,kk,m, evt;
  int fnMPD=10;
  int error_count;
  uint32_t hcount;
  int scount, sfreq;
  int sch0, sch1;
  int hgain;
  int rdone, rtout;

  uint16_t mfull,mempty;
  uint32_t e_head, e_head0, e_size;
  uint32_t e_data32[130];
  uint32_t e_trai, e_eblo;

#define MAX_HDATA 4096
#define MAX_SDATA 1024
  uint32_t hdata[MAX_HDATA]; // histogram data buffer
  uint32_t sdata[MAX_SDATA]; // samples data buffer

  char outfile[1000];
  int acq_mode = 1;
  int n_event=10;

  if (argc<2) {
    printf("SYNTAX: %s [out_data_file 0xacq_mode #events]\n",argv[0]);
    printf("        acq_mode_bit0 : EVENT readout");
    printf("        acq_mode_bit1 : SAMPLE check");
    printf("        acq_mode_bit2 : HISTO output");
    printf("        #events: number of events only for EVENT_READOUT\n");
  }

  if (argc>1) {
    sprintf(outfile,"%s",argv[1]);
  } else {
    sprintf(outfile,"out.txt");
  }

  if (argc>2) {
    sscanf(argv[2],"%x",&acq_mode);
  }

  if (argc>3) {
    sscanf(argv[3],"%d",&n_event);
  }

  printf("\nMPD Library Tests\n");
  printf("----------------------------\n");
  printf(" outfile = %s\n",outfile);
  printf(" acq_mode= 0x%x\n",acq_mode);
  printf(" n_event = %d\n", n_event);

  if(vmeOpenDefaultWindows()!=OK)
    {
      printf("ERROR opening default VME windows\n");
      goto CLOSE;
    }

  vmeDmaConfig(2,2,0);

  /**
   * Read config file and fill internal variables
   *
   */
  mpdConfigInit("cfg/config_apv.txt");
  mpdConfigLoad();

  /**
   * Init and config MPD+APV
   */

  // discover MPDs and initialize memory mapping
  mpdInit(0x80000,0x80000,21,0x0);

  fnMPD = mpdGetNumberMPD();

  if (fnMPD<=0) { // test all possible vme slot ?
    printf("ERR: no MPD discovered, cannot continue\n");
    return -1;
  } 

  printf(" MPD discovered = %d\n",fnMPD);

  // APV configuration on all active MPDs
  for (k=0;k<fnMPD;k++) { // only active mpd set
    i = mpdSlot(k);

    // first test MPD histo memory write/read
    printf(" test mpd %d histo memory\n",i);

    mpdHISTO_Clear(i,0,-1);
    mpdHISTO_Read(i,0,hdata);   
    error_count=0;
    for (j=0;j<MAX_HDATA;j++) {
      if (hdata[j]!=j) {
	//	printf("ERROR matching histo read/write ch=%d rval=%d\n",j,hdata[j]);
	error_count++;
      }
    }
    if (error_count) {
      printf("ERROR: HISTO Test fail %d time / %d attempts\n",error_count, MAX_HDATA);
    } else {
      printf("HISTO Read/Write test SUCCESS on MPD slot %d\n",i);
    }
    // END of first test MPD histo memory write/read


    printf(" Try initialize I2C mpd in slot %d\n",i);
    if (mpdI2C_Init(i) != OK) {
      printf("WRN: I2C fails on MPD %d\n",i);
    }
    if (mpdI2C_ApvReset(i) != OK) {
      printf("WRN: I2C ApvReset fails on MPD %d\n",i);
    }

    printf("Try APV discovery and init on MPD slot %d\n",i);
    if (mpdAPV_Scan(i)<=0) { // no apd found, skip next 
      //  continue;
    }
      
    // board configuration (APV-ADC clocks phase)
    printf("Do DELAY setting on MPD slot %d\n",i);
    mpdDELAY25_Set(i, mpdGetAdcClockPhase(i,0), mpdGetAdcClockPhase(i,1));

    // apv reset
    printf("Do APV reset on MPD slot %d\n",i);
    if (mpdI2C_ApvReset(i) != OK) {
      printf("ERR: apv resert faild on mpd %d\n",i);
    }

    // apv configuration
    printf("Configure single APV on MPD slot %d\n",i);
    for (j=0; j < mpdGetNumberAPV(i); j++) {
      if (mpdAPV_Config(i,j) != OK) {
	printf("ERR: config apv card %d failed in mpd %d\n",j,i);
      }
    }

    // configure adc on MPD 
    printf("Configure ADC on MPD slot %d\n",i);
    mpdADS5281_Config(i);

    // configure fir
    // not implemented yet

    // 101 reset on the APV
    printf("Do 101 Reset on MPD slot %d\n",i);
    mpdAPV_Reset101(i);

    // <- MPD+APV initialization ends here

  } // end loop on mpds

    /**********************************
     * SAMPLES TEST
     * sample APV output at 40 MHz
     * only for testing not for normal daq
     **********************************/
  if (acq_mode & 0x2) { 
  
    for (k=0;k<fnMPD;k++) { // only active mpd set
      i = mpdSlot(k);

      mpdSetAcqMode(i, "sample");

      // load pedestal and thr default values
      mpdPEDTHR_Write(i);
    
      // set clock phase 
      mpdDELAY25_Set(i, 20, 20); // use 20 as test, but other values may work better
    
      // enable acq
      mpdDAQ_Enable(i);

      // wait for FIFOs to get full
      mfull=0;
      mempty=1;
      do {
	mpdFIFO_GetAllFlags(i,&mfull,&mempty);
	printf("SAMPLE test: %x (%x) %x\n",mfull,mpdGetApvEnableMask(i),mempty);
	sleep(1);
      } while (mfull!=mpdGetApvEnableMask(i));
      
      // read data from FIFOs and estimate synch pulse period
      for (j=0;j<16;j++) { // 16 = number of ADC channel in one MPD
	if (mpdGetApvEnableMask(i) & (1<<j)) { // apv is enabled
	  mpdFIFO_Samples(i,j, sdata, &scount, MAX_SDATA, &error_count);
	  if (error_count != 0) {
	    printf("ERROR returned from FIFO_Samples %d\n",error_count);
	  }
	  printf("MPD/APV : %d / %d, peaks around: ",i,j);
	  sch0=-1;
	  sch1=-1;
	  sfreq=0;
	  for (h=0;h<scount;h++) { // detects synch peaks
	    //	  printf("%04x ", sdata[h]); // output data
	    if (sdata[h]>2500) { // threshold could be lower
	      if (sch0<0) { sch0=h; sch1=sch0;} else { 
		if (h==(sch1+1)) { sch1=h; } else {
		  printf("%d-%d (v %d) ",sch0,sch1, sdata[h]);
		  sch0=h;
		  sch1=sch0;
		  sfreq++;
		}
	      }
	    }
	  }
	  printf("\n Estimated synch period = %f (us) ,expected (20MHz:1.8, 40MHz:0.9)\n",((float) scount)/sfreq/40);
	}
      }

    } // end loop on mpds

  } // sample check mode

    /********************************************
     * HISTO Mode; sampled data are histogrammed
     * only for testing, non for normal daq
     ********************************************/

  if (acq_mode & 0x4) { 

    for (k=0;k<fnMPD;k++) { // only active mpd set
      i = mpdSlot(k);

      mpdSetAcqMode(i, "histo");
      hgain = 5; // this will be read from config file

      // load pedestal and thr default values
      mpdPEDTHR_Write(i);


      // set ADC gain
      mpdADS5281_SetGain(i,0,hgain,hgain,hgain,hgain,hgain,hgain,hgain,hgain);

      // loop on adc channels

      for (j=0;j<16;j++) {

	mpdHISTO_Clear(i,j,0);

	mpdHISTO_Start(i,j);

	sleep(1); // wait to get some data
      
	mpdHISTO_Stop(i,j);

	hcount=0;
	mpdHISTO_GetIntegral(i,j,&hcount);
	error_count = mpdHISTO_Read(i,j, hdata);

	if (error_count != OK) {
	  printf("ERROR: reading histogram data from MPD %d ADCch %d\n",i,j);
	  continue;
	}

	printf(" ### histo peaks integral MPD/ADCch = %d / %d (total Integral= %d)\n",i,j,hcount);
	sch0=-1;
	sch1=0;
	for (h=0;h<4096;h++) {

	  if (hdata[h]>0) { 
	    if (sch0<0) { printf(" first bin= %d: ", h); } else {
	      if ((sch0+1) < h) { printf( "%d last bin= %d\n first bin= %d: ",sch1,sch0,h); sch1=0; }
	    }
	    sch1 += hdata[h];
	    sch0=h;
	  }
	  
	  //	if ((h%64) == 63) { printf("\n"); }
	}
	if (sch1>0) {
	  printf( "%d last bin= %d\n",sch1,sch0);
	} else { printf("\n");}
	
      } // end loop adc channels in histo mode

    }

  } // histo mode

    /**************************
     * "Event" readout
     *  normal DAQ starts here
     **************************/

  if (acq_mode & 1) { 

#define MPD_TIMEOUT 100
//Output file TAG
#define VERSION_TAG 0xE0000000
#define EVENT_TAG   0x10000000
#define MPD_TAG     0x20000000
#define ADC_TAG     0x30000000
#define HEADER_TAG  0x40000000
#define DATA_TAG    0x0
#define TRAILER_TAG 0x50000000

#define FILE_VERSION 0x1
    // open out file
    FILE *fout;
    fout = fopen(outfile,"w");
    if (fout == NULL) { fout = stdout; }
    fprintf(fout,"%x\n", FILE_VERSION | VERSION_TAG);

    for (k=0;k<fnMPD;k++) { // only active mpd set
      i = mpdSlot(k);

      // mpd latest configuration before trigger is enabled
      mpdSetAcqMode(i, "event");

      // load pedestal and thr default values
      mpdPEDTHR_Write(i);    
    
      // enable acq
      mpdDAQ_Enable(i);      

    }    
    // -> now trigger can be enabled

    evt=0;

    printf(" ============ START ACQ ===========\n");
    do { // simulate loop on trigger
      sleep(1); // wait for event
      printf(" ---- Event %d occurred -----\n",evt);
      // event occurred ... need some trigger logic
      rtout=0;

      for (k=0;k<fnMPD;k++) { // only active mpd set
	i = mpdSlot(k);
	mpdArmReadout(i); // prepare internal variables for readout
      }

      rdone = 1;

      fprintf(fout,"%x\n", evt | EVENT_TAG ); // event number
      for (kk=0;kk<fnMPD;kk++) { // only active mpd set
	i = mpdSlot(kk);
	//usleep(10000);
	mpdFIFO_ClearAll(i);
	mpdTRIG_Disable(i);
	//	  	  mpdDAQ_Enable(i);
	mpdTRIG_PauseEnable(i,5000);

	do { // wait for data in MPD
	  mpdTRIG_PauseEnable(i,1000);
	  
	  rdone = mpdFIFO_ReadAll(i,&rtout,&error_count);
	  
	  printf(" Rdone/Tout/error = %d %d %d\n", rdone, rtout, error_count);
	  rtout++;

	} while ((rdone == 0) && (error_count == -1) && (rtout < MPD_TIMEOUT)); // timeout can be changed
	//	mpdTRIG_Disable(i);
	



	if ((error_count != 0) || (rtout > MPD_TIMEOUT)) { // reset MPD on error or timeout
	  printf("%s: ERROR in readout, clear fifo\n",__FUNCTION__);
	  mpdFIFO_ClearAll(i);
	  mpdTRIG_Enable(i);
	}
	
	if (rdone) { // data need to be written on file

	  printf("%d",i); // slot
	  fprintf(fout,"%x\n", i | MPD_TAG);
	  
	  for (j=0; j < mpdGetNumberAPV(i); j++) { // loop on APV (ADC channels)

	    fprintf(fout,"%x\n", (mpdApvGetAdc(i,j) | ADC_TAG));
	    k=0; // buffer element index
	    for (h=0; h<mpdApvGetBufferSample(i,j); h++) { // loop on samples
	      e_size = mpdApvGetEventSize(i,j);
	      e_head0 = mpdApvGetBufferElement(i, j, k);
	      k++;
	      e_head = ((e_head0 & 0xfff) << 4) | (mpdApvGetAdc(i,j) & 0xf);
	      fprintf(fout,"%x\n", e_head0 | HEADER_TAG);
	      // fwrite e_head
	      for (m=0;m<e_size-2;m++) {
		e_data32[m] = 0x80000 | ((i<<12) & 0x7F000) | (mpdApvGetBufferElement(i,j,k) & 0xfff);
		fprintf(fout,"%x\n",(mpdApvGetBufferElement(i,j,k) & 0xfff) | DATA_TAG );
		k++;
	      }
	      // fwire e_data32 (e_size-2)
	      e_trai = 0x100000 | ((i & 0x1F) << 12) | (mpdApvGetBufferElement(i,j,k)& 0xfff);
	      e_eblo = 0x180000 | (e_size & 0xff);
	      fprintf(fout,"%x\n",(mpdApvGetBufferElement(i,j,k) & 0xfff) | TRAILER_TAG);
	      k++;
	      // fwrite e_trai // trailer
	      // fwrite e_eblo // end sample block
	    }
	    mpdApvShiftDataBuffer(i,j,k);
	  } // end loop on apv
	  
	} // if rdone 
	  
      } // end mpd loop

	// e_head = 0x40000;
	// fwrite end block when loop on MPD
      evt++;
      //  mpdFIFO_ClearAll(i);
    } while (evt<n_event); // end loop on events
     
    if (fout != stdout)  fclose(fout);

  }
  
 CLOSE:

  vmeCloseDefaultWindows();
  
  exit(0);

}

