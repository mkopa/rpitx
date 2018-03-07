extern "C"
{
#include "mailbox.h"
}
#include "gpio.h"
#include "raspberry_pi_revision.h"
#include "stdio.h"
#include <unistd.h>

gpio::gpio(uint32_t base, uint32_t len)
{
	
	gpioreg=( uint32_t *)mapmem(base,len);

}

int gpio::setmode(uint32_t gpio, uint32_t mode)
{
   int reg, shift;

   reg   =  gpio/10;
   shift = (gpio%10) * 3;

   gpioreg[reg] = (gpioreg[reg] & ~(7<<shift)) | (mode<<shift);

   return 0;
}

uint32_t gpio::GetPeripheralBase()
{
	RASPBERRY_PI_INFO_T info;
	uint32_t  BCM2708_PERI_BASE=0;
    if (getRaspberryPiInformation(&info) > 0)
	{
		if(info.peripheralBase==RPI_BROADCOM_2835_PERIPHERAL_BASE)
		{
			BCM2708_PERI_BASE = info.peripheralBase ;
		}

		if((info.peripheralBase==RPI_BROADCOM_2836_PERIPHERAL_BASE)||(info.peripheralBase==RPI_BROADCOM_2837_PERIPHERAL_BASE))
		{
			BCM2708_PERI_BASE = info.peripheralBase ;
		}
	}
	return BCM2708_PERI_BASE;
}


//******************** DMA Registers ***************************************

dmagpio::dmagpio():gpio(GetPeripheralBase()+DMA_BASE,DMA_LEN)
{
}

// ***************** CLK Registers *****************************************
clkgpio::clkgpio():gpio(GetPeripheralBase()+CLK_BASE,CLK_LEN)
{
}

clkgpio::~clkgpio()
{
	gpioreg[GPCLK_CNTL]= 0x5A000000 | (Mash << 9) | pllnumber|(0 << 4)  ; //4 is START CLK
}

int clkgpio::SetPllNumber(int PllNo,int MashType)
{
	//print_clock_tree();
	if(PllNo<8)
		pllnumber=PllNo;
	else
		pllnumber=clk_pllc;
	
	if(MashType<4)
		Mash=MashType;
	else
		Mash=0;
	gpioreg[GPCLK_CNTL]= 0x5A000000 | (Mash << 9) | pllnumber|(1 << 4)  ; //4 is START CLK
	usleep(100);
	Pllfrequency=GetPllFrequency(pllnumber);
	return 0;
}

uint64_t clkgpio::GetPllFrequency(int PllNo)
{
	uint64_t Freq=0;
	switch(PllNo)
	{
		case clk_osc:Freq=XOSC_FREQUENCY;break;
		case clk_plla:Freq=XOSC_FREQUENCY*((uint64_t)gpioreg[PLLA_CTRL]&0x3ff) +XOSC_FREQUENCY*(uint64_t)gpioreg[PLLA_FRAC]/(1<<20);break;
		//case clk_pllb:Freq=XOSC_FREQUENCY*((uint64_t)gpioreg[PLLB_CTRL]&0x3ff) +XOSC_FREQUENCY*(uint64_t)gpioreg[PLLB_FRAC]/(1<<20);break;
		case clk_pllc:Freq=XOSC_FREQUENCY*((uint64_t)gpioreg[PLLC_CTRL]&0x3ff) +XOSC_FREQUENCY*(uint64_t)gpioreg[PLLC_FRAC]/(1<<20);break;
		case clk_plld:Freq=(XOSC_FREQUENCY*((uint64_t)gpioreg[PLLD_CTRL]&0x3ff) +(XOSC_FREQUENCY*(uint64_t)gpioreg[PLLD_FRAC])/(1<<20))/(gpioreg[PLLD_PER]>>1);break;
		case clk_hdmi:Freq=XOSC_FREQUENCY*((uint64_t)gpioreg[PLLH_CTRL]&0x3ff) +XOSC_FREQUENCY*(uint64_t)gpioreg[PLLH_FRAC]/(1<<20);break;
	}
	fprintf(stderr,"Freq = %lld\n",Freq);

	return Freq;
}


int clkgpio::SetClkDivFrac(uint32_t Div,uint32_t Frac)
{
	
	gpioreg[GPCLK_DIV] = 0x5A000000 | ((Div)<<12) | Frac;
	usleep(10);
	//gpioreg[GPCLK_CNTL]= 0x5A000000 | (Mash << 9) | pllnumber |(1<<4)  ; //4 is START CLK
	//	usleep(10);
	return 0;

}

int clkgpio::SetMasterMultFrac(uint32_t Mult,uint32_t Frac)
{
	
	gpioreg[PLLA_CTRL] = (0x5a<<24) | (0x21<<12) | Mult;
	
	gpioreg[PLLA_FRAC]= 0x5A000000 | Frac  ; 
	
	return 0;

}

int clkgpio::SetFrequency(int Frequency)
{
	if(ModulateFromMasterPLL)
	{
		double FloatMult=((double)(CentralFrequency+Frequency)*PllFixDivider)/(double)(XOSC_FREQUENCY);
		uint32_t freqctl = FloatMult*((double)(1<<20)) ; 
		int IntMultiply= freqctl>>20; // Need to be calculated to have a center frequency
		freqctl&=0xFFFFF; // Fractionnal is 20bits
		uint32_t FracMultiply=freqctl&0xFFFFF; 
		SetMasterMultFrac(IntMultiply,FracMultiply);
	}
	else
	{
		double Freqresult=(double)Pllfrequency/(double)(CentralFrequency+Frequency);
		uint32_t FreqDivider=(uint32_t)Freqresult;
		uint32_t FreqFractionnal=(uint32_t) (4096*(Freqresult-(double)FreqDivider));
		if((FreqDivider>4096)||(FreqDivider<2)) fprintf(stderr,"Frequency out of range\n");
		//printf("DIV/FRAC %u/%u \n",FreqDivider,FreqFractionnal);
	
		SetClkDivFrac(FreqDivider,FreqFractionnal);
	}
	
	return 0;

}

uint32_t clkgpio::GetMasterFrac(int Frequency)
{
	if(ModulateFromMasterPLL)
	{
		double FloatMult=((double)(CentralFrequency+Frequency)*PllFixDivider)/(double)(XOSC_FREQUENCY);
		uint32_t freqctl = FloatMult*((double)(1<<20)) ; 
		int IntMultiply= freqctl>>20; // Need to be calculated to have a center frequency
		freqctl&=0xFFFFF; // Fractionnal is 20bits
		uint32_t FracMultiply=freqctl&0xFFFFF; 
		return FracMultiply;
	}
	else
		return 0; //Not in Master CLk mode
	
}

int clkgpio::ComputeBestLO(uint64_t Frequency)
{ 
	for(int i=1;i<4096;i++)
	{
		
	}
	return 0;
}

int clkgpio::SetCenterFrequency(uint64_t Frequency)
{
	CentralFrequency=Frequency;
	if(ModulateFromMasterPLL)
	{
		//Choose best PLLDiv and Div
		ComputeBestLO(Frequency);
		SetClkDivFrac(PllFixDivider,0); // NO MASH !!!!
		SetFrequency(Frequency);
	}
	else
	{
		GetPllFrequency(pllnumber);// Be sure to get the master PLL frequency
	}
	return 0;	
}

void clkgpio::SetPhase(bool inversed)
{
	uint32_t StateBefore=clkgpio::gpioreg[GPCLK_CNTL];
	clkgpio::gpioreg[GPCLK_CNTL]= (0x5A<<24) | StateBefore | ((inversed?1:0)<<8) | 1<<5;
	clkgpio::gpioreg[GPCLK_CNTL]= (0x5A<<24) | StateBefore | ((inversed?1:0)<<8) | 0<<5;
}

void clkgpio::SetAdvancedPllMode(bool Advanced)
{
	ModulateFromMasterPLL=Advanced;
	if(ModulateFromMasterPLL)
	{
		SetPllNumber(clk_plla,0); // Use PPL_A , Do not USE MASH which generates spurious
		gpioreg[0x104/4]=0x5A00020A; // Enable Plla_PER
		usleep(100);
		gpioreg[PLLA_PER]=0x5A000002; // Div ? 
		usleep(100);
	}
}

void clkgpio::print_clock_tree(void)
{

   printf("PLLC_DIG0=%08x\n",gpioreg[(0x1020/4)]);
   printf("PLLC_DIG1=%08x\n",gpioreg[(0x1024/4)]);
   printf("PLLC_DIG2=%08x\n",gpioreg[(0x1028/4)]);
   printf("PLLC_DIG3=%08x\n",gpioreg[(0x102c/4)]);
   printf("PLLC_ANA0=%08x\n",gpioreg[(0x1030/4)]);
   printf("PLLC_ANA1=%08x\n",gpioreg[(0x1034/4)]);
   printf("PLLC_ANA2=%08x\n",gpioreg[(0x1038/4)]);
   printf("PLLC_ANA3=%08x\n",gpioreg[(0x103c/4)]);
   printf("PLLC_DIG0R=%08x\n",gpioreg[(0x1820/4)]);
   printf("PLLC_DIG1R=%08x\n",gpioreg[(0x1824/4)]);
   printf("PLLC_DIG2R=%08x\n",gpioreg[(0x1828/4)]);
   printf("PLLC_DIG3R=%08x\n",gpioreg[(0x182c/4)]);

   printf("GNRIC CTL=%08x DIV=%8x  ",gpioreg[ 0],gpioreg[ 1]);
   printf("VPU   CTL=%08x DIV=%8x\n",gpioreg[ 2],gpioreg[ 3]);
   printf("SYS   CTL=%08x DIV=%8x  ",gpioreg[ 4],gpioreg[ 5]);
   printf("PERIA CTL=%08x DIV=%8x\n",gpioreg[ 6],gpioreg[ 7]);
   printf("PERII CTL=%08x DIV=%8x  ",gpioreg[ 8],gpioreg[ 9]);
   printf("H264  CTL=%08x DIV=%8x\n",gpioreg[10],gpioreg[11]);
   printf("ISP   CTL=%08x DIV=%8x  ",gpioreg[12],gpioreg[13]);
   printf("V3D   CTL=%08x DIV=%8x\n",gpioreg[14],gpioreg[15]);

   printf("CAM0  CTL=%08x DIV=%8x  ",gpioreg[16],gpioreg[17]);
   printf("CAM1  CTL=%08x DIV=%8x\n",gpioreg[18],gpioreg[19]);
   printf("CCP2  CTL=%08x DIV=%8x  ",gpioreg[20],gpioreg[21]);
   printf("DSI0E CTL=%08x DIV=%8x\n",gpioreg[22],gpioreg[23]);
   printf("DSI0P CTL=%08x DIV=%8x  ",gpioreg[24],gpioreg[25]);
   printf("DPI   CTL=%08x DIV=%8x\n",gpioreg[26],gpioreg[27]);
   printf("GP0   CTL=%08x DIV=%8x  ",gpioreg[28],gpioreg[29]);
   printf("GP1   CTL=%08x DIV=%8x\n",gpioreg[30],gpioreg[31]);

   printf("GP2   CTL=%08x DIV=%8x  ",gpioreg[32],gpioreg[33]);
   printf("HSM   CTL=%08x DIV=%8x\n",gpioreg[34],gpioreg[35]);
   printf("OTP   CTL=%08x DIV=%8x  ",gpioreg[36],gpioreg[37]);
   printf("PCM   CTL=%08x DIV=%8x\n",gpioreg[38],gpioreg[39]);
   printf("PWM   CTL=%08x DIV=%8x  ",gpioreg[40],gpioreg[41]);
   printf("SLIM  CTL=%08x DIV=%8x\n",gpioreg[42],gpioreg[43]);
   printf("SMI   CTL=%08x DIV=%8x  ",gpioreg[44],gpioreg[45]);
   printf("SMPS  CTL=%08x DIV=%8x\n",gpioreg[46],gpioreg[47]);

   printf("TCNT  CTL=%08x DIV=%8x  ",gpioreg[48],gpioreg[49]);
   printf("TEC   CTL=%08x DIV=%8x\n",gpioreg[50],gpioreg[51]);
   printf("TD0   CTL=%08x DIV=%8x  ",gpioreg[52],gpioreg[53]);
   printf("TD1   CTL=%08x DIV=%8x\n",gpioreg[54],gpioreg[55]);

   printf("TSENS CTL=%08x DIV=%8x  ",gpioreg[56],gpioreg[57]);
   printf("TIMER CTL=%08x DIV=%8x\n",gpioreg[58],gpioreg[59]);
   printf("UART  CTL=%08x DIV=%8x  ",gpioreg[60],gpioreg[61]);
   printf("VEC   CTL=%08x DIV=%8x\n",gpioreg[62],gpioreg[63]);
   	

   printf("PULSE CTL=%08x DIV=%8x  ",gpioreg[100],gpioreg[101]);
   printf("PLLT  CTL=%08x DIV=????????\n",gpioreg[76]);

   printf("DSI1E CTL=%08x DIV=%8x  ",gpioreg[86],gpioreg[87]);
   printf("DSI1P CTL=%08x DIV=%8x\n",gpioreg[88],gpioreg[89]);
   printf("AVE0  CTL=%08x DIV=%8x\n",gpioreg[90],gpioreg[91]);

   printf("CMPLLA=%08x  ",gpioreg[0x104/4]);
   printf("CMPLLC=%08x \n",gpioreg[0x108/4]);
   printf("CMPLLD=%08x   ",gpioreg[0x10C/4]);
   printf("CMPLLH=%08x \n",gpioreg[0x110/4]);
	
     printf("EMMC  CTL=%08x DIV=%8x\n",gpioreg[112],gpioreg[113]);
	   printf("EMMC  CTL=%08x DIV=%8x\n",gpioreg[112],gpioreg[113]);
   printf("EMMC  CTL=%08x DIV=%8x\n",gpioreg[112],gpioreg[113]);	
	

   // Sometimes calculated frequencies are off by a factor of 2
   // ANA1 bit 14 may indicate that a /2 prescaler is active
   printf("PLLA PDIV=%d NDIV=%d FRAC=%d  ",(gpioreg[PLLA_CTRL]>>16) ,gpioreg[PLLA_CTRL]&0x3ff, gpioreg[PLLA_FRAC] );
   printf(" %f MHz\n",19.2* ((float)(gpioreg[PLLA_CTRL]&0x3ff) + ((float)gpioreg[PLLA_FRAC])/((float)(1<<20))) );
   printf("DSI0=%d CORE=%d PER=%d CCP2=%d\n\n",gpioreg[PLLA_DSI0],gpioreg[PLLA_CORE],gpioreg[PLLA_PER],gpioreg[PLLA_CCP2]);


   printf("PLLB PDIV=%d NDIV=%d FRAC=%d  ",(gpioreg[PLLB_CTRL]>>16) ,gpioreg[PLLB_CTRL]&0x3ff, gpioreg[PLLB_FRAC] );
   printf(" %f MHz\n",19.2* ((float)(gpioreg[PLLB_CTRL]&0x3ff) + ((float)gpioreg[PLLB_FRAC])/((float)(1<<20))) );
   printf("ARM=%d SP0=%d SP1=%d SP2=%d\n\n",gpioreg[PLLB_ARM],gpioreg[PLLB_SP0],gpioreg[PLLB_SP1],gpioreg[PLLB_SP2]);

   printf("PLLC PDIV=%d NDIV=%d FRAC=%d  ",(gpioreg[PLLC_CTRL]>>16) ,gpioreg[PLLC_CTRL]&0x3ff, gpioreg[PLLC_FRAC] );
   printf(" %f MHz\n",19.2* ((float)(gpioreg[PLLC_CTRL]&0x3ff) + ((float)gpioreg[PLLC_FRAC])/((float)(1<<20))) );
   printf("CORE2=%d CORE1=%d PER=%d CORE0=%d\n\n",gpioreg[PLLC_CORE2],gpioreg[PLLC_CORE1],gpioreg[PLLC_PER],gpioreg[PLLC_CORE0]);

   printf("PLLD %x PDIV=%d NDIV=%d FRAC=%d  ",gpioreg[PLLD_CTRL],(gpioreg[PLLD_CTRL]>>16) ,gpioreg[PLLD_CTRL]&0x3ff, gpioreg[PLLD_FRAC] );
   printf(" %f MHz\n",19.2* ((float)(gpioreg[PLLD_CTRL]&0x3ff) + ((float)gpioreg[PLLD_FRAC])/((float)(1<<20))) );
   printf("DSI0=%d CORE=%d PER=%d DSI1=%d\n\n",gpioreg[PLLD_DSI0],gpioreg[PLLD_CORE],gpioreg[PLLD_PER],gpioreg[PLLD_DSI1]);

   printf("PLLH PDIV=%d NDIV=%d FRAC=%d  ",(gpioreg[PLLH_CTRL]>>16) ,gpioreg[PLLH_CTRL]&0x3ff, gpioreg[PLLH_FRAC] );
   printf(" %f MHz\n",19.2* ((float)(gpioreg[PLLH_CTRL]&0x3ff) + ((float)gpioreg[PLLH_FRAC])/((float)(1<<20))) );
   printf("AUX=%d RCAL=%d PIX=%d STS=%d\n\n",gpioreg[PLLH_AUX],gpioreg[PLLH_RCAL],gpioreg[PLLH_PIX],gpioreg[PLLH_STS]);


}


// ************************************** GENERAL GPIO *****************************************************

generalgpio::generalgpio():gpio(GetPeripheralBase()+GENERAL_BASE,GENERAL_LEN)
{
}

generalgpio::~generalgpio()
{
	disableclk();
}

void generalgpio::enableclk()
{
	gpioreg[GPFSEL0] = (gpioreg[GPFSEL0] & ~(7 << 12)) | (4 << 12);
}

void generalgpio::disableclk()
{
	gpioreg[GPFSEL0] = (gpioreg[GPFSEL0] & ~(7 << 12)) | (0 << 12);
}

// ********************************** PWM GPIO **********************************

pwmgpio::pwmgpio():gpio(GetPeripheralBase()+PWM_BASE,PWM_LEN)
{
	gpioreg[PWM_CTL] = 0;
}

pwmgpio::~pwmgpio()
{
	
	gpioreg[PWM_CTL] = 0;
	gpioreg[PWM_DMAC] = 0;
}

int pwmgpio::SetPllNumber(int PllNo,int MashType)
{
	if(PllNo<8)
		pllnumber=PllNo;
	else
		pllnumber=clk_pllc;	
	if(MashType<4)
		Mash=MashType;
	else
		Mash=0;
	clk.gpioreg[PWMCLK_CNTL]= 0x5A000000 | (Mash << 9) | pllnumber|(0 << 4)  ; //4 is STOP CLK
	usleep(100);
	Pllfrequency=GetPllFrequency(pllnumber);
	return 0;
}

uint64_t pwmgpio::GetPllFrequency(int PllNo)
{
	return clk.GetPllFrequency(PllNo);

}

int pwmgpio::SetFrequency(uint64_t Frequency)
{
	Prediv=32; // Fixe for now , need investigation if not 32 !!!! FixMe !
	double Freqresult=(double)Pllfrequency/(double)(Frequency*Prediv);
	uint32_t FreqDivider=(uint32_t)Freqresult;
	uint32_t FreqFractionnal=(uint32_t) (4096*(Freqresult-(double)FreqDivider));
	if((FreqDivider>4096)||(FreqDivider<2)) fprintf(stderr,"Frequency out of range\n");
	fprintf(stderr,"PWM clk=%d / %d\n",FreqDivider,FreqFractionnal);
	clk.gpioreg[PWMCLK_DIV] = 0x5A000000 | ((FreqDivider)<<12) | FreqFractionnal;
	
	usleep(100);
	clk.gpioreg[PWMCLK_CNTL]= 0x5A000000 | (Mash << 9) | pllnumber|(1 << 4)  ; //4 is STAR CLK
	usleep(100);
	
	
	SetPrediv(Prediv);	
	return 0;

}

int pwmgpio::SetPrediv(int predivisor) //Mode should be only for SYNC or a Data serializer : Todo
{
		Prediv=predivisor;
		if(Prediv>32) 
		{
			fprintf(stderr,"PWM Prediv is max 32\n");
			Prediv=2;
		}
		fprintf(stderr,"PWM Prediv %d\n",Prediv);
		gpioreg[PWM_RNG1] = Prediv;// 250 -> 8KHZ
		usleep(100);
		gpioreg[PWM_RNG2] = Prediv;// 32 Mandatory for Serial Mode without gap
	
		//gpioreg[PWM_FIFO]=0xAAAAAAAA;

		gpioreg[PWM_DMAC] = PWMDMAC_ENAB | PWMDMAC_THRSHLD;
		usleep(100);
		gpioreg[PWM_CTL] = PWMCTL_CLRF;
		usleep(100);
		//gpioreg[PWM_CTL] =   PWMCTL_USEF1 | PWMCTL_PWEN1; 
		gpioreg[PWM_CTL] = PWMCTL_USEF1| PWMCTL_MODE1| PWMCTL_PWEN1|PWMCTL_MSEN1;
		usleep(100);
	
	return 0;

}
		
// ********************************** PCM GPIO (I2S) **********************************

pcmgpio::pcmgpio():gpio(GetPeripheralBase()+PCM_BASE,PCM_LEN)
{
	gpioreg[PCM_CS_A] = 1;				// Disable Rx+Tx, Enable PCM block
}

pcmgpio::~pcmgpio()
{
	
}

int pcmgpio::SetPllNumber(int PllNo,int MashType)
{
	if(PllNo<8)
		pllnumber=PllNo;
	else
		pllnumber=clk_pllc;	
	if(MashType<4)
		Mash=MashType;
	else
		Mash=0;
	clk.gpioreg[PCMCLK_CNTL]= 0x5A000000 | (Mash << 9) | pllnumber|(1 << 4)  ; //4 is START CLK
	Pllfrequency=GetPllFrequency(pllnumber);
	return 0;
}

uint64_t pcmgpio::GetPllFrequency(int PllNo)
{
	return clk.GetPllFrequency(PllNo);

}

int pcmgpio::SetFrequency(uint64_t Frequency)
{
	Prediv=10;
	double Freqresult=(double)Pllfrequency/(double)(Frequency*Prediv);
	uint32_t FreqDivider=(uint32_t)Freqresult;
	uint32_t FreqFractionnal=(uint32_t) (4096*(Freqresult-(double)FreqDivider));
	fprintf(stderr,"PCM clk=%d / %d\n",FreqDivider,FreqFractionnal);
	if((FreqDivider>4096)||(FreqDivider<2)) fprintf(stderr,"PCM Frequency out of range\n");
	clk.gpioreg[PCMCLK_DIV] = 0x5A000000 | ((FreqDivider)<<12) | FreqFractionnal;
	SetPrediv(Prediv);
	return 0;

}

int pcmgpio::SetPrediv(int predivisor) //Carefull we use a 10 fixe divisor for now : frequency is thus f/10 as a samplerate
{
	if(predivisor>1000)
	{
		fprintf(stderr,"PCM prediv should be <1000");
		predivisor=1000;
	}
	
	gpioreg[PCM_TXC_A] = 0<<31 | 1<<30 | 0<<20 | 0<<16; // 1 channel, 8 bits
	usleep(100);
	
	//printf("Nb PCM STEP (<1000):%d\n",NbStepPCM);
	gpioreg[PCM_MODE_A] = (predivisor-1)<<10; // SHOULD NOT EXCEED 1000 !!! 
	usleep(100);
	gpioreg[PCM_CS_A] |= 1<<4 | 1<<3;		// Clear FIFOs
	usleep(100);
	gpioreg[PCM_DREQ_A] = 64<<24 | 64<<8 ;		//TX Fifo PCM=64 DMA Req when one slot is free?
	usleep(100);
	gpioreg[PCM_CS_A] |= 1<<9;			// Enable DMA
	usleep(100);
	gpioreg[PCM_CS_A] |= 1<<2; //START TX PCM
	
	return 0;

}


// ********************************** PADGPIO (Amplitude) **********************************

padgpio::padgpio():gpio(GetPeripheralBase()+PADS_GPIO,PADS_GPIO_LEN)
{
	
}

padgpio::~padgpio()
{
	
}



